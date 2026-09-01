/*
 * Focused host regressions for Dharumanyo's root/carrier topology, shared
 * replay delegation, carrier yaw pre-advance, and exact projectile admission.
 * The shared Hyper scheduler is mocked; these tests do not certify gameplay.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     tests/hyper_dharumanyo_test.c -o /tmp/hyper_dharumanyo_test
 *   /tmp/hyper_dharumanyo_test
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_dharumanyo_test.c -o /tmp/hyper_dharumanyo_test_ubsan
 *   /tmp/hyper_dharumanyo_test_ubsan
 *
 * Native task pointers at +0xDC are four-byte aligned for the target's
 * 32-bit ABI, but not for host pointers.  The UBSan recipe therefore disables
 * alignment checking only; all other undefined-behavior checks remain active.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Suppress target-only hook/import sections and the save-memory address. */
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
static unsigned int s_phase_flags;
#define DHARUMANYO_TASK_AI(task) test_task_ai(task)
#define DHARUMANYO_TASK_POST(task) test_task_post(task)
#define DHARUMANYO_PHASE_FLAGS s_phase_flags

#include "../src/hyper_dharumanyo.c"

_Static_assert(ACTOR_DARUMANYO == 0x00CCu,
               "file 31 Dharumanyo must not be confused with actor 0x132");

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

#define FIXTURE_TASK_COUNT 24u
#define HOST_AI_OFFSET 0xE8u
#define HOST_POST_OFFSET 0xF0u

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x100];
} TestBuffer;

unsigned short D_800C7AB2;
void *D_8016DAB4_16E6B4;

static TestBuffer s_tasks[FIXTURE_TASK_COUNT];
static TestBuffer s_objects[FIXTURE_TASK_COUNT];
static int s_save_loaded;
static unsigned long s_config;
static int s_combat_paused;
static unsigned int s_track_messages;
static unsigned int s_replay_messages;
static char s_last_message[256];
static void *s_capture_task;
static ExtraOptionsHyperTargetPredicate s_capture_predicate;
static ExtraOptionsHyperTargetPredicate s_replay_predicate;
static ExtraOptionsHyperBeforeTick s_before_tick;
static int s_capture_live;
static unsigned int s_completed_replays;

static void set_pointer(void *task, size_t offset, void *value)
{
    memcpy((unsigned char *)task + offset, &value, sizeof(value));
}

static void set_post(void *task, TestTaskCallback callback)
{
    memcpy((unsigned char *)task + HOST_POST_OFFSET,
           &callback, sizeof(callback));
}

static void set_ai(void *task, TestTaskCallback callback)
{
    memcpy((unsigned char *)task + HOST_AI_OFFSET,
           &callback, sizeof(callback));
}

static TestTaskCallback test_task_ai(void *task)
{
    TestTaskCallback callback;

    memcpy(&callback, (unsigned char *)task + HOST_AI_OFFSET,
           sizeof(callback));
    return callback;
}

static TestTaskCallback test_task_post(void *task)
{
    TestTaskCallback callback;

    memcpy(&callback, (unsigned char *)task + HOST_POST_OFFSET,
           sizeof(callback));
    return callback;
}

static void initialize_task(unsigned int index, unsigned short actor_id,
                            unsigned short entity_id,
                            unsigned char generation,
                            TestTaskCallback post)
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
    set_post(task, post);
}

static void initialize_boss_pair(unsigned int root_index,
                                 unsigned int carrier_index)
{
    void *root;
    void *carrier;

    initialize_task(root_index, ACTOR_DARUMANYO, ACTOR_DARUMANYO,
                    (unsigned char)(root_index + 1),
                    func_08003810_6CBA20);
    initialize_task(carrier_index, ACTOR_DARUMANYO,
                    ENTITY_DARUMANYO_CARRIER,
                    (unsigned char)(carrier_index + 1),
                    func_08003F84_6CC194);
    root = s_tasks[root_index].bytes;
    carrier = s_tasks[carrier_index].bytes;
    set_pointer(root, 0xDC, carrier);
    set_pointer(carrier, 0xDC, root);
    DARUMANYO_LIVES(carrier) = 12;
}

static void latch_boss(unsigned int root_index, unsigned int carrier_index)
{
    initialize_boss_pair(root_index, carrier_index);
    extra_options_start_hyper_dharumanyo(s_tasks[root_index].bytes);
    CHECK(hyper_dharumanyo_is_live(s_tasks[root_index].bytes));
}

static void initialize_projectile(unsigned int index,
                                  unsigned char generation)
{
    initialize_task(index, ACTOR_DARUMANYO, 0, generation, 0);
}

static void construct_projectile(unsigned int index, void *root)
{
    void *task = s_tasks[index].bytes;

    initialize_projectile(index, (unsigned char)(index + 40));
    set_pointer(task, 0xDC, root);
    extra_options_capture_hyper_dharumanyo_projectile(task);
    TASK_ENTITY_ID(task) = ENTITY_DARUMANYO_CARRIER;
    set_ai(task, func_0800284C_6CAA5C);
    set_post(task, func_80218F30_5D4400);
    extra_options_track_hyper_dharumanyo_projectile();
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(task));
}

static void reset_fixture(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_objects, 0, sizeof(s_objects));
    D_800C7AB2 = ROOM_DARUMANYO;
    D_8016DAB4_16E6B4 = 0;
    s_save_loaded = 1;
    s_config = 1; /* Disabled is the second mod.toml enum entry. */
    s_combat_paused = 0;
    s_phase_flags = 0;
    s_track_messages = 0;
    s_replay_messages = 0;
    s_last_message[0] = '\0';
    s_capture_task = 0;
    s_capture_predicate = 0;
    s_replay_predicate = 0;
    s_before_tick = (ExtraOptionsHyperBeforeTick)(size_t)1;
    s_capture_live = 0;
    s_completed_replays = 0;
    s_runtime_active = 0;
    s_runtime_room = 0;
    clear_dharumanyo_tracking();
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

int recomp_printf(const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsnprintf(s_last_message, sizeof(s_last_message),
                       format, arguments);
    va_end(arguments);
    if (strstr(s_last_message, "Dharumanyo tracked:"))
        s_track_messages++;
    if (strstr(s_last_message, "Hyper Dharumanyo active:"))
        s_replay_messages++;
    return result;
}

int func_800240DC_24CDC(int flag_id)
{
    CHECK(flag_id == DARUMANYO_COMBAT_PAUSED_FLAG);
    return s_combat_paused;
}

void func_08003810_6CBA20(void *task, void *object)
{
    (void)task;
    (void)object;
}

void func_0800284C_6CAA5C(void *task, void *object)
{
    (void)task;
    (void)object;
}

void func_08002A40_6CAC50(void *task, void *object)
{
    (void)task;
    (void)object;
}

void func_08003F84_6CC194(void *task, void *object)
{
    (void)task;
    (void)object;
}

void func_80218F30_5D4400(void *task, void *object)
{
    (void)task;
    (void)object;
}

static void wrong_post(void *task, void *object)
{
    (void)task;
    (void)object;
}

void extra_options_hyper_capture_from_post(
    void *task, ExtraOptionsHyperTargetPredicate target_is_live)
{
    s_capture_task = task;
    s_capture_predicate = target_is_live;
    s_capture_live = target_is_live && target_is_live(task);
}

unsigned int extra_options_hyper_run_captured_tick(
    ExtraOptionsHyperTargetPredicate target_is_live,
    ExtraOptionsHyperBeforeTick before_tick)
{
    s_replay_predicate = target_is_live;
    s_before_tick = before_tick;
    return s_completed_replays;
}

static void test_latch_requires_exact_combat_topology(void)
{
    void *root;
    void *carrier;

    reset_fixture();
    initialize_boss_pair(0, 1);
    root = s_tasks[0].bytes;
    carrier = s_tasks[1].bytes;

    D_800C7AB2 = 0x48;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);

    D_800C7AB2 = ROOM_DARUMANYO;
    TASK_ACTOR_ID(root) = 0x132;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    TASK_ACTOR_ID(root) = ACTOR_DARUMANYO;

    TASK_ACTOR_ID(root) = 0x131;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    TASK_ACTOR_ID(root) = ACTOR_DARUMANYO;

    set_post(root, wrong_post);
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    set_post(root, func_08003810_6CBA20);

    set_pointer(root, 0xDC, 0);
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    set_pointer(root, 0xDC, carrier);

    set_pointer(carrier, 0xDC, 0);
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    set_pointer(carrier, 0xDC, root);

    TASK_ACTOR_ID(carrier) = 0x131;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    TASK_ACTOR_ID(carrier) = ACTOR_DARUMANYO;

    TASK_ENTITY_ID(carrier) = 0xCD;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    TASK_ENTITY_ID(carrier) = ENTITY_DARUMANYO_CARRIER;

    set_post(carrier, wrong_post);
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    set_post(carrier, func_08003F84_6CC194);

    DARUMANYO_LIVES(carrier) = 0;
    extra_options_start_hyper_dharumanyo(root);
    CHECK(!s_combat_active);
    DARUMANYO_LIVES(carrier) = 12;

    extra_options_start_hyper_dharumanyo(root);
    CHECK(s_combat_active);
    CHECK(s_dharumanyo.task == root);
    CHECK(s_lives_carrier.task == carrier);
    CHECK(hyper_dharumanyo_is_live(root));
    CHECK(s_track_messages == 1);
}

static void test_parent_capture_and_diagnostic_delegation(void)
{
    void *root;

    reset_fixture();
    s_config = 0;
    latch_boss(0, 1);
    root = s_tasks[0].bytes;

    extra_options_capture_hyper_dharumanyo(root);
    CHECK(s_capture_task == root);
    CHECK(s_capture_predicate == hyper_dharumanyo_is_live);
    CHECK(s_capture_live);

    s_completed_replays = DARUMANYO_EXTRA_TICKS;
    extra_options_run_hyper_dharumanyo_tick();
    CHECK(s_replay_predicate == hyper_dharumanyo_is_live);
    CHECK(s_before_tick == 0);
    CHECK(s_replay_messages == 1);

    extra_options_run_hyper_dharumanyo_tick();
    CHECK(s_replay_messages == 1);

    reset_fixture();
    s_config = 0;
    latch_boss(0, 1);
    s_completed_replays = DARUMANYO_EXTRA_TICKS - 1;
    extra_options_run_hyper_dharumanyo_tick();
    CHECK(s_replay_messages == 0);
}

static void test_carrier_yaw_phase_steps_and_guards(void)
{
    static const unsigned int phase_flags[] = {
        0, DARUMANYO_PHASE_2, DARUMANYO_PHASE_3, DARUMANYO_PHASE_4,
        DARUMANYO_PHASE_2 | DARUMANYO_PHASE_4
    };
    static const unsigned short increments[] = {
        0x60, 0x90, 0xC0, 0xF0, 0xF0
    };
    unsigned int index;
    void *root;
    void *carrier;

    reset_fixture();
    s_config = 0;
    latch_boss(0, 1);
    root = s_tasks[0].bytes;
    carrier = s_tasks[1].bytes;
    D_8016DAB4_16E6B4 = carrier;

    for (index = 0; index < sizeof(phase_flags) / sizeof(phase_flags[0]);
         index++)
    {
        s_phase_flags = phase_flags[index];
        DARUMANYO_CARRIER_YAW(carrier) = 0x10;
        extra_options_advance_hyper_dharumanyo_carrier(carrier);
        CHECK(DARUMANYO_CARRIER_YAW(carrier) ==
              (unsigned short)(0x10 + increments[index]));
    }

    s_phase_flags = 0;
    DARUMANYO_CARRIER_YAW(carrier) = 0x3E0;
    extra_options_advance_hyper_dharumanyo_carrier(carrier);
    CHECK(DARUMANYO_CARRIER_YAW(carrier) == 0x40);

    s_config = 1;
    DARUMANYO_CARRIER_YAW(carrier) = 0x100;
    extra_options_advance_hyper_dharumanyo_carrier(carrier);
    CHECK(DARUMANYO_CARRIER_YAW(carrier) == 0x100);

    s_config = 0;
    DARUMANYO_LIVES(carrier) = 0;
    extra_options_advance_hyper_dharumanyo_carrier(carrier);
    CHECK(DARUMANYO_CARRIER_YAW(carrier) == 0x100);
    DARUMANYO_LIVES(carrier) = 12;

    D_8016DAB4_16E6B4 = root;
    extra_options_advance_hyper_dharumanyo_carrier(carrier);
    CHECK(DARUMANYO_CARRIER_YAW(carrier) == 0x100);
}

static void test_projectile_constructor_contract(void)
{
    void *root;
    void *projectile;

    reset_fixture();
    latch_boss(0, 1);
    root = s_tasks[0].bytes;
    projectile = s_tasks[2].bytes;

    initialize_projectile(2, 42);
    extra_options_capture_hyper_dharumanyo_projectile(projectile);
    CHECK(!s_pending_projectile_valid);
    set_pointer(projectile, 0xDC, root);
    extra_options_capture_hyper_dharumanyo_projectile(projectile);
    CHECK(s_pending_projectile_valid);
    TASK_ENTITY_ID(projectile) = ENTITY_DARUMANYO_CARRIER;
    set_ai(projectile, func_0800284C_6CAA5C);
    set_post(projectile, func_80218F30_5D4400);
    extra_options_track_hyper_dharumanyo_projectile();
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(projectile));

    initialize_projectile(3, 43);
    set_pointer(s_tasks[3].bytes, 0xDC, root);
    extra_options_capture_hyper_dharumanyo_projectile(s_tasks[3].bytes);
    set_ai(s_tasks[3].bytes, func_0800284C_6CAA5C);
    set_post(s_tasks[3].bytes, func_80218F30_5D4400);
    extra_options_track_hyper_dharumanyo_projectile();
    TASK_ENTITY_ID(s_tasks[3].bytes) = ENTITY_DARUMANYO_CARRIER;
    set_pointer(s_tasks[3].bytes, 0xDC, root);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(
        s_tasks[3].bytes));

    initialize_projectile(4, 44);
    set_pointer(s_tasks[4].bytes, 0xDC, root);
    extra_options_capture_hyper_dharumanyo_projectile(s_tasks[4].bytes);
    TASK_ENTITY_ID(s_tasks[4].bytes) = ENTITY_DARUMANYO_CARRIER;
    set_ai(s_tasks[4].bytes, func_0800284C_6CAA5C);
    set_post(s_tasks[4].bytes, wrong_post);
    extra_options_track_hyper_dharumanyo_projectile();
    set_pointer(s_tasks[4].bytes, 0xDC, root);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(
        s_tasks[4].bytes));

    initialize_projectile(5, 45);
    set_pointer(s_tasks[5].bytes, 0xDC, root);
    extra_options_capture_hyper_dharumanyo_projectile(s_tasks[5].bytes);
    TASK_ENTITY_ID(s_tasks[5].bytes) = ENTITY_DARUMANYO_CARRIER;
    set_ai(s_tasks[5].bytes, func_08002A40_6CAC50);
    set_post(s_tasks[5].bytes, func_80218F30_5D4400);
    extra_options_track_hyper_dharumanyo_projectile();
    set_pointer(s_tasks[5].bytes, 0xDC, root);
    /* The native constructor can immediately select its falling variant. */
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(
        s_tasks[5].bytes));
}

static void test_projectile_exact_identity_rejections(void)
{
    void *root;
    void *projectile;
    void *original_object;
    unsigned char original_generation;

    reset_fixture();
    latch_boss(0, 1);
    root = s_tasks[0].bytes;
    construct_projectile(2, root);
    projectile = s_tasks[2].bytes;

    /* A trail-like task with identical public fields is still unregistered. */
    initialize_task(3, ACTOR_DARUMANYO, ENTITY_DARUMANYO_CARRIER,
                    43, func_80218F30_5D4400);
    set_ai(s_tasks[3].bytes, func_0800284C_6CAA5C);
    set_pointer(s_tasks[3].bytes, 0xDC, root);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(
        s_tasks[3].bytes));

    set_pointer(projectile, 0xDC, s_tasks[3].bytes);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    set_pointer(projectile, 0xDC, root);

    TASK_STATUS(projectile) |= TASK_STATUS_REMOVE_PENDING;
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    TASK_STATUS(projectile) &= ~TASK_STATUS_REMOVE_PENDING;

    original_object = TASK_OBJECT(projectile);
    set_pointer(projectile, 0x18, s_objects[4].bytes);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    set_pointer(projectile, 0x18, original_object);

    original_generation = TASK_GENERATION(projectile);
    TASK_GENERATION(projectile)++;
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    TASK_GENERATION(projectile) = original_generation;

    set_post(projectile, wrong_post);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    set_post(projectile, func_80218F30_5D4400);
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(projectile));

    set_ai(projectile, func_08002A40_6CAC50);
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    set_ai(projectile, wrong_post);
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
}

static void test_death_pause_and_room_stop_root_and_projectile(void)
{
    void *root;
    void *carrier;
    void *projectile;

    reset_fixture();
    latch_boss(0, 1);
    root = s_tasks[0].bytes;
    carrier = s_tasks[1].bytes;
    construct_projectile(2, root);
    projectile = s_tasks[2].bytes;

    DARUMANYO_LIVES(carrier) = 0;
    CHECK(!hyper_dharumanyo_is_live(root));
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    DARUMANYO_LIVES(carrier) = 12;
    CHECK(hyper_dharumanyo_is_live(root));
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(projectile));

    s_combat_paused = 1;
    CHECK(!hyper_dharumanyo_is_live(root));
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    s_combat_paused = 0;

    D_800C7AB2 = 0x4A;
    CHECK(!hyper_dharumanyo_is_live(root));
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
    D_800C7AB2 = ROOM_DARUMANYO;
    CHECK(!hyper_dharumanyo_is_live(root));
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(projectile));
}

static void test_relatch_clears_old_projectile_registry(void)
{
    void *first_root;
    void *second_root;
    void *old_projectile;

    reset_fixture();
    latch_boss(0, 1);
    first_root = s_tasks[0].bytes;
    construct_projectile(2, first_root);
    old_projectile = s_tasks[2].bytes;

    initialize_boss_pair(3, 4);
    second_root = s_tasks[3].bytes;
    extra_options_start_hyper_dharumanyo(second_root);
    CHECK(hyper_dharumanyo_is_live(second_root));
    CHECK(!hyper_dharumanyo_is_live(first_root));
    CHECK(!extra_options_hyper_dharumanyo_projectile_is_live(old_projectile));
    CHECK(s_track_messages == 2);

    construct_projectile(5, second_root);
    CHECK(extra_options_hyper_dharumanyo_projectile_is_live(
        s_tasks[5].bytes));
}

int main(void)
{
    test_latch_requires_exact_combat_topology();
    test_parent_capture_and_diagnostic_delegation();
    test_carrier_yaw_phase_steps_and_guards();
    test_projectile_constructor_contract();
    test_projectile_exact_identity_rejections();
    test_death_pause_and_room_stop_root_and_projectile();
    test_relatch_clears_old_projectile_registry();

    puts("hyper_dharumanyo_test: all checks passed");
    return 0;
}
