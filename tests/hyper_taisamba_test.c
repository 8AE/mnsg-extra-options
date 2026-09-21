/* Production replay/guard regressions with a 64-bit model of native tasks.
 * Run: xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *   tests/hyper_taisamba_test.c -o /tmp/hyper_taisamba_test
 * Native callbacks below model the verified per-tick state operations;
 * these tests are not an in-game scheduler/gameplay certification.
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
typedef void (*HostCallback)(void *, void *);
static HostCallback host_ai(void *task);
static void *s_host_root;
static void *s_host_model;
static unsigned short s_host_encounter;
static unsigned short s_host_frame;
static void excluded(void *task, void *object);
static void disabled(void *task, void *object);
unsigned long recomp_get_config_u32(const char *key);
int recomp_printf(const char *format, ...);
#define TAISAMBA_AI(task) host_ai(task)
#define TAISAMBA_ROOT(state) s_host_root
#define TAISAMBA_MODEL(state) s_host_model
#define TAISAMBA_ENCOUNTER s_host_encounter
#define TAISAMBA_WORLD_FRAME s_host_frame
#define TAISAMBA_CALLBACK_ENABLED(callback) ((callback) && (callback) != disabled)
#include "../src/hyper_impact_cadence.c"
#include "../src/hyper_taisamba.c"

/* Production keeps each Impact clock's phase for the whole session; the
 * fixtures share one process, so reset the clocks per case.  Each frame then
 * runs its native callback plus the frame's 1/2 extra replays (2.5x). */
static void reset_impact_clocks(void)
{
    memset(s_impact_clocks, 0, sizeof(s_impact_clocks));
}

static unsigned int root_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(TAISAMBA_CLOCK_ROOT);
}

static unsigned int child_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(TAISAMBA_CLOCK_CHILD);
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
typedef struct { _Alignas(max_align_t) unsigned char bytes[0x340]; } Buffer;
static Buffer s_tasks[4];
static Buffer s_objects[4];
static Buffer s_states[2];
static unsigned long s_config;
static unsigned int s_calls;
static unsigned int s_contacts;
static unsigned int s_emissions;
static unsigned int s_messages;
static unsigned int s_seen_clocks[4];
static int s_behavior;
static int s_contact_pending;
static void *s_expected_task;
static void *s_expected_object;
void *D_8020EED0_63A2B0;
void *D_8016DAB4_16E6B4;

enum { NORMAL, TRANSITION, EXCLUDED, DISABLED, REPLACE_OBJECT, REPLACE_ROOT,
       REPLACE_STATE, REINITIALIZE, KILL_BOSS, KILL_PLAYER, PAUSE, DISABLE_CONFIG,
       CHANGE_ENCOUNTER, REENTER, DELETE_TASK, HIT_EFFECT };

static HostCallback host_ai(void *task)
{
    HostCallback result;
    memcpy(&result, (char *)task + 0x300, sizeof(result));
    return result;
}
static void set_ai(void *task, HostCallback callback)
{
    memcpy((char *)task + 0x300, &callback, sizeof(callback));
}
static void set_object(void *task, void *object)
{
    memcpy((char *)task + 0x18, &object, sizeof(object));
}
static void excluded(void *task, void *object) { (void)task; (void)object; }
static void disabled(void *task, void *object) { (void)task; (void)object; }

unsigned long recomp_get_config_u32(const char *key)
{
    CHECK(strcmp(key, "hyper_enemies") == 0);
    return s_config;
}
int recomp_printf(const char *format, ...)
{
    CHECK(strstr(format, "Taisamba 2 Hyper") != 0);
    s_messages++;
    return 0;
}

static void native_tick(void *task, void *object, HostCallback callback)
{
    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(task == s_expected_task && object == s_expected_object);
    if (s_calls < 4)
        s_seen_clocks[s_calls] = TAISAMBA_CLOCK(D_8020EED0_63A2B0);
    s_calls++;
    *(float *)((char *)object + 8) += 1.0f;
    *(float *)((char *)object + 0x28) += 1.0f;
    if ((s_calls & 3) == 0)
        s_emissions++;
    if (callback == func_801F1788_61CB68)
        TAISAMBA_ARENA_RISE(D_8020EED0_63A2B0) = 1.0f;
    if (s_contact_pending)
    {
        s_contact_pending = 0;
        s_contacts++;
        if (s_behavior == HIT_EFFECT)
        {
            set_ai(task, excluded);
            set_object(task, s_objects[2].bytes);
        }
    }
    if (s_calls != 2)
        return;
    switch (s_behavior)
    {
    case TRANSITION:
        set_ai(task, taisamba_root_callback(callback) ?
            func_801F3A68_61EE48 : func_801F9464_624844);
        break;
    case EXCLUDED: set_ai(task, excluded); break;
    case DISABLED: set_ai(task, disabled); break;
    case REPLACE_OBJECT: set_object(task, s_objects[2].bytes); break;
    case REPLACE_ROOT: s_host_root = s_tasks[2].bytes; break;
    case REPLACE_STATE: D_8020EED0_63A2B0 = s_states[1].bytes; break;
    case REINITIALIZE: extra_options_track_hyper_taisamba(s_tasks[0].bytes); break;
    case KILL_BOSS: TAISAMBA_HP(D_8020EED0_63A2B0) = 0; break;
    case KILL_PLAYER: TAISAMBA_PLAYER_HP(D_8020EED0_63A2B0) = 0; break;
    case PAUSE: TAISAMBA_PAUSED(D_8020EED0_63A2B0) = 1; break;
    case DISABLE_CONFIG: s_config = 1; break;
    case CHANGE_ENCOUNTER: s_host_encounter = 3; break;
    case REENTER: extra_options_run_hyper_taisamba_tick(); break;
    case DELETE_TASK:
        set_ai(task, excluded);
        D_8016DAB4_16E6B4 = s_tasks[3].bytes;
        break;
    default: break;
    }
}

#define ROOT_STUB(name) void name(void *task, void *object) \
    { native_tick(task, object, name); \
      if (name == func_801F1788_61CB68) extra_options_hyper_taisamba_ascent_step(); }
TAISAMBA_ROOT_CALLBACKS(ROOT_STUB)
#undef ROOT_STUB
#define CHILD_STUB(name) void name(void *task, void *object) \
    { taisamba_capture_child(task, object, name); \
      native_tick(task, object, name); taisamba_run_child(); }
TAISAMBA_CHILD_CALLBACKS(CHILD_STUB)
#undef CHILD_STUB

static void fixture(void)
{
    reset_impact_clocks();
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    memset(s_states, 0, sizeof(s_states));
    memset(s_seen_clocks, 0, sizeof(s_seen_clocks));
    s_host_root = s_tasks[0].bytes;
    s_host_model = s_objects[0].bytes;
    D_8020EED0_63A2B0 = s_states[0].bytes;
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    s_host_encounter = 2;
    s_host_frame = 20;
    s_calls = s_contacts = s_emissions = s_messages = 0;
    s_contact_pending = 0;
    s_config = 0;
    s_behavior = NORMAL;
    s_expected_task = s_tasks[0].bytes;
    s_expected_object = s_objects[0].bytes;
    set_object(s_tasks[0].bytes, s_objects[0].bytes);
    set_object(s_tasks[1].bytes, s_objects[1].bytes);
    set_ai(s_tasks[0].bytes, func_801F03C4_61B7A4);
    set_ai(s_tasks[1].bytes, func_801F833C_62371C);
    TAISAMBA_ID(s_tasks[0].bytes) = 0x5A;
    TAISAMBA_HP(D_8020EED0_63A2B0) = 2000;
    TAISAMBA_PLAYER_HP(D_8020EED0_63A2B0) = 500;
    TAISAMBA_CLOCK(D_8020EED0_63A2B0) = 100;
    extra_options_track_hyper_taisamba(s_tasks[0].bytes);
    CHECK(taisamba_live());
}

static void run_root_frame(void)
{
    D_8016DAB4_16E6B4 = s_tasks[0].bytes;
    host_ai(s_tasks[0].bytes)(s_tasks[0].bytes, s_objects[0].bytes);
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    extra_options_run_hyper_taisamba_tick();
}

/* A Hyper frame runs the native callback once plus the frame's 1/2 extra
 * replays (2.5x).  Every tick advances both tracked floats by 1.0, so the
 * expected call count fully determines the pose. */
static void check_frame_ticks(unsigned int expected_calls)
{
    CHECK(s_calls == expected_calls);
    CHECK(*(float *)((char *)s_expected_object + 8) ==
          (float)expected_calls);
    CHECK(*(float *)((char *)s_expected_object + 0x28) ==
          (float)expected_calls);
    /* F76D0 emits on every fourth tick; one Hyper frame stays below four. */
    CHECK(s_emissions == expected_calls / 4u);
    CHECK(TAISAMBA_HP(s_states[0].bytes) == 2000);
    CHECK(TAISAMBA_PLAYER_HP(s_states[0].bytes) == 500);
}

static void test_all_callbacks(void)
{
#define TEST_ROOT(name) \
    fixture(); set_ai(s_tasks[0].bytes, name); run_root_frame(); \
    check_frame_ticks(1u + root_extra_ticks()); CHECK(s_messages == 1); \
    CHECK(D_8016DAB4_16E6B4 == s_tasks[3].bytes);
    TAISAMBA_ROOT_CALLBACKS(TEST_ROOT)
#undef TEST_ROOT
#define TEST_CHILD(name) \
    fixture(); s_expected_task = s_tasks[1].bytes; \
    s_expected_object = s_objects[1].bytes; set_ai(s_tasks[1].bytes, name); \
    D_8016DAB4_16E6B4 = s_tasks[1].bytes; name(s_tasks[1].bytes, s_objects[1].bytes); \
    check_frame_ticks(1u + child_extra_ticks()); \
    CHECK(D_8016DAB4_16E6B4 == s_tasks[1].bytes);
    TAISAMBA_CHILD_CALLBACKS(TEST_CHILD)
#undef TEST_CHILD
}

static void test_root_lifecycle(void)
{
    int behavior;
    fixture();
    run_root_frame();
    extra_options_run_hyper_taisamba_tick();
    /* One native call plus the frame's 1/2 replays; replays see the next
     * battle-clock ticks after the native call's base (100 -> 101 -> ...). */
    CHECK(s_calls == 1u + root_extra_ticks());
    CHECK(s_seen_clocks[0] == 100 && s_seen_clocks[1] == 101);
    CHECK(TAISAMBA_CLOCK(s_states[0].bytes) == 100);
    {
        unsigned int first_frame = 1u + root_extra_ticks();
        s_host_frame++;
        run_root_frame();
        CHECK(s_calls == first_frame + 1u + root_extra_ticks());
    }
    CHECK(s_messages == 1);
    fixture(); s_behavior = TRANSITION; run_root_frame();
    check_frame_ticks(1u + root_extra_ticks());
    fixture(); s_behavior = REENTER; run_root_frame();
    check_frame_ticks(1u + root_extra_ticks());
    for (behavior = EXCLUDED; behavior <= CHANGE_ENCOUNTER; behavior++)
    {
        fixture(); s_behavior = behavior; run_root_frame();
        CHECK(s_calls == 2);
        CHECK(D_8016DAB4_16E6B4 == s_tasks[3].bytes);
    }
    fixture(); s_behavior = DELETE_TASK; run_root_frame(); CHECK(s_calls == 2);
    fixture(); s_config = 1; run_root_frame(); CHECK(s_calls == 1);
    fixture(); s_host_encounter = 1; run_root_frame(); CHECK(s_calls == 1);
    fixture(); s_host_encounter = 3; run_root_frame(); CHECK(s_calls == 1);
    fixture(); TAISAMBA_PAUSED(s_states[0].bytes) = 1;
    run_root_frame(); CHECK(s_calls == 1);
    fixture(); set_ai(s_tasks[0].bytes, excluded);
    extra_options_run_hyper_taisamba_tick(); CHECK(s_calls == 0);
    fixture(); TAISAMBA_ID(s_tasks[0].bytes) = 0;
    extra_options_run_hyper_taisamba_tick(); CHECK(s_calls == 0);
    fixture(); s_host_model = 0;
    extra_options_run_hyper_taisamba_tick(); CHECK(s_calls == 0);
    fixture(); s_host_frame = 0xFFFF; run_root_frame();
    {
        unsigned int first_frame = 1u + root_extra_ticks();
        s_host_frame = 0; run_root_frame();
        CHECK(s_calls == first_frame + 1u + root_extra_ticks());
    }
}

static void test_ascent_clock(void)
{
    /* The ascent hook counts requests per frame, capped at four.  A Hyper
     * frame therefore reaches min(1 + budget, 4) requests, not a fixed 4. */
    fixture(); set_ai(s_tasks[0].bytes, func_801F1788_61CB68);
    run_root_frame();
    {
        unsigned int requests = 1u + root_extra_ticks();
        CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) ==
              (float)(requests < 4u ? requests : 4u));
    }
    fixture(); set_ai(s_tasks[0].bytes, func_801F1788_61CB68);
    TAISAMBA_ARENA_HEIGHT(s_states[0].bytes) = 398.0f;
    run_root_frame();
    CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) == 2.0f); /* clamped to remaining */
    fixture(); set_ai(s_tasks[0].bytes, func_801F1788_61CB68);
    TAISAMBA_ARENA_HEIGHT(s_states[0].bytes) = 400.0f;
    run_root_frame(); CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) == 0.0f);
    fixture(); set_ai(s_tasks[0].bytes, func_801F1788_61CB68);
    extra_options_run_hyper_taisamba_tick();
    CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) ==
          (float)root_extra_ticks());
    fixture(); TAISAMBA_ARENA_RISE(s_states[0].bytes) = 2.0f;
    run_root_frame(); CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) == 2.0f);
    /* Native sibling ordering runs the damage-return replay BEFORE root.
     * Its replay frame's requests must not overwrite +17C back to one. */
    fixture(); set_ai(s_tasks[0].bytes, func_801F1788_61CB68);
    extra_options_run_hyper_taisamba_tick();
    CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) ==
          (float)root_extra_ticks());
    D_8016DAB4_16E6B4 = s_tasks[0].bytes;
    func_801F1788_61CB68(s_tasks[0].bytes, s_objects[0].bytes);
    {
        unsigned int requests = 1u + root_extra_ticks();
        CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) ==
              (float)(requests < 4u ? requests : 4u));
    }
    CHECK(s_calls == root_extra_ticks() + 1u);
    CHECK(s_seen_clocks[0] == 101 && s_seen_clocks[1] == 100);
    s_host_frame++;
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    extra_options_run_hyper_taisamba_tick();
    CHECK(TAISAMBA_ARENA_RISE(s_states[0].bytes) ==
          (float)root_extra_ticks());
}

static void run_child_frame(int behavior)
{
    fixture();
    s_behavior = behavior;
    s_expected_task = s_tasks[1].bytes;
    s_expected_object = s_objects[1].bytes;
    D_8016DAB4_16E6B4 = s_tasks[1].bytes;
    s_contact_pending = 1;
    func_801F833C_62371C(s_tasks[1].bytes, s_objects[1].bytes);
}

static void test_child_lifecycle(void)
{
    int behavior;
    run_child_frame(NORMAL);
    check_frame_ticks(1u + child_extra_ticks()); CHECK(s_contacts == 1);
    run_child_frame(TRANSITION);
    check_frame_ticks(1u + child_extra_ticks()); CHECK(s_contacts == 1);
    run_child_frame(HIT_EFFECT); CHECK(s_calls == 1 && s_contacts == 1);
    CHECK(host_ai(s_tasks[1].bytes) == excluded);
    for (behavior = EXCLUDED; behavior <= CHANGE_ENCOUNTER; behavior++)
    {
        run_child_frame(behavior);
        CHECK(s_calls == 2 && s_contacts == 1);
    }
    run_child_frame(DELETE_TASK);
    CHECK(s_calls == 2);
    CHECK(D_8016DAB4_16E6B4 == s_tasks[3].bytes);
    /* Original native callback can delete before its return hook too. */
    fixture();
    D_8016DAB4_16E6B4 = s_tasks[1].bytes;
    taisamba_capture_child(s_tasks[1].bytes, s_objects[1].bytes, func_801F833C_62371C);
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    taisamba_run_child(); CHECK(s_calls == 0);
    fixture(); s_config = 1;
    D_8016DAB4_16E6B4 = s_tasks[1].bytes;
    taisamba_capture_child(s_tasks[1].bytes, s_objects[1].bytes, func_801F833C_62371C);
    taisamba_run_child(); CHECK(s_calls == 0);
}

int main(void)
{
    test_all_callbacks();
    test_root_lifecycle();
    test_ascent_clock();
    test_child_lifecycle();
    puts("hyper_taisamba_test: passed");
    return 0;
}
