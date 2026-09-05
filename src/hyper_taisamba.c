#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"

/* Taisamba 2 is encounter 2 in file_13, NOT an ordinary room actor.
 * USA decompressed-ROM SHA1 6ea0ed71032ce08fc2745f412d84936382197494:
 * 801F7088 creates root 801EF2E0 (ID 0x5A) and separate damage task
 * 801F6C4C. Root combat states own their movement, animation advancement,
 * attack counters, and native blocking flags; replay those states, never
 * the shared input/collision pass. Task +0x74 is a velocity, not a lifetime.
 */
typedef void (*TaisambaCallback)(void *task, void *object);

extern unsigned char *D_8015C5C8_15D1C8;
extern void *D_8020EED0_63A2B0;
extern void *D_8016DAB4_16E6B4;

#define TAISAMBA_OBJECT(task) (*(void *volatile *)((char *)(task) + 0x18))
#define TAISAMBA_ID(task) (*(volatile unsigned short *)((char *)(task) + 0x5C))
#define TAISAMBA_HP(state) (*(volatile int *)((char *)(state) + 0x60))
#define TAISAMBA_PLAYER_HP(state) (*(volatile int *)((char *)(state) + 0x68))
#define TAISAMBA_PAUSED(state) (*(volatile unsigned char *)((char *)(state) + 0x2C0))
#define TAISAMBA_CLOCK(state) (*(volatile unsigned int *)((char *)(state) + 0x2C8))
#define TAISAMBA_ARENA_RISE(state) (*(volatile float *)((char *)(state) + 0x17C))
#define TAISAMBA_ARENA_HEIGHT(state) (*(volatile float *)((char *)(state) + 0x188))

/* These overrides model native 32-bit pointer slots on the host test ABI. */
#ifndef TAISAMBA_AI
#define TAISAMBA_AI(task) (*(TaisambaCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef TAISAMBA_ROOT
#define TAISAMBA_ROOT(state) (*(void *volatile *)((char *)(state) + 0x1D8))
#endif
#ifndef TAISAMBA_MODEL
#define TAISAMBA_MODEL(state) (*(void *volatile *)((char *)(state) + 0x1E0))
#endif
#ifndef TAISAMBA_CALLBACK_ENABLED
#define TAISAMBA_CALLBACK_ENABLED(callback) \
    ((callback) && (((unsigned long)(callback) & 0x00800000u) == 0))
#endif
#ifndef TAISAMBA_ENCOUNTER
#define TAISAMBA_ENCOUNTER \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADF4))
#endif
#ifndef TAISAMBA_WORLD_FRAME
#define TAISAMBA_WORLD_FRAME \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADCE))
#endif

/* Both ground and airborne combat trees, including ascent, dives, volleys,
 * the circling/whirlwind sequence, and its returning-weapon attack. The
 * selectors 801F1FA4 / 801F0540 call these through native callback setters.
 * Intro/outro (EF42C..F0398), incoming-hit/throw reactions (F45A0 onward),
 * and shared player-control/QTE scripts deliberately keep their native clock.
 */
#define TAISAMBA_ROOT_CALLBACKS(X) \
    X(func_801F03C4_61B7A4) \
    X(func_801F0414_61B7F4) \
    X(func_801F047C_61B85C) \
    X(func_801F04E0_61B8C0) \
    X(func_801F05BC_61B99C) \
    X(func_801F060C_61B9EC) \
    X(func_801F07E4_61BBC4) \
    X(func_801F0A0C_61BDEC) \
    X(func_801F0A50_61BE30) \
    X(func_801F0AD4_61BEB4) \
    X(func_801F0BF4_61BFD4) \
    X(func_801F0CF8_61C0D8) \
    X(func_801F0E00_61C1E0) \
    X(func_801F0F3C_61C31C) \
    X(func_801F0FCC_61C3AC) \
    X(func_801F1074_61C454) \
    X(func_801F1110_61C4F0) \
    X(func_801F1218_61C5F8) \
    X(func_801F12F8_61C6D8) \
    X(func_801F1340_61C720) \
    X(func_801F14FC_61C8DC) \
    X(func_801F1690_61CA70) \
    X(func_801F16E0_61CAC0) \
    X(func_801F1738_61CB18) \
    X(func_801F1788_61CB68) \
    X(func_801F20B4_61D494) \
    X(func_801F214C_61D52C) \
    X(func_801F223C_61D61C) \
    X(func_801F234C_61D72C) \
    X(func_801F2418_61D7F8) \
    X(func_801F2438_61D818) \
    X(func_801F24F4_61D8D4) \
    X(func_801F25A0_61D980) \
    X(func_801F2644_61DA24) \
    X(func_801F26E0_61DAC0) \
    X(func_801F2788_61DB68) \
    X(func_801F2854_61DC34) \
    X(func_801F28A4_61DC84) \
    X(func_801F2914_61DCF4) \
    X(func_801F2A04_61DDE4) \
    X(func_801F2A98_61DE78) \
    X(func_801F2B1C_61DEFC) \
    X(func_801F2B7C_61DF5C) \
    X(func_801F2C24_61E004) \
    X(func_801F2C70_61E050) \
    X(func_801F2CB4_61E094) \
    X(func_801F2DDC_61E1BC) \
    X(func_801F2ED8_61E2B8) \
    X(func_801F2FA0_61E380) \
    X(func_801F300C_61E3EC) \
    X(func_801F30A0_61E480) \
    X(func_801F31A0_61E580) \
    X(func_801F3254_61E634) \
    X(func_801F331C_61E6FC) \
    X(func_801F3498_61E878) \
    X(func_801F3634_61EA14) \
    X(func_801F3714_61EAF4) \
    X(func_801F37F4_61EBD4) \
    X(func_801F3878_61EC58) \
    X(func_801F38DC_61ECBC) \
    X(func_801F3950_61ED30) \
    X(func_801F39A8_61ED88) \
    X(func_801F3A34_61EE14) \
    X(func_801F3A68_61EE48) \
    X(func_801F3AC8_61EEA8) \
    X(func_801F3B20_61EF00) \
    X(func_801F3BAC_61EF8C) \
    X(func_801F3C9C_61F07C) \
    X(func_801F3DCC_61F1AC) \
    X(func_801F3E24_61F204) \
    X(func_801F3EB8_61F298) \
    X(func_801F3FC0_61F3A0) \
    X(func_801F40DC_61F4BC) \
    X(func_801F4220_61F600) \
    X(func_801F436C_61F74C) \
    X(func_801F43D0_61F7B0) \
    X(func_801F443C_61F81C) \
    X(func_801F453C_61F91C)

/* Ballistic and staged shots, the whirlwind emitter, and the independently
 * timed returning weapon. Advancing only position would leave gravity,
 * homing transitions, and F3950's wait-for-weapon completion at 1x.
 * Native shot hits replace the AI with E2874 (an excluded impact effect).
 * Returning-weapon contact is consumed/cleared by F9350. None of these
 * callbacks deduct HP or run the game's global collision pass.
 */
#define TAISAMBA_CHILD_CALLBACKS(X) \
    X(func_801F7CEC_6230CC) \
    X(func_801F7E54_623234) \
    X(func_801F7EE8_6232C8) \
    X(func_801F833C_62371C) \
    X(func_801F85C8_6239A8) \
    X(func_801F8830_623C10) \
    X(func_801F89D8_623DB8) \
    X(func_801F92D0_6246B0) \
    X(func_801F9350_624730) \
    X(func_801F9464_624844) \
    X(func_801F9508_6248E8)

#define DECLARE_CALLBACK(name) extern void name(void *task, void *object);
TAISAMBA_ROOT_CALLBACKS(DECLARE_CALLBACK)
TAISAMBA_CHILD_CALLBACKS(DECLARE_CALLBACK)
#undef DECLARE_CALLBACK

static void *s_taisamba_state;
static void *s_taisamba_task;
static void *s_taisamba_object;
static void *s_taisamba_model;
static unsigned int s_taisamba_epoch;
static unsigned short s_taisamba_frame;
static unsigned char s_taisamba_frame_valid;
static unsigned char s_taisamba_guard;
static unsigned char s_taisamba_reported;
static unsigned short s_taisamba_rise_frame;
static unsigned char s_taisamba_rise_frame_valid;
static unsigned char s_taisamba_rise_requests;

static int taisamba_root_callback(TaisambaCallback callback)
{
    if (!TAISAMBA_CALLBACK_ENABLED(callback))
        return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    TAISAMBA_ROOT_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int taisamba_child_callback(TaisambaCallback callback)
{
    if (!TAISAMBA_CALLBACK_ENABLED(callback))
        return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    TAISAMBA_CHILD_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int taisamba_live(void)
{
    void *state;
    void *object;
    void *model;

    /* The ordinary save/room gate incorrectly disables title-menu Impact
     * boss rush. This pointer is read only within file_13's own hooks. */
    if (TAISAMBA_ENCOUNTER != 2)
        return 0;
    state = D_8020EED0_63A2B0;
    if (!state || state != s_taisamba_state || !s_taisamba_task ||
        TAISAMBA_ROOT(state) != s_taisamba_task ||
        TAISAMBA_HP(state) <= 0 || TAISAMBA_PLAYER_HP(state) <= 0 ||
        TAISAMBA_PAUSED(state) || TAISAMBA_ID(s_taisamba_task) != 0x5A)
        return 0;
    object = TAISAMBA_OBJECT(s_taisamba_task);
    model = TAISAMBA_MODEL(state);
    if (!object || !model)
        return 0;
    if (!s_taisamba_object)
    {
        s_taisamba_object = object;
        s_taisamba_model = model;
    }
    return object == s_taisamba_object && model == s_taisamba_model;
}

RECOMP_HOOK("func_801EF2E0_61A6C0")
void extra_options_track_hyper_taisamba(void *task)
{
    s_taisamba_epoch++;
    s_taisamba_state = D_8020EED0_63A2B0;
    s_taisamba_task = task;
    s_taisamba_object = 0;
    s_taisamba_model = 0;
    s_taisamba_frame_valid = 0;
    s_taisamba_reported = 0;
    s_taisamba_rise_frame_valid = 0;
    s_taisamba_rise_requests = 0;
}

/* F1788 writes +17C=1, while D0C70 integrates arena height +188 only once
 * per scheduler frame. Count requests at the real callback return: sibling
 * ordering in 80034D24 puts the damage task before the root, so the final
 * ordinary root update would overwrite an adjustment made in its damage
 * hook. F728C applies this same accumulated displacement to the boss once.
 * There is no replay of the arena, camera, or player input task. */
RECOMP_HOOK_RETURN("func_801F1788_61CB68")
void extra_options_hyper_taisamba_ascent_step(void)
{
    unsigned short frame;
    float rise;
    float remaining;
    if (recomp_get_config_u32("hyper_enemies") != 0 || !taisamba_live() ||
        D_8016DAB4_16E6B4 != s_taisamba_task)
        return;
    frame = TAISAMBA_WORLD_FRAME;
    if (!s_taisamba_rise_frame_valid || s_taisamba_rise_frame != frame)
    {
        s_taisamba_rise_frame = frame;
        s_taisamba_rise_frame_valid = 1;
        s_taisamba_rise_requests = 0;
    }
    if (s_taisamba_rise_requests < 4)
        s_taisamba_rise_requests++;
    rise = (float)s_taisamba_rise_requests;
    remaining = 400.0f - TAISAMBA_ARENA_HEIGHT(s_taisamba_state);
    if (remaining < 0.0f)
        remaining = 0.0f;
    TAISAMBA_ARENA_RISE(s_taisamba_state) = rise < remaining ? rise : remaining;
}

RECOMP_HOOK_RETURN("func_801F6C4C_62202C")
void extra_options_run_hyper_taisamba_tick(void)
{
    void *task;
    void *state;
    void *saved_current;
    unsigned int epoch;
    unsigned int tick;
    unsigned int clock;
    unsigned short frame;

    if (s_taisamba_guard || recomp_get_config_u32("hyper_enemies") != 0 ||
        !taisamba_live() || !taisamba_root_callback(TAISAMBA_AI(s_taisamba_task)))
        return;
    frame = TAISAMBA_WORLD_FRAME;
    if (s_taisamba_frame_valid && s_taisamba_frame == frame)
        return;
    s_taisamba_frame = frame;
    s_taisamba_frame_valid = 1;
    task = s_taisamba_task;
    state = s_taisamba_state;
    epoch = s_taisamba_epoch;
    clock = TAISAMBA_CLOCK(state);
    saved_current = D_8016DAB4_16E6B4;
    s_taisamba_guard = 1;
    for (tick = 0; tick < 3; tick++)
    {
        TaisambaCallback callback;
        int context_changed;

        if (epoch != s_taisamba_epoch || !taisamba_live() ||
            recomp_get_config_u32("hyper_enemies") != 0)
            break;
        callback = TAISAMBA_AI(task);
        if (!taisamba_root_callback(callback))
            break;
        /* F76D0 emits every fourth battle-clock tick. Distinct substeps
         * keep its trail/voice cadence in sync without changing the clock
         * seen by input, the arena, or other tasks. */
        TAISAMBA_CLOCK(state) = clock + tick + 1;
        D_8016DAB4_16E6B4 = task;
        callback(task, s_taisamba_object);
        context_changed = D_8016DAB4_16E6B4 != task;
        D_8016DAB4_16E6B4 = saved_current;
        if (epoch != s_taisamba_epoch || D_8020EED0_63A2B0 != state)
            break;
        TAISAMBA_CLOCK(state) = clock;
        if (context_changed)
            break;
    }
    D_8016DAB4_16E6B4 = saved_current;
    s_taisamba_guard = 0;
    if (epoch == s_taisamba_epoch && taisamba_live())
    {
        if (tick == 3 && !s_taisamba_reported)
        {
            recomp_printf("[Extra Options] Taisamba 2 Hyper: 4x movement, animation, attacks, and projectiles active.\n");
            s_taisamba_reported = 1;
        }
    }
}

static void *s_taisamba_child;
static void *s_taisamba_child_object;
static unsigned int s_taisamba_child_epoch;
static unsigned char s_taisamba_child_guard;

static void taisamba_capture_child(void *task, void *object, TaisambaCallback callback)
{
    if (s_taisamba_child_guard)
        return;
    s_taisamba_child = 0;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !taisamba_live() ||
        TAISAMBA_OBJECT(task) != object || TAISAMBA_AI(task) != callback)
        return;
    s_taisamba_child = task;
    s_taisamba_child_object = object;
    s_taisamba_child_epoch = s_taisamba_epoch;
}

static void taisamba_run_child(void)
{
    void *task;
    void *object;
    void *saved_current;
    unsigned int epoch;
    unsigned int tick;

    if (s_taisamba_child_guard || !s_taisamba_child)
        return;
    task = s_taisamba_child;
    object = s_taisamba_child_object;
    epoch = s_taisamba_child_epoch;
    s_taisamba_child = 0;
    saved_current = D_8016DAB4_16E6B4;
    /* Native task deletion either installs its retirement callback or
     * changes the current-task pointer (800350C4). Check context before
     * dereferencing a task which its original callback may have removed. */
    if (saved_current != task)
        return;
    s_taisamba_child_guard = 1;
    for (tick = 0; tick < 3; tick++)
    {
        TaisambaCallback callback;
        if (epoch != s_taisamba_epoch || !taisamba_live() ||
            recomp_get_config_u32("hyper_enemies") != 0 ||
            TAISAMBA_OBJECT(task) != object)
            break;
        callback = TAISAMBA_AI(task);
        if (!taisamba_child_callback(callback))
            break;
        callback(task, object);
        if (D_8016DAB4_16E6B4 != task)
            break;
    }
    /* Unlike root replay we are inside this child's scheduler callback.
     * Keep native deletion's changed current-task pointer: restoring the
     * removed child would make the scheduler follow a freed list node. */
    s_taisamba_child_guard = 0;
}

#define CHILD_HOOKS(name) \
    RECOMP_HOOK(#name) \
    void extra_options_hyper_taisamba_capture_##name(void *task, void *object) \
    { taisamba_capture_child(task, object, name); } \
    RECOMP_HOOK_RETURN(#name) \
    void extra_options_hyper_taisamba_return_##name(void) \
    { taisamba_run_child(); }
TAISAMBA_CHILD_CALLBACKS(CHILD_HOOKS)
#undef CHILD_HOOKS
