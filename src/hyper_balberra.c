#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"
#include "hyper_enemies.h"

/* Independent 2.5x cadence clocks for the root, deck/pod parts, drones and
 * the beam charge.  Tasks of one kind advance together on the same native
 * frame, so they share one alternating 1/2-tick clock. */
#define BALBERRA_CLOCK_ROOT 6u
#define BALBERRA_CLOCK_PART 7u
#define BALBERRA_CLOCK_SHOT 12u

/* Balberra: USA file_13, ROM 5F6840 / VRAM 801CB460, decompressed ROM
 * SHA1 6ea0ed71032ce08fc2745f412d84936382197494. 801FFE80 allocates
 * 80200200, whose ID is 0x78. Its cannon decks, opening side pods and
 * flying drones are separate tasks; accelerating the root alone loses
 * their firing/animation clocks. D'Etoile reuses this root after the native
 * defeat cinematic but changes its ID to 0x64 and is deliberately excluded.
 */
typedef void (*BalberraCallback)(void *, void *);
extern unsigned char *D_8015C5C8_15D1C8;
extern void *D_8020EED0_63A2B0;
extern unsigned char *D_8020EF40_63A320;
extern void *D_8016DAB4_16E6B4;

#define BALBERRA_S32(p, o) (*(volatile signed int *)((char *)(p) + (o)))
#define BALBERRA_U8(p, o) (*(volatile unsigned char *)((char *)(p) + (o)))
#define BALBERRA_ID(p) (*(volatile unsigned short *)((char *)(p) + 0x5C))
#ifndef BALBERRA_POINTER
#define BALBERRA_POINTER(p, o) (*(void *volatile *)((char *)(p) + (o)))
#endif
#ifndef BALBERRA_AI
#define BALBERRA_AI(p) (*(BalberraCallback volatile *)((char *)(p) + 0x0C))
#endif
#ifndef BALBERRA_ENABLED_CALLBACK
#define BALBERRA_ENABLED_CALLBACK(f) ((f) && !((unsigned long)(f) & 0x00800000u))
#endif
#ifndef BALBERRA_ENCOUNTER
#define BALBERRA_ENCOUNTER (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADF4))
#endif
#ifndef BALBERRA_FRAME
#define BALBERRA_FRAME (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADCE))
#endif

enum { BALBERRA_ROOT = 1, BALBERRA_PART, BALBERRA_DRONE, BALBERRA_CHARGE };

/* Only autonomous combat states, not initialization, incoming-hit/Impact
 * throw reactions, destruction, shared input or cinematic coordination.
 * 03424 is intentionally NOT replayed: each intact rocket tube consumes
 * the root's one-frame +7 cue once, with no clock of its own to accelerate.
 */
#define BALBERRA_CALLBACKS(X) \
    X(func_80200508_62B8E8, BALBERRA_ROOT) \
    X(func_80200BA4_62BF84, BALBERRA_ROOT) \
    X(func_80200BE8_62BFC8, BALBERRA_ROOT) \
    X(func_80200D4C_62C12C, BALBERRA_ROOT) \
    X(func_80201018_62C3F8, BALBERRA_ROOT) \
    X(func_80201088_62C468, BALBERRA_ROOT) \
    X(func_80201218_62C5F8, BALBERRA_ROOT) \
    X(func_802012C8_62C6A8, BALBERRA_ROOT) \
    X(func_80201374_62C754, BALBERRA_ROOT) \
    X(func_802013D0_62C7B0, BALBERRA_ROOT) \
    X(func_8020143C_62C81C, BALBERRA_ROOT) \
    X(func_80201598_62C978, BALBERRA_ROOT) \
    X(func_80201618_62C9F8, BALBERRA_ROOT) \
    X(func_80201684_62CA64, BALBERRA_ROOT) \
    X(func_80201770_62CB50, BALBERRA_ROOT) \
    X(func_802017C8_62CBA8, BALBERRA_ROOT) \
    X(func_802018B4_62CC94, BALBERRA_ROOT) \
    X(func_8020190C_62CCEC, BALBERRA_ROOT) \
    X(func_80202264_62D644, BALBERRA_PART) \
    X(func_80202598_62D978, BALBERRA_PART) \
    X(func_80202904_62DCE4, BALBERRA_PART) \
    X(func_80202BE4_62DFC4, BALBERRA_PART) \
    X(func_80202EA4_62E284, BALBERRA_PART) \
    X(func_80203154_62E534, BALBERRA_PART) \
    X(func_802055B4_630994, BALBERRA_DRONE) \
    X(func_80205614_6309F4, BALBERRA_DRONE) \
    X(func_80205734_630B14, BALBERRA_DRONE) \
    X(func_8020580C_630BEC, BALBERRA_DRONE) \
    X(func_802058B0_630C90, BALBERRA_DRONE) \
    X(func_80205978_630D58, BALBERRA_DRONE) \
    X(func_80205A04_630DE4, BALBERRA_DRONE) \
    X(func_80204C5C_63003C, BALBERRA_CHARGE)

#define DECLARE_CALLBACK(name, kind) extern void name(void *, void *);
BALBERRA_CALLBACKS(DECLARE_CALLBACK)
#undef DECLARE_CALLBACK

typedef struct {
    void *task;
    void *object;
    unsigned short frame;
    unsigned char frame_valid;
    unsigned char kind;
} BalberraClock;

/* At most one root, six clocked deck/pod tasks, six drones and the beam
 * charge are live. Spare entries allow an old slot to await reuse safely. */
static BalberraClock s_balberra_clocks[32];
static BalberraClock *s_balberra_current;
static void *s_balberra_state;
static void *s_balberra_root;
static void *s_balberra_object;
static void *s_balberra_model;
static void *s_balberra_scratch;
static unsigned int s_balberra_epoch;
static unsigned char s_balberra_replay;
static unsigned char s_balberra_terminated;
static unsigned char s_balberra_reported;
static unsigned char s_balberra_combat_started;

static unsigned int balberra_callback_kind(BalberraCallback callback)
{
    if (!BALBERRA_ENABLED_CALLBACK(callback)) return 0;
#define MATCH_CALLBACK(name, kind) if (callback == name) return kind;
    BALBERRA_CALLBACKS(MATCH_CALLBACK)
#undef MATCH_CALLBACK
    return 0;
}

static int balberra_live(void)
{
    void *state = D_8020EED0_63A2B0;
    if (!s_balberra_combat_started || BALBERRA_ENCOUNTER != 3 ||
        !state || state != s_balberra_state ||
        !s_balberra_root || BALBERRA_POINTER(state, 0x1D8) != s_balberra_root ||
        BALBERRA_ID(s_balberra_root) != 0x78 ||
        BALBERRA_S32(s_balberra_root, 0xAC) <= 0 ||
        BALBERRA_S32(state, 0x60) <= 0 || BALBERRA_S32(state, 0x68) <= 0 ||
        BALBERRA_U8(state, 0x2C0) || !D_8020EF40_63A320 ||
        D_8020EF40_63A320 != s_balberra_scratch)
        return 0;
    if (!s_balberra_object) {
        s_balberra_object = BALBERRA_POINTER(s_balberra_root, 0x18);
        s_balberra_model = BALBERRA_POINTER(state, 0x1E0);
    }
    return s_balberra_object && s_balberra_model &&
        BALBERRA_POINTER(s_balberra_root, 0x18) == s_balberra_object &&
        BALBERRA_POINTER(state, 0x1E0) == s_balberra_model;
}

RECOMP_HOOK("func_80200200_62B5E0")
void extra_options_track_hyper_balberra(void *task)
{
    unsigned int i;
    ++s_balberra_epoch;
    s_balberra_root = task;
    s_balberra_state = D_8020EED0_63A2B0;
    s_balberra_scratch = D_8020EF40_63A320;
    s_balberra_object = 0;
    s_balberra_model = 0;
    s_balberra_current = 0;
    s_balberra_reported = 0;
    s_balberra_combat_started = 0;
    for (i = 0; i < 32; ++i) {
        s_balberra_clocks[i].task = 0;
        s_balberra_clocks[i].frame_valid = 0;
    }
}

static void balberra_begin(void *task, void *object, BalberraCallback callback)
{
    BalberraClock *slot = 0;
    unsigned int i;
    if (s_balberra_replay) return;
    s_balberra_current = 0;
    s_balberra_terminated = 0;
    /* Keep the independently flying shots/drones Hyper during a root hit
     * reaction, but never accelerate them during the opening cinematic. */
    if (task && task == s_balberra_root && D_8016DAB4_16E6B4 == task &&
        BALBERRA_AI(task) == callback &&
        balberra_callback_kind(callback) == BALBERRA_ROOT)
        s_balberra_combat_started = 1;
    if (!task || !object || D_8016DAB4_16E6B4 != task ||
        recomp_get_config_u32("hyper_enemies") != 0 || !balberra_live() ||
        BALBERRA_AI(task) != callback || BALBERRA_POINTER(task, 0x18) != object)
        return;
    for (i = 0; i < 32; ++i) {
        if (s_balberra_clocks[i].task == task) {
            slot = &s_balberra_clocks[i];
            break;
        }
        if (!slot && (!s_balberra_clocks[i].task ||
            (unsigned short)(BALBERRA_FRAME - s_balberra_clocks[i].frame) > 2))
            slot = &s_balberra_clocks[i];
    }
    if (!slot) return;
    if (slot->task != task || slot->object != object) slot->frame_valid = 0;
    slot->task = task;
    slot->object = object;
    slot->kind = (unsigned char)balberra_callback_kind(callback);
    s_balberra_current = slot;
}

static int balberra_clock_live(BalberraClock *slot, unsigned int epoch)
{
    return !s_balberra_terminated && epoch == s_balberra_epoch && balberra_live() &&
        slot && slot->task && BALBERRA_POINTER(slot->task, 0x18) == slot->object &&
        balberra_callback_kind(BALBERRA_AI(slot->task)) == slot->kind &&
        (slot->kind != BALBERRA_ROOT || slot->task == s_balberra_root) &&
        recomp_get_config_u32("hyper_enemies") == 0;
}

static void balberra_finish(void)
{
    BalberraClock *slot = s_balberra_current;
    unsigned int tick, epoch = s_balberra_epoch;
    unsigned int clock_id, extra_ticks;
    unsigned char rocket_cue;
    if (s_balberra_replay || !balberra_clock_live(slot, epoch) ||
        D_8016DAB4_16E6B4 != slot->task) return;
    if (slot->frame_valid && slot->frame == BALBERRA_FRAME) return;
    slot->frame = BALBERRA_FRAME;
    slot->frame_valid = 1;
    clock_id = slot->kind == BALBERRA_ROOT ? BALBERRA_CLOCK_ROOT
                                           : BALBERRA_CLOCK_PART;
    extra_options_hyper_impact_cadence_begin(clock_id, BALBERRA_FRAME);
    extra_ticks = extra_options_hyper_impact_extra_ticks(clock_id);
    rocket_cue = D_8020EF40_63A320[7];
    s_balberra_replay = 1;
    for (tick = 0; tick < extra_ticks; ++tick) {
        if (!balberra_clock_live(slot, epoch)) break;
        D_8016DAB4_16E6B4 = slot->task;
        BALBERRA_AI(slot->task)(slot->task, slot->object);
        if (epoch == s_balberra_epoch && D_8020EF40_63A320 == s_balberra_scratch)
            rocket_cue |= D_8020EF40_63A320[7];
        if (D_8016DAB4_16E6B4 != slot->task) break;
    }
    /* We are inside this task's scheduler callback, not a separate damage
     * dispatcher. Native deletion rewinds the current-task cursor; retain
     * that new cursor instead of restoring a task that has been retired. */
    s_balberra_replay = 0;
    /* 01218 raises +7, then 012C8 clears it on the next virtual tick. Keep
     * that one-shot event visible to all six native 03424 tube callbacks.
     * Their native single dispatch emits one shot each, not four duplicates. */
    if (slot->kind == BALBERRA_ROOT && balberra_clock_live(slot, epoch)) {
        D_8020EF40_63A320[7] = rocket_cue;
        if (!s_balberra_reported && tick == extra_ticks) {
            recomp_printf("[Extra Options] Balberra Hyper: 2.5x movement, components, and attacks active.\n");
            s_balberra_reported = 1;
        }
    }
    s_balberra_current = 0;
}

#define HOOK_CALLBACK(name, kind) \
    RECOMP_HOOK(#name) \
    void extra_options_balberra_enter_##name(void *task, void *object) \
    { balberra_begin(task, object, name); } \
    RECOMP_HOOK_RETURN(#name) \
    void extra_options_balberra_leave_##name(void) { balberra_finish(); }
BALBERRA_CALLBACKS(HOOK_CALLBACK)
#undef HOOK_CALLBACK

/* Damage helpers consume +38 once through 0451C. Keep their flashing /
 * invulnerability counter on the native frame clock during extra AI ticks;
 * no input or collision pass is rerun and HP is never restored by Hyper. */
static void balberra_preserve_damage_clock(void *task)
{
    if (s_balberra_replay && s_balberra_current &&
        s_balberra_current->task == task && D_8016DAB4_16E6B4 == task &&
        BALBERRA_S32(task, 0xB0) > 0 && BALBERRA_S32(task, 0xB0) < 0x7FFFFFFF)
        ++BALBERRA_S32(task, 0xB0);
}
RECOMP_HOOK("func_8020407C_62F45C")
void extra_options_balberra_root_damage_clock(void *task)
{ balberra_preserve_damage_clock(task); }
RECOMP_HOOK("func_802043E4_62F7C4")
void extra_options_balberra_part_damage_clock(void *task)
{ balberra_preserve_damage_clock(task); }

/* Charges may retire without clearing their callback first. Drones also
 * clear their attached effects (34F20 removes children, not the drone) while
 * replacing their model/AI with an explosion. End this virtual-frame budget
 * before either lifecycle operation, including in the original callback. */
static void balberra_retiring_current(void)
{
    if (s_balberra_current && D_8016DAB4_16E6B4 == s_balberra_current->task)
        s_balberra_terminated = 1;
}
RECOMP_HOOK("func_80034ED4_35AD4")
void extra_options_balberra_retire_task(void) { balberra_retiring_current(); }
RECOMP_HOOK("func_80034F20_35B20")
void extra_options_balberra_retire_children(void) { balberra_retiring_current(); }
RECOMP_HOOK("func_80035020_35C20")
void extra_options_balberra_delete_task(void) { balberra_retiring_current(); }

/* Travelling projectiles do collision/damage in their AI. Scope acceleration
 * to 801D614C, the velocity-only XYZ integrator, and leave collision once per
 * native frame. Charged beam growth is already clocked separately above. */
#define BALBERRA_SHOTS(X) \
    X(func_8020396C_62ED4C) \
    X(func_80203A54_62EE34) \
    X(func_80203DFC_62F1DC) \
    X(func_80204EF4_6302D4)
#define DECLARE_SHOT(name) extern void name(void *, void *);
BALBERRA_SHOTS(DECLARE_SHOT)
#undef DECLARE_SHOT
extern void func_801D614C_60152C(void *task);
static void *s_balberra_shot;
static void *s_balberra_shot_object;
static unsigned int s_balberra_shot_epoch;
static unsigned char s_balberra_motion_guard;

static void balberra_begin_shot(void *task, void *object, BalberraCallback callback)
{
    s_balberra_shot = 0;
    if (!task || !object || recomp_get_config_u32("hyper_enemies") != 0 ||
        !balberra_live() || D_8016DAB4_16E6B4 != task ||
        BALBERRA_POINTER(task, 0x18) != object || BALBERRA_AI(task) != callback)
        return;
    s_balberra_shot = task;
    s_balberra_shot_object = object;
    s_balberra_shot_epoch = s_balberra_epoch;
    /* The shot's lifetime timer advances one frame per motion step, so it must
     * shed the same number of frames the movement helper replays.  Roll this
     * frame's 2.5x budget here; the motion helper shares this clock. */
    extra_options_hyper_impact_cadence_begin(BALBERRA_CLOCK_SHOT, BALBERRA_FRAME);
    if (callback == func_8020396C_62ED4C) {
        unsigned int extra_ticks =
            extra_options_hyper_impact_extra_ticks(BALBERRA_CLOCK_SHOT);
        signed int timer = BALBERRA_S32(task, 0x7C);
        if (timer >= 0)
            BALBERRA_S32(task, 0x7C) =
                timer > (signed int)extra_ticks
                    ? timer - (signed int)extra_ticks
                    : -1;
    }
    else if (callback == func_80203A54_62EE34 &&
             *(volatile float *)((char *)task + 0x78) < 0.0f) {
        unsigned int extra_ticks =
            extra_options_hyper_impact_extra_ticks(BALBERRA_CLOCK_SHOT);
        signed int timer = BALBERRA_S32(task, 0x7C);
        if (timer > 0)
            BALBERRA_S32(task, 0x7C) =
                timer > (signed int)extra_ticks
                    ? timer - (signed int)extra_ticks
                    : 0;
    }
}

#define HOOK_SHOT(name) \
    RECOMP_HOOK(#name) \
    void extra_options_balberra_enter_##name(void *task, void *object) \
    { balberra_begin_shot(task, object, name); } \
    RECOMP_HOOK_RETURN(#name) \
    void extra_options_balberra_leave_##name(void) { s_balberra_shot = 0; }
BALBERRA_SHOTS(HOOK_SHOT)
#undef HOOK_SHOT

RECOMP_HOOK("func_801D614C_60152C")
void extra_options_balberra_projectile_motion(void *task)
{
    unsigned int i;
    if (s_balberra_motion_guard || !task || s_balberra_shot != task ||
        s_balberra_shot_epoch != s_balberra_epoch ||
        D_8016DAB4_16E6B4 != task || BALBERRA_POINTER(task, 0x18) != s_balberra_shot_object ||
        recomp_get_config_u32("hyper_enemies") != 0 || !balberra_live()) return;
    s_balberra_motion_guard = 1;
    extra_options_hyper_impact_cadence_begin(BALBERRA_CLOCK_SHOT, BALBERRA_FRAME);
    for (i = 0;
         i < extra_options_hyper_impact_extra_ticks(BALBERRA_CLOCK_SHOT);
         ++i)
        func_801D614C_60152C(task);
    s_balberra_motion_guard = 0;
}
