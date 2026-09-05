/* Production-source host regressions, not in-game scheduler certification.
 * xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *   tests/hyper_detoile_test.c -o /tmp/hyper_detoile_test
 */
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __MODDING_H__
#define __RECOMPCONFIG_H__
#define __RECOMPUTILS_H__
#define RECOMP_HOOK(name)
#define RECOMP_HOOK_RETURN(name)
typedef void (*HostCallback)(void *, void *);
static void *host_pointer(void *p, unsigned int offset);
static HostCallback host_ai(void *p);
static void disabled(void *task, void *object);
static unsigned short encounter, frame;
unsigned long recomp_get_config_u32(const char *key);
int recomp_printf(const char *format, ...);
#define DETOILE_POINTER(p, o) host_pointer(p, o)
#define DETOILE_AI(p) host_ai(p)
#define DETOILE_ENABLED_CALLBACK(f) ((f) && (f) != disabled)
#define DETOILE_ENCOUNTER encounter
#define DETOILE_FRAME frame
#include "../src/hyper_detoile.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
typedef struct {
    _Alignas(max_align_t) unsigned char data[0x300];
    HostCallback ai;
    void *object, *root, *model, *hit;
    unsigned int calls, shots, damage;
    float position, animation;
} Fixture;
static Fixture states[2], tasks[5], objects[5];
static unsigned char scratch[2][0x820];
void *D_8020EED0_63A2B0;
unsigned char *D_8020EF40_63A320;
void *D_8016DAB4_16E6B4;
unsigned char *D_8015C5C8_15D1C8;
static unsigned long config;
static unsigned int behavior, messages, history_count, motion_count, collision_count;
static float history_position[16], history_animation[16];
static unsigned char observed_cue[16];
enum { NORMAL, TRANSITION, EXCLUDED, DISABLED_CALLBACK, REPLACE_OBJECT,
       REPLACE_ROOT, REPLACE_MODEL, REPLACE_STATE, REPLACE_SCRATCH, NEW_EPOCH,
       KILL_BOSS, KILL_PLAYER, PAUSE, SECOND_PAUSE, DISABLE_CONFIG,
       CHANGE_ENCOUNTER, CHANGE_ID, REENTER, CHANGE_CURSOR, RETIRE,
       RETIRE_CHILDREN, DELETE_TASK, RETIRE_FIRST, CONSUME_SHIELD };

static void *host_pointer(void *p, unsigned int offset)
{
    Fixture *fixture = p;
    switch (offset) {
    case 0x18: return fixture->object;
    case 0x38: return fixture->hit;
    case 0x1D8: return fixture->root;
    case 0x1E0: return fixture->model;
    default: CHECK(0); return 0;
    }
}
static HostCallback host_ai(void *p) { return ((Fixture *)p)->ai; }
static void excluded(void *task, void *object) { (void)task; (void)object; CHECK(0); }
static void disabled(void *task, void *object) { (void)task; (void)object; CHECK(0); }
unsigned long recomp_get_config_u32(const char *key)
{ CHECK(strcmp(key, "hyper_enemies") == 0); return config; }
int recomp_printf(const char *format, ...)
{ CHECK(strstr(format, "D'Etoile Hyper") != 0); ++messages; return 0; }

static void native_tick(void *task, void *object, HostCallback callback)
{
    Fixture *t = task, *o = object;
    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(t->object == object);
    CHECK(t->calls < sizeof(observed_cue));
    observed_cue[t->calls] = D_8020EF40_63A320[0x816];
    ++t->calls;
    ++o->position;
    ++o->animation;
    /* An equality-triggered emission must survive the three extra steps. */
    if (DETOILE_S32(t, 0x80) > 0 && --DETOILE_S32(t, 0x80) == 2) ++t->shots;
    if (callback == func_802070C0_6324A0 || callback == func_80207158_632538) {
        extra_options_detoile_meteor_damage_clock(task);
        if (DETOILE_S32(t, 0x7C) > 0) --DETOILE_S32(t, 0x7C);
        if (t->hit) { ++t->damage; t->hit = 0; }
    }
    if (behavior == CONSUME_SHIELD && D_8020EF40_63A320[0x816] == 0xFF) {
        /* Model FDF50's native consumption, not a hook-owned cue rewrite. */
        D_8020EF40_63A320[0x816] = 0;
        t->ai = func_801FDE94_629274;
    }
    if (behavior == RETIRE_FIRST) {
        extra_options_detoile_retire_task();
        D_8016DAB4_16E6B4 = &tasks[4];
        return;
    }
    if (t->calls != 2) return;
    switch (behavior) {
    case TRANSITION:
        t->ai = detoile_root_callback(callback) ?
            func_801FD30C_6286EC : func_802078E0_632CC0;
        break;
    case EXCLUDED: t->ai = excluded; break;
    case DISABLED_CALLBACK: t->ai = disabled; break;
    case REPLACE_OBJECT: t->object = &objects[3]; break;
    case REPLACE_ROOT: states[0].root = &tasks[3]; break;
    case REPLACE_MODEL: states[0].model = &objects[3]; break;
    case REPLACE_STATE: D_8020EED0_63A2B0 = &states[1]; break;
    case REPLACE_SCRATCH: D_8020EF40_63A320 = scratch[1]; break;
    case NEW_EPOCH: extra_options_track_hyper_detoile(&tasks[0]); break;
    case KILL_BOSS: DETOILE_S32(&states[0], 0x60) = 0; break;
    case KILL_PLAYER: DETOILE_S32(&states[0], 0x68) = 0; break;
    case PAUSE: DETOILE_U8(&states[0], 0x2C0) = 1; break;
    case SECOND_PAUSE: DETOILE_U8(&states[0], 0x2C4) = 1; break;
    case DISABLE_CONFIG: config = 1; break;
    case CHANGE_ENCOUNTER: encounter = 2; break;
    case CHANGE_ID: DETOILE_ID(&tasks[0]) = 0x82; break;
    case REENTER:
        if (task == &tasks[0]) extra_options_run_hyper_detoile_tick();
        else detoile_run_child();
        break;
    case CHANGE_CURSOR: D_8016DAB4_16E6B4 = &tasks[4]; break;
    case RETIRE: extra_options_detoile_retire_task(); break;
    case RETIRE_CHILDREN: extra_options_detoile_retire_children(); break;
    case DELETE_TASK:
        extra_options_detoile_delete_task();
        D_8016DAB4_16E6B4 = &tasks[4];
        break;
    default: break;
    }
}

#define ROOT_STUB(name) void name(void *task, void *object) \
    { native_tick(task, object, name); }
DETOILE_ROOT_CALLBACKS(ROOT_STUB)
#undef ROOT_STUB
#define CHILD_STUB(name) void name(void *task, void *object) \
    { extra_options_detoile_enter_##name(task, object); \
      native_tick(task, object, name); extra_options_detoile_leave_##name(); }
DETOILE_CHILD_CALLBACKS(CHILD_STUB)
#undef CHILD_STUB

void func_80205FC8_6313A8(void)
{
    Fixture *object = tasks[0].object;
    CHECK(D_8016DAB4_16E6B4 == &tasks[0] || D_8016DAB4_16E6B4 == &tasks[2]);
    CHECK(history_count < 16);
    history_position[history_count] = object->position;
    history_animation[history_count] = object->animation;
    ++history_count;
    D_8020EF40_63A320[0x814] = (D_8020EF40_63A320[0x814] + 1) & 63;
}

void func_801D614C_60152C(void *task)
{
    extra_options_detoile_projectile_motion(task);
    ++motion_count;
    ++((Fixture *)((Fixture *)task)->object)->position;
}
void func_80203DFC_62F1DC(void *task, void *object)
{
    extra_options_detoile_begin_shot(task, object);
    func_801D614C_60152C(task);
    ++collision_count;
    extra_options_detoile_end_shot();
}

/* Aura native ticks allocate/render once, after the hook advances clocks. */
static void aura_native_tick(void *task, int orbiter)
{
    int alpha = DETOILE_S32(task, 0x90);
    if (D_8020EF40_63A320[0x817]) {
        if (alpha < (orbiter ? 255 : 200)) ++alpha;
    } else alpha = alpha > 2 ? alpha - 2 : 0;
    DETOILE_S32(task, 0x90) = alpha;
    if (alpha) {
        if (orbiter) DETOILE_S32(task, 0x94) = (DETOILE_S32(task, 0x94) + 2) & 0x3FF;
        DETOILE_S32(task, 0x98) = (DETOILE_S32(task, 0x98) + (orbiter ? 16 : 3)) & 0x3FF;
    }
    ++((Fixture *)task)->calls;
}
void func_80206AA4_631E84(void *task, void *object)
{ extra_options_detoile_aura_center(task, object); aura_native_tick(task, 0); }
void func_80206D38_632118(void *task, void *object)
{ extra_options_detoile_aura_orbiter(task, object); aura_native_tick(task, 1); }

static void reset(void)
{
    memset(states, 0, sizeof(states)); memset(tasks, 0, sizeof(tasks));
    memset(objects, 0, sizeof(objects)); memset(scratch, 0, sizeof(scratch));
    memset(history_position, 0, sizeof(history_position));
    memset(history_animation, 0, sizeof(history_animation));
    memset(observed_cue, 0, sizeof(observed_cue));
    D_8020EED0_63A2B0 = &states[0]; D_8020EF40_63A320 = scratch[0];
    states[0].root = &tasks[0]; states[0].model = &objects[0];
    for (unsigned int i = 0; i < 5; ++i) {
        tasks[i].object = &objects[i];
        DETOILE_S32(&tasks[i], 0x7C) = 40;
        DETOILE_S32(&tasks[i], 0x80) = 5;
    }
    tasks[0].ai = func_801FBD6C_62714C;
    tasks[1].ai = func_802070C0_6324A0;
    DETOILE_ID(&tasks[0]) = 0x64;
    DETOILE_S32(&states[0], 0x60) = 2000;
    DETOILE_S32(&states[0], 0x68) = 500;
    encounter = 4; frame = 100; config = 0; behavior = NORMAL;
    messages = history_count = motion_count = collision_count = 0;
    D_8016DAB4_16E6B4 = &tasks[0];
    extra_options_track_hyper_detoile(&tasks[0]);
    CHECK(!detoile_live());
    extra_options_start_hyper_detoile(&tasks[0]);
    CHECK(detoile_live());
    D_8016DAB4_16E6B4 = &tasks[3];
}
static void tick(Fixture *task)
{ D_8016DAB4_16E6B4 = task; task->ai(task, task->object); }
static void replay_root(void)
{
    D_8016DAB4_16E6B4 = &tasks[3];
    extra_options_run_hyper_detoile_tick();
    CHECK(D_8016DAB4_16E6B4 == &tasks[3]);
}
static void root_frame(void)
{
    /* Native damage sibling precedes root; history sibling follows it. */
    replay_root(); tick(&tasks[0]);
    D_8016DAB4_16E6B4 = &tasks[2]; func_80205FC8_6313A8();
}
static void check_four_ticks(Fixture *task)
{
    Fixture *object = task->object;
    CHECK(task->calls == 4 && task->shots == 1);
    CHECK(object->position == 4.0f && object->animation == 4.0f);
    CHECK(DETOILE_S32(&states[0], 0x60) == 2000);
    CHECK(DETOILE_S32(&states[0], 0x68) == 500);
}

static void test_all_callbacks(void)
{
    unsigned int root_count = 0, child_count = 0;
#define TEST_ROOT(name) do { \
    reset(); tasks[0].ai = name; root_frame(); check_four_ticks(&tasks[0]); \
    CHECK(history_count == 4 && messages == 1); ++root_count; \
    for (unsigned int i = 0; i < 4; ++i) { \
        CHECK(history_position[i] == (float)(i + 1)); \
        CHECK(history_animation[i] == (float)(i + 1)); \
    } \
} while (0);
    DETOILE_ROOT_CALLBACKS(TEST_ROOT)
#undef TEST_ROOT
#define TEST_CHILD(name) do { \
    reset(); tasks[1].ai = name; tick(&tasks[1]); check_four_ticks(&tasks[1]); \
    CHECK(history_count == 0 && D_8016DAB4_16E6B4 == &tasks[1]); ++child_count; \
} while (0);
    DETOILE_CHILD_CALLBACKS(TEST_CHILD)
#undef TEST_CHILD
    CHECK(root_count == 36 && child_count == 4);
    CHECK(!detoile_root_callback(excluded) && !detoile_root_callback(disabled));
    CHECK(!detoile_child_callback(excluded) && !detoile_child_callback(disabled));
}

static void test_root_lifecycle(void)
{
    static const unsigned int stops[] = { EXCLUDED, DISABLED_CALLBACK,
        REPLACE_OBJECT, REPLACE_ROOT, REPLACE_MODEL, REPLACE_STATE, REPLACE_SCRATCH,
        NEW_EPOCH, KILL_BOSS, KILL_PLAYER, PAUSE, SECOND_PAUSE, DISABLE_CONFIG,
        CHANGE_ENCOUNTER, CHANGE_ID, CHANGE_CURSOR, RETIRE, RETIRE_CHILDREN, DELETE_TASK };
    for (unsigned int i = 0; i < sizeof(stops) / sizeof(stops[0]); ++i) {
        reset(); behavior = stops[i]; replay_root();
        CHECK(tasks[0].calls == 2);
        CHECK(history_count == (stops[i] == EXCLUDED || stops[i] == DISABLED_CALLBACK ||
                                stops[i] == DISABLE_CONFIG ? 2u : 1u));
    }
    reset(); behavior = TRANSITION; root_frame(); check_four_ticks(&tasks[0]);
    reset(); behavior = REENTER; root_frame(); check_four_ticks(&tasks[0]);
    reset(); behavior = RETIRE_FIRST; replay_root();
    CHECK(tasks[0].calls == 1 && history_count == 0);
    reset(); root_frame(); replay_root(); CHECK(tasks[0].calls == 4 && history_count == 4);
    ++frame; root_frame(); CHECK(tasks[0].calls == 8 && history_count == 8 && messages == 1);
    reset(); frame = 0xFFFF; scratch[0][0x814] = 62; root_frame();
    CHECK(scratch[0][0x814] == 2); frame = 0; root_frame();
    CHECK(tasks[0].calls == 8 && scratch[0][0x814] == 6);
}

static void test_entry_guards(void)
{
    reset(); encounter = 3; root_frame(); check_four_ticks(&tasks[0]);
    for (unsigned int e = 0; e < 6; ++e) {
        if (e == 3 || e == 4) continue;
        reset(); encounter = e; root_frame(); CHECK(tasks[0].calls == 1);
    }
    reset(); config = 1; root_frame(); CHECK(tasks[0].calls == 1 && history_count == 1);
    config = 0; ++frame; replay_root(); CHECK(tasks[0].calls == 4);
    reset(); DETOILE_ID(&tasks[0]) = 0x82; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); DETOILE_ID(&tasks[0]) = 0x78; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); DETOILE_S32(&states[0], 0x60) = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); DETOILE_S32(&states[0], 0x68) = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); DETOILE_U8(&states[0], 0x2C0) = 1; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); DETOILE_U8(&states[0], 0x2C4) = 1; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); tasks[0].ai = excluded; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); tasks[0].ai = disabled; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); tasks[0].ai = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); states[0].model = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); tasks[0].object = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); D_8020EF40_63A320 = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); D_8020EED0_63A2B0 = 0; replay_root(); CHECK(tasks[0].calls == 0);
    reset(); extra_options_track_hyper_detoile(&tasks[0]);
    extra_options_start_hyper_detoile(&tasks[0]); CHECK(!detoile_live());
    D_8016DAB4_16E6B4 = &tasks[0];
    extra_options_start_hyper_detoile(&tasks[1]); CHECK(!detoile_live());
    extra_options_start_hyper_detoile(&tasks[0]); CHECK(detoile_live());
    reset(); extra_options_track_hyper_detoile(&tasks[0]);
    tick(&tasks[1]); CHECK(tasks[1].calls == 1); /* Intro children stay native. */
    reset(); tasks[0].ai = excluded; tick(&tasks[1]);
    CHECK(tasks[1].calls == 4); /* Projectiles continue during native hit reaction. */
}

static void test_child_lifecycle_and_damage(void)
{
    static const unsigned int stops[] = { EXCLUDED, DISABLED_CALLBACK,
        REPLACE_OBJECT, REPLACE_ROOT, REPLACE_MODEL, REPLACE_STATE, REPLACE_SCRATCH,
        NEW_EPOCH, KILL_BOSS, KILL_PLAYER, PAUSE, SECOND_PAUSE, DISABLE_CONFIG,
        CHANGE_ENCOUNTER, CHANGE_ID, CHANGE_CURSOR, RETIRE, RETIRE_CHILDREN, DELETE_TASK };
    for (unsigned int i = 0; i < sizeof(stops) / sizeof(stops[0]); ++i) {
        reset(); behavior = stops[i]; tick(&tasks[1]);
        CHECK(tasks[1].calls == 2);
        CHECK(D_8016DAB4_16E6B4 ==
            (stops[i] == CHANGE_CURSOR || stops[i] == DELETE_TASK ? &tasks[4] : &tasks[1]));
    }
    reset(); behavior = RETIRE_FIRST; tick(&tasks[1]);
    CHECK(tasks[1].calls == 1 && D_8016DAB4_16E6B4 == &tasks[4]);
    reset(); behavior = TRANSITION; tick(&tasks[1]); check_four_ticks(&tasks[1]);
    reset(); behavior = REENTER; tick(&tasks[1]); check_four_ticks(&tasks[1]);
    reset(); tasks[1].hit = &tasks[4]; tick(&tasks[1]);
    CHECK(tasks[1].damage == 1 && DETOILE_S32(&tasks[1], 0x7C) == 39);
    reset(); DETOILE_S32(&tasks[1], 0x7C) = 0; tick(&tasks[1]);
    CHECK(DETOILE_S32(&tasks[1], 0x7C) == 0);
    reset(); extra_options_detoile_meteor_damage_clock(&tasks[1]);
    CHECK(DETOILE_S32(&tasks[1], 0x7C) == 40); /* Outside a replay is untouched. */
    s_detoile_child_guard = 1; s_detoile_child = &tasks[1];
    D_8016DAB4_16E6B4 = &tasks[1];
    extra_options_detoile_meteor_damage_clock(&tasks[1]);
    CHECK(DETOILE_S32(&tasks[1], 0x7C) == 41);
    extra_options_detoile_meteor_damage_clock(&tasks[0]);
    CHECK(DETOILE_S32(&tasks[0], 0x7C) == 40);
    DETOILE_S32(&tasks[1], 0x7C) = INT_MAX;
    extra_options_detoile_meteor_damage_clock(&tasks[1]);
    CHECK(DETOILE_S32(&tasks[1], 0x7C) == INT_MAX);
    s_detoile_child_guard = 0; s_detoile_child = 0;
}

static void test_history_and_shield_cues(void)
{
    reset(); scratch[0][0x815] = 1; scratch[0][0x816] = 0xFF;
    root_frame(); CHECK(scratch[0][0x815] == 1 && scratch[0][0x816] == 0xFF);
    for (unsigned int i = 0; i < 4; ++i) CHECK(observed_cue[i] == 0xFF);
    reset(); scratch[0][0x816] = 0xFF; behavior = CONSUME_SHIELD;
    root_frame(); CHECK(observed_cue[0] == 0xFF && observed_cue[1] == 0);
    CHECK(scratch[0][0x816] == 0 && tasks[0].ai == func_801FDE94_629274);
    CHECK(history_count == 4); check_four_ticks(&tasks[0]);
}

static void test_projectiles(void)
{
    reset(); tasks[1].ai = func_80203DFC_62F1DC; tick(&tasks[1]);
    CHECK(motion_count == 4 && collision_count == 1 && objects[1].position == 4.0f);
    CHECK(D_8016DAB4_16E6B4 == &tasks[1]);
    func_801D614C_60152C(&tasks[1]); CHECK(motion_count == 5);
    reset(); config = 1; tasks[1].ai = func_80203DFC_62F1DC; tick(&tasks[1]);
    CHECK(motion_count == 1 && collision_count == 1);
    reset(); encounter = 3; tasks[1].ai = func_80203DFC_62F1DC; tick(&tasks[1]);
    CHECK(motion_count == 4 && collision_count == 1);
    reset(); DETOILE_ID(&tasks[0]) = 0x78; tasks[1].ai = func_80203DFC_62F1DC;
    tick(&tasks[1]); CHECK(motion_count == 1 && collision_count == 1);
    reset(); tasks[1].ai = func_80203DFC_62F1DC;
    D_8016DAB4_16E6B4 = &tasks[1];
    extra_options_detoile_begin_shot(&tasks[1], &objects[1]);
    tasks[1].object = &objects[2]; func_801D614C_60152C(&tasks[1]);
    CHECK(motion_count == 1); extra_options_detoile_end_shot();
    reset(); tasks[1].ai = func_80203DFC_62F1DC;
    D_8016DAB4_16E6B4 = &tasks[1];
    extra_options_detoile_begin_shot(&tasks[1], &objects[1]);
    extra_options_track_hyper_detoile(&tasks[0]); func_801D614C_60152C(&tasks[1]);
    CHECK(motion_count == 1); extra_options_detoile_end_shot();
}

static void test_aura_clocks(void)
{
    for (unsigned int orbiter = 0; orbiter < 2; ++orbiter) {
        reset(); tasks[1].ai = orbiter ? func_80206D38_632118 : func_80206AA4_631E84;
        scratch[0][0x817] = 1; tick(&tasks[1]);
        CHECK(tasks[1].calls == 1 && DETOILE_S32(&tasks[1], 0x90) == 4);
        CHECK(DETOILE_S32(&tasks[1], 0x94) == (orbiter ? 8 : 0));
        CHECK(DETOILE_S32(&tasks[1], 0x98) == (orbiter ? 64 : 12));
        DETOILE_S32(&tasks[1], 0x90) = orbiter ? 254 : 199;
        DETOILE_S32(&tasks[1], 0x94) = 1022; DETOILE_S32(&tasks[1], 0x98) = 1022;
        tick(&tasks[1]); CHECK(DETOILE_S32(&tasks[1], 0x90) == (orbiter ? 255 : 200));
        CHECK(DETOILE_S32(&tasks[1], 0x94) == (orbiter ? 6 : 1022));
        CHECK(DETOILE_S32(&tasks[1], 0x98) == (orbiter ? 62 : 10));
        scratch[0][0x817] = 0; DETOILE_S32(&tasks[1], 0x90) = 5;
        tick(&tasks[1]); CHECK(DETOILE_S32(&tasks[1], 0x90) == 0);
        config = 1; scratch[0][0x817] = 1; tick(&tasks[1]);
        CHECK(DETOILE_S32(&tasks[1], 0x90) == 1);
    }
}

int main(void)
{
    test_all_callbacks(); test_root_lifecycle(); test_entry_guards();
    test_child_lifecycle_and_damage(); test_history_and_shield_cues();
    test_projectiles(); test_aura_clocks();
    puts("D'Etoile Hyper host regressions passed.");
    return 0;
}
