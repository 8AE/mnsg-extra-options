#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"

/* D'Etoile: USA file_13, ROM 5F6840 / VRAM 801CB460, decompressed ROM
 * SHA1 6ea0ed71032ce08fc2745f412d84936382197494. 801FFF70 allocates the
 * standalone encounter; 80201C7C reuses Balberra's root in story mode.
 * Both use constructor 801FAEB0 (ID 0x64) and damage task 801FFAAC.
 * Advance native combat callbacks, not global input/collision or damage.
 * Task +74 is float velocity, not the ordinary actor generation counter.
 */
typedef void (*DetoileCallback)(void *, void *);
extern unsigned char *D_8015C5C8_15D1C8;
extern void *D_8020EED0_63A2B0;
extern unsigned char *D_8020EF40_63A320;
extern void *D_8016DAB4_16E6B4;

#define DETOILE_S32(p, o) (*(volatile signed int *)((char *)(p) + (o)))
#define DETOILE_U8(p, o) (*(volatile unsigned char *)((char *)(p) + (o)))
#define DETOILE_ID(p) (*(volatile unsigned short *)((char *)(p) + 0x5C))
/* Host tests override native 32-bit pointer slots. */
#ifndef DETOILE_POINTER
#define DETOILE_POINTER(p, o) (*(void *volatile *)((char *)(p) + (o)))
#endif
#ifndef DETOILE_AI
#define DETOILE_AI(p) (*(DetoileCallback volatile *)((char *)(p) + 0x0C))
#endif
#ifndef DETOILE_ENABLED_CALLBACK
#define DETOILE_ENABLED_CALLBACK(f) ((f) && !((unsigned long)(f) & 0x00800000u))
#endif
#ifndef DETOILE_ENCOUNTER
#define DETOILE_ENCOUNTER (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADF4))
#endif
#ifndef DETOILE_FRAME
#define DETOILE_FRAME (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADCE))
#endif

/* Neutral, all six branches selected by FC328, shield deflection recovery,
 * and autonomous post-hit evasion. Leave the actual hit/throw reactions,
 * player-special hold FF9EC/FFA40, introduction and defeat scripts native.
 * All movement, animation frames and attack counters advance together.
 */
#define DETOILE_ROOT_CALLBACKS(X) \
    X(func_801FBD6C_62714C) \
    X(func_801FC460_627840) \
    X(func_801FC510_6278F0) \
    X(func_801FC638_627A18) \
    X(func_801FC75C_627B3C) \
    X(func_801FC8A4_627C84) \
    X(func_801FC9E4_627DC4) \
    X(func_801FCA6C_627E4C) \
    X(func_801FCAB4_627E94) \
    X(func_801FCB88_627F68) \
    X(func_801FCC64_628044) \
    X(func_801FCD1C_6280FC) \
    X(func_801FCDAC_62818C) \
    X(func_801FCE74_628254) \
    X(func_801FCF68_628348) \
    X(func_801FD028_628408) \
    X(func_801FD088_628468) \
    X(func_801FD114_6284F4) \
    X(func_801FD1D0_6285B0) \
    X(func_801FD30C_6286EC) \
    X(func_801FD46C_62884C) \
    X(func_801FD500_6288E0) \
    X(func_801FD634_628A14) \
    X(func_801FD724_628B04) \
    X(func_801FD840_628C20) \
    X(func_801FD888_628C68) \
    X(func_801FD8F0_628CD0) \
    X(func_801FDA20_628E00) \
    X(func_801FDB70_628F50) \
    X(func_801FDC84_629064) \
    X(func_801FDD84_629164) \
    X(func_801FDE94_629274) \
    X(func_801FF730_62AB10) \
    X(func_801FF798_62AB78) \
    X(func_801FF8B4_62AC94) \
    X(func_801FF944_62AD24)

/* Meteor flight/acceleration, its timed breakup and the emitted particles.
 * 07698 consumes meteor hits once; its hit-stun clock is held below during
 * extra flight steps. Breakup emits at 1/8/16/24, so do not skip counters.
 * Shield 065B8 and melee proxy 00148 are NOT replayed: they follow the root
 * pose and consume contact/cue state, without an independent motion clock.
 */
#define DETOILE_CHILD_CALLBACKS(X) \
    X(func_802070C0_6324A0) \
    X(func_80207158_632538) \
    X(func_80207288_632668) \
    X(func_802078E0_632CC0)
#define DECLARE_CALLBACK(name) extern void name(void *, void *);
DETOILE_ROOT_CALLBACKS(DECLARE_CALLBACK)
DETOILE_CHILD_CALLBACKS(DECLARE_CALLBACK)
#undef DECLARE_CALLBACK
extern void func_80205FC8_6313A8(void);
extern void func_80203DFC_62F1DC(void *, void *);
extern void func_801D614C_60152C(void *task);
extern void func_80206AA4_631E84(void *, void *);
extern void func_80206D38_632118(void *, void *);

static void *s_detoile_state;
static void *s_detoile_root;
static void *s_detoile_object;
static void *s_detoile_model;
static void *s_detoile_scratch;
static unsigned int s_detoile_epoch;
static unsigned short s_detoile_frame;
static unsigned char s_detoile_frame_valid;
static unsigned char s_detoile_combat_started;
static unsigned char s_detoile_root_guard;
static unsigned char s_detoile_reported;
static void *s_detoile_child;
static void *s_detoile_child_object;
static unsigned int s_detoile_child_epoch;
static unsigned char s_detoile_child_guard;
static unsigned char s_detoile_retired;

static int detoile_root_callback(DetoileCallback callback)
{
    if (!DETOILE_ENABLED_CALLBACK(callback)) return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    DETOILE_ROOT_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int detoile_child_callback(DetoileCallback callback)
{
    if (!DETOILE_ENABLED_CALLBACK(callback)) return 0;
#define MATCH_CALLBACK(name) if (callback == name) return 1;
    DETOILE_CHILD_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int detoile_live(void)
{
    void *state = D_8020EED0_63A2B0;
    if (!s_detoile_combat_started ||
        (DETOILE_ENCOUNTER != 3 && DETOILE_ENCOUNTER != 4) ||
        !state || state != s_detoile_state || !s_detoile_root ||
        DETOILE_POINTER(state, 0x1D8) != s_detoile_root ||
        DETOILE_ID(s_detoile_root) != 0x64 ||
        DETOILE_S32(state, 0x60) <= 0 || DETOILE_S32(state, 0x68) <= 0 ||
        DETOILE_U8(state, 0x2C0) || DETOILE_U8(state, 0x2C4) ||
        !D_8020EF40_63A320 || D_8020EF40_63A320 != s_detoile_scratch)
        return 0;
    if (!s_detoile_object) {
        s_detoile_object = DETOILE_POINTER(s_detoile_root, 0x18);
        s_detoile_model = DETOILE_POINTER(state, 0x1E0);
    }
    return s_detoile_object && s_detoile_model &&
        DETOILE_POINTER(s_detoile_root, 0x18) == s_detoile_object &&
        DETOILE_POINTER(state, 0x1E0) == s_detoile_model;
}

RECOMP_HOOK("func_801FAEB0_626290")
void extra_options_track_hyper_detoile(void *task)
{
    ++s_detoile_epoch;
    s_detoile_state = D_8020EED0_63A2B0;
    s_detoile_root = task;
    s_detoile_scratch = D_8020EF40_63A320;
    s_detoile_object = s_detoile_model = 0;
    s_detoile_frame_valid = s_detoile_combat_started = s_detoile_reported = 0;
    s_detoile_child = 0;
}

RECOMP_HOOK("func_801FC3FC_6277DC")
void extra_options_start_hyper_detoile(void *task)
{
    /* FB67C reaches this neutral reset only after the introduction. Set
     * even with the option off so enabling it mid-fight remains supported. */
    if (task && task == s_detoile_root && D_8016DAB4_16E6B4 == task)
        s_detoile_combat_started = 1;
}

RECOMP_HOOK_RETURN("func_801FFAAC_62AE8C")
void extra_options_run_hyper_detoile_tick(void)
{
    unsigned int tick, epoch;
    void *task, *saved_current;
    if (s_detoile_root_guard || recomp_get_config_u32("hyper_enemies") != 0 ||
        !detoile_live() || !detoile_root_callback(DETOILE_AI(s_detoile_root)))
        return;
    if (s_detoile_frame_valid && s_detoile_frame == DETOILE_FRAME) return;
    s_detoile_frame = DETOILE_FRAME;
    s_detoile_frame_valid = 1;
    task = s_detoile_root;
    epoch = s_detoile_epoch;
    saved_current = D_8016DAB4_16E6B4;
    s_detoile_root_guard = 1;
    s_detoile_retired = 0;
    for (tick = 0; tick < 3; ++tick) {
        DetoileCallback callback;
        if (s_detoile_retired || epoch != s_detoile_epoch || !detoile_live() ||
            recomp_get_config_u32("hyper_enemies") != 0) break;
        callback = DETOILE_AI(task);
        if (!detoile_root_callback(callback)) break;
        D_8016DAB4_16E6B4 = task;
        callback(task, s_detoile_object);
        if (D_8016DAB4_16E6B4 != task || s_detoile_retired ||
            epoch != s_detoile_epoch || !detoile_live()) break;
        /* History writer is otherwise a separate once-per-frame task.
         * Sample each virtual pose; native root + writer supply tick four.
         * Readers remain once per frame, with their native 10/20 delays.
         * No replay of shield +816 consumption (including break cue FF).
         */
        func_80205FC8_6313A8();
    }
    /* This hook belongs to the damage dispatcher, not the replayed root. */
    D_8016DAB4_16E6B4 = saved_current;
    s_detoile_root_guard = 0;
    if (tick == 3 && epoch == s_detoile_epoch && detoile_live() &&
        !s_detoile_reported) {
        recomp_printf("[Extra Options] D'Etoile Hyper: 4x movement, animation, attacks, and projectiles active.\n");
        s_detoile_reported = 1;
    }
}

static void detoile_capture_child(void *task, void *object, DetoileCallback callback)
{
    if (s_detoile_child_guard) return;
    s_detoile_child = 0;
    s_detoile_retired = 0;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !detoile_live() ||
        DETOILE_POINTER(task, 0x18) != object || DETOILE_AI(task) != callback) return;
    s_detoile_child = task;
    s_detoile_child_object = object;
    s_detoile_child_epoch = s_detoile_epoch;
}

static void detoile_run_child(void)
{
    void *task = s_detoile_child;
    void *object = s_detoile_child_object;
    unsigned int tick, epoch = s_detoile_child_epoch;
    if (s_detoile_child_guard || !task) return;
    if (s_detoile_retired || D_8016DAB4_16E6B4 != task) {
        s_detoile_child = 0;
        return;
    }
    s_detoile_child_guard = 1;
    for (tick = 0; tick < 3; ++tick) {
        DetoileCallback callback;
        if (s_detoile_retired || epoch != s_detoile_epoch || !detoile_live() ||
            recomp_get_config_u32("hyper_enemies") != 0 ||
            DETOILE_POINTER(task, 0x18) != object) break;
        callback = DETOILE_AI(task);
        if (!detoile_child_callback(callback)) break;
        callback(task, object);
        if (D_8016DAB4_16E6B4 != task) break;
    }
    /* Inside the child's scheduler callback: preserve native deletion's
     * changed current-task cursor instead of restoring a freed list node. */
    s_detoile_child_guard = 0;
    s_detoile_child = 0;
}

#define CHILD_HOOKS(name) \
    RECOMP_HOOK(#name) \
    void extra_options_detoile_enter_##name(void *task, void *object) \
    { detoile_capture_child(task, object, name); } \
    RECOMP_HOOK_RETURN(#name) \
    void extra_options_detoile_leave_##name(void) { detoile_run_child(); }
DETOILE_CHILD_CALLBACKS(CHILD_HOOKS)
#undef CHILD_HOOKS

/* A meteor's incoming hit is consumed by 07698 in its ordinary update.
 * Hold the native stun/invulnerability counter during extra physics steps;
 * it gates flight at 20 and must not be shortened along with movement.
 */
RECOMP_HOOK("func_80207698_632A78")
void extra_options_detoile_meteor_damage_clock(void *task)
{
    if (s_detoile_child_guard && s_detoile_child == task &&
        D_8016DAB4_16E6B4 == task && DETOILE_S32(task, 0x7C) > 0 &&
        DETOILE_S32(task, 0x7C) < 0x7FFFFFFF)
        ++DETOILE_S32(task, 0x7C);
}

static void detoile_retiring_current(void)
{
    if ((s_detoile_root_guard && D_8016DAB4_16E6B4 == s_detoile_root) ||
        (s_detoile_child && D_8016DAB4_16E6B4 == s_detoile_child))
        s_detoile_retired = 1;
}
RECOMP_HOOK("func_80034ED4_35AD4")
void extra_options_detoile_retire_task(void) { detoile_retiring_current(); }
RECOMP_HOOK("func_80034F20_35B20")
void extra_options_detoile_retire_children(void) { detoile_retiring_current(); }
RECOMP_HOOK("func_80035020_35C20")
void extra_options_detoile_delete_task(void) { detoile_retiring_current(); }

/* Shared volley projectiles process contact/effects in 03DFC: accelerate
 * only the velocity integrator. The Balberra hook is disjoint (root ID78).
 */
static void *s_detoile_shot;
static void *s_detoile_shot_object;
static unsigned int s_detoile_shot_epoch;
static unsigned char s_detoile_motion_guard;
RECOMP_HOOK("func_80203DFC_62F1DC")
void extra_options_detoile_begin_shot(void *task, void *object)
{
    s_detoile_shot = 0;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !detoile_live() ||
        DETOILE_POINTER(task, 0x18) != object ||
        DETOILE_AI(task) != func_80203DFC_62F1DC) return;
    s_detoile_shot = task;
    s_detoile_shot_object = object;
    s_detoile_shot_epoch = s_detoile_epoch;
}
RECOMP_HOOK_RETURN("func_80203DFC_62F1DC")
void extra_options_detoile_end_shot(void) { s_detoile_shot = 0; }

RECOMP_HOOK("func_801D614C_60152C")
void extra_options_detoile_projectile_motion(void *task)
{
    unsigned int tick;
    if (s_detoile_motion_guard || !task || s_detoile_shot != task ||
        s_detoile_shot_epoch != s_detoile_epoch || D_8016DAB4_16E6B4 != task ||
        DETOILE_POINTER(task, 0x18) != s_detoile_shot_object ||
        recomp_get_config_u32("hyper_enemies") != 0 || !detoile_live()) return;
    s_detoile_motion_guard = 1;
    for (tick = 0; tick < 3; ++tick) func_801D614C_60152C(task);
    s_detoile_motion_guard = 0;
}

/* Aura callbacks allocate a material each invocation. Advance just their
 * documented alpha/orbit/pulse clocks three extra ticks; native code then
 * computes the final pose and allocates the material once, as before.
 */
static void detoile_aura_clock(void *task, void *object, DetoileCallback callback,
                               int orbiter)
{
    unsigned int tick;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !detoile_live() ||
        DETOILE_POINTER(task, 0x18) != object || DETOILE_AI(task) != callback) return;
    for (tick = 0; tick < 3; ++tick) {
        int alpha = DETOILE_S32(task, 0x90);
        if (D_8020EF40_63A320[0x817]) {
            if (alpha < (orbiter ? 255 : 200)) ++alpha;
        } else {
            alpha = alpha > 2 ? alpha - 2 : 0;
        }
        DETOILE_S32(task, 0x90) = alpha;
        if (alpha) {
            if (orbiter) DETOILE_S32(task, 0x94) = (DETOILE_S32(task, 0x94) + 2) & 0x3FF;
            DETOILE_S32(task, 0x98) = (DETOILE_S32(task, 0x98) + (orbiter ? 16 : 3)) & 0x3FF;
        }
    }
}
RECOMP_HOOK("func_80206AA4_631E84")
void extra_options_detoile_aura_center(void *task, void *object)
{ detoile_aura_clock(task, object, func_80206AA4_631E84, 0); }
RECOMP_HOOK("func_80206D38_632118")
void extra_options_detoile_aura_orbiter(void *task, void *object)
{ detoile_aura_clock(task, object, func_80206D38_632118, 1); }
