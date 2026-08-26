#include "modding.h"
#include "extra_options.h"
#include "recompconfig.h"

/*
 * Ghidra shows that the native task scheduler runs callbacks at task +0x08,
 * +0x0C, and +0x10.  Actors use +0x0C for their overlay AI and +0x10 for
 * func_80218F30, the common movement/collision/finalization pass.  Replaying
 * only those latter two callbacks gives selected combat actors additional AI
 * and movement ticks without duplicating the global scheduler or its culling
 * and animation bookkeeping.
 */

#define HYPER_SEEN_CAPACITY 256
#define HYPER_EXTRA_TICKS 3u

#define TASK_AI_CALLBACK(task) \
    (*(ExtraOptionsTaskCallback volatile *)((char *)(task) + 0x0C))
#define TASK_POST_CALLBACK(task) \
    (*(ExtraOptionsTaskCallback volatile *)((char *)(task) + 0x10))
#define TASK_OBJECT(task) \
    (*(void *volatile *)((char *)(task) + 0x18))
#define TASK_ACTOR_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5C))
#define TASK_ENTITY_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5E))
#define TASK_STATUS(task) \
    (*(volatile unsigned int *)((char *)(task) + 0x68))
#define TASK_GENERATION(task) \
    (*(volatile unsigned char *)((char *)(task) + 0x74))
#define TASK_HEALTH(task) \
    (*(volatile unsigned char *)((char *)(task) + 0x8D))
#define DARUMANYO_LIVES(task) \
    (*(volatile unsigned char *)((char *)(task) + 0xD1))
#define BENKEI_STATE(task) \
    (*(volatile unsigned char *)((char *)(task) + 0xD0))
#define BENKEI_LIVES(task) \
    (*(volatile signed char *)((char *)(task) + 0xD3))

#define THAISAMBA_STATE_HEALTH(state) \
    (*(volatile signed int *)((char *)(state) + 0x60))
#define THAISAMBA_STATE_BOSS_TASK(state) \
    (*(void *volatile *)((char *)(state) + 0x1D8))
#define THAISAMBA_STATE_MODEL_TASK(state) \
    (*(void *volatile *)((char *)(state) + 0x1E0))
#define THAISAMBA_STATE_COMBAT_PAUSED(state) \
    (*(volatile unsigned char *)((char *)(state) + 0x2C0))

#define TASK_STATUS_REMOVE_PENDING 0x00000002u
#define TASK_CALLBACK_DISABLED_BIT 0x00800000u

#define ENTITY_DARUMANYO 0x00CCu
#define ENTITY_MIND_CONTROL_ROBOT 0x01B0u
#define ENTITY_BENKEI 0x01C0u
#define ENTITY_CONGO 0x0323u

#define ROOM_CONGO 0x016u
#define ROOM_DARUMANYO 0x049u
#define ROOM_TSURAMI 0x071u
#define ROOM_DRAGON_FIGHT 0x155u
#define ROOM_BENKEI 0x171u

#define BENKEI_FIGHT_ACTIVE_FLAG 0x07Au

typedef void (*ExtraOptionsTaskCallback)(void *task, void *object);

typedef struct
{
    void *task;
    void *object;
    unsigned short actor_id;
    unsigned char generation;
    unsigned char padding;
} HyperActorIdentity;

typedef struct
{
    void *state;
    void *task;
    void *object;
} HyperSpecialBossIdentity;

extern void *D_8016DAB4_16E6B4;
extern unsigned short D_800C7AB2;
extern void *D_8020EED0_63A2B0;
extern int func_800240DC_24CDC(int flag_id);

static HyperActorIdentity s_seen_actors[HYPER_SEEN_CAPACITY];
static HyperActorIdentity s_captured_actor;
static unsigned int s_seen_replace_index;
static unsigned short s_runtime_room;
static unsigned char s_runtime_active;
static unsigned char s_capture_valid;
static unsigned char s_replay_guard;
static unsigned char s_mind_control_combat_active;
static ExtraOptionsTaskCallback s_captured_post_callback;

static HyperActorIdentity s_congo_actor;
static HyperActorIdentity s_tsurami_actor;
static HyperActorIdentity s_darumanyo_actor;
static HyperActorIdentity s_mind_control_robot;
static HyperActorIdentity s_benkei_actor;
static HyperSpecialBossIdentity s_thaisamba_special_boss;

static int hyper_enemies_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("hyper_enemies") == 0;
}

static int is_regular_enemy(unsigned short actor_id)
{
    /* Curated from multiplayer's live-enemy roster.  Top-level hazards,
     * destructibles, spawners, projectiles, and boss visual children are not
     * added independently.  Child tasks that retain a regular enemy's actor
     * ID naturally remain part of that enemy's native update path. */
    return (actor_id >= 0x0FAu && actor_id <= 0x100u) ||
           (actor_id >= 0x102u && actor_id <= 0x10Cu) ||
           (actor_id >= 0x10Fu && actor_id <= 0x110u) ||
           (actor_id >= 0x12Cu && actor_id <= 0x133u) ||
           actor_id == 0x136u ||
           (actor_id >= 0x13Au && actor_id <= 0x141u) ||
           (actor_id >= 0x144u && actor_id <= 0x145u) ||
           (actor_id >= 0x147u && actor_id <= 0x148u) ||
           actor_id == 0x190u || actor_id == 0x1A6u;
}

static void clear_runtime_tracking(void)
{
    unsigned int index;

    for (index = 0; index < HYPER_SEEN_CAPACITY; index++)
        s_seen_actors[index].task = 0;

    s_seen_replace_index = 0;
    s_capture_valid = 0;
    s_mind_control_combat_active = 0;
    s_captured_post_callback = 0;
    s_congo_actor.task = 0;
    s_tsurami_actor.task = 0;
    s_darumanyo_actor.task = 0;
    s_mind_control_robot.task = 0;
    s_benkei_actor.task = 0;
    s_thaisamba_special_boss.state = 0;
    s_thaisamba_special_boss.task = 0;
    s_thaisamba_special_boss.object = 0;
}

static int refresh_runtime_state(void)
{
    /* Keep exact boss identities current while the option is disabled.  That
     * lets a player enable Hyper Enemies after a fight has already started. */
    if (!extra_options_save_is_loaded())
    {
        if (s_runtime_active)
            clear_runtime_tracking();
        s_runtime_active = 0;
        return 0;
    }

    if (!s_runtime_active || s_runtime_room != D_800C7AB2)
    {
        clear_runtime_tracking();
        s_runtime_room = D_800C7AB2;
        s_runtime_active = 1;
    }
    return 1;
}

static int identity_matches(const HyperActorIdentity *identity)
{
    void *task = identity->task;

    return task && D_8016DAB4_16E6B4 == task &&
           TASK_OBJECT(task) == identity->object &&
           TASK_ACTOR_ID(task) == identity->actor_id &&
           TASK_GENERATION(task) == identity->generation;
}

static int identities_equal(const HyperActorIdentity *left,
                            const HyperActorIdentity *right)
{
    return left->task == right->task && left->object == right->object &&
           left->actor_id == right->actor_id &&
           left->generation == right->generation;
}

static int actor_has_completed_native_update(
    const HyperActorIdentity *identity)
{
    unsigned int index;
    unsigned int empty_index = HYPER_SEEN_CAPACITY;

    for (index = 0; index < HYPER_SEEN_CAPACITY; index++)
    {
        HyperActorIdentity *seen = &s_seen_actors[index];

        if (!seen->task)
        {
            if (empty_index == HYPER_SEEN_CAPACITY)
                empty_index = index;
            continue;
        }

        if (identities_equal(seen, identity))
        {
            return 1;
        }

        /* Actor-pool slots and their object storage are recycled.  Replace
         * the prior generation in place so the eight-bit generation counter
         * cannot wrap back to an ancient identity left elsewhere in cache. */
        if (seen->task == identity->task)
        {
            *seen = *identity;
            return 0;
        }
    }

    if (empty_index != HYPER_SEEN_CAPACITY)
        s_seen_actors[empty_index] = *identity;
    else
        s_seen_actors[s_seen_replace_index] = *identity;

    s_seen_replace_index++;
    if (s_seen_replace_index == HYPER_SEEN_CAPACITY)
        s_seen_replace_index = 0;
    return 0;
}

static int callback_is_enabled(ExtraOptionsTaskCallback callback)
{
    unsigned int address = (unsigned int)(unsigned long)callback;

    return callback && (address & TASK_CALLBACK_DISABLED_BIT) == 0;
}

static int tracked_actor_matches(const HyperActorIdentity *tracked,
                                 void *task)
{
    return tracked->task == task && task &&
           tracked->object == TASK_OBJECT(task) &&
           tracked->actor_id == TASK_ACTOR_ID(task) &&
           tracked->generation == TASK_GENERATION(task);
}

static int is_live_hyper_target(void *task)
{
    unsigned short room;

    if (!task || (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0)
        return 0;

    if (is_regular_enemy(TASK_ACTOR_ID(task)))
        return TASK_HEALTH(task) > 0;

    room = D_800C7AB2;
    if (tracked_actor_matches(&s_congo_actor, task) && room == ROOM_CONGO &&
        TASK_ENTITY_ID(task) == ENTITY_CONGO)
    {
        return TASK_HEALTH(task) > 0;
    }

    /* Tsurami enters its native death sequence at one HP. */
    if (tracked_actor_matches(&s_tsurami_actor, task) && room == ROOM_TSURAMI)
        return TASK_HEALTH(task) > 1;

    if (tracked_actor_matches(&s_darumanyo_actor, task) &&
        room == ROOM_DARUMANYO &&
        TASK_ENTITY_ID(task) == ENTITY_DARUMANYO)
    {
        return DARUMANYO_LIVES(task) > 0;
    }

    if (tracked_actor_matches(&s_mind_control_robot, task) &&
        room == ROOM_DRAGON_FIGHT &&
        TASK_ENTITY_ID(task) == ENTITY_MIND_CONTROL_ROBOT)
    {
        return s_mind_control_combat_active && TASK_HEALTH(task) > 0;
    }

    if (tracked_actor_matches(&s_benkei_actor, task) && room == ROOM_BENKEI &&
        TASK_ENTITY_ID(task) == ENTITY_BENKEI)
    {
        return BENKEI_STATE(task) != 0 && BENKEI_LIVES(task) > 0 &&
               func_800240DC_24CDC(BENKEI_FIGHT_ACTIVE_FLAG);
    }

    return 0;
}

static void track_boss_actor(HyperActorIdentity *tracked_actor, void *actor)
{
    void *object;

    if (!actor || !refresh_runtime_state())
        return;

    object = TASK_OBJECT(actor);
    if (!object)
        return;

    tracked_actor->task = actor;
    tracked_actor->object = object;
    tracked_actor->actor_id = TASK_ACTOR_ID(actor);
    tracked_actor->generation = TASK_GENERATION(actor);
}

/* The multiplayer implementation established these as the exact live boss
 * roots/controllers.  Tracking callbacks avoids broad actor-ID matches for
 * multipart bosses and Benkei's non-combat NPC form. */
RECOMP_HOOK("func_0800A228_6BD4C8")
void extra_options_track_hyper_congo(void *actor)
{
    if (actor && TASK_ENTITY_ID(actor) == ENTITY_CONGO)
        track_boss_actor(&s_congo_actor, actor);
}

RECOMP_HOOK("func_080017F4_6B4A94")
void extra_options_capture_hyper_tsurami(void *actor)
{
    track_boss_actor(&s_tsurami_actor, actor);
}

RECOMP_HOOK("func_08000388_6B3628")
void extra_options_track_hyper_tsurami(void *actor)
{
    track_boss_actor(&s_tsurami_actor, actor);
}

RECOMP_HOOK("func_08003E64_6CC074")
void extra_options_capture_hyper_darumanyo(void *actor)
{
    track_boss_actor(&s_darumanyo_actor, actor);
}

RECOMP_HOOK("func_08003F84_6CC194")
void extra_options_track_hyper_darumanyo(void *actor)
{
    if (actor && TASK_ENTITY_ID(actor) == ENTITY_DARUMANYO)
        track_boss_actor(&s_darumanyo_actor, actor);
}

RECOMP_HOOK("func_08000B98_70ABD8")
void extra_options_track_hyper_benkei(void *actor)
{
    track_boss_actor(&s_benkei_actor, actor);
}

/* The Dragon Fight robot is a multipart scripted miniboss.  Track only its
 * exact health-bearing child; matching entity 0x1B0 globally would also catch
 * encounter/cutscene tasks that are required for progression. */
RECOMP_HOOK("func_08002D18_703E18")
void extra_options_capture_hyper_mind_control_robot(void *actor)
{
    if (!actor)
        return;

    s_mind_control_combat_active = 0;
    track_boss_actor(&s_mind_control_robot, actor);
}

RECOMP_HOOK("func_08002EB4_703FB4")
void extra_options_track_hyper_mind_control_robot(void *actor)
{
    if (actor && TASK_ENTITY_ID(actor) == ENTITY_MIND_CONTROL_ROBOT)
        track_boss_actor(&s_mind_control_robot, actor);
}

/* 02EB4 is the pre-fight proximity waiter.  Its transition installs 02F58 as
 * the first combat state, so latch Hyper behavior there instead of replaying
 * the encounter trigger itself. */
RECOMP_HOOK("func_08002F58_704058")
void extra_options_start_hyper_mind_control_robot(void *actor)
{
    if (!actor || TASK_ENTITY_ID(actor) != ENTITY_MIND_CONTROL_ROBOT)
        return;

    track_boss_actor(&s_mind_control_robot, actor);
    if (tracked_actor_matches(&s_mind_control_robot, actor))
        s_mind_control_combat_active = 1;
}

/* Thaisamba runs in the file_13 Impact-battle overlay instead of the common
 * room-actor system.  Its unique initializer gives us the exact task and
 * shared battle state without relying on overlapping overlay addresses. */
RECOMP_HOOK("func_801EF2E0_61A6C0")
void extra_options_track_hyper_thaisamba(void *task)
{
    void *state = D_8020EED0_63A2B0;

    if (!task || !state || !refresh_runtime_state())
        return;

    s_thaisamba_special_boss.state = state;
    s_thaisamba_special_boss.task = task;
    s_thaisamba_special_boss.object = TASK_OBJECT(task);
}

static int thaisamba_special_boss_is_live(void)
{
    void *state = D_8020EED0_63A2B0;
    void *task = s_thaisamba_special_boss.task;
    void *object;

    if (!state || state != s_thaisamba_special_boss.state || !task ||
        THAISAMBA_STATE_BOSS_TASK(state) != task ||
        !THAISAMBA_STATE_MODEL_TASK(state) ||
        THAISAMBA_STATE_HEALTH(state) <= 0 ||
        THAISAMBA_STATE_COMBAT_PAUSED(state) != 0)
    {
        return 0;
    }

    object = TASK_OBJECT(task);
    if (!object)
        return 0;

    /* The task object is installed before the initializer normally runs.  A
     * lazy bind also covers an allocator that finishes it during setup. */
    if (!s_thaisamba_special_boss.object)
        s_thaisamba_special_boss.object = object;
    return object == s_thaisamba_special_boss.object;
}

/* The dedicated collision dispatcher runs once per Thaisamba frame and is
 * separate from the boss's dynamic AI callback.  After it consumes native
 * damage once, replay only the current AI state so movement, attack timers,
 * and projectile spawning advance four times while collision/damage stays
 * normal. */
RECOMP_HOOK_RETURN("func_801F6C4C_62202C")
void extra_options_run_hyper_thaisamba_tick(void)
{
    ExtraOptionsTaskCallback callback;
    unsigned int extra_tick;
    unsigned short room;
    void *saved_current_task;
    void *task;

    if (s_replay_guard || !hyper_enemies_is_enabled() ||
        !refresh_runtime_state() || !thaisamba_special_boss_is_live())
    {
        return;
    }

    /* State callbacks use the scheduler's current-task global when changing
     * callbacks or spawning children.  Recreate that native context for the
     * extra ticks, then restore the scheduler's post-pass value after every
     * replay even if the callback deletes its own task. */
    task = s_thaisamba_special_boss.task;
    room = D_800C7AB2;
    saved_current_task = D_8016DAB4_16E6B4;
    s_replay_guard = 1;
    for (extra_tick = 0; extra_tick < HYPER_EXTRA_TICKS; extra_tick++)
    {
        if (D_800C7AB2 != room || !thaisamba_special_boss_is_live())
            break;

        callback = TASK_AI_CALLBACK(task);
        if (!callback_is_enabled(callback))
            break;

        D_8016DAB4_16E6B4 = task;
        callback(task, s_thaisamba_special_boss.object);

        /* A task deletion or transition can replace the scheduler's current
         * task.  Do not replay a stale task after that native state change. */
        if (D_8016DAB4_16E6B4 != task)
        {
            D_8016DAB4_16E6B4 = saved_current_task;
            break;
        }
        D_8016DAB4_16E6B4 = saved_current_task;
    }
    s_replay_guard = 0;
    D_8016DAB4_16E6B4 = saved_current_task;
}

RECOMP_HOOK("func_80218F30_5D4400")
void extra_options_capture_hyper_actor(void *task)
{
    void *object;

    if (s_replay_guard)
        return;

    s_capture_valid = 0;
    s_captured_post_callback = 0;
    if (!hyper_enemies_is_enabled() || !refresh_runtime_state() ||
        task != D_8016DAB4_16E6B4 ||
        !is_live_hyper_target(task))
    {
        return;
    }

    object = TASK_OBJECT(task);
    if (!object)
        return;

    s_captured_actor.task = task;
    s_captured_actor.object = object;
    s_captured_actor.actor_id = TASK_ACTOR_ID(task);
    s_captured_actor.generation = TASK_GENERATION(task);
    s_captured_post_callback = TASK_POST_CALLBACK(task);
    s_capture_valid = 1;
}

RECOMP_HOOK_RETURN("func_80218F30_5D4400")
void extra_options_run_hyper_actor_tick(void)
{
    HyperActorIdentity identity;
    ExtraOptionsTaskCallback ai_callback;
    ExtraOptionsTaskCallback post_callback;
    unsigned int extra_tick;
    unsigned short room;
    void *object;

    if (s_replay_guard || !s_capture_valid)
        return;

    identity = s_captured_actor;
    post_callback = s_captured_post_callback;
    s_capture_valid = 0;
    s_captured_post_callback = 0;

    if (!hyper_enemies_is_enabled() || !refresh_runtime_state() ||
        !identity_matches(&identity) ||
        !is_live_hyper_target(identity.task) ||
        !actor_has_completed_native_update(&identity))
    {
        return;
    }

    room = D_800C7AB2;
    if (!callback_is_enabled(post_callback))
        return;

    /* The fresh actor's first completed update registered its identity and
     * returned above, so this is a recurring AI state, not its constructor. */
    s_replay_guard = 1;
    for (extra_tick = 0; extra_tick < HYPER_EXTRA_TICKS; extra_tick++)
    {
        /* Reload the AI callback between ticks so native state transitions
         * take effect immediately.  Room, identity, and liveness guards stop
         * a deletion or recycled task slot from receiving another replay. */
        if (D_800C7AB2 != room || !identity_matches(&identity) ||
            !is_live_hyper_target(identity.task) ||
            TASK_POST_CALLBACK(identity.task) != post_callback)
        {
            break;
        }

        ai_callback = TASK_AI_CALLBACK(identity.task);
        if (!callback_is_enabled(ai_callback))
            break;

        ai_callback(identity.task, identity.object);

        /* AI may delete/recycle the task or replace its object.  If it merely
         * marks the actor for removal, still run the native post callback so
         * the game's own cleanup consumes that state. */
        if (D_800C7AB2 != room || !identity_matches(&identity))
            break;

        object = TASK_OBJECT(identity.task);
        /* A state transition may replace +0x10 with a teardown callback.  It
         * belongs to the following scheduler pass, not this extra movement
         * tick, so only replay the post callback that just completed. */
        if (TASK_POST_CALLBACK(identity.task) == post_callback &&
            callback_is_enabled(post_callback))
        {
            post_callback(identity.task, object);
        }
        else
        {
            break;
        }
    }
    s_replay_guard = 0;
}
