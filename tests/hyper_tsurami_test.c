/*
 * Focused host regressions for Ghost Robot Tsurami's three-phase combat
 * whitelist, safe root replay, persistent visual child, and exact travelling
 * projectile admission.  These tests exercise the production source directly;
 * they do not certify native scheduler ordering or in-game gameplay.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     tests/hyper_tsurami_test.c -o /tmp/hyper_tsurami_test
 *   /tmp/hyper_tsurami_test
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_tsurami_test.c -o /tmp/hyper_tsurami_test_ubsan
 *   /tmp/hyper_tsurami_test_ubsan
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

static int extra_options_save_is_loaded(void);
unsigned long recomp_get_config_u32(const char *key);
int recomp_printf(const char *format, ...);

typedef void (*TestTaskCallback)(void *task, void *object);
static TestTaskCallback test_task_ai(void *task);
static TestTaskCallback test_task_post(void *task);
static int test_callback_is_enabled(TestTaskCallback callback);

#define TSURAMI_TASK_AI(task) test_task_ai(task)
#define TSURAMI_TASK_POST(task) test_task_post(task)
#define TSURAMI_CALLBACK_IS_ENABLED(callback) \
    test_callback_is_enabled((TestTaskCallback)(callback))

#include "../src/hyper_tsurami.c"

_Static_assert(TSURAMI_MIN_EXTRA_TICKS == 1u,
               "Tsurami's low cadence half must add one tick");
_Static_assert(TSURAMI_MAX_EXTRA_TICKS == 2u,
               "Tsurami's high cadence half must add two ticks");

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

#define FIXTURE_TASK_COUNT 40u
#define HOST_AI_OFFSET 0x100u
#define HOST_POST_OFFSET 0x108u

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x120];
} TestBuffer;

enum
{
    ROOT_AI_NORMAL,
    ROOT_AI_ENTER_HIT_REACTION,
    ROOT_AI_REPLACE_CURRENT_TASK,
    ROOT_AI_CHANGE_ROOM
};

unsigned short D_800C7AB2;
void *D_8016DAB4_16E6B4;
unsigned int D_8015C5E4;

static TestBuffer s_tasks[FIXTURE_TASK_COUNT];
static TestBuffer s_objects[FIXTURE_TASK_COUNT];
static int s_save_loaded;
static unsigned long s_config;
static unsigned int s_root_ai_behavior;
static unsigned int s_root_ai_calls;
static unsigned int s_visual_callback_calls;
static unsigned int s_projectile_callback_calls;
static unsigned int s_private_post_calls;
static unsigned int s_common_post_calls;
static unsigned int s_root_animation_calls;
static unsigned int s_visual_animation_calls;
static unsigned int s_other_animation_calls;
static unsigned int s_root_movement_calls;
static unsigned int s_other_movement_calls;
static unsigned int s_track_messages;
static unsigned int s_replay_messages;
static unsigned int s_forget_task_calls;
static void *s_last_forgotten_task;
static char s_last_message[256];

static void disabled_callback(void *task, void *object);
static void wrong_callback(void *task, void *object);

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

static void set_ai(void *task, TestTaskCallback callback)
{
    set_callback(task, HOST_AI_OFFSET, callback);
}

static void set_post(void *task, TestTaskCallback callback)
{
    set_callback(task, HOST_POST_OFFSET, callback);
}

static TestTaskCallback test_task_ai(void *task)
{
    return get_callback(task, HOST_AI_OFFSET);
}

static TestTaskCallback test_task_post(void *task)
{
    return get_callback(task, HOST_POST_OFFSET);
}

static int test_callback_is_enabled(TestTaskCallback callback)
{
    return callback && callback != disabled_callback;
}

static void initialize_task(unsigned int index, unsigned short actor_id,
                            unsigned short entity_id,
                            unsigned char generation)
{
    void *task;

    CHECK(index < FIXTURE_TASK_COUNT);
    task = s_tasks[index].bytes;
    memset(task, 0, sizeof(s_tasks[index].bytes));
    memset(s_objects[index].bytes, 0, sizeof(s_objects[index].bytes));
    set_pointer(task, 0x18, s_objects[index].bytes);
    TASK_ACTOR_ID(task) = actor_id;
    TASK_ENTITY_ID(task) = entity_id;
    TASK_GENERATION(task) = generation;
    TASK_STATUS(task) = 1;
    TASK_HEALTH(task) = 12;
}

static void reset_fixture(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    D_800C7AB2 = ROOM_TSURAMI;
    D_8016DAB4_16E6B4 = 0;
    D_8015C5E4 = 1;
    s_save_loaded = 1;
    s_config = 0; /* Enabled is the first mod.toml enum entry. */
    s_root_ai_behavior = ROOT_AI_NORMAL;
    s_root_ai_calls = 0;
    s_visual_callback_calls = 0;
    s_projectile_callback_calls = 0;
    s_private_post_calls = 0;
    s_common_post_calls = 0;
    s_root_animation_calls = 0;
    s_visual_animation_calls = 0;
    s_other_animation_calls = 0;
    s_root_movement_calls = 0;
    s_other_movement_calls = 0;
    s_track_messages = 0;
    s_replay_messages = 0;
    s_forget_task_calls = 0;
    s_last_forgotten_task = 0;
    s_last_message[0] = '\0';
    s_runtime_active = 0;
    s_runtime_room = 0;
    clear_tsurami_tracking();
}

static void initialize_root(void)
{
    void *root = s_tasks[0].bytes;

    initialize_task(0, ACTOR_TSURAMI, 0, 7);
    /* The hook runs at the start of the exact native root initializer. */
    extra_options_begin_hyper_tsurami(root);
    set_ai(root, func_08001D54_6B4FF4);
    set_post(root, func_08000388_6B3628);
}

static void latch_root(void)
{
    initialize_root();
    extra_options_start_hyper_tsurami(s_tasks[0].bytes);
    CHECK(s_combat_active);
    CHECK(hyper_tsurami_root_is_live(s_tasks[0].bytes));
}

static void construct_visual(unsigned int index)
{
    void *child;

    initialize_task(index, ACTOR_TSURAMI, 0,
                    (unsigned char)(index + 20));
    child = s_tasks[index].bytes;
    set_pointer(child, 0xD0, s_tasks[0].bytes);
    extra_options_capture_hyper_tsurami_visual(child);
    TASK_ENTITY_ID(child) = ENTITY_TSURAMI_CHILD;
    set_ai(child, func_080046B8_6B7958);
    set_post(child, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_visual();
}

static void construct_projectile(unsigned int index, unsigned int flags)
{
    unsigned int forget_calls = s_forget_task_calls;
    void *projectile;

    initialize_task(index, ACTOR_TSURAMI, 0,
                    (unsigned char)(index + 40));
    projectile = s_tasks[index].bytes;
    D_8016DAB4_16E6B4 = projectile;
    extra_options_capture_hyper_tsurami_projectile(projectile);
    TASK_ENTITY_ID(projectile) = ENTITY_TSURAMI_CHILD;
    TASK_ATTACK_FLAGS(projectile) = flags;
    set_ai(projectile, func_08004ED0_6B8170);
    set_post(projectile, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(s_forget_task_calls == forget_calls + 1u);
    CHECK(s_last_forgotten_task == projectile);
}

static unsigned int take_projectile_extra_ticks(void *task)
{
    unsigned int extra_ticks = 0;

    CHECK(extra_options_hyper_tsurami_take_projectile_extra_ticks(
        task, &extra_ticks));
    return extra_ticks;
}

static void check_projectile_take_rejected(void *task)
{
    unsigned int extra_ticks = 0xA5A5A5A5u;

    CHECK(!extra_options_hyper_tsurami_take_projectile_extra_ticks(
        task, &extra_ticks));
    CHECK(extra_ticks == 0xA5A5A5A5u);
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

void extra_options_hyper_forget_task(void *task)
{
    s_forget_task_calls++;
    s_last_forgotten_task = task;
}

int recomp_printf(const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsnprintf(s_last_message, sizeof(s_last_message),
                       format, arguments);
    va_end(arguments);
    if (strstr(s_last_message, "Tsurami tracked:"))
        s_track_messages++;
    if (strstr(s_last_message, "Hyper Tsurami active:"))
        s_replay_messages++;
    return result;
}

void func_08002E00_6B60A0(void *task, void *object);

static void run_root_ai(void *task, void *object)
{
    CHECK(task == s_tasks[0].bytes);
    CHECK(object == s_objects[0].bytes);
    s_root_ai_calls++;

    if (s_root_ai_behavior == ROOT_AI_ENTER_HIT_REACTION)
        set_ai(task, func_08002E00_6B60A0);
    else if (s_root_ai_behavior == ROOT_AI_REPLACE_CURRENT_TASK)
        D_8016DAB4_16E6B4 = s_tasks[FIXTURE_TASK_COUNT - 1].bytes;
    else if (s_root_ai_behavior == ROOT_AI_CHANGE_ROOM)
        D_800C7AB2 = ROOM_TSURAMI + 1u;
}

#define ROOT_AI_CALLBACK(name)                                              \
    void name(void *task, void *object)                                     \
    {                                                                       \
        run_root_ai(task, object);                                          \
    }

ROOT_AI_CALLBACK(func_08001D54_6B4FF4)
ROOT_AI_CALLBACK(func_08001DB0_6B5050)
ROOT_AI_CALLBACK(func_08001DF0_6B5090)
ROOT_AI_CALLBACK(func_08001EAC_6B514C)
ROOT_AI_CALLBACK(func_08001F28_6B51C8)
ROOT_AI_CALLBACK(func_08001F68_6B5208)
ROOT_AI_CALLBACK(func_08001FE8_6B5288)
ROOT_AI_CALLBACK(func_08002028_6B52C8)
ROOT_AI_CALLBACK(func_080020D8_6B5378)
ROOT_AI_CALLBACK(func_08002128_6B53C8)
ROOT_AI_CALLBACK(func_08002190_6B5430)
ROOT_AI_CALLBACK(func_080023E0_6B5680)
ROOT_AI_CALLBACK(func_08002460_6B5700)
ROOT_AI_CALLBACK(func_080024A0_6B5740)
ROOT_AI_CALLBACK(func_080024F0_6B5790)
ROOT_AI_CALLBACK(func_08002534_6B57D4)
ROOT_AI_CALLBACK(func_0800257C_6B581C)
ROOT_AI_CALLBACK(func_080027E4_6B5A84)
ROOT_AI_CALLBACK(func_08002864_6B5B04)
ROOT_AI_CALLBACK(func_080028A4_6B5B44)
ROOT_AI_CALLBACK(func_08002918_6B5BB8)
ROOT_AI_CALLBACK(func_080029D8_6B5C78)
ROOT_AI_CALLBACK(func_08002C4C_6B5EEC)
ROOT_AI_CALLBACK(func_08002CD8_6B5F78)
ROOT_AI_CALLBACK(func_08002D2C_6B5FCC)
ROOT_AI_CALLBACK(func_08002D6C_6B600C)
ROOT_AI_CALLBACK(func_08002DA8_6B6048)

#define EMPTY_CALLBACK(name)                                                \
    void name(void *task, void *object)                                     \
    {                                                                       \
        (void)task;                                                         \
        (void)object;                                                       \
    }

EMPTY_CALLBACK(func_08001A50_6B4CF0)
EMPTY_CALLBACK(func_08001AA8_6B4D48)
EMPTY_CALLBACK(func_08001B38_6B4DD8)
EMPTY_CALLBACK(func_08001C58_6B4EF8)
EMPTY_CALLBACK(func_08001D0C_6B4FAC)
EMPTY_CALLBACK(func_08002E00_6B60A0)
EMPTY_CALLBACK(func_08002E34_6B60D4)
EMPTY_CALLBACK(func_08002EC8_6B6168)
EMPTY_CALLBACK(func_08002F0C_6B61AC)
EMPTY_CALLBACK(func_08002F50_6B61F0)
EMPTY_CALLBACK(func_08002FA4_6B6244)
EMPTY_CALLBACK(func_08002FFC_6B629C)
EMPTY_CALLBACK(func_08003058_6B62F8)
EMPTY_CALLBACK(func_080030D0_6B6370)
EMPTY_CALLBACK(func_0800332C_6B65CC)
EMPTY_CALLBACK(func_080033DC_6B667C)
EMPTY_CALLBACK(func_08003814_6B6AB4)
EMPTY_CALLBACK(func_08003850_6B6AF0)
EMPTY_CALLBACK(func_080038B8_6B6B58)
EMPTY_CALLBACK(func_0800390C_6B6BAC)
EMPTY_CALLBACK(func_080039B4_6B6C54)
EMPTY_CALLBACK(func_08003A00_6B6CA0)
EMPTY_CALLBACK(func_0800587C_6B8B1C)

void func_080046B8_6B7958(void *task, void *object)
{
    (void)task;
    (void)object;
    s_visual_callback_calls++;
}

void func_0800476C_6B7A0C(void *task, void *object)
{
    (void)task;
    (void)object;
    s_visual_callback_calls++;
}

void func_08004ED0_6B8170(void *task, void *object)
{
    (void)task;
    (void)object;
    s_projectile_callback_calls++;
}

void func_08000388_6B3628(void *task, void *object)
{
    (void)task;
    (void)object;
    s_private_post_calls++;
}

void func_80218F30_5D4400(void *task, void *object)
{
    (void)task;
    (void)object;
    s_common_post_calls++;
}

static void disabled_callback(void *task, void *object)
{
    (void)task;
    (void)object;
}

static void wrong_callback(void *task, void *object)
{
    (void)task;
    (void)object;
}

void func_80216AB8_5D1F88(void *task)
{
    if (task == s_tasks[0].bytes)
        s_root_animation_calls++;
    else if (task == s_visual_child.task)
        s_visual_animation_calls++;
    else
        s_other_animation_calls++;
}

void func_8021B808_5D6CD8(void *task)
{
    if (task == s_tasks[0].bytes)
        s_root_movement_calls++;
    else
        s_other_movement_calls++;
}

static void capture_and_replay_root(void)
{
    void *root = s_tasks[0].bytes;

    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami(root);
    extra_options_run_hyper_tsurami_tick();
}

static void test_root_tracking_and_latch_contract(void)
{
    void *root = s_tasks[0].bytes;

    reset_fixture();
    initialize_task(0, ACTOR_TSURAMI, 0, 7);

    D_800C7AB2 = ROOM_TSURAMI + 1u;
    extra_options_begin_hyper_tsurami(root);
    CHECK(!s_tsurami.task);

    D_800C7AB2 = ROOM_TSURAMI;
    TASK_ACTOR_ID(root) = ACTOR_TSURAMI + 1u;
    extra_options_begin_hyper_tsurami(root);
    CHECK(!s_tsurami.task);

    TASK_ACTOR_ID(root) = ACTOR_TSURAMI;
    set_pointer(root, 0x18, 0);
    extra_options_begin_hyper_tsurami(root);
    CHECK(!s_tsurami.task);

    set_pointer(root, 0x18, s_objects[0].bytes);
    extra_options_begin_hyper_tsurami(root);
    CHECK(s_tsurami.task == root);
    CHECK(!s_combat_active);

    set_ai(root, func_08001D54_6B4FF4);
    set_post(root, wrong_callback);
    extra_options_start_hyper_tsurami(root);
    CHECK(!s_combat_active);

    set_post(root, func_08000388_6B3628);
    TASK_STATUS(root) |= TASK_STATUS_REMOVE_PENDING;
    extra_options_start_hyper_tsurami(root);
    CHECK(!s_combat_active);
    TASK_STATUS(root) &= ~TASK_STATUS_REMOVE_PENDING;

    TASK_HEALTH(root) = 1;
    extra_options_start_hyper_tsurami(root);
    CHECK(!s_combat_active);
    TASK_HEALTH(root) = 12;

    extra_options_start_hyper_tsurami(root);
    CHECK(s_combat_active);
    CHECK(hyper_tsurami_root_is_live(root));

    set_pointer(root, 0x18, s_objects[1].bytes);
    CHECK(!hyper_tsurami_root_is_live(root));
    set_pointer(root, 0x18, s_objects[0].bytes);
    CHECK(hyper_tsurami_root_is_live(root));

    TASK_GENERATION(root)++;
    CHECK(!hyper_tsurami_root_is_live(root));
}

static void test_three_phase_callback_whitelist(void)
{
    static const TestTaskCallback combat_callbacks[] = {
        func_08001D54_6B4FF4,
        func_08001DB0_6B5050,
        func_08001DF0_6B5090,
        func_08001EAC_6B514C,
        func_08001F28_6B51C8,
        func_08001F68_6B5208,
        func_08001FE8_6B5288,
        func_08002028_6B52C8,
        func_080020D8_6B5378,
        func_08002128_6B53C8,
        func_08002190_6B5430,
        func_080023E0_6B5680,
        func_08002460_6B5700,
        func_080024A0_6B5740,
        func_080024F0_6B5790,
        func_08002534_6B57D4,
        func_0800257C_6B581C,
        func_080027E4_6B5A84,
        func_08002864_6B5B04,
        func_080028A4_6B5B44,
        func_08002918_6B5BB8,
        func_080029D8_6B5C78,
        func_08002C4C_6B5EEC,
        func_08002CD8_6B5F78,
        func_08002D2C_6B5FCC,
        func_08002D6C_6B600C,
        func_08002DA8_6B6048
    };
    static const TestTaskCallback excluded_callbacks[] = {
        /* Intro. */
        func_08001A50_6B4CF0,
        func_08001AA8_6B4D48,
        func_08001B38_6B4DD8,
        func_08001C58_6B4EF8,
        func_08001D0C_6B4FAC,
        /* Hit reaction. */
        func_08002E00_6B60A0,
        func_08002E34_6B60D4,
        func_08002EC8_6B6168,
        func_08002F0C_6B61AC,
        func_08002F50_6B61F0,
        func_08002FA4_6B6244,
        /* Defeat. */
        func_08002FFC_6B629C,
        func_08003058_6B62F8,
        func_080030D0_6B6370,
        func_0800332C_6B65CC,
        func_080033DC_6B667C,
        func_08003814_6B6AB4,
        func_08003850_6B6AF0,
        func_080038B8_6B6B58,
        func_0800390C_6B6BAC,
        func_080039B4_6B6C54,
        func_08003A00_6B6CA0,
        wrong_callback
    };
    unsigned int index;
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;

    for (index = 0;
         index < sizeof(combat_callbacks) / sizeof(combat_callbacks[0]);
         index++)
    {
        set_ai(root, combat_callbacks[index]);
        CHECK(hyper_tsurami_root_is_live(root));
    }

    for (index = 0;
         index < sizeof(excluded_callbacks) / sizeof(excluded_callbacks[0]);
         index++)
    {
        set_ai(root, excluded_callbacks[index]);
        CHECK(!hyper_tsurami_root_is_live(root));
    }

    set_ai(root, disabled_callback);
    CHECK(!hyper_tsurami_root_is_live(root));
    set_ai(root, 0);
    CHECK(!hyper_tsurami_root_is_live(root));
}

static void test_exact_post_self_latches_active_combat(void)
{
    void *root;

    reset_fixture();
    initialize_root();
    root = s_tasks[0].bytes;
    CHECK(!s_combat_active);

    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami(root);
    CHECK(s_combat_active);
    CHECK(s_root_capture_valid);

    extra_options_run_hyper_tsurami_tick();
    CHECK(s_root_ai_calls == TSURAMI_MIN_EXTRA_TICKS);
    CHECK(s_root_animation_calls == TSURAMI_MIN_EXTRA_TICKS);
    CHECK(s_root_movement_calls == TSURAMI_MIN_EXTRA_TICKS);
}

static void test_safe_root_replay_alternates_for_two_point_five_x(void)
{
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1u);
    CHECK(s_root_animation_calls == 1u);
    CHECK(s_root_movement_calls == 1u);
    CHECK(s_visual_animation_calls == 0);
    CHECK(s_other_animation_calls == 0);
    CHECK(s_other_movement_calls == 0);
    CHECK(s_private_post_calls == 0);
    CHECK(s_common_post_calls == 0);
    CHECK(s_projectile_callback_calls == 0);
    CHECK(D_8016DAB4_16E6B4 == root);
    CHECK(s_track_messages == 1);
    CHECK(s_replay_messages == 1);
    CHECK(strcmp(s_last_message,
                 "[Extra Options] Hyper Tsurami active: "
                 "alternating 1/2 extra AI/movement/animation ticks "
                 "(2.5x average).\n") == 0);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 3u);
    CHECK(s_root_animation_calls == 3u);
    CHECK(s_root_movement_calls == 3u);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 4u);
    CHECK(s_root_animation_calls == 4u);
    CHECK(s_root_movement_calls == 4u);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 6u);
    CHECK(s_root_animation_calls == 6u);
    CHECK(s_root_movement_calls == 6u);
    CHECK(s_track_messages == 1);
    CHECK(s_replay_messages == 1);
}

static void test_replay_rechecks_state_between_steps(void)
{
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    s_root_ai_behavior = ROOT_AI_ENTER_HIT_REACTION;
    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1);
    CHECK(s_root_animation_calls == 0);
    CHECK(s_root_movement_calls == 0);
    CHECK(s_private_post_calls == 0);
    CHECK(s_common_post_calls == 0);
    CHECK(s_replay_messages == 0);

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    s_root_ai_behavior = ROOT_AI_REPLACE_CURRENT_TASK;
    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1);
    CHECK(s_root_animation_calls == 0);
    CHECK(s_root_movement_calls == 0);
    CHECK(D_8016DAB4_16E6B4 == root);

    reset_fixture();
    latch_root();
    s_root_ai_behavior = ROOT_AI_CHANGE_ROOM;
    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1);
    CHECK(s_root_animation_calls == 0);
    CHECK(s_root_movement_calls == 0);
    CHECK(D_800C7AB2 == ROOM_TSURAMI + 1u);

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami(root);
    CHECK(s_root_capture_valid);
    D_8015C5E4 = 0;
    extra_options_run_hyper_tsurami_tick();
    CHECK(s_root_ai_calls == 0);
    CHECK(s_root_animation_calls == 0);
    CHECK(s_root_movement_calls == 0);
    CHECK(!hyper_tsurami_root_is_live(root));
}

static void test_visual_child_exact_tracking_and_animation_only(void)
{
    void *child;
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    construct_visual(1);
    child = s_tasks[1].bytes;
    CHECK(visual_child_is_live(root));
    set_ai(child, func_0800476C_6B7A0C);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1u);
    CHECK(s_root_animation_calls == 1u);
    CHECK(s_root_movement_calls == 1u);
    CHECK(s_visual_animation_calls == 1u);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 3u);
    CHECK(s_root_animation_calls == 3u);
    CHECK(s_root_movement_calls == 3u);
    CHECK(s_visual_animation_calls == 3u);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 4u);
    CHECK(s_root_animation_calls == 4u);
    CHECK(s_root_movement_calls == 4u);
    CHECK(s_visual_animation_calls == 4u);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 6u);
    CHECK(s_root_animation_calls == 6u);
    CHECK(s_root_movement_calls == 6u);
    CHECK(s_visual_animation_calls == 6u);
    CHECK(s_visual_callback_calls == 0);
    CHECK(s_other_movement_calls == 0);
    CHECK(s_common_post_calls == 0);

    set_pointer(child, 0xD0, s_tasks[2].bytes);
    CHECK(!visual_child_is_live(root));
    set_pointer(child, 0xD0, root);
    TASK_GENERATION(child)++;
    CHECK(!visual_child_is_live(root));

    reset_fixture();
    latch_root();
    initialize_task(1, ACTOR_TSURAMI, ENTITY_TSURAMI_CHILD, 21);
    child = s_tasks[1].bytes;
    set_pointer(child, 0xD0, s_tasks[2].bytes);
    extra_options_capture_hyper_tsurami_visual(child);
    set_ai(child, func_080046B8_6B7958);
    set_post(child, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_visual();
    CHECK(!s_visual_child.task);

    /* An effect or lookalike not seen in the exact 045F8 hook stays out. */
    initialize_task(2, ACTOR_TSURAMI, ENTITY_TSURAMI_CHILD, 22);
    child = s_tasks[2].bytes;
    set_pointer(child, 0xD0, s_tasks[0].bytes);
    set_ai(child, func_0800476C_6B7A0C);
    set_post(child, func_80218F30_5D4400);
    CHECK(!visual_child_is_live(s_tasks[0].bytes));
}

static void test_all_projectile_modes_from_exact_constructor(void)
{
    static const unsigned int modes[] = {
        TSURAMI_PROJECTILE_MODE_1,
        TSURAMI_PROJECTILE_MODE_2,
        TSURAMI_PROJECTILE_MODE_4,
        TSURAMI_PROJECTILE_MODE_8,
        TSURAMI_PROJECTILE_MODE_10
    };
    unsigned int index;
    unsigned int task_index = 1;

    reset_fixture();
    latch_root();

    for (index = 0; index < sizeof(modes) / sizeof(modes[0]); index++)
    {
        construct_projectile(task_index, modes[index]);
        CHECK(extra_options_hyper_tsurami_projectile_is_live(
            s_tasks[task_index].bytes));
        CHECK(take_projectile_extra_ticks(s_tasks[task_index].bytes) ==
              TSURAMI_MIN_EXTRA_TICKS);
        CHECK(take_projectile_extra_ticks(s_tasks[task_index].bytes) ==
              TSURAMI_MAX_EXTRA_TICKS);
        task_index++;

        construct_projectile(task_index, modes[index] | 0x40u);
        CHECK(extra_options_hyper_tsurami_projectile_is_live(
            s_tasks[task_index].bytes));
        CHECK(take_projectile_extra_ticks(s_tasks[task_index].bytes) ==
              TSURAMI_MIN_EXTRA_TICKS);
        CHECK(take_projectile_extra_ticks(s_tasks[task_index].bytes) ==
              TSURAMI_MAX_EXTRA_TICKS);
        task_index++;
    }
}

static void test_projectile_cadence_is_independent_and_reregisters_low(void)
{
    void *first;
    void *second;

    reset_fixture();
    latch_root();
    construct_projectile(1, TSURAMI_PROJECTILE_MODE_1);
    construct_projectile(2, TSURAMI_PROJECTILE_MODE_8 | 0x40u);
    first = s_tasks[1].bytes;
    second = s_tasks[2].bytes;

    /* Root and each projectile own independent low/high cadence phases. */
    capture_and_replay_root();
    CHECK(s_root_ai_calls == 1u);
    CHECK(take_projectile_extra_ticks(first) ==
          TSURAMI_MIN_EXTRA_TICKS);
    CHECK(take_projectile_extra_ticks(second) ==
          TSURAMI_MIN_EXTRA_TICKS);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 3u);
    CHECK(take_projectile_extra_ticks(first) ==
          TSURAMI_MAX_EXTRA_TICKS);
    CHECK(take_projectile_extra_ticks(second) ==
          TSURAMI_MAX_EXTRA_TICKS);

    capture_and_replay_root();
    CHECK(s_root_ai_calls == 4u);
    CHECK(take_projectile_extra_ticks(first) ==
          TSURAMI_MIN_EXTRA_TICKS);
    CHECK(take_projectile_extra_ticks(second) ==
          TSURAMI_MIN_EXTRA_TICKS);

    /* Recycling the same task address into a new generation must not inherit
     * the former projectile's high phase. */
    TASK_GENERATION(first)++;
    D_8016DAB4_16E6B4 = first;
    extra_options_capture_hyper_tsurami_projectile(first);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(s_forget_task_calls == 3u);
    CHECK(s_last_forgotten_task == first);
    CHECK(extra_options_hyper_tsurami_projectile_is_live(first));
    CHECK(take_projectile_extra_ticks(first) ==
          TSURAMI_MIN_EXTRA_TICKS);
    CHECK(take_projectile_extra_ticks(first) ==
          TSURAMI_MAX_EXTRA_TICKS);
}

static void test_projectile_constructor_and_identity_rejections(void)
{
    void *original_object;
    void *projectile;
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;

    /* A 0587C impact/effect lookalike is never constructor-registered. */
    initialize_task(1, ACTOR_TSURAMI, ENTITY_TSURAMI_CHILD, 41);
    set_ai(s_tasks[1].bytes, func_0800587C_6B8B1C);
    set_post(s_tasks[1].bytes, func_80218F30_5D4400);
    TASK_ATTACK_FLAGS(s_tasks[1].bytes) = TSURAMI_PROJECTILE_MODE_8;
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[1].bytes));
    check_projectile_take_rejected(s_tasks[1].bytes);

    /* Even an exact-field clone is rejected without the 049A4 hook pair. */
    initialize_task(2, ACTOR_TSURAMI, ENTITY_TSURAMI_CHILD, 42);
    set_ai(s_tasks[2].bytes, func_08004ED0_6B8170);
    set_post(s_tasks[2].bytes, func_80218F30_5D4400);
    TASK_ATTACK_FLAGS(s_tasks[2].bytes) = TSURAMI_PROJECTILE_MODE_1;
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[2].bytes));
    check_projectile_take_rejected(s_tasks[2].bytes);

    initialize_task(3, ACTOR_TSURAMI, 0, 43);
    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami_projectile(s_tasks[3].bytes);
    CHECK(!s_pending_projectile_valid);
    check_projectile_take_rejected(s_tasks[3].bytes);

    initialize_task(4, ACTOR_TSURAMI, 0, 44);
    D_8016DAB4_16E6B4 = s_tasks[4].bytes;
    extra_options_capture_hyper_tsurami_projectile(s_tasks[4].bytes);
    TASK_ENTITY_ID(s_tasks[4].bytes) = ENTITY_TSURAMI_CHILD + 1u;
    TASK_ATTACK_FLAGS(s_tasks[4].bytes) = TSURAMI_PROJECTILE_MODE_1;
    set_ai(s_tasks[4].bytes, func_08004ED0_6B8170);
    set_post(s_tasks[4].bytes, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[4].bytes));
    check_projectile_take_rejected(s_tasks[4].bytes);

    initialize_task(5, ACTOR_TSURAMI, 0, 45);
    D_8016DAB4_16E6B4 = s_tasks[5].bytes;
    extra_options_capture_hyper_tsurami_projectile(s_tasks[5].bytes);
    TASK_ENTITY_ID(s_tasks[5].bytes) = ENTITY_TSURAMI_CHILD;
    TASK_ATTACK_FLAGS(s_tasks[5].bytes) = TSURAMI_PROJECTILE_MODE_2;
    set_ai(s_tasks[5].bytes, func_0800587C_6B8B1C);
    set_post(s_tasks[5].bytes, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[5].bytes));
    check_projectile_take_rejected(s_tasks[5].bytes);

    initialize_task(6, ACTOR_TSURAMI, 0, 46);
    D_8016DAB4_16E6B4 = s_tasks[6].bytes;
    extra_options_capture_hyper_tsurami_projectile(s_tasks[6].bytes);
    TASK_ENTITY_ID(s_tasks[6].bytes) = ENTITY_TSURAMI_CHILD;
    TASK_ATTACK_FLAGS(s_tasks[6].bytes) = TSURAMI_PROJECTILE_MODE_4;
    set_ai(s_tasks[6].bytes, func_08004ED0_6B8170);
    set_post(s_tasks[6].bytes, wrong_callback);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[6].bytes));
    check_projectile_take_rejected(s_tasks[6].bytes);

    initialize_task(7, ACTOR_TSURAMI, 0, 47);
    D_8016DAB4_16E6B4 = s_tasks[7].bytes;
    extra_options_capture_hyper_tsurami_projectile(s_tasks[7].bytes);
    TASK_ENTITY_ID(s_tasks[7].bytes) = ENTITY_TSURAMI_CHILD;
    TASK_ATTACK_FLAGS(s_tasks[7].bytes) = 0x20u;
    set_ai(s_tasks[7].bytes, func_08004ED0_6B8170);
    set_post(s_tasks[7].bytes, func_80218F30_5D4400);
    extra_options_track_hyper_tsurami_projectile();
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(
        s_tasks[7].bytes));
    check_projectile_take_rejected(s_tasks[7].bytes);
    CHECK(s_forget_task_calls == 0);

    construct_projectile(8, TSURAMI_PROJECTILE_MODE_10 | 0x40u);
    CHECK(s_forget_task_calls == 1u);
    projectile = s_tasks[8].bytes;
    CHECK(extra_options_hyper_tsurami_projectile_is_live(projectile));
    CHECK(take_projectile_extra_ticks(projectile) ==
          TSURAMI_MIN_EXTRA_TICKS);
    CHECK(!extra_options_hyper_tsurami_take_projectile_extra_ticks(
        projectile, 0));

    TASK_STATUS(projectile) |= TASK_STATUS_REMOVE_PENDING;
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    TASK_STATUS(projectile) &= ~TASK_STATUS_REMOVE_PENDING;

    TASK_GENERATION(projectile)++;
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    TASK_GENERATION(projectile)--;

    original_object = TASK_OBJECT(projectile);
    set_pointer(projectile, 0x18, s_objects[9].bytes);
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    set_pointer(projectile, 0x18, original_object);

    TASK_ENTITY_ID(projectile)++;
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    TASK_ENTITY_ID(projectile)--;

    set_ai(projectile, func_0800587C_6B8B1C);
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    set_ai(projectile, func_08004ED0_6B8170);

    set_post(projectile, wrong_callback);
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    check_projectile_take_rejected(projectile);
    set_post(projectile, func_80218F30_5D4400);
    CHECK(extra_options_hyper_tsurami_projectile_is_live(projectile));
    CHECK(take_projectile_extra_ticks(projectile) ==
          TSURAMI_MAX_EXTRA_TICKS);
}

static void test_config_save_room_and_hp_guards(void)
{
    void *projectile;
    void *root;

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    construct_projectile(1, TSURAMI_PROJECTILE_MODE_1);
    projectile = s_tasks[1].bytes;

    s_config = 1;
    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami(root);
    CHECK(!s_root_capture_valid);
    extra_options_run_hyper_tsurami_tick();
    CHECK(s_root_ai_calls == 0);
    /* Constructor identities remain available for a mid-fight enable. */
    CHECK(extra_options_hyper_tsurami_projectile_is_live(projectile));

    s_config = 0;
    capture_and_replay_root();
    CHECK(s_root_ai_calls == TSURAMI_MIN_EXTRA_TICKS);

    TASK_HEALTH(root) = 1;
    CHECK(!hyper_tsurami_root_is_live(root));
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    D_8016DAB4_16E6B4 = root;
    extra_options_capture_hyper_tsurami(root);
    extra_options_run_hyper_tsurami_tick();
    CHECK(s_root_ai_calls == TSURAMI_MIN_EXTRA_TICKS);

    TASK_HEALTH(root) = 12;
    D_800C7AB2 = ROOM_TSURAMI + 1u;
    CHECK(!hyper_tsurami_root_is_live(root));
    CHECK(!extra_options_hyper_tsurami_projectile_is_live(projectile));
    D_800C7AB2 = ROOM_TSURAMI;
    CHECK(!hyper_tsurami_root_is_live(root));

    reset_fixture();
    latch_root();
    root = s_tasks[0].bytes;
    s_save_loaded = 0;
    CHECK(!hyper_tsurami_root_is_live(root));
    s_save_loaded = 1;
    CHECK(!hyper_tsurami_root_is_live(root));
}

int main(void)
{
    test_root_tracking_and_latch_contract();
    test_three_phase_callback_whitelist();
    test_exact_post_self_latches_active_combat();
    test_safe_root_replay_alternates_for_two_point_five_x();
    test_replay_rechecks_state_between_steps();
    test_visual_child_exact_tracking_and_animation_only();
    test_all_projectile_modes_from_exact_constructor();
    test_projectile_cadence_is_independent_and_reregisters_low();
    test_projectile_constructor_and_identity_rejections();
    test_config_save_room_and_hp_guards();

    puts("hyper_tsurami_test: all checks passed");
    return 0;
}
