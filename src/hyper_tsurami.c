#include "modding.h"
#include "extra_options.h"
#include "hyper_enemies.h"
#include "recompconfig.h"
#include "recomputils.h"

/*
 * Ghost Robot Tsurami's health-bearing root does not use the ordinary
 * func_80218F30 post callback.  Its private func_08000388 callback runs the
 * whole common actor pipeline, then damage/phase bookkeeping and visual
 * effects.  Replaying that callback would process damage again.
 *
 * Instead, after the one native post pass, Hyper alternates one and two
 * additional combat AI ticks and runs only the two proven common helpers
 * that advance model animation and apply velocity.  That yields 2.5x over
 * each stable two-frame pair.  Damage, collision, phase transitions,
 * particles, and defeat therefore remain single-pass.  Every travelling
 * attack from the three phases is registered through its exact 049A4
 * constructor and delegated to the ordinary Hyper projectile path.
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
#define TASK_HEALTH(task) \
    (*(volatile unsigned char *)((char *)(task) + 0x8D))
#define TASK_OWNER(task) \
    (*(void *volatile *)((char *)(task) + 0xD0))
#define TASK_ATTACK_FLAGS(task) \
    (*(volatile unsigned int *)((char *)(task) + 0xE8))

#define TASK_STATUS_REMOVE_PENDING 0x00000002u
#define TASK_CALLBACK_DISABLED_BIT 0x00800000u

#define ROOM_TSURAMI 0x0071u
#define ACTOR_TSURAMI 0x00CBu
#define ENTITY_TSURAMI_CHILD 0x00CBu

#define TSURAMI_MIN_EXTRA_TICKS 1u
#define TSURAMI_MAX_EXTRA_TICKS 2u
#define TSURAMI_PROJECTILE_CAPACITY 128u

#define TSURAMI_PROJECTILE_MODE_MASK 0x0000001Fu
#define TSURAMI_PROJECTILE_MODE_1 0x00000001u
#define TSURAMI_PROJECTILE_MODE_2 0x00000002u
#define TSURAMI_PROJECTILE_MODE_4 0x00000004u
#define TSURAMI_PROJECTILE_MODE_8 0x00000008u
#define TSURAMI_PROJECTILE_MODE_10 0x00000010u

typedef void (*HyperTsuramiTaskCallback)(void *task, void *object);

/* Host tests replace the target's four-byte callback slots. */
#ifndef TSURAMI_TASK_AI
#define TSURAMI_TASK_AI(task) \
    (*(HyperTsuramiTaskCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef TSURAMI_TASK_POST
#define TSURAMI_TASK_POST(task) \
    (*(HyperTsuramiTaskCallback volatile *)((char *)(task) + 0x10))
#endif
#ifndef TSURAMI_CALLBACK_IS_ENABLED
#define TSURAMI_CALLBACK_IS_ENABLED(callback) \
    ((callback) && \
     (((unsigned long)(callback) & TASK_CALLBACK_DISABLED_BIT) == 0))
#endif

typedef struct
{
    void *task;
    void *object;
    unsigned short actor_id;
    unsigned char generation;
    unsigned char cadence_phase;
} HyperTsuramiIdentity;

extern unsigned short D_800C7AB2;
extern unsigned int D_8015C5E4;
extern void *D_8016DAB4_16E6B4;
extern void func_80216AB8_5D1F88(void *task);
extern void func_8021B808_5D6CD8(void *task);
extern void func_80218F30_5D4400(void *task, void *object);
extern void func_08000388_6B3628(void *task, void *object);
extern void func_08001D54_6B4FF4(void *task, void *object);
extern void func_08001DB0_6B5050(void *task, void *object);
extern void func_08001DF0_6B5090(void *task, void *object);
extern void func_08001EAC_6B514C(void *task, void *object);
extern void func_08001F28_6B51C8(void *task, void *object);
extern void func_08001F68_6B5208(void *task, void *object);
extern void func_08001FE8_6B5288(void *task, void *object);
extern void func_08002028_6B52C8(void *task, void *object);
extern void func_080020D8_6B5378(void *task, void *object);
extern void func_08002128_6B53C8(void *task, void *object);
extern void func_08002190_6B5430(void *task, void *object);
extern void func_080023E0_6B5680(void *task, void *object);
extern void func_08002460_6B5700(void *task, void *object);
extern void func_080024A0_6B5740(void *task, void *object);
extern void func_080024F0_6B5790(void *task, void *object);
extern void func_08002534_6B57D4(void *task, void *object);
extern void func_0800257C_6B581C(void *task, void *object);
extern void func_080027E4_6B5A84(void *task, void *object);
extern void func_08002864_6B5B04(void *task, void *object);
extern void func_080028A4_6B5B44(void *task, void *object);
extern void func_08002918_6B5BB8(void *task, void *object);
extern void func_080029D8_6B5C78(void *task, void *object);
extern void func_08002C4C_6B5EEC(void *task, void *object);
extern void func_08002CD8_6B5F78(void *task, void *object);
extern void func_08002D2C_6B5FCC(void *task, void *object);
extern void func_08002D6C_6B600C(void *task, void *object);
extern void func_08002DA8_6B6048(void *task, void *object);
extern void func_080046B8_6B7958(void *task, void *object);
extern void func_0800476C_6B7A0C(void *task, void *object);
extern void func_08004ED0_6B8170(void *task, void *object);

static HyperTsuramiIdentity s_tsurami;
static HyperTsuramiIdentity s_visual_child;
static HyperTsuramiIdentity
    s_projectiles[TSURAMI_PROJECTILE_CAPACITY];
static HyperTsuramiIdentity s_pending_visual;
static HyperTsuramiIdentity s_pending_projectile;
static HyperTsuramiIdentity s_captured_root;
static unsigned int s_next_projectile;
static unsigned short s_runtime_room;
static unsigned char s_runtime_active;
static unsigned char s_combat_active;
static unsigned char s_pending_visual_valid;
static unsigned char s_pending_projectile_valid;
static unsigned char s_root_capture_valid;
static unsigned char s_replay_guard;
static unsigned char s_track_reported;
static unsigned char s_replay_reported;

static int hyper_tsurami_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("hyper_enemies") == 0;
}

static void clear_tsurami_tracking(void)
{
    unsigned int index;

    s_tsurami.task = 0;
    s_visual_child.task = 0;
    s_pending_visual.task = 0;
    s_pending_projectile.task = 0;
    s_captured_root.task = 0;
    s_tsurami.cadence_phase = 0;
    s_visual_child.cadence_phase = 0;
    s_pending_visual.cadence_phase = 0;
    s_pending_projectile.cadence_phase = 0;
    s_captured_root.cadence_phase = 0;
    s_next_projectile = 0;
    s_combat_active = 0;
    s_pending_visual_valid = 0;
    s_pending_projectile_valid = 0;
    s_root_capture_valid = 0;
    s_replay_guard = 0;
    s_track_reported = 0;
    s_replay_reported = 0;
    for (index = 0; index < TSURAMI_PROJECTILE_CAPACITY; index++)
    {
        s_projectiles[index].task = 0;
        s_projectiles[index].cadence_phase = 0;
    }
}

static int refresh_tsurami_runtime_state(void)
{
    /* Continue tracking while disabled so a mid-fight config toggle works. */
    if (!extra_options_save_is_loaded())
    {
        if (s_runtime_active)
            clear_tsurami_tracking();
        s_runtime_active = 0;
        return 0;
    }

    if (!s_runtime_active || s_runtime_room != D_800C7AB2)
    {
        clear_tsurami_tracking();
        s_runtime_room = D_800C7AB2;
        s_runtime_active = 1;
    }
    return 1;
}

static void bind_identity(HyperTsuramiIdentity *identity, void *task)
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
                                   ? TSURAMI_MAX_EXTRA_TICKS
                                   : TSURAMI_MIN_EXTRA_TICKS;

    *cadence_phase = *cadence_phase ? 0 : 1;
    return extra_ticks;
}

static int identity_matches(const HyperTsuramiIdentity *identity,
                            void *task)
{
    return identity->task == task && task &&
           identity->object == TASK_OBJECT(task) &&
           identity->actor_id == TASK_ACTOR_ID(task) &&
           identity->generation == TASK_GENERATION(task);
}

static int callback_is_enabled(HyperTsuramiTaskCallback callback)
{
    return TSURAMI_CALLBACK_IS_ENABLED(callback);
}

static int callback_is_combat_state(HyperTsuramiTaskCallback callback)
{
    /* Explicitly omit intro 1A50..1D0C, hit reaction 2E00..2FA4, and the
     * complete 2FFC..3A00 defeat sequence. */
    return callback == func_08001D54_6B4FF4 ||
           callback == func_08001DB0_6B5050 ||
           callback == func_08001DF0_6B5090 ||
           callback == func_08001EAC_6B514C ||
           callback == func_08001F28_6B51C8 ||
           callback == func_08001F68_6B5208 ||
           callback == func_08001FE8_6B5288 ||
           callback == func_08002028_6B52C8 ||
           callback == func_080020D8_6B5378 ||
           callback == func_08002128_6B53C8 ||
           callback == func_08002190_6B5430 ||
           callback == func_080023E0_6B5680 ||
           callback == func_08002460_6B5700 ||
           callback == func_080024A0_6B5740 ||
           callback == func_080024F0_6B5790 ||
           callback == func_08002534_6B57D4 ||
           callback == func_0800257C_6B581C ||
           callback == func_080027E4_6B5A84 ||
           callback == func_08002864_6B5B04 ||
           callback == func_080028A4_6B5B44 ||
           callback == func_08002918_6B5BB8 ||
           callback == func_080029D8_6B5C78 ||
           callback == func_08002C4C_6B5EEC ||
           callback == func_08002CD8_6B5F78 ||
           callback == func_08002D2C_6B5FCC ||
           callback == func_08002D6C_6B600C ||
           callback == func_08002DA8_6B6048;
}

static int tsurami_encounter_is_live(void *task)
{
    return refresh_tsurami_runtime_state() && s_combat_active &&
           D_800C7AB2 == ROOM_TSURAMI &&
           identity_matches(&s_tsurami, task) &&
           TASK_ACTOR_ID(task) == ACTOR_TSURAMI &&
           (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) == 0 &&
           TASK_HEALTH(task) > 1 &&
           TSURAMI_TASK_POST(task) == func_08000388_6B3628;
}

static int hyper_tsurami_root_is_live(void *task)
{
    HyperTsuramiTaskCallback callback;

    /* The native 00388 post skips its entire body while this player-state
     * counter is zero.  Never synthesize ticks when the native tick did not
     * run. */
    if (!D_8015C5E4 || !tsurami_encounter_is_live(task))
        return 0;

    callback = TSURAMI_TASK_AI(task);
    return callback_is_enabled(callback) &&
           callback_is_combat_state(callback);
}

static void register_projectile(
    const HyperTsuramiIdentity *projectile)
{
    unsigned int index;
    unsigned int empty_index = TSURAMI_PROJECTILE_CAPACITY;

    for (index = 0; index < TSURAMI_PROJECTILE_CAPACITY; index++)
    {
        if (s_projectiles[index].task == projectile->task)
        {
            s_projectiles[index] = *projectile;
            return;
        }
        if (!s_projectiles[index].task &&
            empty_index == TSURAMI_PROJECTILE_CAPACITY)
        {
            empty_index = index;
        }
    }

    if (empty_index != TSURAMI_PROJECTILE_CAPACITY)
        s_projectiles[empty_index] = *projectile;
    else
        s_projectiles[s_next_projectile] = *projectile;

    s_next_projectile++;
    if (s_next_projectile == TSURAMI_PROJECTILE_CAPACITY)
        s_next_projectile = 0;
}

static int projectile_mode_is_valid(unsigned int flags)
{
    switch (flags & TSURAMI_PROJECTILE_MODE_MASK)
    {
    case TSURAMI_PROJECTILE_MODE_1:
    case TSURAMI_PROJECTILE_MODE_2:
    case TSURAMI_PROJECTILE_MODE_4:
    case TSURAMI_PROJECTILE_MODE_8:
    case TSURAMI_PROJECTILE_MODE_10:
        return 1;
    default:
        return 0;
    }
}

/* The unique root initializer marks a fresh encounter before it creates the
 * model and intro children.  Final root validation happens in its 00388 post. */
RECOMP_HOOK("func_080017F4_6B4A94")
void extra_options_begin_hyper_tsurami(void *task)
{
    if (!task || !refresh_tsurami_runtime_state() ||
        D_800C7AB2 != ROOM_TSURAMI || !TASK_OBJECT(task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI)
    {
        return;
    }

    clear_tsurami_tracking();
    bind_identity(&s_tsurami, task);
}

/* 1D54 is the first neutral combat callback after the intro flag completes.
 * Hit reactions later return here, while their own callbacks stay excluded. */
RECOMP_HOOK("func_08001D54_6B4FF4")
void extra_options_start_hyper_tsurami(void *task)
{
    if (!task || !refresh_tsurami_runtime_state() ||
        D_800C7AB2 != ROOM_TSURAMI ||
        !identity_matches(&s_tsurami, task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        TASK_HEALTH(task) <= 1 ||
        TSURAMI_TASK_POST(task) != func_08000388_6B3628)
    {
        return;
    }

    s_combat_active = 1;
}

/* 00388 is installed only on Tsurami's health-bearing root.  Capture here;
 * after its native damage pass returns, the return hook rechecks the state
 * before running any synthetic work. */
RECOMP_HOOK("func_08000388_6B3628")
void extra_options_capture_hyper_tsurami(void *task)
{
    s_root_capture_valid = 0;
    if (s_replay_guard || !task || !refresh_tsurami_runtime_state() ||
        D_800C7AB2 != ROOM_TSURAMI || !TASK_OBJECT(task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        TSURAMI_TASK_POST(task) != func_08000388_6B3628)
    {
        return;
    }

    if (!identity_matches(&s_tsurami, task))
    {
        clear_tsurami_tracking();
        bind_identity(&s_tsurami, task);
    }

    /* This exact post hook sees the root after its native AI callback.  Latch
     * combat here too so attaching during an active encounter cannot miss
     * the one transition into the first combat state. */
    if (callback_is_combat_state(TSURAMI_TASK_AI(task)))
        s_combat_active = 1;

    if (!s_track_reported)
    {
        s_track_reported = 1;
        recomp_printf("[Extra Options] Tsurami tracked: room=0x%03X, "
                      "Hyper Enemies=%s.\n",
                      (unsigned int)D_800C7AB2,
                      hyper_tsurami_is_enabled()
                          ? "enabled" : "disabled");
    }

    if (!hyper_tsurami_is_enabled() ||
        task != D_8016DAB4_16E6B4 ||
        !hyper_tsurami_root_is_live(task))
    {
        return;
    }

    s_captured_root = s_tsurami;
    s_root_capture_valid = 1;
}

static int visual_child_is_live(void *root)
{
    HyperTsuramiTaskCallback callback;
    void *task = s_visual_child.task;

    if (!task || !identity_matches(&s_visual_child, task) ||
        TASK_OWNER(task) != root ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        TASK_ENTITY_ID(task) != ENTITY_TSURAMI_CHILD ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        TSURAMI_TASK_POST(task) != func_80218F30_5D4400)
    {
        return 0;
    }

    callback = TSURAMI_TASK_AI(task);
    return callback_is_enabled(callback) &&
           (callback == func_080046B8_6B7958 ||
            callback == func_0800476C_6B7A0C);
}

static void advance_visual_animation(void *root)
{
    void *task;

    if (!visual_child_is_live(root))
        return;

    task = s_visual_child.task;
    func_80216AB8_5D1F88(task);
}

RECOMP_HOOK_RETURN("func_08000388_6B3628")
void extra_options_run_hyper_tsurami_tick(void)
{
    HyperTsuramiIdentity identity;
    HyperTsuramiTaskCallback callback;
    unsigned int completed_ticks = 0;
    unsigned int extra_tick;
    unsigned int extra_tick_limit;
    unsigned short room;
    void *saved_current_task;

    if (s_replay_guard || !s_root_capture_valid)
        return;

    identity = s_captured_root;
    s_root_capture_valid = 0;
    s_captured_root.task = 0;

    if (!hyper_tsurami_is_enabled() ||
        !identity_matches(&identity, identity.task) ||
        !hyper_tsurami_root_is_live(identity.task))
    {
        return;
    }

    room = D_800C7AB2;
    saved_current_task = D_8016DAB4_16E6B4;
    /* Consume one cadence half only after every encounter/liveness gate.
     * Start low, then alternate 1,2 synthetic ticks for a 2.5x average. */
    extra_tick_limit = take_extra_tick_budget(&s_tsurami.cadence_phase);
    s_replay_guard = 1;
    for (extra_tick = 0; extra_tick < extra_tick_limit; extra_tick++)
    {
        if (D_800C7AB2 != room ||
            !identity_matches(&identity, identity.task) ||
            !hyper_tsurami_root_is_live(identity.task))
        {
            break;
        }

        callback = TSURAMI_TASK_AI(identity.task);
        if (!callback_is_enabled(callback) ||
            !callback_is_combat_state(callback))
        {
            break;
        }

        D_8016DAB4_16E6B4 = identity.task;
        callback(identity.task, identity.object);
        if (D_8016DAB4_16E6B4 != identity.task ||
            D_800C7AB2 != room ||
            !identity_matches(&identity, identity.task) ||
            !hyper_tsurami_root_is_live(identity.task))
        {
            break;
        }

        /* These are the animation and velocity-integration calls inside the
         * native common pipeline.  Collision/damage and A228 are omitted. */
        func_80216AB8_5D1F88(identity.task);
        if (D_8016DAB4_16E6B4 != identity.task ||
            !identity_matches(&identity, identity.task) ||
            !hyper_tsurami_root_is_live(identity.task))
        {
            break;
        }
        func_8021B808_5D6CD8(identity.task);
        if (D_8016DAB4_16E6B4 != identity.task ||
            !identity_matches(&identity, identity.task) ||
            !hyper_tsurami_root_is_live(identity.task))
        {
            break;
        }

        /* The exact 045F8 model child has no combat logic of its own.  Its
         * clip receives the same one-or-two missing animation steps as the
         * root; its normal scheduler pass follows the final position. */
        advance_visual_animation(identity.task);
        completed_ticks++;
    }
    s_replay_guard = 0;
    D_8016DAB4_16E6B4 = saved_current_task;

    if (completed_ticks == extra_tick_limit && !s_replay_reported)
    {
        s_replay_reported = 1;
        recomp_printf("[Extra Options] Hyper Tsurami active: "
                      "alternating 1/2 extra AI/movement/animation ticks "
                      "(2.5x average).\n");
    }
}

/* 045F8 is Tsurami's exact persistent model child.  The separate 03DB0
 * intro/destruction child is intentionally never registered. */
RECOMP_HOOK("func_080045F8_6B7898")
void extra_options_capture_hyper_tsurami_visual(void *task)
{
    s_pending_visual_valid = 0;
    if (!task || !refresh_tsurami_runtime_state() ||
        D_800C7AB2 != ROOM_TSURAMI || !TASK_OBJECT(task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        !identity_matches(&s_tsurami, TASK_OWNER(task)))
    {
        return;
    }

    bind_identity(&s_pending_visual, task);
    s_pending_visual_valid = 1;
}

RECOMP_HOOK_RETURN("func_080045F8_6B7898")
void extra_options_track_hyper_tsurami_visual(void)
{
    HyperTsuramiIdentity identity;

    if (!s_pending_visual_valid)
        return;

    identity = s_pending_visual;
    s_pending_visual_valid = 0;
    s_pending_visual.task = 0;
    if (!identity_matches(&identity, identity.task) ||
        TASK_OWNER(identity.task) != s_tsurami.task ||
        TASK_ENTITY_ID(identity.task) != ENTITY_TSURAMI_CHILD ||
        TSURAMI_TASK_AI(identity.task) != func_080046B8_6B7958 ||
        TSURAMI_TASK_POST(identity.task) != func_80218F30_5D4400)
    {
        return;
    }

    s_visual_child = identity;
}

/* Every phase uses 049A4 for its travelling attacks.  Register constructor
 * identities rather than all entity-0xCB children, which would also admit
 * Tsurami's model and intro/effect tasks. */
RECOMP_HOOK("func_080049A4_6B7C44")
void extra_options_capture_hyper_tsurami_projectile(void *task)
{
    s_pending_projectile_valid = 0;
    if (!task || task != D_8016DAB4_16E6B4 || !TASK_OBJECT(task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        !tsurami_encounter_is_live(s_tsurami.task))
    {
        return;
    }

    bind_identity(&s_pending_projectile, task);
    s_pending_projectile_valid = 1;
}

RECOMP_HOOK_RETURN("func_080049A4_6B7C44")
void extra_options_track_hyper_tsurami_projectile(void)
{
    HyperTsuramiIdentity identity;

    if (!s_pending_projectile_valid)
        return;

    identity = s_pending_projectile;
    s_pending_projectile_valid = 0;
    s_pending_projectile.task = 0;
    if (!tsurami_encounter_is_live(s_tsurami.task) ||
        !identity_matches(&identity, identity.task) ||
        TASK_ENTITY_ID(identity.task) != ENTITY_TSURAMI_CHILD ||
        TSURAMI_TASK_AI(identity.task) != func_08004ED0_6B8170 ||
        TSURAMI_TASK_POST(identity.task) != func_80218F30_5D4400 ||
        !projectile_mode_is_valid(TASK_ATTACK_FLAGS(identity.task)))
    {
        return;
    }

    /* The native generation is only eight bits.  Forget any ancient shared
     * replay-cache identity before this constructor's first common post. */
    extra_options_hyper_forget_task(identity.task);
    register_projectile(&identity);
}

int extra_options_hyper_tsurami_projectile_is_live(void *task)
{
    unsigned int index;

    if (!task || !tsurami_encounter_is_live(s_tsurami.task) ||
        TASK_ACTOR_ID(task) != ACTOR_TSURAMI ||
        TASK_ENTITY_ID(task) != ENTITY_TSURAMI_CHILD ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        TSURAMI_TASK_AI(task) != func_08004ED0_6B8170 ||
        TSURAMI_TASK_POST(task) != func_80218F30_5D4400)
    {
        return 0;
    }

    for (index = 0; index < TSURAMI_PROJECTILE_CAPACITY; index++)
        if (identity_matches(&s_projectiles[index], task))
            return 1;
    return 0;
}

int extra_options_hyper_tsurami_take_projectile_extra_ticks(
    void *task, unsigned int *extra_ticks)
{
    unsigned int index;

    if (!extra_ticks ||
        !extra_options_hyper_tsurami_projectile_is_live(task))
        return 0;

    for (index = 0; index < TSURAMI_PROJECTILE_CAPACITY; index++)
    {
        if (identity_matches(&s_projectiles[index], task))
        {
            *extra_ticks = take_extra_tick_budget(
                &s_projectiles[index].cadence_phase);
            return 1;
        }
    }
    return 0;
}
