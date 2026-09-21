/*
 * Focused host regressions for Kashiwagi's dedicated Impact-battle replay.
 * These tests exercise the production source directly; they do not certify
 * native scheduler ordering or in-game gameplay.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     tests/hyper_kashiwagi_test.c -o /tmp/hyper_kashiwagi_test
 *   /tmp/hyper_kashiwagi_test
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_kashiwagi_test.c -o /tmp/hyper_kashiwagi_test_ubsan
 *   /tmp/hyper_kashiwagi_test_ubsan
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Suppress target-only imports and hook declarations. */
#define __MODDING_H__
#define __RECOMPCONFIG_H__
#define __RECOMPUTILS_H__
#define EXTRA_OPTIONS_H
#define RECOMP_HOOK(function)
#define RECOMP_HOOK_RETURN(function)
#define RECOMP_PATCH

int extra_options_save_is_loaded(void);
unsigned long recomp_get_config_u32(const char *key);
int recomp_printf(const char *format, ...);

typedef void (*TestTaskCallback)(void *task, void *object);
static TestTaskCallback test_task_ai(void *task);
static int test_callback_is_enabled(TestTaskCallback callback);
static void *test_state_root(void *state);
static void *test_state_model(void *state);
static void *test_charge_link(void *task);
static unsigned short s_encounter;
static unsigned short s_world_frame;

#define KASHIWAGI_TASK_AI(task) test_task_ai(task)
#define KASHIWAGI_CALLBACK_IS_ENABLED(callback) \
    test_callback_is_enabled((TestTaskCallback)(callback))
#define KASHIWAGI_ENCOUNTER s_encounter
#define KASHIWAGI_WORLD_FRAME s_world_frame
#define KASHIWAGI_STATE_ROOT(state) test_state_root(state)
#define KASHIWAGI_STATE_MODEL(state) test_state_model(state)
/* Native stores this pointer at +0x90. Keep it out of the byte fixture on a
 * 64-bit host because an eight-byte pointer would overlap native +0x94. */
#define KASHIWAGI_CHARGE_LINK(task) test_charge_link(task)

#include "../src/hyper_impact_cadence.c"
#include "../src/hyper_kashiwagi.c"

/* Production keeps each Impact clock's phase across the whole session.  The
 * fixtures reuse one process for every case, so reset the shared clocks here
 * to make each case start from the low (one extra tick) half.  The expected
 * per-frame budget is then read straight from the production clock the source
 * under test just advanced, so the assertions cannot drift from production. */
static void reset_impact_clocks(void)
{
    memset(s_impact_clocks, 0, sizeof(s_impact_clocks));
}

static unsigned int root_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_ROOT);
}

static unsigned int motion_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_SHOT_MOTION);
}

static unsigned int clone_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_CLONE);
}

static unsigned int charge_extra_ticks(void)
{
    return extra_options_hyper_impact_extra_ticks(KASHIWAGI_CLOCK_CHARGE);
}

#define TEST_KASHIWAGI_ENCOUNTER_INDEX 1u
#define TEST_KASHIWAGI_ROOT_TASK_ID 0x50u
#define TEST_KASHIWAGI_TASK_ID(task) \
    (*(volatile unsigned short *)((unsigned char *)(task) + 0x5C))

#define CHECK(condition)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(condition))                                                   \
        {                                                                   \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                         \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

#define HOST_AI_OFFSET 0x300u
#define TASK_BUFFER_SIZE 0x320u
#define STATE_BUFFER_SIZE 0x300u

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[TASK_BUFFER_SIZE];
} TestTaskBuffer;

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[STATE_BUFFER_SIZE];
} TestStateBuffer;

enum
{
    AI_NORMAL,
    AI_TRANSITION_COMBAT,
    AI_TRANSITION_EXCLUDED,
    AI_REPLACE_CURRENT_TASK,
    AI_REPLACE_STATE,
    AI_REPLACE_ROOT,
    AI_REPLACE_OBJECT,
    AI_KILL_BOSS,
    AI_KILL_PLAYER,
    AI_PAUSE_COMBAT,
    AI_CHANGE_ENCOUNTER,
    AI_REENTER_RETURN_HOOK
};

enum
{
    CLONE_AI_NORMAL,
    CLONE_AI_TRANSITION_COMBAT,
    CLONE_AI_CLEAR_HIT_AND_REACT,
    CLONE_AI_END_ROOT_ATTACK,
    CLONE_AI_REPLACE_OBJECT,
    CLONE_AI_REPLACE_TASK_ID,
    CLONE_AI_REPLACE_CONTEXT,
    CLONE_AI_CHANGE_EPOCH,
    CLONE_AI_REENTER_RETURN_HOOK
};

enum
{
    CHARGE_AI_NORMAL,
    CHARGE_AI_END_ROOT_ATTACK,
    CHARGE_AI_REPLACE_CURRENT,
    CHARGE_AI_REPLACE_OBJECT,
    CHARGE_AI_CHANGE_EPOCH,
    CHARGE_AI_BREAK_ROOT_LINK,
    CHARGE_AI_BREAK_CHILD_LINK,
    CHARGE_AI_DISABLE_CONFIG,
    CHARGE_AI_DELETE_PROXY
};

void *D_8020EED0_63A2B0;
void *D_8016DAB4_16E6B4;

static TestTaskBuffer s_tasks[4];
static TestTaskBuffer s_objects[4];
static TestStateBuffer s_states[2];
static void *s_state_root;
static void *s_state_model;
static unsigned long s_config;
static unsigned int s_ai_behavior;
static unsigned int s_ai_calls;
static unsigned int s_save_gate_calls;
static unsigned int s_replay_messages;
static unsigned int s_fixture_number;
static unsigned int s_projectile_ai_calls;
static unsigned int s_projectile_collision_calls;
static unsigned int s_motion_calls;
static unsigned char s_root_invokes_motion;
static unsigned int s_clone_ai_behavior;
static unsigned int s_clone_ai_calls;
static void *s_expected_clone;
static void *s_expected_clone_object;
static void *s_charge_links[4];
static unsigned int s_charge_ai_behavior;
static unsigned int s_charge_ai_calls;
static unsigned int s_charge_contact_emits;
static unsigned int s_charge_trail_emits;
static unsigned int s_charge_delete_calls;
static TestTaskCallback s_transition_callback;
static char s_last_message[256];

static void excluded_callback(void *task, void *object);
static void disabled_callback(void *task, void *object);

static void set_pointer(void *base, size_t offset, void *value)
{
    memcpy((unsigned char *)base + offset, &value, sizeof(value));
}

static void *get_pointer(void *base, size_t offset)
{
    void *value;

    memcpy(&value, (unsigned char *)base + offset, sizeof(value));
    return value;
}

static void *test_charge_link(void *task)
{
    unsigned int index;

    for (index = 0; index < sizeof(s_tasks) / sizeof(s_tasks[0]); index++)
    {
        if (task == s_tasks[index].bytes)
            return s_charge_links[index];
    }
    return 0;
}

static void set_callback(void *task, size_t offset,
                         TestTaskCallback callback)
{
    memcpy((unsigned char *)task + offset, &callback, sizeof(callback));
}

static TestTaskCallback get_callback(void *task, size_t offset)
{
    TestTaskCallback callback;

    memcpy(&callback, (unsigned char *)task + offset, sizeof(callback));
    return callback;
}

static void set_ai(void *task, TestTaskCallback callback)
{
    set_callback(task, HOST_AI_OFFSET, callback);
}

static TestTaskCallback test_task_ai(void *task)
{
    return get_callback(task, HOST_AI_OFFSET);
}

static int test_callback_is_enabled(TestTaskCallback callback)
{
    return callback && callback != disabled_callback;
}

static void *test_state_root(void *state)
{
    CHECK(state == D_8020EED0_63A2B0);
    return s_state_root;
}

static void *test_state_model(void *state)
{
    CHECK(state == D_8020EED0_63A2B0);
    return s_state_model;
}

#define CALLBACK_ENTRY(name) name,
static const HyperKashiwagiCallback s_combat_callbacks[] = {
    KASHIWAGI_ROOT_CALLBACKS(CALLBACK_ENTRY)
};
static const HyperKashiwagiCallback s_clone_callbacks[] = {
    KASHIWAGI_CLONE_CALLBACKS(CALLBACK_ENTRY)
};
#undef CALLBACK_ENTRY

static void initialize_task(unsigned int index)
{
    void *task;

    CHECK(index < sizeof(s_tasks) / sizeof(s_tasks[0]));
    task = s_tasks[index].bytes;
    memset(task, 0, sizeof(s_tasks[index].bytes));
    memset(s_objects[index].bytes, 0, sizeof(s_objects[index].bytes));
}

static void set_state_s32(size_t offset, int value)
{
    memcpy((unsigned char *)D_8020EED0_63A2B0 + offset,
           &value, sizeof(value));
}

static void set_state_u8(size_t offset, unsigned char value)
{
    *((unsigned char *)D_8020EED0_63A2B0 + offset) = value;
}

static void reset_fixture(void)
{
    void *root;

    reset_impact_clocks();
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    memset(s_states, 0, sizeof(s_states));
    s_fixture_number++;
    D_8020EED0_63A2B0 = s_states[s_fixture_number & 1u].bytes;
    D_8016DAB4_16E6B4 = 0;
    s_encounter = TEST_KASHIWAGI_ENCOUNTER_INDEX;
    s_world_frame = (unsigned short)(s_fixture_number * 10u);
    s_config = 0; /* Enabled is the first mod.toml enum entry. */
    s_ai_behavior = AI_NORMAL;
    s_ai_calls = 0;
    s_save_gate_calls = 0;
    s_replay_messages = 0;
    s_projectile_ai_calls = 0;
    s_projectile_collision_calls = 0;
    s_motion_calls = 0;
    s_root_invokes_motion = 0;
    s_clone_ai_behavior = CLONE_AI_NORMAL;
    s_clone_ai_calls = 0;
    s_expected_clone = 0;
    s_expected_clone_object = 0;
    memset(s_charge_links, 0, sizeof(s_charge_links));
    s_charge_ai_behavior = CHARGE_AI_NORMAL;
    s_charge_ai_calls = 0;
    s_charge_contact_emits = 0;
    s_charge_trail_emits = 0;
    s_charge_delete_calls = 0;
    s_transition_callback = 0;
    s_last_message[0] = '\0';

    initialize_task(0);
    initialize_task(1);
    initialize_task(2);
    initialize_task(3);
    root = s_tasks[0].bytes;
    s_state_root = root;
    s_state_model = s_tasks[1].bytes;
    set_state_s32(0x60, 2000);
    set_state_s32(0x68, 999);
    set_state_u8(0x2C0, 0);

    /* The native hook enters before 4800 has created/bound the model object.
     * The return-side liveness check must therefore bind root+0x18 lazily. */
    extra_options_track_hyper_kashiwagi(root);
    set_pointer(root, 0x18, s_objects[0].bytes);
    TEST_KASHIWAGI_TASK_ID(root) = TEST_KASHIWAGI_ROOT_TASK_ID;
    set_ai(root, s_combat_callbacks[0]);
}

static void run_replay(void)
{
    extra_options_run_hyper_kashiwagi_tick();
}

int extra_options_save_is_loaded(void)
{
    s_save_gate_calls++;
    return 0;
}

unsigned long recomp_get_config_u32(const char *key)
{
    CHECK(strcmp(key, "hyper_enemies") == 0);
    return s_config;
}

int recomp_printf(const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsnprintf(s_last_message, sizeof(s_last_message),
                       format, arguments);
    va_end(arguments);
    if (strstr(s_last_message, "Kashiwagi Hyper:"))
        s_replay_messages++;
    return result;
}

static void run_root_ai(void *task, void *object)
{
    CHECK(task == s_tasks[0].bytes);
    CHECK(object == s_objects[0].bytes);
    CHECK(D_8016DAB4_16E6B4 == task);
    s_ai_calls++;

    /* Root combat callbacks also use 614C. Projectile scoping must not turn
     * each already-replayed root tick into another four movement steps. */
    if (s_root_invokes_motion)
        func_801D614C_60152C(task);

    switch (s_ai_behavior)
    {
    case AI_TRANSITION_COMBAT:
        if (s_ai_calls == 1)
            set_ai(task, s_transition_callback);
        break;
    case AI_TRANSITION_EXCLUDED:
        if (s_ai_calls == 1)
            set_ai(task, excluded_callback);
        break;
    case AI_REPLACE_CURRENT_TASK:
        D_8016DAB4_16E6B4 = s_tasks[3].bytes;
        break;
    case AI_REPLACE_STATE:
        D_8020EED0_63A2B0 = s_states[(s_fixture_number + 1u) & 1u].bytes;
        break;
    case AI_REPLACE_ROOT:
        s_state_root = s_tasks[2].bytes;
        break;
    case AI_REPLACE_OBJECT:
        set_pointer(task, 0x18, s_objects[2].bytes);
        break;
    case AI_KILL_BOSS:
        set_state_s32(0x60, 0);
        break;
    case AI_KILL_PLAYER:
        set_state_s32(0x68, 0);
        break;
    case AI_PAUSE_COMBAT:
        set_state_u8(0x2C0, 1);
        break;
    case AI_CHANGE_ENCOUNTER:
        s_encounter = TEST_KASHIWAGI_ENCOUNTER_INDEX + 1u;
        break;
    case AI_REENTER_RETURN_HOOK:
        extra_options_run_hyper_kashiwagi_tick();
        break;
    default:
        break;
    }
}

#define DEFINE_COMBAT_CALLBACK(name)                                       \
    void name(void *task, void *object)                                     \
    {                                                                       \
        run_root_ai(task, object);                                          \
    }
KASHIWAGI_ROOT_CALLBACKS(DEFINE_COMBAT_CALLBACK)
#undef DEFINE_COMBAT_CALLBACK

#define TEST_TASK_VELOCITY_X(task) \
    (*(float *)((unsigned char *)(task) + 0x70))
#define TEST_TASK_VELOCITY_Y(task) \
    (*(float *)((unsigned char *)(task) + 0x74))
#define TEST_OBJECT_POSITION_X(object) \
    (*(float *)((unsigned char *)(object) + 0x08))
#define TEST_OBJECT_POSITION_Y(object) \
    (*(float *)((unsigned char *)(object) + 0x0C))
#define TEST_OBJECT_POSITION_Z(object) \
    (*(float *)((unsigned char *)(object) + 0x10))

/* Simulate the hook nesting around the movement-only native helper. The
 * three mod-issued calls re-enter this stub; the production motion guard
 * makes each nested hook a no-op before the one native position update. */
void func_801D614C_60152C(void *task)
{
    void *object;

    extra_options_hyper_kashiwagi_shot_motion(task);
    s_motion_calls++;
    object = KASHIWAGI_TASK_OBJECT(task);
    if (!object)
        return;
    TEST_OBJECT_POSITION_X(object) += TEST_TASK_VELOCITY_X(task);
    TEST_OBJECT_POSITION_Y(object) += TEST_TASK_VELOCITY_Y(task);
    TEST_OBJECT_POSITION_Z(object) += KASHIWAGI_SHOT_VELOCITY_Z(task);
}

static void run_native_projectile_body(void *task)
{
    s_projectile_ai_calls++;
    /* Represents the collision/damage work surrounding 614C in the native
     * callback. It must remain one-pass while movement reaches four passes. */
    s_projectile_collision_calls++;
    func_801D614C_60152C(task);
}

void func_801EA3F0_6157D0(void *task, void *object)
{
    signed int old_timer;

    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    run_native_projectile_body(task);
    old_timer = KASHIWAGI_SHOT_TIMER(task);
    KASHIWAGI_SHOT_TIMER(task) = old_timer - 1;
    extra_options_hyper_kashiwagi_aiming_shot_done();
}

void func_801EA534_615914(void *task, void *object)
{
    extra_options_hyper_kashiwagi_travelling_shot(task, object);
    run_native_projectile_body(task);
    KASHIWAGI_SHOT_ROLL(task) += 10u;
    if (KASHIWAGI_SHOT_VELOCITY_Z(task) < 0.0f)
        KASHIWAGI_SHOT_TIMER(task)--;
    extra_options_hyper_kashiwagi_travelling_shot_done();
}

void func_801EA900_615CE0(void *task, void *object)
{
    extra_options_hyper_kashiwagi_volley_shot(task, object);
    run_native_projectile_body(task);
    KASHIWAGI_OBJECT_YAW(object) += 3u;
    extra_options_hyper_kashiwagi_volley_shot_done();
}

#define TEST_CHARGE_ACTIVE(task) \
    (*(unsigned char *)((unsigned char *)(task) + 0x30))
#define TEST_CHARGE_TIMER(task) \
    (*(signed int *)((unsigned char *)(task) + 0x7C))
#define TEST_CHARGE_FADE(task) \
    (*(signed int *)((unsigned char *)(task) + 0x94))
#define TEST_CHARGE_LEVEL(task) \
    (*(signed int *)((unsigned char *)(task) + 0x98))

/* Model only the AC4C behavior relevant to safe acceleration. Hook entry and
 * return calls deliberately surround the native body so recursive mod-issued
 * ticks exercise the same nesting as the recompiled runtime. */
void func_801EAC4C_61602C(void *task, void *object)
{
    int outer_call = !s_kashiwagi_charge_guard;
    signed int value;

    extra_options_capture_hyper_kashiwagi_charge(task, object);
    s_charge_ai_calls++;
    if (KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) != 2)
    {
        s_charge_delete_calls++;
        extra_options_run_hyper_kashiwagi_charge_tick();
        return;
    }

    value = TEST_CHARGE_FADE(task) + 5;
    TEST_CHARGE_FADE(task) = value > 180 ? 180 : value;
    value = TEST_CHARGE_LEVEL(task) + 6;
    TEST_CHARGE_LEVEL(task) = value > 255 ? 255 : value;
    if (TEST_CHARGE_LEVEL(task) == 255 && TEST_CHARGE_ACTIVE(task))
    {
        TEST_CHARGE_TIMER(task)--;
        if (TEST_CHARGE_TIMER(task) == 0)
        {
            TEST_CHARGE_TIMER(task) = 16;
            s_charge_trail_emits++;
        }
    }

    /* Native 801D36C4 consumes +0x34 after the first contact pass. */
    if (get_pointer(task, 0x34))
    {
        s_charge_contact_emits++;
        set_pointer(task, 0x34, 0);
    }

    if (outer_call)
    {
        switch (s_charge_ai_behavior)
        {
        case CHARGE_AI_END_ROOT_ATTACK:
            KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 0;
            break;
        case CHARGE_AI_REPLACE_CURRENT:
            D_8016DAB4_16E6B4 = s_tasks[3].bytes;
            break;
        case CHARGE_AI_REPLACE_OBJECT:
            set_pointer(task, 0x18, s_objects[3].bytes);
            break;
        case CHARGE_AI_CHANGE_EPOCH:
            extra_options_track_hyper_kashiwagi(s_tasks[0].bytes);
            break;
        case CHARGE_AI_BREAK_ROOT_LINK:
            s_charge_links[0] = s_tasks[3].bytes;
            break;
        case CHARGE_AI_BREAK_CHILD_LINK:
            s_charge_links[2] = s_objects[3].bytes;
            break;
        case CHARGE_AI_DISABLE_CONFIG:
            s_config = 1;
            break;
        case CHARGE_AI_DELETE_PROXY:
            KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 0;
            s_charge_links[0] = 0;
            s_charge_links[2] = 0;
            set_pointer(task, 0x18, 0);
            set_ai(task, 0);
            D_8016DAB4_16E6B4 = 0;
            break;
        default:
            break;
        }
    }

    extra_options_run_hyper_kashiwagi_charge_tick();
}

static void run_clone_ai(void *task, void *object)
{
    CHECK(task == s_expected_clone);
    CHECK(object == s_expected_clone_object);
    CHECK(D_8016DAB4_16E6B4 == task);
    s_clone_ai_calls++;

    switch (s_clone_ai_behavior)
    {
    case CLONE_AI_TRANSITION_COMBAT:
        if (s_clone_ai_calls == 1)
            set_ai(task, s_transition_callback);
        break;
    case CLONE_AI_CLEAR_HIT_AND_REACT:
        if (s_clone_ai_calls == 1)
        {
            set_pointer(task, 0x38, 0);
            set_ai(task, excluded_callback);
        }
        break;
    case CLONE_AI_END_ROOT_ATTACK:
        KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 0;
        break;
    case CLONE_AI_REPLACE_OBJECT:
        set_pointer(task, 0x18, s_objects[3].bytes);
        break;
    case CLONE_AI_REPLACE_TASK_ID:
        TEST_KASHIWAGI_TASK_ID(task) = 0x52u;
        break;
    case CLONE_AI_REPLACE_CONTEXT:
        D_8016DAB4_16E6B4 = s_tasks[0].bytes;
        break;
    case CLONE_AI_CHANGE_EPOCH:
        extra_options_track_hyper_kashiwagi(s_tasks[0].bytes);
        break;
    case CLONE_AI_REENTER_RETURN_HOOK:
        run_kashiwagi_clone_tick();
        break;
    default:
        break;
    }
}

#define DEFINE_CLONE_CALLBACK(name)                                        \
    void name(void *task, void *object)                                     \
    {                                                                       \
        run_clone_ai(task, object);                                         \
    }
KASHIWAGI_CLONE_CALLBACKS(DEFINE_CLONE_CALLBACK)
#undef DEFINE_CLONE_CALLBACK

static void excluded_callback(void *task, void *object)
{
    (void)task;
    (void)object;
}

static void disabled_callback(void *task, void *object)
{
    (void)task;
    (void)object;
}

static void test_exact_two_and_a_half_x_replay_without_save(void)
{
    void *saved_current;

    reset_fixture();
    saved_current = s_tasks[2].bytes;
    D_8016DAB4_16E6B4 = saved_current;
    run_replay();

    /* Impact Hyper alternates 1/2 extra ticks; the first frame after a clock
     * reset uses the low half, so a single replay is 2.5x's one extra tick. */
    CHECK(root_extra_ticks() == 1u);
    CHECK(s_ai_calls == root_extra_ticks());
    CHECK(D_8016DAB4_16E6B4 == saved_current);
    CHECK(s_save_gate_calls == 0);
    CHECK(s_replay_messages == 1);
}

static void test_all_combat_callbacks_are_admitted(void)
{
    unsigned int index;
    unsigned int expected = 0;

    reset_fixture();
    for (index = 0;
         index < sizeof(s_combat_callbacks) / sizeof(s_combat_callbacks[0]);
         index++)
    {
        set_ai(s_tasks[0].bytes, s_combat_callbacks[index]);
        s_world_frame++;
        run_replay();
        expected += root_extra_ticks();
        CHECK(s_ai_calls == expected);
    }

    set_ai(s_tasks[0].bytes, excluded_callback);
    s_world_frame++;
    run_replay();
    CHECK(s_ai_calls == expected);
}

static void test_callback_is_reloaded_between_ticks(void)
{
    reset_fixture();
    CHECK(sizeof(s_combat_callbacks) / sizeof(s_combat_callbacks[0]) > 1u);
    s_ai_behavior = AI_TRANSITION_COMBAT;
    s_transition_callback = s_combat_callbacks[1];
    run_replay();
    /* The first frame after a clock reset runs the low half: one replay.  The
     * reloaded callback is still a combat state, so the tick counts. */
    CHECK(root_extra_ticks() == 1u);
    CHECK(s_ai_calls == root_extra_ticks());
    CHECK(KASHIWAGI_TASK_AI(s_tasks[0].bytes) == s_combat_callbacks[1]);

    reset_fixture();
    s_ai_behavior = AI_TRANSITION_EXCLUDED;
    run_replay();
    /* The low-half frame is one replay; the excluded transition still
     * completes that single tick, so the frame reports as usual. */
    CHECK(s_ai_calls == 1u);
    CHECK(D_8016DAB4_16E6B4 == 0);
    CHECK(s_replay_messages == 1u);
}

static void test_disabled_and_null_callbacks_are_rejected(void)
{
    reset_fixture();
    set_ai(s_tasks[0].bytes, disabled_callback);
    run_replay();
    CHECK(s_ai_calls == 0);

    set_ai(s_tasks[0].bytes, 0);
    run_replay();
    CHECK(s_ai_calls == 0);

    /* A failed preflight must not consume the world's replay ticket. */
    set_ai(s_tasks[0].bytes, s_combat_callbacks[0]);
    run_replay();
    CHECK(s_ai_calls == root_extra_ticks());
}

static void test_lifecycle_gates(void)
{
    reset_fixture();
    s_encounter = TEST_KASHIWAGI_ENCOUNTER_INDEX + 1u;
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    s_state_root = s_tasks[2].bytes;
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    s_state_model = 0;
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    set_state_s32(0x60, 0);
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    set_state_s32(0x68, 0);
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    set_state_u8(0x2C0, 1);
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    TEST_KASHIWAGI_TASK_ID(s_tasks[0].bytes) =
        TEST_KASHIWAGI_ROOT_TASK_ID + 1u;
    run_replay();
    CHECK(s_ai_calls == 0);

    reset_fixture();
    set_pointer(s_tasks[0].bytes, 0x18, 0);
    run_replay();
    CHECK(s_ai_calls == 0);
}

static void test_bound_identity_cannot_be_replaced(void)
{
    void *root;
    unsigned int before;
    unsigned int budget;

    reset_fixture();
    root = s_tasks[0].bytes;
    before = s_ai_calls;
    run_replay();
    budget = root_extra_ticks();
    /* The first frame after a clock reset is the low half: one replay.  Each
     * later call either passes preflight and adds the frame's budget, or is
     * rejected on the replaced identity and adds nothing. */
    CHECK(s_ai_calls == before + budget);

    s_world_frame++;
    set_pointer(root, 0x18, s_objects[2].bytes);
    before = s_ai_calls;
    run_replay();
    CHECK(s_ai_calls == before);

    set_pointer(root, 0x18, s_objects[0].bytes);
    s_world_frame++;
    s_state_model = s_tasks[2].bytes;
    before = s_ai_calls;
    run_replay();
    CHECK(s_ai_calls == before);

    s_state_model = s_tasks[1].bytes;
    s_world_frame++;
    D_8020EED0_63A2B0 = s_states[(s_fixture_number + 1u) & 1u].bytes;
    before = s_ai_calls;
    run_replay();
    CHECK(s_ai_calls == before);
}

static void test_replay_rechecks_liveness_between_ticks(void)
{
    static const unsigned int invalidating_behaviors[] = {
        AI_REPLACE_CURRENT_TASK,
        AI_REPLACE_STATE,
        AI_REPLACE_ROOT,
        AI_REPLACE_OBJECT,
        AI_KILL_BOSS,
        AI_KILL_PLAYER,
        AI_PAUSE_COMBAT,
        AI_CHANGE_ENCOUNTER
    };
    unsigned int index;

    for (index = 0;
         index < sizeof(invalidating_behaviors) /
                     sizeof(invalidating_behaviors[0]);
         index++)
    {
        void *saved_current;

        reset_fixture();
        saved_current = s_tasks[2].bytes;
        D_8016DAB4_16E6B4 = saved_current;
        s_ai_behavior = invalidating_behaviors[index];
        run_replay();
        /* The callback completes this low-half frame's single tick before the
         * invalidating behavior takes effect, so the frame reports normally
         * while the aborted loop leaves no further replays. */
        CHECK(s_ai_calls == 1u);
        CHECK(D_8016DAB4_16E6B4 == saved_current);
        CHECK(s_replay_messages == 1u);
    }
}

static void test_duplicate_frame_and_reentry_guards(void)
{
    reset_fixture();
    run_replay();
    /* Two hooks on one native frame share the frame's single clock roll. */
    CHECK(root_extra_ticks() == 1u);
    CHECK(s_ai_calls == root_extra_ticks());
    run_replay();
    CHECK(s_ai_calls == root_extra_ticks());

    s_world_frame++;
    run_replay();
    CHECK(s_ai_calls == 1u + root_extra_ticks());

    reset_fixture();
    s_ai_behavior = AI_REENTER_RETURN_HOOK;
    run_replay();
    CHECK(s_ai_calls == root_extra_ticks());
}

static void test_disabled_config_does_not_consume_frame(void)
{
    reset_fixture();
    s_config = 1;
    run_replay();
    CHECK(s_ai_calls == 0);

    s_config = 0;
    run_replay();
    CHECK(s_ai_calls == root_extra_ticks());
}

static void prepare_projectile(unsigned int index,
                               HyperKashiwagiCallback callback)
{
    void *task;

    initialize_task(index);
    task = s_tasks[index].bytes;
    set_pointer(task, 0x18, s_objects[index].bytes);
    set_ai(task, callback);
    TEST_TASK_VELOCITY_X(task) = 1.0f;
    TEST_TASK_VELOCITY_Y(task) = 2.0f;
    KASHIWAGI_SHOT_VELOCITY_Z(task) = 3.0f;
    D_8016DAB4_16E6B4 = task;
}

static void check_projectile_motion_only(HyperKashiwagiCallback callback)
{
    void *object;
    void *task;
    unsigned int extra;

    reset_fixture();
    prepare_projectile(2, callback);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    callback(task, object);

    /* One native 614C pass plus the frame's 1/2 extra replays. */
    extra = motion_extra_ticks();
    CHECK(s_projectile_ai_calls == 1u);
    CHECK(s_projectile_collision_calls == 1u);
    CHECK(s_motion_calls == 1u + extra);
    CHECK(TEST_OBJECT_POSITION_X(object) == 1.0f + (float)extra);
    CHECK(TEST_OBJECT_POSITION_Y(object) == 2.0f + (float)(2u * extra));
    CHECK(TEST_OBJECT_POSITION_Z(object) == 3.0f + (float)(3u * extra));
    CHECK(!s_kashiwagi_shot);
    CHECK(!s_kashiwagi_motion_guard);

    /* Once the exact callback returns, an unrelated 614C call is native. */
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 2u + extra);
    CHECK(TEST_OBJECT_POSITION_X(object) == 2.0f + (float)extra);
    CHECK(TEST_OBJECT_POSITION_Y(object) == 4.0f + (float)(2u * extra));
    CHECK(TEST_OBJECT_POSITION_Z(object) == 6.0f + (float)(3u * extra));
}

static void test_projectile_callbacks_accelerate_motion_only(void)
{
    check_projectile_motion_only(func_801EA3F0_6157D0);
    check_projectile_motion_only(func_801EA534_615914);
    check_projectile_motion_only(func_801EA900_615CE0);
}

static void test_projectile_timer_and_rotation_compensation(void)
{
    void *object;
    void *task;
    unsigned int extra;

    reset_fixture();
    prepare_projectile(2, func_801EA3F0_6157D0);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    /* Each frame the hook sheds one lifetime frame per replayed motion step. */
    extra = motion_extra_ticks();
    KASHIWAGI_SHOT_TIMER(task) = 10;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    extra = motion_extra_ticks();
    CHECK(extra == 1u);
    CHECK(KASHIWAGI_SHOT_TIMER(task) == 10 - (int)extra);
    extra_options_hyper_kashiwagi_aiming_shot_done();
    KASHIWAGI_SHOT_TIMER(task) = 2;
    s_world_frame++;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    extra = motion_extra_ticks();
    CHECK(KASHIWAGI_SHOT_TIMER(task) ==
          (2 > (int)extra ? 2 - (int)extra : -1));
    extra_options_hyper_kashiwagi_aiming_shot_done();

    set_ai(task, func_801EA534_615914);
    s_world_frame++;
    KASHIWAGI_SHOT_TIMER(task) = 10;
    KASHIWAGI_SHOT_ROLL(task) = 7;
    KASHIWAGI_SHOT_VELOCITY_Z(task) = -3.0f;
    extra_options_hyper_kashiwagi_travelling_shot(task, object);
    extra = motion_extra_ticks();
    CHECK(KASHIWAGI_SHOT_TIMER(task) == 10 - (int)extra);
    CHECK(KASHIWAGI_SHOT_ROLL(task) == 7u + 10u * extra);
    extra_options_hyper_kashiwagi_travelling_shot_done();
    KASHIWAGI_SHOT_TIMER(task) = 2;
    s_world_frame++;
    extra_options_hyper_kashiwagi_travelling_shot(task, object);
    extra = motion_extra_ticks();
    CHECK(KASHIWAGI_SHOT_TIMER(task) ==
          (2 > (int)extra ? 2 - (int)extra : 0));
    extra_options_hyper_kashiwagi_travelling_shot_done();
    KASHIWAGI_SHOT_TIMER(task) = 10;
    KASHIWAGI_SHOT_VELOCITY_Z(task) = 3.0f;
    s_world_frame++;
    extra_options_hyper_kashiwagi_travelling_shot(task, object);
    CHECK(KASHIWAGI_SHOT_TIMER(task) == 10);
    extra_options_hyper_kashiwagi_travelling_shot_done();

    set_ai(task, func_801EA900_615CE0);
    KASHIWAGI_OBJECT_YAW(object) = 100;
    s_world_frame++;
    extra_options_hyper_kashiwagi_volley_shot(task, object);
    extra = motion_extra_ticks();
    CHECK(KASHIWAGI_OBJECT_YAW(object) == 100u + 3u * extra);
    extra_options_hyper_kashiwagi_volley_shot_done();
}

static void test_projectile_scope_is_exact_and_short_lived(void)
{
    void *object;
    void *task;

    /* Wrong callback identity never opens the movement scope. */
    reset_fixture();
    prepare_projectile(2, excluded_callback);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 1u);
    CHECK(!s_kashiwagi_shot);

    /* A mismatched object argument is rejected before any task state binds. */
    reset_fixture();
    prepare_projectile(2, func_801EA3F0_6157D0);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    extra_options_hyper_kashiwagi_aiming_shot(task, s_objects[3].bytes);
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 1u);
    CHECK(!s_kashiwagi_shot);

    /* The return hook closes a valid scope before later helper calls. */
    reset_fixture();
    prepare_projectile(2, func_801EA3F0_6157D0);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    CHECK(s_kashiwagi_shot == task);
    extra_options_hyper_kashiwagi_aiming_shot_done();
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 1u);
    CHECK(!s_kashiwagi_shot);

    /* Task-context and object changes invalidate an already-open scope. */
    reset_fixture();
    prepare_projectile(2, func_801EA3F0_6157D0);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 1u);
    extra_options_hyper_kashiwagi_aiming_shot_done();

    reset_fixture();
    prepare_projectile(2, func_801EA3F0_6157D0);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    extra_options_hyper_kashiwagi_aiming_shot(task, object);
    set_pointer(task, 0x18, s_objects[3].bytes);
    func_801D614C_60152C(task);
    CHECK(s_motion_calls == 1u);
    extra_options_hyper_kashiwagi_aiming_shot_done();
}

static void test_disabled_projectile_falls_back_to_native_motion(void)
{
    void *object;
    void *task;

    reset_fixture();
    prepare_projectile(2, func_801EA534_615914);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    s_config = 1;
    KASHIWAGI_SHOT_TIMER(task) = 10;
    KASHIWAGI_SHOT_ROLL(task) = 7;
    KASHIWAGI_SHOT_VELOCITY_Z(task) = -3.0f;
    func_801EA534_615914(task, object);

    CHECK(s_projectile_ai_calls == 1u);
    CHECK(s_projectile_collision_calls == 1u);
    CHECK(s_motion_calls == 1u);
    CHECK(TEST_OBJECT_POSITION_X(object) == 1.0f);
    CHECK(TEST_OBJECT_POSITION_Y(object) == 2.0f);
    CHECK(TEST_OBJECT_POSITION_Z(object) == -3.0f);
    CHECK(KASHIWAGI_SHOT_TIMER(task) == 9);
    CHECK(KASHIWAGI_SHOT_ROLL(task) == 17u);
}

static void test_root_replay_does_not_nest_projectile_motion(void)
{
    void *root;

    reset_fixture();
    root = s_tasks[0].bytes;
    TEST_TASK_VELOCITY_X(root) = 1.0f;
    TEST_TASK_VELOCITY_Y(root) = 2.0f;
    KASHIWAGI_SHOT_VELOCITY_Z(root) = 3.0f;
    s_root_invokes_motion = 1;
    run_replay();

    /* The root is not a registered shot, so 614C stays native: one motion
     * step per replayed root tick, no projectile replay. */
    CHECK(root_extra_ticks() == 1u);
    CHECK(s_ai_calls == root_extra_ticks());
    CHECK(s_motion_calls == root_extra_ticks());
    CHECK(TEST_OBJECT_POSITION_X(s_objects[0].bytes) ==
          (float)root_extra_ticks());
    CHECK(TEST_OBJECT_POSITION_Y(s_objects[0].bytes) ==
          2.0f * (float)root_extra_ticks());
    CHECK(TEST_OBJECT_POSITION_Z(s_objects[0].bytes) ==
          3.0f * (float)root_extra_ticks());
}

static void prepare_clone(unsigned int index,
                          HyperKashiwagiCallback callback)
{
    void *clone;
    void *object;

    initialize_task(index);
    clone = s_tasks[index].bytes;
    object = s_objects[index].bytes;
    set_pointer(clone, 0x18, object);
    TEST_KASHIWAGI_TASK_ID(clone) = 0x51u;
    set_ai(clone, callback);
    KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 5;
    D_8016DAB4_16E6B4 = clone;
    s_expected_clone = clone;
    s_expected_clone_object = object;
    extra_options_track_hyper_kashiwagi_clone(clone, object);
}

static void test_clone_callbacks_receive_two_and_a_half_x_ticks(void)
{
    unsigned int index;
    unsigned int expected = 0;

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    for (index = 0;
         index < sizeof(s_clone_callbacks) / sizeof(s_clone_callbacks[0]);
         index++)
    {
        set_ai(s_tasks[2].bytes, s_clone_callbacks[index]);
        s_world_frame++;
        run_kashiwagi_clone_tick();
        expected += clone_extra_ticks();
        CHECK(s_clone_ai_calls == expected);
        CHECK(D_8016DAB4_16E6B4 == s_tasks[2].bytes);
    }
}

static void test_clone_reloads_callbacks_and_excludes_reaction(void)
{
    void *hit = s_tasks[3].bytes;

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    CHECK(sizeof(s_clone_callbacks) / sizeof(s_clone_callbacks[0]) > 1u);
    s_clone_ai_behavior = CLONE_AI_TRANSITION_COMBAT;
    s_transition_callback = s_clone_callbacks[1];
    run_kashiwagi_clone_tick();
    CHECK(clone_extra_ticks() == 1u);
    CHECK(s_clone_ai_calls == clone_extra_ticks());
    CHECK(KASHIWAGI_TASK_AI(s_tasks[2].bytes) == s_clone_callbacks[1]);

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    set_pointer(s_tasks[2].bytes, 0x38, hit);
    s_clone_ai_behavior = CLONE_AI_CLEAR_HIT_AND_REACT;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == 1u);
    CHECK(KASHIWAGI_TASK_AI(s_tasks[2].bytes) == excluded_callback);
    {
        void *remaining_hit = hit;
        memcpy(&remaining_hit,
               (unsigned char *)s_tasks[2].bytes + 0x38,
               sizeof(remaining_hit));
        CHECK(!remaining_hit);
    }
}

static void test_clone_rechecks_attack_identity_and_context(void)
{
    static const unsigned int invalidating_behaviors[] = {
        CLONE_AI_END_ROOT_ATTACK,
        CLONE_AI_REPLACE_OBJECT,
        CLONE_AI_REPLACE_TASK_ID,
        CLONE_AI_REPLACE_CONTEXT,
        CLONE_AI_CHANGE_EPOCH
    };
    unsigned int index;

    for (index = 0;
         index < sizeof(invalidating_behaviors) /
                     sizeof(invalidating_behaviors[0]);
         index++)
    {
        reset_fixture();
        prepare_clone(2, s_clone_callbacks[0]);
        s_clone_ai_behavior = invalidating_behaviors[index];
        run_kashiwagi_clone_tick();
        CHECK(s_clone_ai_calls == 1u);
        CHECK(D_8016DAB4_16E6B4 == s_tasks[2].bytes);
    }

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 0;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == 0);

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == 0);
}

static void test_clone_frame_epoch_and_reentry_guards(void)
{
    unsigned int before;

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == clone_extra_ticks());
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == clone_extra_ticks());
    s_world_frame++;
    before = s_clone_ai_calls;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == before + clone_extra_ticks());

    /* A new exact clone may reuse the same world frame and must get a fresh
     * replay ticket rather than inheriting the previous clone's identity. */
    prepare_clone(3, s_clone_callbacks[0]);
    before = s_clone_ai_calls;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == before + clone_extra_ticks());

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    s_clone_ai_behavior = CLONE_AI_REENTER_RETURN_HOOK;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == clone_extra_ticks());
}

static void test_clone_preflight_failures_do_not_consume_frame(void)
{
    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    s_config = 1;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == 0);
    s_config = 0;
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == clone_extra_ticks());

    reset_fixture();
    prepare_clone(2, s_clone_callbacks[0]);
    set_ai(s_tasks[2].bytes, disabled_callback);
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == 0);
    set_ai(s_tasks[2].bytes, s_clone_callbacks[0]);
    run_kashiwagi_clone_tick();
    CHECK(s_clone_ai_calls == clone_extra_ticks());
}

static void prepare_charge(unsigned int index)
{
    void *root;
    void *task;

    CHECK(index < sizeof(s_tasks) / sizeof(s_tasks[0]));
    root = s_tasks[0].bytes;
    initialize_task(index);
    task = s_tasks[index].bytes;
    set_pointer(task, 0x18, s_objects[index].bytes);
    set_ai(task, func_801EAC4C_61602C);
    KASHIWAGI_ATTACK_KIND(root) = 2;
    TEST_CHARGE_ACTIVE(task) = 2;
    TEST_CHARGE_TIMER(task) = 15;
    TEST_CHARGE_FADE(task) = 0;
    TEST_CHARGE_LEVEL(task) = 0;
    s_charge_links[0] = task;
    s_charge_links[index] = s_objects[0].bytes;
    D_8016DAB4_16E6B4 = task;
}

static void test_charge_proxy_receives_two_and_a_half_x_ticks(void)
{
    void *object;
    void *task;
    unsigned int extra;

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    func_801EAC4C_61602C(task, object);

    /* One native proxy call plus this frame's 1/2 extra replays. */
    extra = charge_extra_ticks();
    CHECK(extra == 1u);
    CHECK(s_charge_ai_calls == 1u + extra);
    CHECK(TEST_CHARGE_FADE(task) == (signed int)(5u * (1u + extra)));
    CHECK(TEST_CHARGE_LEVEL(task) == (signed int)(6u * (1u + extra)));
    CHECK(TEST_CHARGE_TIMER(task) == 15);
    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(!s_kashiwagi_charge);
    CHECK(!s_kashiwagi_charge_guard);

    /* The return scope is consumed exactly once. */
    extra_options_run_hyper_kashiwagi_charge_tick();
    CHECK(s_charge_ai_calls == 1u + extra);
}

static void test_charge_clock_reaches_native_tick_57_in_fifteen_frames(void)
{
    void *object;
    void *task;
    unsigned int frame;

    /* With replay disabled, the native constructor's timer 15 emits on
     * callback 57: level first reaches 255 on 43, then 14 more decrements. */
    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    s_config = 1;
    for (frame = 0; frame < 56u; frame++)
        func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 56u);
    CHECK(s_charge_trail_emits == 0u);
    CHECK(TEST_CHARGE_TIMER(task) == 1);
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 57u);
    CHECK(s_charge_trail_emits == 1u);
    CHECK(TEST_CHARGE_TIMER(task) == 16);

    /* Hyper executes the frame's native callback plus its 1/2 extra proxy
     * replays.  Walk the real clocks until native tick 57 fires, accumulating
     * callbacks, and assert the trail crosses exactly once at that point. */
    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    {
        unsigned int expected = 0;

        for (frame = 0; frame < 40u && s_charge_trail_emits == 0u; frame++)
        {
            s_world_frame++;
            func_801EAC4C_61602C(task, object);
            expected += 1u + charge_extra_ticks();
            CHECK(s_charge_ai_calls == expected);
        }
    }
    CHECK(s_charge_trail_emits == 1u);
    /* The crossing lands on native tick 57, so the timer re-arms to 16 and
     * every later frame keeps it in the mod's 1/2-tick cadence. */
    CHECK(s_charge_ai_calls >= 43u);
    CHECK(TEST_CHARGE_TIMER(task) > 0 && TEST_CHARGE_TIMER(task) <= 16);
}

static void test_charge_contact_is_consumed_once(void)
{
    void *contact;
    void *object;
    void *task;

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    contact = s_tasks[3].bytes;
    set_pointer(task, 0x34, contact);
    func_801EAC4C_61602C(task, object);

    CHECK(s_charge_ai_calls == 1u + charge_extra_ticks());
    CHECK(s_charge_contact_emits == 1u);
    CHECK(!get_pointer(task, 0x34));
}

static void test_charge_scope_requires_exact_live_identity(void)
{
    void *object;
    void *task;

    /* Disabled config leaves the one native callback untouched. */
    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    s_config = 1;
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);
    CHECK(TEST_CHARGE_LEVEL(task) == 6);

    /* The hook is specific to AC4C, its linked object, current task, and both
     * directions of the root/child relationship. */
    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    set_ai(task, excluded_callback);
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    func_801EAC4C_61602C(task, s_objects[3].bytes);
    CHECK(s_charge_ai_calls == 1u);

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    D_8016DAB4_16E6B4 = s_tasks[3].bytes;
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    s_charge_links[0] = s_tasks[3].bytes;
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);

    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    s_charge_links[2] = s_objects[3].bytes;
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);

    /* Attack 2 ending follows native deletion and is never replayed. */
    reset_fixture();
    prepare_charge(2);
    task = s_tasks[2].bytes;
    object = s_objects[2].bytes;
    KASHIWAGI_ATTACK_KIND(s_tasks[0].bytes) = 0;
    func_801EAC4C_61602C(task, object);
    CHECK(s_charge_ai_calls == 1u);
    CHECK(s_charge_delete_calls == 1u);
    CHECK(TEST_CHARGE_LEVEL(task) == 0);
}

static void test_charge_return_rechecks_liveness_before_replay(void)
{
    static const unsigned int invalidating_behaviors[] = {
        CHARGE_AI_END_ROOT_ATTACK,
        CHARGE_AI_REPLACE_CURRENT,
        CHARGE_AI_REPLACE_OBJECT,
        CHARGE_AI_CHANGE_EPOCH,
        CHARGE_AI_BREAK_ROOT_LINK,
        CHARGE_AI_BREAK_CHILD_LINK,
        CHARGE_AI_DISABLE_CONFIG,
        CHARGE_AI_DELETE_PROXY
    };
    unsigned int index;

    for (index = 0;
         index < sizeof(invalidating_behaviors) /
                     sizeof(invalidating_behaviors[0]);
         index++)
    {
        reset_fixture();
        prepare_charge(2);
        s_charge_ai_behavior = invalidating_behaviors[index];
        func_801EAC4C_61602C(s_tasks[2].bytes, s_objects[2].bytes);
        CHECK(s_charge_ai_calls == 1u);
        CHECK(!s_kashiwagi_charge);
        CHECK(!s_kashiwagi_charge_guard);

        /* A second return notification cannot resurrect an invalid/deleted
         * child or consume its native callback twice. */
        extra_options_run_hyper_kashiwagi_charge_tick();
        CHECK(s_charge_ai_calls == 1u);
    }
}

int main(void)
{
    test_exact_two_and_a_half_x_replay_without_save();
    test_all_combat_callbacks_are_admitted();
    test_callback_is_reloaded_between_ticks();
    test_disabled_and_null_callbacks_are_rejected();
    test_lifecycle_gates();
    test_bound_identity_cannot_be_replaced();
    test_replay_rechecks_liveness_between_ticks();
    test_duplicate_frame_and_reentry_guards();
    test_disabled_config_does_not_consume_frame();
    test_projectile_callbacks_accelerate_motion_only();
    test_projectile_timer_and_rotation_compensation();
    test_projectile_scope_is_exact_and_short_lived();
    test_disabled_projectile_falls_back_to_native_motion();
    test_root_replay_does_not_nest_projectile_motion();
    test_clone_callbacks_receive_two_and_a_half_x_ticks();
    test_clone_reloads_callbacks_and_excludes_reaction();
    test_clone_rechecks_attack_identity_and_context();
    test_clone_frame_epoch_and_reentry_guards();
    test_clone_preflight_failures_do_not_consume_frame();
    test_charge_proxy_receives_two_and_a_half_x_ticks();
    test_charge_clock_reaches_native_tick_57_in_fifteen_frames();
    test_charge_contact_is_consumed_once();
    test_charge_scope_requires_exact_live_identity();
    test_charge_return_rechecks_liveness_before_replay();

    printf("Passed Kashiwagi host regressions. Native runtime remains required.\n");
    return 0;
}
