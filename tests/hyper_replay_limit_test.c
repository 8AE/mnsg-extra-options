/*
 * Host regression for the shared Hyper replay loop's per-target tick limit.
 * Tsurami projectile registration itself is covered by hyper_tsurami_test.c;
 * this test proves that the shared common-post path consumes its independent
 * alternating 1/2-tick policy while ordinary enemies retain the normal three
 * synthetic ticks.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
 *     -I include \
 *     tests/hyper_replay_limit_test.c -o /tmp/hyper_replay_limit_test
 *   /tmp/hyper_replay_limit_test
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
 *     -I include -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_replay_limit_test.c -o /tmp/hyper_replay_limit_test_ubsan
 *   /tmp/hyper_replay_limit_test_ubsan
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __MODDING_H__
#define __RECOMPCONFIG_H__
#define EXTRA_OPTIONS_H
#define RECOMP_HOOK(function)
#define RECOMP_HOOK_RETURN(function)
#define RECOMP_PATCH

static int extra_options_save_is_loaded(void);
unsigned long recomp_get_config_u32(const char *key);

typedef void (*TestTaskCallback)(void *task, void *object);
static TestTaskCallback test_task_ai(void *task);
static TestTaskCallback test_task_post(void *task);

#define TASK_AI_CALLBACK(task) test_task_ai(task)
#define TASK_POST_CALLBACK(task) test_task_post(task)
#define HYPER_CALLBACK_IS_ENABLED(callback) ((callback) != 0)

#include "../src/hyper_enemies.c"

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

#define HOST_AI_OFFSET 0x100u
#define HOST_POST_OFFSET 0x108u
#define TEST_TASK_COUNT 2u
#define TSURAMI_TEST_MIN_EXTRA_TICKS 1u
#define TSURAMI_TEST_MAX_EXTRA_TICKS 2u

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x120];
} TestBuffer;

unsigned short D_800C7AB2;
void *D_8016DAB4_16E6B4;
void *D_8020EED0_63A2B0;

static TestBuffer s_tasks[TEST_TASK_COUNT];
static TestBuffer s_objects[TEST_TASK_COUNT];
static int s_save_loaded;
static unsigned long s_config;
static int s_tsurami_registered;
static unsigned int s_tsurami_policy_queries;
static unsigned int s_tsurami_policy_selections;
static unsigned int s_tsurami_cadence_phase;
static unsigned int s_ai_calls[TEST_TASK_COUNT];
static unsigned int s_post_calls[TEST_TASK_COUNT];

static void set_pointer(void *task, size_t offset, void *value)
{
    memcpy((unsigned char *)task + offset, &value, sizeof(value));
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

static TestTaskCallback test_task_ai(void *task)
{
    return get_callback(task, HOST_AI_OFFSET);
}

static TestTaskCallback test_task_post(void *task)
{
    return get_callback(task, HOST_POST_OFFSET);
}

static unsigned int task_index(void *task)
{
    unsigned int index;

    for (index = 0; index < TEST_TASK_COUNT; index++)
        if (task == s_tasks[index].bytes)
            return index;
    CHECK(0 && "callback received an unknown fixture task");
    return 0;
}

static void test_ai_callback(void *task, void *object)
{
    unsigned int index = task_index(task);

    CHECK(object == s_objects[index].bytes);
    s_ai_calls[index]++;
}

static void test_post_callback(void *task, void *object)
{
    unsigned int index = task_index(task);

    CHECK(object == s_objects[index].bytes);
    s_post_calls[index]++;
}

static void initialize_task(unsigned int index, unsigned short actor_id)
{
    void *task;

    CHECK(index < TEST_TASK_COUNT);
    task = s_tasks[index].bytes;
    memset(task, 0, sizeof(s_tasks[index].bytes));
    memset(s_objects[index].bytes, 0, sizeof(s_objects[index].bytes));
    set_pointer(task, 0x18, s_objects[index].bytes);
    TASK_ACTOR_ID(task) = actor_id;
    TASK_STATUS(task) = 1;
    TASK_GENERATION(task) = (unsigned char)(index + 1);
    TASK_HEALTH(task) = 12;
    set_callback(task, HOST_AI_OFFSET, test_ai_callback);
    set_callback(task, HOST_POST_OFFSET, test_post_callback);
}

static void reset_fixture(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    memset(s_ai_calls, 0, sizeof(s_ai_calls));
    memset(s_post_calls, 0, sizeof(s_post_calls));
    D_800C7AB2 = 0x0071u;
    D_8016DAB4_16E6B4 = 0;
    D_8020EED0_63A2B0 = 0;
    s_save_loaded = 1;
    s_config = 0;
    s_tsurami_registered = 0;
    s_tsurami_policy_queries = 0;
    s_tsurami_policy_selections = 0;
    s_tsurami_cadence_phase = 0;
    s_runtime_active = 0;
    s_runtime_room = 0;
    s_replay_guard = 0;
    clear_runtime_tracking();
}

static unsigned int complete_native_post(void *task)
{
    D_8016DAB4_16E6B4 = task;
    extra_options_capture_hyper_actor(task);
    return extra_options_hyper_run_captured_tick(
        is_live_hyper_target, 0);
}

static int extra_options_save_is_loaded(void)
{
    return s_save_loaded;
}

unsigned long recomp_get_config_u32(const char *key)
{
    CHECK(strcmp(key, "hyper_enemies") == 0);
    return s_config;
}

int extra_options_hyper_congo_child_is_live(void *task)
{
    (void)task;
    return 0;
}

int extra_options_hyper_dharumanyo_projectile_is_live(void *task)
{
    (void)task;
    return 0;
}

int extra_options_hyper_tsurami_projectile_is_live(void *task)
{
    return s_tsurami_registered && task == s_tasks[0].bytes;
}

int extra_options_hyper_tsurami_take_projectile_extra_ticks(
    void *task, unsigned int *extra_ticks)
{
    s_tsurami_policy_queries++;
    if (!extra_ticks ||
        !extra_options_hyper_tsurami_projectile_is_live(task))
        return 0;

    *extra_ticks = s_tsurami_cadence_phase
                       ? TSURAMI_TEST_MAX_EXTRA_TICKS
                       : TSURAMI_TEST_MIN_EXTRA_TICKS;
    s_tsurami_cadence_phase = s_tsurami_cadence_phase ? 0 : 1;
    s_tsurami_policy_selections++;
    return 1;
}

int extra_options_hyper_koryuta_enemy_is_live(void *task)
{
    (void)task;
    return 0;
}

int func_800240DC_24CDC(int flag_id)
{
    (void)flag_id;
    return 0;
}

void *func_800141C4_14DC4(unsigned int file_id)
{
    (void)file_id;
    return 0;
}

void *func_802171A8_5D2678(
    void *owner, ExtraOptionsTaskCallback initializer,
    unsigned char task_kind)
{
    (void)owner;
    (void)initializer;
    (void)task_kind;
    return 0;
}

void func_08000224_6AC774(void *task, void *object)
{
    (void)task;
    (void)object;
}

static void test_registered_tsurami_projectile_alternates_one_two(void)
{
    void *task;

    reset_fixture();
    initialize_task(0, 0x00CBu);
    task = s_tasks[0].bytes;
    s_tsurami_registered = 1;

    /* The first completed native update seeds the anti-constructor cache. */
    CHECK(complete_native_post(task) == 0);
    CHECK(s_ai_calls[0] == 0);
    CHECK(s_post_calls[0] == 0);
    CHECK(s_tsurami_policy_queries == 0);
    CHECK(s_tsurami_policy_selections == 0);

    CHECK(complete_native_post(task) == TSURAMI_TEST_MIN_EXTRA_TICKS);
    CHECK(s_ai_calls[0] == 1u);
    CHECK(s_post_calls[0] == 1u);
    CHECK(s_tsurami_policy_queries == 1);
    CHECK(s_tsurami_policy_selections == 1);

    CHECK(complete_native_post(task) == TSURAMI_TEST_MAX_EXTRA_TICKS);
    CHECK(s_ai_calls[0] == 3u);
    CHECK(s_post_calls[0] == 3u);

    CHECK(complete_native_post(task) == TSURAMI_TEST_MIN_EXTRA_TICKS);
    CHECK(s_ai_calls[0] == 4u);
    CHECK(s_post_calls[0] == 4u);

    CHECK(complete_native_post(task) == TSURAMI_TEST_MAX_EXTRA_TICKS);
    CHECK(s_ai_calls[0] == 6u);
    CHECK(s_post_calls[0] == 6u);
    CHECK(s_tsurami_policy_queries == 4);
    CHECK(s_tsurami_policy_selections == 4);
}

static void test_ordinary_enemy_retains_three_ticks_without_advancing_tsurami(void)
{
    void *ordinary;
    void *tsurami;

    reset_fixture();
    initialize_task(0, 0x00CBu);
    initialize_task(1, 0x00FAu);
    tsurami = s_tasks[0].bytes;
    ordinary = s_tasks[1].bytes;
    s_tsurami_registered = 1;

    CHECK(complete_native_post(tsurami) == 0);
    CHECK(complete_native_post(ordinary) == 0);

    CHECK(complete_native_post(tsurami) ==
          TSURAMI_TEST_MIN_EXTRA_TICKS);
    CHECK(s_tsurami_policy_queries == 1);
    CHECK(s_tsurami_policy_selections == 1);

    CHECK(complete_native_post(ordinary) == HYPER_EXTRA_TICKS);
    CHECK(HYPER_EXTRA_TICKS == 3u);
    CHECK(s_ai_calls[1] == 3u);
    CHECK(s_post_calls[1] == 3u);
    CHECK(s_tsurami_policy_queries == 2);
    CHECK(s_tsurami_policy_selections == 1);

    CHECK(complete_native_post(tsurami) ==
          TSURAMI_TEST_MAX_EXTRA_TICKS);
    CHECK(s_ai_calls[0] == 3u);
    CHECK(s_post_calls[0] == 3u);
    CHECK(s_tsurami_policy_queries == 3);
    CHECK(s_tsurami_policy_selections == 2);
}

static void test_forget_task_requires_a_fresh_native_post(void)
{
    void *task;

    reset_fixture();
    initialize_task(1, 0x00FAu);
    task = s_tasks[1].bytes;

    CHECK(complete_native_post(task) == 0);
    CHECK(complete_native_post(task) == HYPER_EXTRA_TICKS);
    CHECK(s_ai_calls[1] == 3u);
    CHECK(s_post_calls[1] == 3u);
    CHECK(s_tsurami_policy_queries == 1);

    extra_options_hyper_forget_task(task);
    CHECK(complete_native_post(task) == 0);
    CHECK(s_ai_calls[1] == 3u);
    CHECK(s_post_calls[1] == 3u);
    CHECK(s_tsurami_policy_queries == 1);

    CHECK(complete_native_post(task) == HYPER_EXTRA_TICKS);
    CHECK(s_ai_calls[1] == 6u);
    CHECK(s_post_calls[1] == 6u);
    CHECK(s_tsurami_policy_queries == 2);
}

int main(void)
{
    test_registered_tsurami_projectile_alternates_one_two();
    test_ordinary_enemy_retains_three_ticks_without_advancing_tsurami();
    test_forget_task_requires_a_fresh_native_post();
    puts("hyper_replay_limit_test: all checks passed");
    return 0;
}
