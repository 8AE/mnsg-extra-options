/*
 * Host regression tests for the production Congo identity, flame admission,
 * emitter, model-part ordering, and diagnostic code.  Shared scheduler replay
 * is mocked, and callback-slot access/enabling uses a host ABI fixture.  These
 * tests do not certify the native scheduler or in-game AI.
 *
 * Run from the repository root:
 *   xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *     -fsanitize=undefined -fno-sanitize=alignment \
 *     tests/hyper_congo_test.c -o /tmp/hyper_congo_test_ubsan
 *   /tmp/hyper_congo_test_ubsan
 *
 * The production game uses 32-bit pointers.  Fake task buffers are aligned,
 * but the game's pointer slots at +0x2C and +0xDC are not 8-byte aligned on a
 * 64-bit host.  Alignment checking alone is disabled for that ABI difference;
 * other undefined-behavior checks remain enabled.  We do not emulate shared
 * replay's adjacent 32-bit +0x0C/+0x10 callback slots.  An optional ASan-enabled
 * run did not reach test output in this environment, so it is not part of
 * the known-passing recipe or claimed verification.
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
static int test_callback_is_enabled(TestTaskCallback callback);
#define CONGO_TASK_AI(task) test_task_ai(task)
#define CONGO_TASK_POST(task) test_task_post(task)
#define CONGO_CALLBACK_IS_ENABLED(callback) test_callback_is_enabled(callback)

#include "../src/hyper_congo.c"

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

#define MOCK_CAPACITY 64u
#define WORLD_FRAME_OFFSET 0x3ADCEu

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x100];
} TestBuffer;

typedef struct
{
    void *owner;
    HyperCongoTaskCallback initializer;
    unsigned char task_kind;
    float x;
    float y;
    float z;
    int flags;
    unsigned int ai_call;
    unsigned short world_frame;
    void *child;
} MockSpawn;

unsigned short D_800C7AB2;
unsigned char *D_8015C5C8_15D1C8;
void *D_8016DAB4_16E6B4;

static _Alignas(max_align_t) unsigned char s_world[0x3AE00];
static TestBuffer s_root;
static TestBuffer s_other_root;
static TestBuffer s_object;
static TestBuffer s_other_object;
static TestBuffer s_children[MOCK_CAPACITY];
static TestBuffer s_child_objects[MOCK_CAPACITY];
static TestBuffer s_resource;
static TestBuffer s_model_parts[CONGO_PART_COUNT + 1];
static TestBuffer s_model_objects[CONGO_PART_COUNT + 1];
static TestTaskCallback s_part_ai[CONGO_PART_COUNT + 1];
static TestTaskCallback s_part_post[CONGO_PART_COUNT + 1];
static unsigned int s_part_ai_calls[CONGO_PART_COUNT + 1];
static unsigned int s_part_post_calls[CONGO_PART_COUNT + 1];
static unsigned int s_part_signals[CONGO_PART_COUNT + 1][4];
static unsigned int s_part_behavior[CONGO_PART_COUNT + 1];
static int s_mock_run_before_ticks;
static MockSpawn s_spawns[MOCK_CAPACITY];
static unsigned int s_spawn_calls;
static unsigned int s_resource_calls;
static unsigned int s_ai_call;
static int s_allocation_fails;
static int s_save_loaded;
static unsigned long s_config;
static unsigned int s_track_messages;
static unsigned int s_replay_messages;
static char s_last_message[256];
static void *s_capture_task;
static ExtraOptionsHyperTargetPredicate s_capture_predicate;
static ExtraOptionsHyperTargetPredicate s_replay_predicate;
static ExtraOptionsHyperBeforeTick s_before_tick;
static int s_capture_live;
static unsigned int s_completed_replays;

enum
{
    PART_NORMAL,
    PART_REMOVE,
    PART_RECYCLE,
    PART_REPLACE_OBJECT,
    PART_REPLACE_POST,
    PART_REPLACE_CURRENT_TASK,
    PART_CHANGE_ROOM,
    PART_POST_REPLACES_CURRENT_TASK
};

static unsigned int model_index(void *task)
{
    unsigned int index;

    for (index = 0; index <= CONGO_PART_COUNT; index++)
        if (task == s_model_parts[index].bytes)
            return index;
    CHECK(0 && "callback called on an unregistered host model fixture");
    return 0;
}

static TestTaskCallback test_task_ai(void *task)
{
    return s_part_ai[model_index(task)];
}

static TestTaskCallback test_task_post(void *task)
{
    return s_part_post[model_index(task)];
}

static int test_callback_is_enabled(TestTaskCallback callback)
{
    /* Actual host code addresses have unrelated ASLR bits.  A sentinel tests
     * disabled-slot handling; native numeric mask checks appear below. */
    return callback && callback != (TestTaskCallback)(size_t)0x80800000u;
}

static void set_pointer(void *task, size_t offset, void *value)
{
    memcpy((unsigned char *)task + offset, &value, sizeof(value));
}

static void *get_pointer(void *task, size_t offset)
{
    void *value;

    memcpy(&value, (unsigned char *)task + offset, sizeof(value));
    return value;
}

static void set_world_frame(unsigned short frame)
{
    memcpy(s_world + WORLD_FRAME_OFFSET, &frame, sizeof(frame));
}

static unsigned short get_world_frame(void)
{
    unsigned short frame;

    memcpy(&frame, s_world + WORLD_FRAME_OFFSET, sizeof(frame));
    return frame;
}

static void initialize_task(void *task, void *object,
                            unsigned short actor_id,
                            unsigned short entity_id,
                            unsigned char generation)
{
    memset(task, 0, sizeof(TestBuffer));
    set_pointer(task, 0x18, object);
    TASK_ACTOR_ID(task) = actor_id;
    TASK_ENTITY_ID(task) = entity_id;
    TASK_GENERATION(task) = generation;
    TASK_STATUS(task) = 1;
    TASK_HEALTH(task) = 30;
}

static void bind_root(void)
{
    initialize_task(s_root.bytes, s_object.bytes, 0x323, ENTITY_CONGO, 7);
    TASK_CONGO_FLAGS(s_root.bytes) = CONGO_HEALTH_CONTROLLER;
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(hyper_congo_is_live(s_root.bytes));
}

static void initialize_flame(unsigned int index, void *owner,
                             unsigned short entity_id)
{
    CHECK(index < MOCK_CAPACITY);
    initialize_task(s_children[index].bytes, s_child_objects[index].bytes,
                    0x323, entity_id, (unsigned char)(index + 1));
    set_pointer(s_children[index].bytes, 0xD0, owner);
}

static void reset_fixture(void)
{
    memset(s_world, 0, sizeof(s_world));
    memset(&s_root, 0, sizeof(s_root));
    memset(&s_other_root, 0, sizeof(s_other_root));
    memset(s_children, 0, sizeof(s_children));
    memset(s_child_objects, 0, sizeof(s_child_objects));
    memset(s_spawns, 0, sizeof(s_spawns));
    memset(s_model_parts, 0, sizeof(s_model_parts));
    memset(s_model_objects, 0, sizeof(s_model_objects));
    memset(s_part_ai, 0, sizeof(s_part_ai));
    memset(s_part_post, 0, sizeof(s_part_post));
    memset(s_part_ai_calls, 0, sizeof(s_part_ai_calls));
    memset(s_part_post_calls, 0, sizeof(s_part_post_calls));
    memset(s_part_signals, 0, sizeof(s_part_signals));
    memset(s_part_behavior, 0, sizeof(s_part_behavior));
    s_mock_run_before_ticks = 0;
    D_8015C5C8_15D1C8 = s_world;
    D_800C7AB2 = 0x016;
    D_8016DAB4_16E6B4 = 0;
    s_runtime_active = 0;
    s_runtime_room = 0;
    clear_congo_tracking();
    s_breath_phase = 0;
    s_spawn_calls = 0;
    s_resource_calls = 0;
    s_ai_call = 0;
    s_allocation_fails = 0;
    s_save_loaded = 1;
    s_config = 1; /* Disabled is the mod.toml default and second enum entry. */
    s_track_messages = 0;
    s_replay_messages = 0;
    s_last_message[0] = '\0';
    s_capture_task = 0;
    s_capture_predicate = 0;
    s_replay_predicate = 0;
    s_before_tick = 0;
    s_capture_live = 0;
    s_completed_replays = 0;
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
    if (strstr(s_last_message, "Congo tracked:"))
        s_track_messages++;
    if (strstr(s_last_message, "Hyper Congo active:"))
        s_replay_messages++;
    return result;
}

/* Only admission/delegation and the completed-replay diagnostic are tested. */
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
    unsigned int tick;

    s_replay_predicate = target_is_live;
    s_before_tick = before_tick;
    /* This is only the shared helper's enable-gating contract, not its real
     * implementation.  It permits testing Congo's delegated callback wiring. */
    if (s_mock_run_before_ticks)
    {
        if (s_config != 0 || !s_capture_task ||
            !target_is_live(s_capture_task))
            return 0;
        for (tick = 0; tick < s_completed_replays; tick++)
            if (before_tick)
                before_tick(s_capture_task);
    }
    return s_completed_replays;
}

void *func_8021DDE8_5D92B8(
    void *owner, HyperCongoTaskCallback initializer, unsigned char task_kind,
    float x, float y, float z, int flags)
{
    unsigned int index = s_spawn_calls++;
    MockSpawn *spawn;

    CHECK(index < MOCK_CAPACITY);
    spawn = &s_spawns[index];
    spawn->owner = owner;
    spawn->initializer = initializer;
    spawn->task_kind = task_kind;
    spawn->x = x;
    spawn->y = y;
    spawn->z = z;
    spawn->flags = flags;
    spawn->ai_call = s_ai_call;
    spawn->world_frame = get_world_frame();
    if (s_allocation_fails)
        return 0;

    /* Native construction happens later, after the emitter sets the owner. */
    initialize_flame(index, 0, 0);
    spawn->child = s_children[index].bytes;
    return spawn->child;
}

void *func_800141C4_14DC4(unsigned int file_id)
{
    CHECK(file_id == 0x1D);
    s_resource_calls++;
    return s_resource.bytes;
}

void func_08000DCC_6B406C(void *task, void *object)
{
    CHECK(object == get_pointer(task, 0x18));
    TASK_ENTITY_ID(task) = ENTITY_CONGO_FLAME;
}

static void replacement_part_post(void *task, void *object)
{
    (void)task;
    (void)object;
    CHECK(0 && "a replaced teardown post must not run in the same extra tick");
}

static void normal_part_ai(void *task, void *object)
{
    unsigned int part = model_index(task);
    unsigned int call = s_part_ai_calls[part]++;

    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(object == get_pointer(task, 0x18));
    if (call < 4)
        s_part_signals[part][call] = TASK_CONGO_FLAGS(s_root.bytes) & 0x1C0u;
    switch (s_part_behavior[part])
    {
    case PART_REMOVE:
        TASK_STATUS(task) |= TASK_STATUS_REMOVE_PENDING;
        break;
    case PART_RECYCLE:
        TASK_GENERATION(task)++;
        break;
    case PART_REPLACE_OBJECT:
        set_pointer(task, 0x18, s_other_object.bytes);
        break;
    case PART_REPLACE_POST:
        s_part_post[part] = replacement_part_post;
        break;
    case PART_REPLACE_CURRENT_TASK:
        D_8016DAB4_16E6B4 = s_other_root.bytes;
        break;
    case PART_CHANGE_ROOM:
        D_800C7AB2++;
        break;
    default:
        break;
    }
}

static void normal_part_post(void *task, void *object)
{
    unsigned int part = model_index(task);

    CHECK(D_8016DAB4_16E6B4 == task);
    CHECK(object == get_pointer(task, 0x18));
    s_part_post_calls[part]++;
    if (s_part_behavior[part] == PART_POST_REPLACES_CURRENT_TASK)
        D_8016DAB4_16E6B4 = s_other_root.bytes;
}

static void initialize_part(unsigned int part, void *owner)
{
    CHECK(part <= CONGO_PART_COUNT);
    initialize_task(s_model_parts[part].bytes, s_model_objects[part].bytes,
                    0x323, ENTITY_CONGO, (unsigned char)(part + 20));
    set_pointer(s_model_parts[part].bytes, 0xDC, owner);
    s_part_ai[part] = normal_part_ai;
    s_part_post[part] = normal_part_post;
}

static void invoke_part_constructor_return(unsigned int part)
{
    static void (*const hooks[])(void) = {
        extra_options_track_congo_part_0, extra_options_track_congo_part_1,
        extra_options_track_congo_part_2, extra_options_track_congo_part_3,
        extra_options_track_congo_part_4, extra_options_track_congo_part_5
    };

    CHECK(part < CONGO_PART_COUNT);
    D_8016DAB4_16E6B4 = s_model_parts[part].bytes;
    hooks[part]();
    D_8016DAB4_16E6B4 = s_root.bytes;
}

static void test_part_tracking_and_callback_guards(void)
{
    unsigned int part;

    bind_root();
    for (part = 0; part <= CONGO_PART_COUNT; part++)
        initialize_part(part, s_root.bytes);
    invoke_part_constructor_return(0);
    set_pointer(s_model_parts[1].bytes, 0xDC, s_other_root.bytes);
    invoke_part_constructor_return(1);
    set_pointer(s_model_parts[2].bytes, 0x18, s_object.bytes);
    invoke_part_constructor_return(2);
    set_pointer(s_model_parts[3].bytes, 0x18, 0);
    invoke_part_constructor_return(3);
    /* Parts 4/5 and the extra unrelated model have no constructor hook. */
    advance_congo_parts(s_root.bytes);
    CHECK(s_part_ai_calls[0] == 1 && s_part_post_calls[0] == 1);
    for (part = 1; part <= CONGO_PART_COUNT; part++)
        CHECK(s_part_ai_calls[part] == 0 && s_part_post_calls[part] == 0);
    CHECK(D_8016DAB4_16E6B4 == s_root.bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_model_parts[0].bytes));

    s_part_ai[0] = 0;
    advance_congo_parts(s_root.bytes);
    s_part_ai[0] = (TestTaskCallback)(size_t)0x80800000u;
    advance_congo_parts(s_root.bytes);
    s_part_ai[0] = normal_part_ai;
    s_part_post[0] = 0;
    advance_congo_parts(s_root.bytes);
    s_part_post[0] = (TestTaskCallback)(size_t)0x80800000u;
    advance_congo_parts(s_root.bytes);
    CHECK(s_part_ai_calls[0] == 1 && s_part_post_calls[0] == 1);
    CHECK(TASK_CALLBACK_DISABLED_BIT == 0x00800000u);
    CHECK((0x80001000ul & TASK_CALLBACK_DISABLED_BIT) == 0);
    CHECK((0x80801000ul & TASK_CALLBACK_DISABLED_BIT) != 0);

    s_part_post[0] = normal_part_post;
    TASK_STATUS(s_model_parts[0].bytes) |= TASK_STATUS_REMOVE_PENDING;
    advance_congo_parts(s_root.bytes);
    TASK_STATUS(s_model_parts[0].bytes) &= ~TASK_STATUS_REMOVE_PENDING;
    TASK_GENERATION(s_model_parts[0].bytes)++;
    advance_congo_parts(s_root.bytes);
    CHECK(s_part_ai_calls[0] == 1 && s_part_post_calls[0] == 1);
}

static void test_part_interleave_preserves_each_root_signal(void)
{
    static const unsigned int signals[] = {0x40, 0x80, 0x100, 0x40};
    unsigned int part;
    unsigned int tick;

    bind_root();
    for (part = 0; part < CONGO_PART_COUNT; part++)
    {
        initialize_part(part, s_root.bytes);
        invoke_part_constructor_return(part);
    }
    TASK_CONGO_FLAGS(s_root.bytes) = CONGO_HEALTH_CONTROLLER | signals[0];
    for (tick = 0; tick < 3; tick++)
    {
        advance_congo_parts(s_root.bytes);
        CHECK(D_8016DAB4_16E6B4 == s_root.bytes);
        /* The following extra root AI clears/replaces the one-tick signal. */
        TASK_CONGO_FLAGS(s_root.bytes) =
            CONGO_HEALTH_CONTROLLER | signals[tick + 1];
    }
    /* The real scheduler supplies the fourth part tick after the root. */
    for (part = 0; part < CONGO_PART_COUNT; part++)
    {
        D_8016DAB4_16E6B4 = s_model_parts[part].bytes;
        normal_part_ai(s_model_parts[part].bytes, s_model_objects[part].bytes);
        normal_part_post(s_model_parts[part].bytes, s_model_objects[part].bytes);
        CHECK(s_part_ai_calls[part] == 4 && s_part_post_calls[part] == 4);
        for (tick = 0; tick < 4; tick++)
            CHECK(s_part_signals[part][tick] == signals[tick]);
    }
    D_8016DAB4_16E6B4 = s_root.bytes;
}

static void test_part_mutations_stop_post_and_restore_scheduler_context(void)
{
    unsigned int behavior;

    for (behavior = PART_REMOVE; behavior <= PART_CHANGE_ROOM; behavior++)
    {
        reset_fixture();
        bind_root();
        initialize_part(0, s_root.bytes);
        invoke_part_constructor_return(0);
        s_part_behavior[0] = behavior;
        advance_congo_parts(s_root.bytes);
        CHECK(s_part_ai_calls[0] == 1);
        CHECK(s_part_post_calls[0] == 0);
        CHECK(D_8016DAB4_16E6B4 == s_root.bytes);
    }

    reset_fixture();
    bind_root();
    initialize_part(0, s_root.bytes);
    invoke_part_constructor_return(0);
    s_part_behavior[0] = PART_POST_REPLACES_CURRENT_TASK;
    advance_congo_parts(s_root.bytes);
    CHECK(s_part_ai_calls[0] == 1 && s_part_post_calls[0] == 1);
    CHECK(D_8016DAB4_16E6B4 == s_root.bytes);

    /* Entry outside the root's scheduler context cannot advance any part. */
    D_8016DAB4_16E6B4 = s_other_root.bytes;
    advance_congo_parts(s_root.bytes);
    CHECK(s_part_ai_calls[0] == 1 && s_part_post_calls[0] == 1);
    CHECK(D_8016DAB4_16E6B4 == s_other_root.bytes);
}

static void test_part_delegation_obeys_mocked_shared_enable_contract(void)
{
    bind_root();
    initialize_part(0, s_root.bytes);
    invoke_part_constructor_return(0);
    extra_options_capture_hyper_congo_actor(s_root.bytes);
    s_mock_run_before_ticks = 1;
    s_completed_replays = 3;
    extra_options_run_hyper_congo_tick();
    CHECK(s_config == 1);
    CHECK(s_part_ai_calls[0] == 0 && s_part_post_calls[0] == 0);
    CHECK(s_replay_messages == 0);
    s_config = 0;
    extra_options_run_hyper_congo_tick();
    CHECK(s_part_ai_calls[0] == 3 && s_part_post_calls[0] == 3);
    CHECK(s_before_tick == advance_congo_parts);
}

static void test_native_room_binding_and_transitions(void)
{
    /* Regression: the actual fight is room 0x016, not teleport room 0x01A. */
    bind_root();
    CHECK(s_runtime_room == 0x016);
    CHECK(s_track_messages == 1);
    CHECK(strstr(s_last_message, "room=0x016") != 0);
    CHECK(strstr(s_last_message, "disabled") != 0);
    extra_options_capture_hyper_congo_actor(s_root.bytes);
    CHECK(s_capture_task == s_root.bytes);
    CHECK(s_capture_predicate == hyper_congo_is_live);
    CHECK(s_capture_live);

    /* Binding is native-identity based; moving rooms first invalidates it. */
    D_800C7AB2 = 0x01A;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    CHECK(s_tracked_congo.task == 0);
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(hyper_congo_is_live(s_root.bytes));
    CHECK(s_runtime_room == 0x01A);

    s_save_loaded = 0;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    CHECK(!s_runtime_active);
    CHECK(s_tracked_congo.task == 0);
    s_save_loaded = 1;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(hyper_congo_is_live(s_root.bytes));
}

static void test_exact_root_identity_and_liveness(void)
{
    bind_root();
    initialize_task(s_other_root.bytes, s_object.bytes, 0x323,
                    ENTITY_CONGO, 7);
    CHECK(!hyper_congo_is_live(s_other_root.bytes));
    CHECK(!hyper_congo_is_live(0));

    TASK_GENERATION(s_root.bytes)++;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    TASK_GENERATION(s_root.bytes)--;
    set_pointer(s_root.bytes, 0x18, s_other_object.bytes);
    CHECK(!hyper_congo_is_live(s_root.bytes));
    set_pointer(s_root.bytes, 0x18, s_object.bytes);
    TASK_ACTOR_ID(s_root.bytes)++;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    TASK_ACTOR_ID(s_root.bytes)--;
    TASK_ENTITY_ID(s_root.bytes) = 0x324;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    TASK_ENTITY_ID(s_root.bytes) = ENTITY_CONGO;
    TASK_STATUS(s_root.bytes) |= TASK_STATUS_REMOVE_PENDING;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    TASK_STATUS(s_root.bytes) &= ~TASK_STATUS_REMOVE_PENDING;
    TASK_HEALTH(s_root.bytes) = 0;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    TASK_HEALTH(s_root.bytes) = 30;
    CHECK(hyper_congo_is_live(s_root.bytes));

    /* An unrelated A228 caller must not replace the tracked Congo root. */
    TASK_ENTITY_ID(s_other_root.bytes) = 0x324;
    extra_options_track_hyper_congo(s_other_root.bytes);
    CHECK(hyper_congo_is_live(s_root.bytes));
    extra_options_track_hyper_congo(0);
    CHECK(hyper_congo_is_live(s_root.bytes));
}

static void test_flame_constructor_ownership_and_guards(void)
{
    bind_root();
    initialize_flame(0, s_root.bytes, 0);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    /* Entry hook runs before DCC changes the entity to 0x7E. */
    extra_options_track_hyper_congo_flame(s_children[0].bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    func_08000DCC_6B406C(s_children[0].bytes, s_child_objects[0].bytes);
    CHECK(extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    CHECK(!extra_options_hyper_congo_child_is_live(0));

    initialize_flame(1, s_root.bytes, ENTITY_CONGO_FLAME);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[1].bytes));
    initialize_flame(2, s_root.bytes, ENTITY_CONGO);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[2].bytes));
    initialize_flame(3, s_other_root.bytes, ENTITY_CONGO_FLAME);
    extra_options_track_hyper_congo_flame(s_children[3].bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[3].bytes));
    set_pointer(s_children[3].bytes, 0xD0, s_root.bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[3].bytes));

    TASK_ENTITY_ID(s_children[0].bytes) = ENTITY_CONGO;
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    TASK_ENTITY_ID(s_children[0].bytes) = ENTITY_CONGO_FLAME;
    TASK_STATUS(s_children[0].bytes) |= TASK_STATUS_REMOVE_PENDING;
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    TASK_STATUS(s_children[0].bytes) &= ~TASK_STATUS_REMOVE_PENDING;
    TASK_GENERATION(s_children[0].bytes)++;
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    TASK_GENERATION(s_children[0].bytes)--;
    set_pointer(s_children[0].bytes, 0x18, s_other_object.bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    set_pointer(s_children[0].bytes, 0x18, s_child_objects[0].bytes);
    set_pointer(s_children[0].bytes, 0xD0, s_other_root.bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    set_pointer(s_children[0].bytes, 0xD0, s_root.bytes);
    CHECK(extra_options_hyper_congo_child_is_live(s_children[0].bytes));

    initialize_flame(4, s_root.bytes, ENTITY_CONGO_FLAME);
    set_pointer(s_children[4].bytes, 0x18, 0);
    extra_options_track_hyper_congo_flame(s_children[4].bytes);
    set_pointer(s_children[4].bytes, 0x18, s_child_objects[4].bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[4].bytes));
    extra_options_track_hyper_congo_flame(0);

    D_800C7AB2++;
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
}

static void test_flame_identity_ring(void)
{
    unsigned int index;

    bind_root();
    for (index = 0; index <= CONGO_FLAME_CAPACITY; index++)
    {
        initialize_flame(index, s_root.bytes, ENTITY_CONGO_FLAME);
        extra_options_track_hyper_congo_flame(s_children[index].bytes);
        CHECK(extra_options_hyper_congo_child_is_live(s_children[index].bytes));
    }
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
    CHECK(extra_options_hyper_congo_child_is_live(s_children[1].bytes));
}

static void test_defeat_stays_excluded_after_native_hp_reset(void)
{
    bind_root();
    initialize_flame(0, s_root.bytes, ENTITY_CONGO_FLAME);
    extra_options_track_hyper_congo_flame(s_children[0].bytes);
    TASK_HEALTH(s_root.bytes) = 0;
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(s_defeated);
    TASK_HEALTH(s_root.bytes) = 1; /* A228's native victory preparation. */
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(!hyper_congo_is_live(s_root.bytes));
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));

    /* A newly allocated/rebound boss is not stuck in the old defeat latch. */
    TASK_GENERATION(s_root.bytes)++;
    TASK_HEALTH(s_root.bytes) = 30;
    extra_options_track_hyper_congo(s_root.bytes);
    CHECK(!s_defeated);
    CHECK(hyper_congo_is_live(s_root.bytes));
    CHECK(!extra_options_hyper_congo_child_is_live(s_children[0].bytes));
}

static void check_spawn(unsigned int index, float z, unsigned int flame_flags)
{
    MockSpawn *spawn = &s_spawns[index];

    CHECK(spawn->child != 0);
    CHECK(spawn->owner == s_root.bytes);
    CHECK(spawn->initializer == func_08000DCC_6B406C);
    CHECK(spawn->task_kind == 10);
    CHECK(spawn->x == 0.0f);
    CHECK(spawn->y == 35.0f);
    CHECK(spawn->z == z);
    CHECK(spawn->flags == 0);
    CHECK(TASK_RESOURCE_ID(spawn->child) == 0x1D);
    CHECK(get_pointer(spawn->child, 0x2C) == s_resource.bytes);
    CHECK(get_pointer(spawn->child, 0xD0) == s_root.bytes);
    CHECK(TASK_CONGO_FLAGS(spawn->child) == flame_flags);
}

static void test_default_disabled_emitter_matches_native(void)
{
    unsigned short frame;

    bind_root();
    CHECK(s_config == 1);
    for (frame = 0; frame < 15; frame++)
    {
        set_world_frame(frame);
        s_ai_call = frame;
        func_0800A04C_6BD2EC(s_root.bytes);
        CHECK(get_world_frame() == frame);
    }
    CHECK(s_spawn_calls == 3);
    CHECK(s_resource_calls == 3);
    CHECK(!s_breath_clock_active);
    CHECK(s_spawns[0].world_frame == 0);
    CHECK(s_spawns[1].world_frame == 5);
    CHECK(s_spawns[2].world_frame == 10);
    check_spawn(0, 60.0f, 0x10);
    check_spawn(1, 55.0f, 0x20);
    check_spawn(2, 58.0f, 0x40);
}

static void test_enabled_four_ai_calls_have_fourfold_even_cadence(void)
{
    static const float offsets[] = {60.0f, 55.0f, 58.0f};
    static const unsigned int flags[] = {0x10, 0x20, 0x40};
    unsigned short frame;
    unsigned int tick;
    unsigned int index;

    bind_root();
    s_config = 0;
    for (frame = 0; frame < 15; frame++)
    {
        set_world_frame(frame);
        for (tick = 0; tick < 4; tick++)
        {
            s_ai_call = frame * 4u + tick;
            func_0800A04C_6BD2EC(s_root.bytes);
            CHECK(get_world_frame() == frame);
        }
    }
    CHECK(s_spawn_calls == 12); /* Disabled emitted three in 15 real frames. */
    CHECK(s_resource_calls == 12);
    CHECK(s_breath_clock_active);
    CHECK(s_breath_phase == 0);
    for (index = 0; index < s_spawn_calls; index++)
    {
        CHECK(s_spawns[index].ai_call == index * 5u);
        check_spawn(index, offsets[index % 3], flags[index % 3]);
    }
}

static void test_private_phase_seeding_toggle_and_allocation_failure(void)
{
    unsigned int tick;

    bind_root();
    s_config = 0;
    set_world_frame(7);
    for (tick = 0; tick < 4; tick++)
    {
        s_ai_call = tick;
        func_0800A04C_6BD2EC(s_root.bytes);
    }
    CHECK(s_spawn_calls == 1);
    CHECK(s_spawns[0].ai_call == 3);
    check_spawn(0, 58.0f, 0x40);
    CHECK(get_world_frame() == 7);

    s_config = 1;
    set_world_frame(16); /* Native phase one: no flame, reset private clock. */
    func_0800A04C_6BD2EC(s_root.bytes);
    CHECK(!s_breath_clock_active);
    s_config = 0;
    set_world_frame(15); /* A new breath clock must start at phase zero. */
    s_allocation_fails = 1;
    func_0800A04C_6BD2EC(s_root.bytes);
    CHECK(s_spawn_calls == 2);
    CHECK(s_spawns[1].child == 0);
    CHECK(s_resource_calls == 1);
    CHECK(s_breath_phase == 1);
    s_allocation_fails = 0;
    for (tick = 1; tick <= 5; tick++)
        func_0800A04C_6BD2EC(s_root.bytes);
    CHECK(s_spawn_calls == 3); /* No retry burst for the failed phase zero. */
    CHECK(s_resource_calls == 2);
    check_spawn(2, 55.0f, 0x20);
    CHECK(get_world_frame() == 15);
}

static void test_replay_diagnostic_requires_three_completed_ticks_once(void)
{
    bind_root();
    s_completed_replays = 0;
    extra_options_run_hyper_congo_tick();
    s_completed_replays = 1;
    extra_options_run_hyper_congo_tick();
    s_completed_replays = 2;
    extra_options_run_hyper_congo_tick();
    CHECK(s_replay_messages == 0);
    CHECK(s_replay_predicate == hyper_congo_is_live);
    CHECK(s_before_tick == advance_congo_parts);
    s_completed_replays = 3;
    extra_options_run_hyper_congo_tick();
    CHECK(s_replay_messages == 1);
    CHECK(strstr(s_last_message, "room=0x016") != 0);
    CHECK(strstr(s_last_message, "3 extra AI/movement/animation ticks (4x)") != 0);
    extra_options_run_hyper_congo_tick();
    s_completed_replays = 0; /* Nested guarded post reports no completion. */
    extra_options_run_hyper_congo_tick();
    CHECK(s_replay_messages == 1);

    D_800C7AB2 = 0x01A;
    CHECK(!hyper_congo_is_live(s_root.bytes));
    extra_options_track_hyper_congo(s_root.bytes);
    s_completed_replays = 3;
    extra_options_run_hyper_congo_tick();
    CHECK(s_replay_messages == 2);
}

int main(void)
{
    unsigned int tests_run = 0;

#define RUN_TEST(test)                                                       \
    do                                                                      \
    {                                                                       \
        reset_fixture();                                                    \
        test();                                                             \
        tests_run++;                                                        \
        printf("PASS %s\n", #test);                                       \
    } while (0)

    RUN_TEST(test_native_room_binding_and_transitions);
    RUN_TEST(test_exact_root_identity_and_liveness);
    RUN_TEST(test_flame_constructor_ownership_and_guards);
    RUN_TEST(test_flame_identity_ring);
    RUN_TEST(test_defeat_stays_excluded_after_native_hp_reset);
    RUN_TEST(test_part_tracking_and_callback_guards);
    RUN_TEST(test_part_interleave_preserves_each_root_signal);
    RUN_TEST(test_part_mutations_stop_post_and_restore_scheduler_context);
    RUN_TEST(test_part_delegation_obeys_mocked_shared_enable_contract);
    RUN_TEST(test_default_disabled_emitter_matches_native);
    RUN_TEST(test_enabled_four_ai_calls_have_fourfold_even_cadence);
    RUN_TEST(test_private_phase_seeding_toggle_and_allocation_failure);
    RUN_TEST(test_replay_diagnostic_requires_three_completed_ticks_once);
    printf("Passed %u Congo host regression tests. Shared replay is mocked.\n",
           tests_run);
    return EXIT_SUCCESS;
}
