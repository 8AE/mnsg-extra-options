#include "modding.h"
#include "extra_options.h"
#include "hyper_enemies.h"
#include "recompconfig.h"
#include "recomputils.h"

/*
 * Dharumanyo is a multipart boss.  The visible actor owns movement, attack
 * states, animation, and projectile spawning, while a linked entity-0xCC
 * carrier owns the twelve real lives and all damage/death transitions.
 *
 * Hyper alternates one and two replays of only the visible actor's AI plus
 * its custom post callback, yielding 2.5x over each stable two-frame pair.
 * The damage carrier still receives exactly one collision/damage pass per
 * scheduler frame; only its harmless orbit angle gets the same average rate.
 * Only the travelling 02620 projectile is admitted to the common Hyper path.
 * Trail, impact, hit-reaction, phase, and death children remain native-speed.
 */

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
#define TASK_LINK(task) \
    (*(void *volatile *)((char *)(task) + 0xDC))
#define DARUMANYO_CARRIER_YAW(task) \
    (*(volatile unsigned short *)((char *)(task) + 0xD8))
#define DARUMANYO_LIVES(task) \
    (*(volatile unsigned char *)((char *)(task) + 0xD1))

#define TASK_STATUS_REMOVE_PENDING 0x00000002u

/* File 31 belongs to actor 0xCC.  Actor 0x132 is the unrelated ordinary
 * Bouncing Darumanyo enemy from the multiplayer roster. */
#define ACTOR_DARUMANYO 0x00CCu
#define ENTITY_DARUMANYO_CARRIER 0x00CCu
#define ROOM_DARUMANYO 0x0049u
#define DARUMANYO_COMBAT_PAUSED_FLAG 0x016Cu

#define DARUMANYO_PROJECTILE_CAPACITY 16u
#define DARUMANYO_MIN_EXTRA_TICKS 1u
#define DARUMANYO_MAX_EXTRA_TICKS 2u
#define DARUMANYO_YAW_MASK 0x03FFu

#define DARUMANYO_PHASE_2 0x0200u
#define DARUMANYO_PHASE_3 0x0400u
#define DARUMANYO_PHASE_4 0x0800u

typedef void (*HyperDharumanyoTaskCallback)(void *task, void *object);

/* Host tests replace these target-ABI reads. */
#ifndef DHARUMANYO_TASK_AI
#define DHARUMANYO_TASK_AI(task) \
    (*(HyperDharumanyoTaskCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef DHARUMANYO_TASK_POST
#define DHARUMANYO_TASK_POST(task) \
    (*(HyperDharumanyoTaskCallback volatile *)((char *)(task) + 0x10))
#endif
#ifndef DHARUMANYO_PHASE_FLAGS
#define DHARUMANYO_PHASE_FLAGS \
    (*(volatile unsigned int *)0x8015CDB4)
#endif
#ifndef DHARUMANYO_WORLD_FRAME
extern unsigned char *D_8015C5C8_15D1C8;
#define DHARUMANYO_WORLD_FRAME \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADCE))
#endif

typedef struct
{
    void *task;
    void *object;
    unsigned short actor_id;
    unsigned char generation;
    unsigned char cadence_phase;
} HyperDharumanyoIdentity;

extern unsigned short D_800C7AB2;
extern void *D_8016DAB4_16E6B4;
extern int func_800240DC_24CDC(int flag_id);
extern void func_0800284C_6CAA5C(void *task, void *object);
extern void func_08002A40_6CAC50(void *task, void *object);
extern void func_08003810_6CBA20(void *task, void *object);
extern void func_08003F84_6CC194(void *task, void *object);
extern void func_80218F30_5D4400(void *task, void *object);

static HyperDharumanyoIdentity s_dharumanyo;
static HyperDharumanyoIdentity s_lives_carrier;
static HyperDharumanyoIdentity
    s_projectiles[DARUMANYO_PROJECTILE_CAPACITY];
static HyperDharumanyoIdentity s_pending_projectile;
static unsigned int s_next_projectile;
static unsigned short s_runtime_room;
static unsigned char s_runtime_active;
static unsigned char s_combat_active;
static unsigned char s_pending_projectile_valid;
static unsigned char s_replay_reported;
static unsigned char s_root_return_guard;
static unsigned char s_root_cadence_phase;
static unsigned char s_frame_budget_valid;
static unsigned char s_frame_extra_ticks;
static unsigned short s_budget_world_frame;
static unsigned char s_root_result_valid;
static unsigned char s_root_completed_extra_ticks;
static unsigned short s_root_result_world_frame;

static int hyper_dharumanyo_is_enabled(void)
{
    return recomp_get_config_u32("hyper_enemies") == 0;
}

static void clear_dharumanyo_tracking(void)
{
    unsigned int index;

    s_dharumanyo.task = 0;
    s_lives_carrier.task = 0;
    s_pending_projectile.task = 0;
    s_dharumanyo.cadence_phase = 0;
    s_lives_carrier.cadence_phase = 0;
    s_pending_projectile.cadence_phase = 0;
    s_next_projectile = 0;
    s_combat_active = 0;
    s_pending_projectile_valid = 0;
    s_replay_reported = 0;
    s_root_cadence_phase = 0;
    s_frame_budget_valid = 0;
    s_frame_extra_ticks = 0;
    s_budget_world_frame = 0;
    s_root_result_valid = 0;
    s_root_completed_extra_ticks = 0;
    s_root_result_world_frame = 0;
    for (index = 0; index < DARUMANYO_PROJECTILE_CAPACITY; index++)
    {
        s_projectiles[index].task = 0;
        s_projectiles[index].cadence_phase = 0;
    }
}

static int refresh_dharumanyo_runtime_state(void)
{
    /* Tracking remains live while the option is disabled so toggling Hyper
     * during the encounter can take effect without re-entering the room. */
    if (!extra_options_save_is_loaded())
    {
        if (s_runtime_active)
            clear_dharumanyo_tracking();
        s_runtime_active = 0;
        return 0;
    }

    if (!s_runtime_active || s_runtime_room != D_800C7AB2)
    {
        clear_dharumanyo_tracking();
        s_runtime_room = D_800C7AB2;
        s_runtime_active = 1;
    }
    return 1;
}

static int identity_matches(const HyperDharumanyoIdentity *identity,
                            void *task)
{
    return identity->task == task && task &&
           identity->object == TASK_OBJECT(task) &&
           identity->actor_id == TASK_ACTOR_ID(task) &&
           identity->generation == TASK_GENERATION(task);
}

static void bind_identity(HyperDharumanyoIdentity *identity, void *task)
{
    identity->task = task;
    identity->object = TASK_OBJECT(task);
    identity->actor_id = TASK_ACTOR_ID(task);
    identity->generation = TASK_GENERATION(task);
    identity->cadence_phase = 0;
}

static unsigned int take_extra_tick_budget(unsigned char *cadence_phase)
{
    unsigned int extra_ticks = *cadence_phase
                                   ? DARUMANYO_MAX_EXTRA_TICKS
                                   : DARUMANYO_MIN_EXTRA_TICKS;

    *cadence_phase = *cadence_phase ? 0 : 1;
    return extra_ticks;
}

static unsigned int dharumanyo_frame_extra_tick_budget(void)
{
    unsigned short world_frame = DHARUMANYO_WORLD_FRAME;

    if (!s_frame_budget_valid || s_budget_world_frame != world_frame)
    {
        s_budget_world_frame = world_frame;
        s_frame_extra_ticks = (unsigned char)take_extra_tick_budget(
            &s_root_cadence_phase);
        s_frame_budget_valid = 1;
    }
    return s_frame_extra_ticks;
}

static int hyper_dharumanyo_is_live(void *task)
{
    void *carrier;

    if (!refresh_dharumanyo_runtime_state() || !s_combat_active ||
        D_800C7AB2 != ROOM_DARUMANYO ||
        func_800240DC_24CDC(DARUMANYO_COMBAT_PAUSED_FLAG) ||
        !identity_matches(&s_dharumanyo, task) ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        TASK_ACTOR_ID(task) != ACTOR_DARUMANYO ||
        DHARUMANYO_TASK_POST(task) != func_08003810_6CBA20)
    {
        return 0;
    }

    carrier = TASK_LINK(task);
    return identity_matches(&s_lives_carrier, carrier) &&
           TASK_LINK(carrier) == task &&
           TASK_ACTOR_ID(carrier) == ACTOR_DARUMANYO &&
           TASK_ENTITY_ID(carrier) == ENTITY_DARUMANYO_CARRIER &&
           DHARUMANYO_TASK_POST(carrier) == func_08003F84_6CC194 &&
           (TASK_STATUS(carrier) & TASK_STATUS_REMOVE_PENDING) == 0 &&
           DARUMANYO_LIVES(carrier) > 0;
}

static void register_projectile(
    const HyperDharumanyoIdentity *projectile)
{
    unsigned int index;
    unsigned int empty_index = DARUMANYO_PROJECTILE_CAPACITY;

    for (index = 0; index < DARUMANYO_PROJECTILE_CAPACITY; index++)
    {
        if (s_projectiles[index].task == projectile->task)
        {
            s_projectiles[index] = *projectile;
            return;
        }
        if (!s_projectiles[index].task &&
            empty_index == DARUMANYO_PROJECTILE_CAPACITY)
        {
            empty_index = index;
        }
    }

    if (empty_index != DARUMANYO_PROJECTILE_CAPACITY)
        s_projectiles[empty_index] = *projectile;
    else
        s_projectiles[s_next_projectile] = *projectile;

    s_next_projectile++;
    if (s_next_projectile == DARUMANYO_PROJECTILE_CAPACITY)
        s_next_projectile = 0;
}

/* 00970 is the first neutral combat state after the entire native intro.
 * Latching here keeps camera/setup states native and gives every later replay
 * an exact, bidirectional visible-root/lives-carrier topology to validate. */
RECOMP_HOOK("func_08000970_6C8B80")
void extra_options_start_hyper_dharumanyo(void *task)
{
    void *carrier;
    void *object;

    if (!task || !refresh_dharumanyo_runtime_state() ||
        D_800C7AB2 != ROOM_DARUMANYO ||
        func_800240DC_24CDC(DARUMANYO_COMBAT_PAUSED_FLAG) ||
        TASK_ACTOR_ID(task) != ACTOR_DARUMANYO ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        DHARUMANYO_TASK_POST(task) != func_08003810_6CBA20)
    {
        return;
    }

    object = TASK_OBJECT(task);
    carrier = TASK_LINK(task);
    if (!object || !carrier || !TASK_OBJECT(carrier) ||
        TASK_LINK(carrier) != task ||
        TASK_ACTOR_ID(carrier) != ACTOR_DARUMANYO ||
        TASK_ENTITY_ID(carrier) != ENTITY_DARUMANYO_CARRIER ||
        DHARUMANYO_TASK_POST(carrier) != func_08003F84_6CC194 ||
        (TASK_STATUS(carrier) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        DARUMANYO_LIVES(carrier) == 0)
    {
        return;
    }

    if (!identity_matches(&s_dharumanyo, task) ||
        !identity_matches(&s_lives_carrier, carrier))
    {
        clear_dharumanyo_tracking();
        recomp_printf("[Extra Options] Dharumanyo tracked: room=0x%03X, "
                      "Hyper Enemies=%s.\n",
                      (unsigned int)D_800C7AB2,
                      hyper_dharumanyo_is_enabled()
                          ? "enabled" : "disabled");
        bind_identity(&s_dharumanyo, task);
        bind_identity(&s_lives_carrier, carrier);
    }

    s_combat_active = 1;
}

/* Capture after the native visible-boss post begins, then run the selected
 * one-or-two guarded AI/post pairs after it returns.  The separate carrier's
 * 03F84 damage pass is never called here. */
RECOMP_HOOK("func_08003810_6CBA20")
void extra_options_capture_hyper_dharumanyo(void *task)
{
    extra_options_hyper_capture_from_post(task, hyper_dharumanyo_is_live);
}

RECOMP_HOOK_RETURN("func_08003810_6CBA20")
void extra_options_run_hyper_dharumanyo_tick(void)
{
    unsigned int selected_extra_ticks = 0;
    unsigned int ticks;

    /* The selected post callback is replayed directly below, so its return
     * hook nests.  Only the outer native return may select a cadence half or
     * publish a carrier ticket. */
    if (s_root_return_guard)
        return;

    s_root_return_guard = 1;
    ticks = extra_options_hyper_run_captured_tick_budgeted(
        hyper_dharumanyo_is_live, 0,
        extra_options_hyper_dharumanyo_take_root_extra_ticks,
        &selected_extra_ticks);
    s_root_result_world_frame = DHARUMANYO_WORLD_FRAME;
    s_root_completed_extra_ticks = (unsigned char)ticks;
    s_root_result_valid = 1;
    s_root_return_guard = 0;

    if (selected_extra_ticks != 0 &&
        ticks == selected_extra_ticks && !s_replay_reported)
    {
        s_replay_reported = 1;
        recomp_printf("[Extra Options] Hyper Dharumanyo active: "
                      "alternating 1/2 extra AI/movement/animation ticks "
                      "(2.5x average).\n");
    }
}

static unsigned short dharumanyo_carrier_angle_step(void)
{
    unsigned int flags = DHARUMANYO_PHASE_FLAGS;

    if (flags & DARUMANYO_PHASE_4)
        return 0x0050u;
    if (flags & DARUMANYO_PHASE_3)
        return 0x0040u;
    if (flags & DARUMANYO_PHASE_2)
        return 0x0030u;
    return 0x0020u;
}

/* 03F84 owns collision, damage, lives, and death, so it must execute once.
 * Pre-add only the next one-or-two orbit-angle steps; native 03F84 supplies
 * the first step and computes the carrier position from the final angle. */
RECOMP_HOOK("func_08003F84_6CC194")
void extra_options_advance_hyper_dharumanyo_carrier(void *task)
{
    unsigned int extra_ticks;
    unsigned int yaw;

    if (!hyper_dharumanyo_is_enabled() ||
        task != D_8016DAB4_16E6B4 ||
        !identity_matches(&s_lives_carrier, task) ||
        !hyper_dharumanyo_is_live(s_dharumanyo.task))
    {
        return;
    }

    /* The native task list runs the visible root before this linked carrier.
     * Follow the root's actual completed count so first-seen gates and partial
     * state transitions cannot advance the orbit or consume cadence alone. */
    if (!s_root_result_valid ||
        s_root_result_world_frame != DHARUMANYO_WORLD_FRAME)
    {
        return;
    }
    extra_ticks = s_root_completed_extra_ticks;
    if (extra_ticks == 0)
        return;

    yaw = DARUMANYO_CARRIER_YAW(task);
    yaw += extra_ticks * dharumanyo_carrier_angle_step();
    DARUMANYO_CARRIER_YAW(task) =
        (unsigned short)(yaw & DARUMANYO_YAW_MASK);
}

/* Native 02410 installs projectile+0xDC=root after allocation and before the
 * scheduled 02620 initializer runs.  The constructor then installs entity
 * 0xCC, one of the two travelling AIs, and the common post callback.  Capture
 * the linked identity on entry and validate those native writes on return. */
RECOMP_HOOK("func_08002620_6CA830")
void extra_options_capture_hyper_dharumanyo_projectile(void *task)
{
    s_pending_projectile_valid = 0;
    if (!task || !TASK_OBJECT(task) ||
        TASK_ACTOR_ID(task) != ACTOR_DARUMANYO ||
        !hyper_dharumanyo_is_live(s_dharumanyo.task) ||
        TASK_LINK(task) != s_dharumanyo.task)
    {
        return;
    }

    bind_identity(&s_pending_projectile, task);
    s_pending_projectile_valid = 1;
}

RECOMP_HOOK_RETURN("func_08002620_6CA830")
void extra_options_track_hyper_dharumanyo_projectile(void)
{
    HyperDharumanyoIdentity projectile;

    if (!s_pending_projectile_valid)
        return;

    projectile = s_pending_projectile;
    s_pending_projectile_valid = 0;
    s_pending_projectile.task = 0;
    if (!hyper_dharumanyo_is_live(s_dharumanyo.task) ||
        !identity_matches(&projectile, projectile.task) ||
        TASK_ENTITY_ID(projectile.task) != ENTITY_DARUMANYO_CARRIER ||
        (DHARUMANYO_TASK_AI(projectile.task) != func_0800284C_6CAA5C &&
         DHARUMANYO_TASK_AI(projectile.task) != func_08002A40_6CAC50) ||
        DHARUMANYO_TASK_POST(projectile.task) != func_80218F30_5D4400)
    {
        return;
    }

    /* Force the first common post to seed the shared anti-constructor cache,
     * even when an eight-bit task generation has wrapped on a reused slot. */
    extra_options_hyper_forget_task(projectile.task);
    register_projectile(&projectile);
}

int extra_options_hyper_dharumanyo_projectile_is_live(void *task)
{
    unsigned int index;

    if (!task || !hyper_dharumanyo_is_live(s_dharumanyo.task) ||
        TASK_ACTOR_ID(task) != ACTOR_DARUMANYO ||
        TASK_ENTITY_ID(task) != ENTITY_DARUMANYO_CARRIER ||
        (DHARUMANYO_TASK_AI(task) != func_0800284C_6CAA5C &&
         DHARUMANYO_TASK_AI(task) != func_08002A40_6CAC50) ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        TASK_LINK(task) != s_dharumanyo.task ||
        DHARUMANYO_TASK_POST(task) != func_80218F30_5D4400)
    {
        return 0;
    }

    for (index = 0; index < DARUMANYO_PROJECTILE_CAPACITY; index++)
        if (identity_matches(&s_projectiles[index], task))
            return 1;
    return 0;
}

int extra_options_hyper_dharumanyo_take_root_extra_ticks(
    void *task, unsigned int *extra_ticks)
{
    if (!task || !extra_ticks)
        return 0;

    if (identity_matches(&s_dharumanyo, task) &&
        hyper_dharumanyo_is_live(task))
    {
        *extra_ticks = dharumanyo_frame_extra_tick_budget();
        return 1;
    }

    return 0;
}

int extra_options_hyper_dharumanyo_take_projectile_extra_ticks(
    void *task, unsigned int *extra_ticks)
{
    unsigned int index;

    if (!task || !extra_ticks)
        return 0;

    for (index = 0; index < DARUMANYO_PROJECTILE_CAPACITY; index++)
    {
        if (identity_matches(&s_projectiles[index], task) &&
            extra_options_hyper_dharumanyo_projectile_is_live(task))
        {
            *extra_ticks = take_extra_tick_budget(
                &s_projectiles[index].cadence_phase);
            return 1;
        }
    }
    return 0;
}
