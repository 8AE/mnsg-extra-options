/*
 * Focused host regressions for room 0x155's exact child admission.  Shared
 * Hyper callback replay is covered separately; these tests do not certify
 * gameplay.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     tests/hyper_koryuta_flight_test.c \
 *     -o /tmp/hyper_koryuta_flight_test
 *   /tmp/hyper_koryuta_flight_test
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_koryuta_flight_test.c \
 *     -o /tmp/hyper_koryuta_flight_test_ubsan
 *   /tmp/hyper_koryuta_flight_test_ubsan
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __MODDING_H__
#define EXTRA_OPTIONS_H
#define RECOMP_HOOK(function)
#define RECOMP_HOOK_RETURN(function)

static int extra_options_save_is_loaded(void);

typedef void (*TestTaskCallback)(void *task, void *object);
static TestTaskCallback test_task_ai(void *task);
static TestTaskCallback test_task_post(void *task);

#define KORYUTA_TASK_AI(task) test_task_ai(task)
#define KORYUTA_TASK_POST(task) test_task_post(task)

#include "../src/hyper_koryuta_flight.c"

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

#define FIXTURE_TASK_COUNT 12u
#define HOST_AI_OFFSET 0xE8u
#define HOST_POST_OFFSET 0xF0u

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x110];
} TestBuffer;

unsigned short D_800C7AB2;
void *D_8016DAB4_16E6B4;

static TestBuffer s_tasks[FIXTURE_TASK_COUNT];
static TestBuffer s_objects[FIXTURE_TASK_COUNT];
static int s_save_loaded;

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

static void initialize_task(unsigned int index, unsigned short actor_id,
                            unsigned char generation)
{
    void *task;

    CHECK(index < FIXTURE_TASK_COUNT);
    task = s_tasks[index].bytes;
    memset(task, 0, sizeof(s_tasks[index].bytes));
    memset(s_objects[index].bytes, 0, sizeof(s_objects[index].bytes));
    set_pointer(task, 0x18, s_objects[index].bytes);
    TASK_ACTOR_ID(task) = actor_id;
    TASK_GENERATION(task) = generation;
    set_callback(task, HOST_POST_OFFSET, func_80218F30_5D4400);
}

static void construct_enemy(unsigned int index,
                            TestTaskCallback constructor,
                            unsigned short entity,
                            TestTaskCallback recurring_callback)
{
    unsigned short actor_id;
    void *task = s_tasks[index].bytes;

    actor_id = constructor == func_080044BC_7055BC
                   ? ACTOR_KORYUTA_BEE_SPAWNER
                   : ACTOR_KORYUTA_ROOT;
    initialize_task(index, actor_id, (unsigned char)(index + 1));
    D_8016DAB4_16E6B4 = task;
    if (constructor == func_08003B4C_704C4C)
        extra_options_capture_hyper_koryuta_dragon_head(task);
    else if (constructor == func_08003D64_704E64)
        extra_options_capture_hyper_koryuta_robot_bee(task);
    else
        extra_options_capture_hyper_koryuta_attack_bee(task);

    TASK_ENTITY_ID(task) = entity;
    set_callback(task, HOST_AI_OFFSET, recurring_callback);
    if (constructor == func_08003B4C_704C4C)
        extra_options_track_hyper_koryuta_dragon_head();
    else if (constructor == func_08003D64_704E64)
        extra_options_track_hyper_koryuta_robot_bee();
    else
        extra_options_track_hyper_koryuta_attack_bee();
}

static void reset_fixture(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    D_800C7AB2 = KORYUTA_ROOM;
    D_8016DAB4_16E6B4 = 0;
    s_save_loaded = 1;
    s_runtime_active = 0;
    s_runtime_room = 0;
    clear_koryuta_tracking();
}

static int extra_options_save_is_loaded(void)
{
    return s_save_loaded;
}

#define EMPTY_CALLBACK(name)                                                \
    void name(void *task, void *object)                                     \
    {                                                                       \
        (void)task;                                                         \
        (void)object;                                                       \
    }

EMPTY_CALLBACK(func_80218F30_5D4400)
EMPTY_CALLBACK(func_08003B4C_704C4C)
EMPTY_CALLBACK(func_08003C90_704D90)
EMPTY_CALLBACK(func_08003D64_704E64)
EMPTY_CALLBACK(func_08003E60_704F60)
EMPTY_CALLBACK(func_08003F68_705068)
EMPTY_CALLBACK(func_08004034_705134)
EMPTY_CALLBACK(func_080044BC_7055BC)
EMPTY_CALLBACK(func_080045B8_7056B8)
EMPTY_CALLBACK(func_08004660_705760)

static void test_exact_enemy_admission(void)
{
    void *bee;
    void *head;
    void *attack_bee;

    reset_fixture();
    construct_enemy(0, func_08003D64_704E64, ENTITY_ROBOT_BEE,
                    func_08003E60_704F60);
    bee = s_tasks[0].bytes;
    CHECK(extra_options_hyper_koryuta_enemy_is_live(bee));
    set_callback(bee, HOST_AI_OFFSET, func_08003F68_705068);
    CHECK(extra_options_hyper_koryuta_enemy_is_live(bee));
    set_callback(bee, HOST_AI_OFFSET, func_08004034_705134);
    CHECK(extra_options_hyper_koryuta_enemy_is_live(bee));

    construct_enemy(1, func_08003B4C_704C4C,
                    ENTITY_FLYING_DRAGON_HEAD,
                    func_08003C90_704D90);
    head = s_tasks[1].bytes;
    CHECK(extra_options_hyper_koryuta_enemy_is_live(head));

    construct_enemy(2, func_080044BC_7055BC, ENTITY_ROBOT_BEE,
                    func_080045B8_7056B8);
    attack_bee = s_tasks[2].bytes;
    CHECK(extra_options_hyper_koryuta_enemy_is_live(attack_bee));
    set_callback(attack_bee, HOST_AI_OFFSET, func_08004660_705760);
    CHECK(extra_options_hyper_koryuta_enemy_is_live(attack_bee));

    TASK_STATUS(bee) |= TASK_STATUS_REMOVE_PENDING;
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(bee));
    set_callback(head, HOST_POST_OFFSET, func_08003C90_704D90);
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(head));
    set_callback(attack_bee, HOST_AI_OFFSET, func_08003C90_704D90);
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(attack_bee));
}

static void test_constructor_and_room_guards(void)
{
    void *controller_child;
    void *task;

    reset_fixture();
    initialize_task(0, ACTOR_KORYUTA_ROOT, 1);
    task = s_tasks[0].bytes;
    TASK_ENTITY_ID(task) = ENTITY_ROBOT_BEE;
    set_callback(task, HOST_AI_OFFSET, func_08003E60_704F60);
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(task));

    D_8016DAB4_16E6B4 = task;
    extra_options_capture_hyper_koryuta_dragon_head(task);
    TASK_ENTITY_ID(task) = ENTITY_ROBOT_BEE;
    set_callback(task, HOST_AI_OFFSET, func_08003E60_704F60);
    extra_options_track_hyper_koryuta_dragon_head();
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(task));

    initialize_task(2, ACTOR_KORYUTA_WAVE_CONTROLLER, 3);
    controller_child = s_tasks[2].bytes;
    D_8016DAB4_16E6B4 = controller_child;
    extra_options_capture_hyper_koryuta_robot_bee(controller_child);
    TASK_ENTITY_ID(controller_child) = ENTITY_ROBOT_BEE;
    set_callback(controller_child, HOST_AI_OFFSET,
                 func_08003E60_704F60);
    extra_options_track_hyper_koryuta_robot_bee();
    CHECK(extra_options_hyper_koryuta_enemy_is_live(controller_child));

    initialize_task(3, 0x0222u, 4);
    task = s_tasks[3].bytes;
    D_8016DAB4_16E6B4 = task;
    extra_options_capture_hyper_koryuta_robot_bee(task);
    TASK_ENTITY_ID(task) = ENTITY_ROBOT_BEE;
    set_callback(task, HOST_AI_OFFSET, func_08003E60_704F60);
    extra_options_track_hyper_koryuta_robot_bee();
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(task));

    construct_enemy(1, func_08003D64_704E64, ENTITY_ROBOT_BEE,
                    func_08003E60_704F60);
    task = s_tasks[1].bytes;
    CHECK(extra_options_hyper_koryuta_enemy_is_live(task));
    D_800C7AB2 = 0x0154u;
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(task));
    D_800C7AB2 = KORYUTA_ROOM;
    CHECK(!extra_options_hyper_koryuta_enemy_is_live(task));
}

int main(void)
{
    test_exact_enemy_admission();
    test_constructor_and_room_guards();
    puts("hyper_koryuta_flight_test: PASS");
    return 0;
}
