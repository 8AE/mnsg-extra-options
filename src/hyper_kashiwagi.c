#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"
#include "hyper_enemies.h"

/* Kashiwagi is encounter 1 in the file_13 Impact overlay, not a room actor.
 * His own AI advances positions, attack counters, and object +0x28 animation
 * frames (801D3018 / 801D2F40). Repeat that AI, not the shared Impact input,
 * collision, or rendering passes. In particular, task +0x74 is a velocity,
 * NOT the generation byte used by the ordinary-enemy replay machinery.
 *
 * Native evidence: USA decompressed ROM SHA1
 * 6ea0ed71032ce08fc2745f412d84936382197494, file_13 ROM 5F6840,
 * VRAM 801CB460. 801D0260 -> 801EEB3C creates root 801E4800 and the
 * independent damage dispatcher 801EEF40. Full ROM-qualified symbols below
 * are essential: other overlays reuse these runtime addresses.
 */

/* Independent 2.5x cadence clocks (average 1.5 extra ticks) for the root,
 * travelling-shot motion, the summoned moving clone, and the charge proxy. */
#define KASHIWAGI_CLOCK_ROOT 0u
#define KASHIWAGI_CLOCK_SHOT_MOTION 1u
#define KASHIWAGI_CLOCK_CLONE 2u
#define KASHIWAGI_CLOCK_CHARGE 3u

typedef void (*HyperKashiwagiCallback)(void *task, void *object);

extern unsigned char *D_8015C5C8_15D1C8;
extern void *D_8020EED0_63A2B0;
extern void *D_8016DAB4_16E6B4;

#define KASHIWAGI_TASK_OBJECT(task) \
    (*(void *volatile *)((char *)(task) + 0x18))
#define KASHIWAGI_TASK_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5C))
#define KASHIWAGI_STATE_HP(state) \
    (*(volatile signed int *)((char *)(state) + 0x60))
#define KASHIWAGI_STATE_PLAYER_HP(state) \
    (*(volatile signed int *)((char *)(state) + 0x68))
#define KASHIWAGI_STATE_PAUSED(state) \
    (*(volatile unsigned char *)((char *)(state) + 0x2C0))

/* Override native pointer slots in the 64-bit host regression fixture. */
#ifndef KASHIWAGI_TASK_AI
#define KASHIWAGI_TASK_AI(task) \
    (*(HyperKashiwagiCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef KASHIWAGI_CALLBACK_IS_ENABLED
#define KASHIWAGI_CALLBACK_IS_ENABLED(callback) \
    ((callback) && (((unsigned long)(callback) & 0x00800000u) == 0))
#endif
#ifndef KASHIWAGI_STATE_ROOT
#define KASHIWAGI_STATE_ROOT(state) \
    (*(void *volatile *)((char *)(state) + 0x1D8))
#endif
#ifndef KASHIWAGI_STATE_MODEL
#define KASHIWAGI_STATE_MODEL(state) \
    (*(void *volatile *)((char *)(state) + 0x1E0))
#endif
#ifndef KASHIWAGI_ENCOUNTER
#define KASHIWAGI_ENCOUNTER \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADF4))
#endif
#ifndef KASHIWAGI_WORLD_FRAME
/* Base loop 80000AD8 increments this at 80000CA4..80000CBC, including
 * Impact mode. It is not the ordinary room-actor update counter. */
#define KASHIWAGI_WORLD_FRAME \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADCE))
#endif

/* The neutral state and every state in its six attack/movement branches.
 * Do not replay intro, defeat, incoming-hit/throw reactions, or Impact's
 * shared player-control scripts. Those remain on the native frame clock. */
#define KASHIWAGI_ROOT_CALLBACKS(X) \
    X(func_801E52D8_6106B8) \
    X(func_801E59F8_610DD8) \
    X(func_801E5A88_610E68) \
    X(func_801E5BDC_610FBC) \
    X(func_801E5D38_611118) \
    X(func_801E5DAC_61118C) \
    X(func_801E5E14_6111F4) \
    X(func_801E5EE0_6112C0) \
    X(func_801E5F64_611344) \
    X(func_801E6024_611404) \
    X(func_801E607C_61145C) \
    X(func_801E60CC_6114AC) \
    X(func_801E6150_611530) \
    X(func_801E61A8_611588) \
    X(func_801E628C_61166C) \
    X(func_801E6370_611750) \
    X(func_801E641C_6117FC) \
    X(func_801E6474_611854) \
    X(func_801E651C_6118FC) \
    X(func_801E6600_6119E0) \
    X(func_801E66C4_611AA4) \
    X(func_801E6778_611B58) \
    X(func_801E67D4_611BB4) \
    X(func_801E6854_611C34) \
    X(func_801E69F4_611DD4) \
    X(func_801E6D00_6120E0) \
    X(func_801E6DE0_6121C0) \
    X(func_801E6ECC_6122AC) \
    X(func_801E6FC4_6123A4) \
    X(func_801E700C_6123EC) \
    X(func_801E7064_612444) \
    X(func_801E7134_612514) \
    X(func_801E71E8_6125C8) \
    X(func_801E72F4_6126D4) \
    X(func_801E752C_61290C) \
    X(func_801E75E4_6129C4) \
    X(func_801E7714_612AF4) \
    X(func_801E7838_612C18) \
    X(func_801E79E4_612DC4) \
    X(func_801E7C50_613030) \
    X(func_801E7CA8_613088) \
    X(func_801E7FB0_613390) \
    X(func_801E8108_6134E8)

#define DECLARE_CALLBACK(name) extern void name(void *task, void *object);
KASHIWAGI_ROOT_CALLBACKS(DECLARE_CALLBACK)
#undef DECLARE_CALLBACK

static void *s_kashiwagi_state;
static void *s_kashiwagi_task;
static void *s_kashiwagi_object;
static void *s_kashiwagi_model;
static unsigned int s_kashiwagi_epoch;
static unsigned short s_kashiwagi_frame;
static unsigned char s_kashiwagi_frame_valid;
static unsigned char s_kashiwagi_replay_guard;
static unsigned char s_kashiwagi_reported;

static int kashiwagi_root_callback_is_combat(HyperKashiwagiCallback callback)
{
    if (!KASHIWAGI_CALLBACK_IS_ENABLED(callback))
        return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    KASHIWAGI_ROOT_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int kashiwagi_is_live(void)
{
    void *state;
    void *task;
    void *object;
    void *model;

    /* Read the overlay's battle pointer only from its own hooks and after
     * checking the encounter selector. No save-HP gate: Impact boss rush
     * can be entered directly from the title menu. */
    if (KASHIWAGI_ENCOUNTER != 1)
        return 0;
    state = D_8020EED0_63A2B0;
    task = s_kashiwagi_task;
    if (!state || state != s_kashiwagi_state || !task ||
        KASHIWAGI_STATE_ROOT(state) != task ||
        KASHIWAGI_STATE_HP(state) <= 0 ||
        KASHIWAGI_STATE_PLAYER_HP(state) <= 0 ||
        KASHIWAGI_STATE_PAUSED(state) || KASHIWAGI_TASK_ID(task) != 0x50)
        return 0;

    object = KASHIWAGI_TASK_OBJECT(task);
    model = KASHIWAGI_STATE_MODEL(state);
    if (!object || !model)
        return 0;
    if (!s_kashiwagi_object)
    {
        s_kashiwagi_object = object;
        s_kashiwagi_model = model;
    }
    return object == s_kashiwagi_object && model == s_kashiwagi_model;
}

RECOMP_HOOK("func_801E4800_60FBE0")
void extra_options_track_hyper_kashiwagi(void *task)
{
    s_kashiwagi_epoch++;
    s_kashiwagi_state = D_8020EED0_63A2B0;
    s_kashiwagi_task = task;
    /* The initializer installs the model; bind it on the first live pass. */
    s_kashiwagi_object = 0;
    s_kashiwagi_model = 0;
    s_kashiwagi_frame_valid = 0;
    s_kashiwagi_reported = 0;
}

/* Consume incoming damage once, then perform the frame's extra boss-only
 * updates.  The alternating 1/2-tick budget averages 1.5 extra ticks (2.5x),
 * matching Dharumanyo and Tsurami.  Reload the callback each time: attacks
 * may switch states on any substep.  Native animation completion and attack
 * counters therefore stay together, including equality-triggered shots that
 * a scaled timer could skip. */
RECOMP_HOOK_RETURN("func_801EEF40_61A320")
void extra_options_run_hyper_kashiwagi_tick(void)
{
    void *saved_current_task;
    void *task;
    unsigned int epoch;
    unsigned int tick;
    unsigned int completed = 0;
    unsigned int extra_ticks;
    unsigned short frame;

    if (s_kashiwagi_replay_guard || recomp_get_config_u32("hyper_enemies") != 0 ||
        !kashiwagi_is_live() ||
        !kashiwagi_root_callback_is_combat(KASHIWAGI_TASK_AI(s_kashiwagi_task)))
        return;

    frame = KASHIWAGI_WORLD_FRAME;
    if (s_kashiwagi_frame_valid && s_kashiwagi_frame == frame)
        return;
    s_kashiwagi_frame = frame;
    s_kashiwagi_frame_valid = 1;
    extra_options_hyper_impact_cadence_begin(KASHIWAGI_CLOCK_ROOT, frame);
    extra_ticks = extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_ROOT);
    epoch = s_kashiwagi_epoch;
    task = s_kashiwagi_task;
    saved_current_task = D_8016DAB4_16E6B4;
    s_kashiwagi_replay_guard = 1;
    for (tick = 0; tick < extra_ticks; tick++)
    {
        HyperKashiwagiCallback callback;
        int context_changed;

        if (epoch != s_kashiwagi_epoch || !kashiwagi_is_live() ||
            recomp_get_config_u32("hyper_enemies") != 0)
            break;
        callback = KASHIWAGI_TASK_AI(task);
        if (!kashiwagi_root_callback_is_combat(callback))
            break;
        D_8016DAB4_16E6B4 = task;
        callback(task, s_kashiwagi_object);
        context_changed = D_8016DAB4_16E6B4 != task;
        D_8016DAB4_16E6B4 = saved_current_task;
        completed++;
        if (context_changed)
            break;
    }
    D_8016DAB4_16E6B4 = saved_current_task;
    s_kashiwagi_replay_guard = 0;
    if (completed == extra_ticks && !s_kashiwagi_reported)
    {
        recomp_printf("[Extra Options] Kashiwagi Hyper: 2.5x movement, animation, and attacks active.\n");
        s_kashiwagi_reported = 1;
    }
}

/* These two travelling shot families are Kashiwagi-only. Their AI also
 * handles collisions, so never replay the entire projectile callback. Scope
 * the frame's extra calls to the native movement-only helper instead.
 * Constructors, impact effects, attached proxies, and two-frame melee
 * hitboxes are not admitted to this movement path. The charge proxy has its
 * own clock below. */
extern void func_801D614C_60152C(void *task);
extern void func_801EA3F0_6157D0(void *task, void *object);
extern void func_801EA534_615914(void *task, void *object);
extern void func_801EA900_615CE0(void *task, void *object);

#define KASHIWAGI_SHOT_TIMER(task) \
    (*(volatile signed int *)((char *)(task) + 0x7C))
#define KASHIWAGI_SHOT_ROLL(task) \
    (*(volatile unsigned int *)((char *)(task) + 0x94))
#define KASHIWAGI_SHOT_VELOCITY_Z(task) \
    (*(volatile float *)((char *)(task) + 0x78))
#define KASHIWAGI_OBJECT_YAW(object) \
    (*(volatile unsigned short *)((char *)(object) + 0x16))

static void *s_kashiwagi_shot;
static void *s_kashiwagi_shot_object;
static unsigned int s_kashiwagi_shot_epoch;
static unsigned char s_kashiwagi_motion_guard;

static int begin_kashiwagi_shot(void *task, void *object,
                                HyperKashiwagiCallback callback)
{
    s_kashiwagi_shot = 0;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        KASHIWAGI_TASK_OBJECT(task) != object ||
        KASHIWAGI_TASK_AI(task) != callback ||
        recomp_get_config_u32("hyper_enemies") != 0 || !kashiwagi_is_live())
        return 0;
    s_kashiwagi_shot = task;
    s_kashiwagi_shot_object = object;
    s_kashiwagi_shot_epoch = s_kashiwagi_epoch;
    return 1;
}

RECOMP_HOOK("func_801EA3F0_6157D0")
void extra_options_hyper_kashiwagi_aiming_shot(void *task, void *object)
{
    if (begin_kashiwagi_shot(task, object, func_801EA3F0_6157D0))
    {
        /* The shot advances one motion step per extra movement tick, so the
         * lifetime timer must lose the same number of frames the movement
         * helper replays.  Roll this frame's 2.5x budget here (the motion
         * helper shares the same clock) and pre-subtract it, because native
         * uses old_timer < 0 and then installs the travelling callback.
         * Clamp at -1 so shortening the delay cannot skip that transition. */
        unsigned int extra_ticks;
        signed int timer = KASHIWAGI_SHOT_TIMER(task);
        extra_options_hyper_impact_cadence_begin(
            KASHIWAGI_CLOCK_SHOT_MOTION, KASHIWAGI_WORLD_FRAME);
        extra_ticks = extra_options_hyper_impact_extra_ticks(
            KASHIWAGI_CLOCK_SHOT_MOTION);
        if (timer >= 0)
            KASHIWAGI_SHOT_TIMER(task) =
                timer > (signed int)extra_ticks
                    ? timer - (signed int)extra_ticks
                    : -1;
    }
}

RECOMP_HOOK("func_801EA534_615914")
void extra_options_hyper_kashiwagi_travelling_shot(void *task, void *object)
{
    if (begin_kashiwagi_shot(task, object, func_801EA534_615914))
    {
        unsigned int extra_ticks;
        signed int timer = KASHIWAGI_SHOT_TIMER(task);
        extra_options_hyper_impact_cadence_begin(
            KASHIWAGI_CLOCK_SHOT_MOTION, KASHIWAGI_WORLD_FRAME);
        extra_ticks = extra_options_hyper_impact_extra_ticks(
            KASHIWAGI_CLOCK_SHOT_MOTION);
        /* Native adds the remaining 10 per motion step.  Pre-add the extra
         * steps' share so the visible roll matches the frame's step count. */
        KASHIWAGI_SHOT_ROLL(task) += 10u * extra_ticks;
        /* The native expiry counter is used only by backward-flying shots.
         * Keep their lifetime in step with motion without repeating deletion. */
        if (KASHIWAGI_SHOT_VELOCITY_Z(task) < 0.0f && timer > 0)
            KASHIWAGI_SHOT_TIMER(task) =
                timer > (signed int)extra_ticks
                    ? timer - (signed int)extra_ticks
                    : 0;
    }
}

RECOMP_HOOK("func_801EA900_615CE0")
void extra_options_hyper_kashiwagi_volley_shot(void *task, void *object)
{
    if (begin_kashiwagi_shot(task, object, func_801EA900_615CE0))
    {
        unsigned int extra_ticks;
        extra_options_hyper_impact_cadence_begin(
            KASHIWAGI_CLOCK_SHOT_MOTION, KASHIWAGI_WORLD_FRAME);
        extra_ticks = extra_options_hyper_impact_extra_ticks(
            KASHIWAGI_CLOCK_SHOT_MOTION);
        /* Native adds the remaining 3 per motion step. */
        KASHIWAGI_OBJECT_YAW(object) =
            (unsigned short)(KASHIWAGI_OBJECT_YAW(object) + 3u * extra_ticks);
    }
}

RECOMP_HOOK_RETURN("func_801EA3F0_6157D0")
void extra_options_hyper_kashiwagi_aiming_shot_done(void)
{
    s_kashiwagi_shot = 0;
}

RECOMP_HOOK_RETURN("func_801EA534_615914")
void extra_options_hyper_kashiwagi_travelling_shot_done(void)
{
    s_kashiwagi_shot = 0;
}

RECOMP_HOOK_RETURN("func_801EA900_615CE0")
void extra_options_hyper_kashiwagi_volley_shot_done(void)
{
    s_kashiwagi_shot = 0;
}

RECOMP_HOOK("func_801D614C_60152C")
void extra_options_hyper_kashiwagi_shot_motion(void *task)
{
    unsigned int tick;

    if (s_kashiwagi_motion_guard || !task || task != s_kashiwagi_shot ||
        s_kashiwagi_shot_epoch != s_kashiwagi_epoch ||
        D_8016DAB4_16E6B4 != task ||
        KASHIWAGI_TASK_OBJECT(task) != s_kashiwagi_shot_object ||
        recomp_get_config_u32("hyper_enemies") != 0 || !kashiwagi_is_live())
        return;

    /* 614C only adds velocity to XYZ in the linked model list. No callbacks,
     * task allocation/deletion, collision, timers, or damage are inside it. */
    s_kashiwagi_motion_guard = 1;
    extra_options_hyper_impact_cadence_begin(
        KASHIWAGI_CLOCK_SHOT_MOTION, KASHIWAGI_WORLD_FRAME);
    for (tick = 0;
         tick < extra_options_hyper_impact_extra_ticks(
                    KASHIWAGI_CLOCK_SHOT_MOTION);
         tick++)
        func_801D614C_60152C(task);
    s_kashiwagi_motion_guard = 0;
}

/* Attack 5 summons a separate, moving copy of the boss. Its six offensive
 * callbacks own their own movement/animation clock, so root replay alone
 * cannot accelerate it. CE48 consumes +0x38 hits and installs the excluded
 * CBC0 reaction; CEDC installs the excluded CD54 fade when attack 5 ends.
 * Later substeps see the cleared hit pointer and cannot process it again.
 * These six callbacks never subtract HP or run the global collision pass.
 * CE48's CEE80 call adds the battle +0x6C meter, not boss HP (+0x60), and
 * happens only for the consumed hit before switching to the excluded AI.
 */
#define KASHIWAGI_CLONE_CALLBACKS(X) \
    X(func_801EC488_617868) \
    X(func_801EC61C_6179FC) \
    X(func_801EC7B8_617B98) \
    X(func_801EC85C_617C3C) \
    X(func_801EC908_617CE8) \
    X(func_801ECA74_617E54)

#define DECLARE_CALLBACK(name) extern void name(void *task, void *object);
KASHIWAGI_CLONE_CALLBACKS(DECLARE_CALLBACK)
#undef DECLARE_CALLBACK

#define KASHIWAGI_ATTACK_KIND(task) \
    (*(volatile signed int *)((char *)(task) + 0xD8))

static void *s_kashiwagi_clone;
static void *s_kashiwagi_clone_object;
static unsigned int s_kashiwagi_clone_epoch;
static unsigned short s_kashiwagi_clone_frame;
static unsigned char s_kashiwagi_clone_frame_valid;
static unsigned char s_kashiwagi_clone_guard;

static int kashiwagi_clone_callback_is_combat(HyperKashiwagiCallback callback)
{
    if (!KASHIWAGI_CALLBACK_IS_ENABLED(callback))
        return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    KASHIWAGI_CLONE_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int kashiwagi_clone_is_live(void)
{
    return s_kashiwagi_clone &&
           s_kashiwagi_clone_epoch == s_kashiwagi_epoch &&
           kashiwagi_is_live() &&
           KASHIWAGI_ATTACK_KIND(s_kashiwagi_task) == 5 &&
           KASHIWAGI_TASK_ID(s_kashiwagi_clone) == 0x51 &&
           KASHIWAGI_TASK_OBJECT(s_kashiwagi_clone) == s_kashiwagi_clone_object;
}

RECOMP_HOOK("func_801EC488_617868")
void extra_options_track_hyper_kashiwagi_clone(void *task, void *object)
{
    if (s_kashiwagi_clone_guard || !task || !object ||
        D_8016DAB4_16E6B4 != task || !kashiwagi_is_live() ||
        KASHIWAGI_ATTACK_KIND(s_kashiwagi_task) != 5 ||
        KASHIWAGI_TASK_ID(task) != 0x51 ||
        KASHIWAGI_TASK_OBJECT(task) != object ||
        KASHIWAGI_TASK_AI(task) != func_801EC488_617868)
        return;

    if (s_kashiwagi_clone != task || s_kashiwagi_clone_object != object ||
        s_kashiwagi_clone_epoch != s_kashiwagi_epoch)
        s_kashiwagi_clone_frame_valid = 0;
    s_kashiwagi_clone = task;
    s_kashiwagi_clone_object = object;
    s_kashiwagi_clone_epoch = s_kashiwagi_epoch;
}

static void run_kashiwagi_clone_tick(void)
{
    void *saved_current_task;
    void *task;
    unsigned int tick;
    unsigned int epoch;
    unsigned short frame;

    if (s_kashiwagi_clone_guard || recomp_get_config_u32("hyper_enemies") != 0 ||
        !kashiwagi_clone_is_live() ||
        D_8016DAB4_16E6B4 != s_kashiwagi_clone ||
        !kashiwagi_clone_callback_is_combat(KASHIWAGI_TASK_AI(s_kashiwagi_clone)))
        return;
    frame = KASHIWAGI_WORLD_FRAME;
    if (s_kashiwagi_clone_frame_valid && s_kashiwagi_clone_frame == frame)
        return;
    s_kashiwagi_clone_frame = frame;
    s_kashiwagi_clone_frame_valid = 1;
    extra_options_hyper_impact_cadence_begin(KASHIWAGI_CLOCK_CLONE, frame);
    task = s_kashiwagi_clone;
    epoch = s_kashiwagi_epoch;
    saved_current_task = D_8016DAB4_16E6B4;
    s_kashiwagi_clone_guard = 1;
    for (tick = 0;
         tick < extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_CLONE);
         tick++)
    {
        HyperKashiwagiCallback callback;
        int context_changed;

        if (epoch != s_kashiwagi_epoch || !kashiwagi_clone_is_live() ||
            recomp_get_config_u32("hyper_enemies") != 0)
            break;
        callback = KASHIWAGI_TASK_AI(task);
        if (!kashiwagi_clone_callback_is_combat(callback))
            break;
        D_8016DAB4_16E6B4 = task;
        callback(task, s_kashiwagi_clone_object);
        context_changed = D_8016DAB4_16E6B4 != task;
        D_8016DAB4_16E6B4 = saved_current_task;
        if (context_changed)
            break;
    }
    D_8016DAB4_16E6B4 = saved_current_task;
    s_kashiwagi_clone_guard = 0;
}

#define CLONE_RETURN_HOOK(name) \
    RECOMP_HOOK_RETURN(#name) \
    void extra_options_hyper_kashiwagi_clone_return_##name(void) \
    { \
        run_kashiwagi_clone_tick(); \
    }
KASHIWAGI_CLONE_CALLBACKS(CLONE_RETURN_HOOK)
#undef CLONE_RETURN_HOOK

/* The charge attack owns an attached AC4C proxy with a 43-tick charge and
 * 16-tick trail-emission clock. Keeping it at 1x would outlast the accelerated
 * root's attack window. AC4C clears its outgoing contact +0x34 in the native
 * pass; subsequent updates only snap to the root, charge, and emit trails.
 * It has no direct HP deduction. The emitted AF84 fades remain cosmetic.
 */
extern void func_801EAC4C_61602C(void *task, void *object);

static void *s_kashiwagi_charge;
static void *s_kashiwagi_charge_object;
static unsigned int s_kashiwagi_charge_epoch;
static unsigned char s_kashiwagi_charge_guard;

#ifndef KASHIWAGI_CHARGE_LINK
#define KASHIWAGI_CHARGE_LINK(task) \
    (*(void *volatile *)((char *)(task) + 0x90))
#endif

RECOMP_HOOK("func_801EAC4C_61602C")
void extra_options_capture_hyper_kashiwagi_charge(void *task, void *object)
{
    if (s_kashiwagi_charge_guard)
        return;
    s_kashiwagi_charge = 0;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !kashiwagi_is_live() ||
        KASHIWAGI_ATTACK_KIND(s_kashiwagi_task) != 2 ||
        KASHIWAGI_CHARGE_LINK(s_kashiwagi_task) != task ||
        KASHIWAGI_CHARGE_LINK(task) != s_kashiwagi_object ||
        KASHIWAGI_TASK_OBJECT(task) != object ||
        KASHIWAGI_TASK_AI(task) != func_801EAC4C_61602C)
        return;
    s_kashiwagi_charge = task;
    s_kashiwagi_charge_object = object;
    s_kashiwagi_charge_epoch = s_kashiwagi_epoch;
}

RECOMP_HOOK_RETURN("func_801EAC4C_61602C")
void extra_options_run_hyper_kashiwagi_charge_tick(void)
{
    void *task;
    void *object;
    void *saved_current_task;
    unsigned int epoch;
    unsigned int tick;

    if (s_kashiwagi_charge_guard || !s_kashiwagi_charge)
        return;
    task = s_kashiwagi_charge;
    object = s_kashiwagi_charge_object;
    epoch = s_kashiwagi_charge_epoch;
    s_kashiwagi_charge = 0;
    saved_current_task = D_8016DAB4_16E6B4;
    /* Native deletes the proxy when attack 2 ends. Check context and root
     * before inspecting it again; never dereference a deleted child. */
    if (saved_current_task != task)
        return;
    s_kashiwagi_charge_guard = 1;
    extra_options_hyper_impact_cadence_begin(
        KASHIWAGI_CLOCK_CHARGE, KASHIWAGI_WORLD_FRAME);
    for (tick = 0;
         tick < extra_options_hyper_impact_extra_ticks(
                    KASHIWAGI_CLOCK_CHARGE);
         tick++)
    {
        if (epoch != s_kashiwagi_epoch || !kashiwagi_is_live() ||
            KASHIWAGI_ATTACK_KIND(s_kashiwagi_task) != 2 ||
            recomp_get_config_u32("hyper_enemies") != 0 ||
            KASHIWAGI_CHARGE_LINK(s_kashiwagi_task) != task ||
            KASHIWAGI_CHARGE_LINK(task) != s_kashiwagi_object ||
            KASHIWAGI_TASK_OBJECT(task) != object ||
            KASHIWAGI_TASK_AI(task) != func_801EAC4C_61602C)
            break;
        D_8016DAB4_16E6B4 = task;
        func_801EAC4C_61602C(task, object);
        if (D_8016DAB4_16E6B4 != task)
            break;
    }
    D_8016DAB4_16E6B4 = saved_current_task;
    s_kashiwagi_charge_guard = 0;
}
