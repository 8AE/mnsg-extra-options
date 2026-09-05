/* Production-source host regression; native gameplay still requires testing.
 * xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *   tests/hyper_balberra_test.c -o /tmp/hyper_balberra_test
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __MODDING_H__
#define __RECOMPCONFIG_H__
#define __RECOMPUTILS_H__
#define RECOMP_HOOK(name)
#define RECOMP_HOOK_RETURN(name)
unsigned long recomp_get_config_u32(const char *key);
int recomp_printf(const char *format, ...);
typedef void (*HostCallback)(void *, void *);
static void *host_pointer(void *p, unsigned int offset);
static HostCallback host_ai(void *p);
static unsigned short encounter, frame;
#define BALBERRA_POINTER(p, o) host_pointer(p, o)
#define BALBERRA_AI(p) host_ai(p)
#define BALBERRA_ENABLED_CALLBACK(f) ((f) != 0)
#define BALBERRA_ENCOUNTER encounter
#define BALBERRA_FRAME frame
#include "../src/hyper_balberra.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct {
    _Alignas(max_align_t) unsigned char data[0x300];
    HostCallback ai;
    void *object;
    void *root;
    void *model;
    void *hit;
    unsigned int calls;
    unsigned int shots;
    unsigned int damage;
    float position;
    float animation;
} Fixture;
static Fixture state, tasks[4], objects[4];
static unsigned char scratch[0x820];
void *D_8020EED0_63A2B0;
unsigned char *D_8020EF40_63A320;
void *D_8016DAB4_16E6B4;
unsigned char *D_8015C5C8_15D1C8;
static unsigned long config;
static unsigned int behavior, motion_calls, collision_calls, messages;
enum { NORMAL, TRANSITION, EXCLUDED, PULSE, KILL_ROOT, KILL_PLAYER, PAUSE,
       REPLACE_OBJECT, REPLACE_STATE, REPLACE_CONTEXT, RETIRE, NEW_EPOCH,
       DISABLE, CHANGE_ID, DAMAGE, DEAD_PART, NATIVE_RETIRE };

static void *host_pointer(void *p, unsigned int offset)
{
    Fixture *f = p;
    switch (offset) {
    case 0x18: return f->object;
    case 0x38: return f->hit;
    case 0x1D8: return f->root;
    case 0x1E0: return f->model;
    default: CHECK(0); return 0;
    }
}
static HostCallback host_ai(void *p) { return ((Fixture *)p)->ai; }
unsigned long recomp_get_config_u32(const char *key)
{ CHECK(strcmp(key, "hyper_enemies") == 0); return config; }
int recomp_printf(const char *format, ...)
{ (void)format; ++messages; return 0; }
static void excluded(void *task, void *object)
{ (void)task; (void)object; CHECK(0); }

static void mock_body(void *task, void *object, HostCallback callback, unsigned int kind)
{
    Fixture *t = task;
    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(object == t->object);
    ++t->calls;
    ++t->position;
    ++t->animation;
    if (BALBERRA_S32(task, 0x7C) > 0 && --BALBERRA_S32(task, 0x7C) == 2)
        ++t->shots;
    if (kind == BALBERRA_ROOT || kind == BALBERRA_PART) {
        if (kind == BALBERRA_ROOT) extra_options_balberra_root_damage_clock(task);
        else extra_options_balberra_part_damage_clock(task);
        if (BALBERRA_S32(task, 0xB0) > 0) --BALBERRA_S32(task, 0xB0);
        if (t->hit) {
            ++t->damage;
            t->hit = 0;
            --BALBERRA_S32(task, 0xAC);
        }
    }
    if (behavior == PULSE) {
        if (callback == func_80200508_62B8E8) t->ai = func_80201218_62C5F8;
        else if (callback == func_80201218_62C5F8) {
            scratch[7] = 1;
            t->ai = func_802012C8_62C6A8;
        } else if (callback == func_802012C8_62C6A8) scratch[7] = 0;
    }
    if (behavior == NATIVE_RETIRE) {
        extra_options_balberra_retire_task();
        D_8016DAB4_16E6B4 = &tasks[3];
        return;
    }
    if (t->calls != 2 && behavior != DAMAGE) return;
    switch (behavior) {
    case TRANSITION: t->ai = func_80201088_62C468; break;
    case EXCLUDED: t->ai = excluded; break;
    case KILL_ROOT: BALBERRA_S32(&tasks[0], 0xAC) = 0; break;
    case KILL_PLAYER: BALBERRA_S32(&state, 0x68) = 0; break;
    case PAUSE: BALBERRA_U8(&state, 0x2C0) = 1; break;
    case REPLACE_OBJECT: t->object = &objects[3]; break;
    case REPLACE_STATE: D_8020EED0_63A2B0 = &objects[3]; break;
    case REPLACE_CONTEXT: D_8016DAB4_16E6B4 = &tasks[3]; break;
    case RETIRE:
        extra_options_balberra_retire_task();
        D_8016DAB4_16E6B4 = &tasks[3];
        break;
    case NEW_EPOCH: extra_options_track_hyper_balberra(&tasks[0]); break;
    case DISABLE: config = 1; break;
    case CHANGE_ID: BALBERRA_ID(&tasks[0]) = 0x64; break;
    case DEAD_PART: BALBERRA_S32(task, 0xAC) = 0; t->ai = excluded; break;
    default: break;
    }
}

#define MOCK_CALLBACK(name, kind) \
    void name(void *task, void *object) { \
        extra_options_balberra_enter_##name(task, object); \
        mock_body(task, object, name, kind); \
        extra_options_balberra_leave_##name(); \
    }
BALBERRA_CALLBACKS(MOCK_CALLBACK)
#undef MOCK_CALLBACK

void func_801D614C_60152C(void *task)
{
    extra_options_balberra_projectile_motion(task);
    ++motion_calls;
    ++((Fixture *)task)->position;
}
#define MOCK_SHOT(name) \
    void name(void *task, void *object) { \
        extra_options_balberra_enter_##name(task, object); \
        func_801D614C_60152C(task); \
        ++collision_calls; \
        extra_options_balberra_leave_##name(); \
    }
BALBERRA_SHOTS(MOCK_SHOT)
#undef MOCK_SHOT

static void reset(void)
{
    memset(&state, 0, sizeof(state));
    memset(tasks, 0, sizeof(tasks));
    memset(objects, 0, sizeof(objects));
    memset(scratch, 0, sizeof(scratch));
    D_8020EED0_63A2B0 = &state;
    D_8020EF40_63A320 = scratch;
    state.root = &tasks[0]; state.model = &objects[0];
    for (unsigned int i = 0; i < 4; ++i) {
        tasks[i].object = &objects[i];
        BALBERRA_S32(&tasks[i], 0xAC) = 1000;
        BALBERRA_S32(&tasks[i], 0xB0) = 40;
        BALBERRA_S32(&tasks[i], 0x7C) = 5;
    }
    tasks[0].ai = func_80200508_62B8E8;
    BALBERRA_ID(&tasks[0]) = 0x78;
    BALBERRA_S32(&state, 0x60) = 1000;
    BALBERRA_S32(&state, 0x68) = 1000;
    encounter = 3; ++frame; config = 0; behavior = NORMAL;
    motion_calls = collision_calls = messages = 0;
    D_8016DAB4_16E6B4 = &tasks[0];
    extra_options_track_hyper_balberra(&tasks[0]);
    /* Fixture begins after the first native combat state was entered. */
    s_balberra_combat_started = 1;
}
static void tick(Fixture *task)
{ D_8016DAB4_16E6B4 = task; task->ai(task, task->object); }

static void test_all_clocks(void)
{
#define TEST_CALLBACK(name, kind) do { \
    reset(); \
    Fixture *task = kind == BALBERRA_ROOT ? &tasks[0] : &tasks[1]; \
    task->ai = name; tick(task); \
    CHECK(task->calls == 4); CHECK(task->position == 4.0f); \
    CHECK(task->animation == 4.0f); CHECK(task->shots == 1); \
    CHECK(D_8016DAB4_16E6B4 == task); \
    if (kind == BALBERRA_ROOT || kind == BALBERRA_PART) \
        CHECK(BALBERRA_S32(task, 0xB0) == 39); \
} while (0);
    BALBERRA_CALLBACKS(TEST_CALLBACK)
#undef TEST_CALLBACK
}

static void test_lifecycle(void)
{
    static const unsigned int stops[] = { EXCLUDED, KILL_ROOT, KILL_PLAYER, PAUSE,
        REPLACE_OBJECT, REPLACE_STATE, REPLACE_CONTEXT, RETIRE, NEW_EPOCH,
        DISABLE, CHANGE_ID };
    for (unsigned int i = 0; i < sizeof(stops) / sizeof(stops[0]); ++i) {
        reset(); behavior = stops[i]; tick(&tasks[0]);
        CHECK(tasks[0].calls == 2);
        CHECK(D_8016DAB4_16E6B4 ==
            (stops[i] == REPLACE_CONTEXT || stops[i] == RETIRE ? &tasks[3] : &tasks[0]));
    }
    reset(); behavior = TRANSITION; tick(&tasks[0]); CHECK(tasks[0].calls == 4);
    reset(); config = 1; tick(&tasks[0]); CHECK(tasks[0].calls == 1);
    reset(); encounter = 4; tick(&tasks[0]); CHECK(tasks[0].calls == 1);
    reset(); BALBERRA_ID(&tasks[0]) = 0x64; tick(&tasks[0]); CHECK(tasks[0].calls == 1);
    reset(); BALBERRA_S32(&tasks[0], 0xAC) = 0; tick(&tasks[0]); CHECK(tasks[0].calls == 1);
    reset(); tick(&tasks[0]); tick(&tasks[0]); CHECK(tasks[0].calls == 5);
    ++frame; tick(&tasks[0]); CHECK(tasks[0].calls == 9);
    reset(); frame = 0xFFFF; tick(&tasks[0]); frame = 0; tick(&tasks[0]); CHECK(tasks[0].calls == 8);
    reset(); behavior = NATIVE_RETIRE;
    tasks[1].ai = func_80204C5C_63003C; tick(&tasks[1]);
    CHECK(tasks[1].calls == 1); CHECK(D_8016DAB4_16E6B4 == &tasks[3]);
    reset(); s_balberra_combat_started = 0;
    tasks[0].ai = excluded; tasks[1].ai = func_80205614_6309F4; tick(&tasks[1]);
    CHECK(tasks[1].calls == 1); /* Intro is not accelerated. */
    reset(); tasks[0].ai = excluded; tasks[1].ai = func_80205614_6309F4;
    tick(&tasks[1]); CHECK(tasks[1].calls == 4); /* Combat hit reaction. */
}

static void test_native_damage_and_pulse(void)
{
    reset(); tasks[0].hit = &tasks[3]; tick(&tasks[0]);
    CHECK(tasks[0].damage == 1); CHECK(BALBERRA_S32(&tasks[0], 0xAC) == 999);
    reset(); tasks[1].ai = func_80202264_62D644;
    tasks[1].hit = &tasks[3]; tick(&tasks[1]);
    CHECK(tasks[1].damage == 1); CHECK(BALBERRA_S32(&tasks[1], 0xAC) == 999);
    reset(); behavior = DEAD_PART; tasks[1].ai = func_80202264_62D644; tick(&tasks[1]);
    CHECK(tasks[1].calls == 2);
    reset(); behavior = PULSE; tick(&tasks[0]);
    CHECK(tasks[0].calls == 4); CHECK(scratch[7] == 1);
    /* Native un-replayed tube dispatches see this once and each emit once. */
    unsigned int rockets = 0;
    for (unsigned int i = 0; i < 6; ++i) if (scratch[7]) ++rockets;
    CHECK(rockets == 6);
    ++frame; tick(&tasks[0]); CHECK(scratch[7] == 0);
}

static void test_shots(void)
{
#define TEST_SHOT(name) do { \
    reset(); tasks[1].ai = name; tick(&tasks[1]); \
    CHECK(motion_calls == 4); CHECK(collision_calls == 1); \
    func_801D614C_60152C(&tasks[1]); CHECK(motion_calls == 5); \
    reset(); config = 1; tasks[1].ai = name; tick(&tasks[1]); \
    CHECK(motion_calls == 1); CHECK(collision_calls == 1); \
} while (0);
    BALBERRA_SHOTS(TEST_SHOT)
#undef TEST_SHOT
    reset(); tasks[1].ai = func_8020396C_62ED4C;
    BALBERRA_S32(&tasks[1], 0x7C) = 2; tick(&tasks[1]);
    CHECK(BALBERRA_S32(&tasks[1], 0x7C) == -1);
    reset(); tasks[1].ai = func_80203A54_62EE34;
    *(float *)(tasks[1].data + 0x78) = -1.0f;
    BALBERRA_S32(&tasks[1], 0x7C) = 2; tick(&tasks[1]);
    CHECK(BALBERRA_S32(&tasks[1], 0x7C) == 0);
}

int main(void)
{
    test_all_clocks(); test_lifecycle(); test_native_damage_and_pulse(); test_shots();
    puts("Balberra Hyper host regressions passed.");
    return 0;
}
