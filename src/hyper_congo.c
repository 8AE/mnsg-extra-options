#include "modding.h"
#include "extra_options.h"
#include "hyper_enemies.h"
#include "recompconfig.h"
#include "recomputils.h"

/* Congo's actor initializer replaces the common func_80218F30 task +0x10
 * post callback with func_0800000C.  Keep his exact root and liveness
 * checks here while reusing Hyper Enemies' single guarded replay loop. */

#define TASK_OBJECT(task) \
    (*(void *volatile *)((char *)(task) + 0x18))
#define TASK_ACTOR_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5C))
#define TASK_ENTITY_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5E))
#define TASK_STATUS(task) \
    (*(volatile unsigned int *)((char *)(task) + 0x68))
#define TASK_GENERATION(task) \
    (*(volatile unsigned char *)((char *)(task) + 0x74))
#define TASK_HEALTH(task) \
    (*(volatile unsigned char *)((char *)(task) + 0x8D))
#define TASK_OWNER(task) \
    (*(void *volatile *)((char *)(task) + 0xD0))
#define TASK_PART_OWNER(task) \
    (*(void *volatile *)((char *)(task) + 0xDC))
#define TASK_CONGO_FLAGS(task) \
    (*(volatile unsigned int *)((char *)(task) + 0xE8))
#define TASK_RESOURCE_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x28))
#define TASK_RESOURCE(task) \
    (*(void *volatile *)((char *)(task) + 0x2C))

#define TASK_STATUS_REMOVE_PENDING 0x00000002u

#define ENTITY_CONGO 0x0323u
#define ENTITY_CONGO_FLAME 0x007Eu
#define CONGO_FLAME_CAPACITY 32u
#define CONGO_HEALTH_CONTROLLER 0x04000000u
#define CONGO_PART_COUNT 6u
#define TASK_CALLBACK_DISABLED_BIT 0x00800000u

typedef void (*HyperCongoTaskCallback)(void *task, void *object);

/* Host tests substitute this callback ABI: slots are four bytes apart, and
 * the disabled bit belongs to game addresses, not 64-bit host addresses. */
#ifndef CONGO_TASK_AI
#define CONGO_TASK_AI(task) \
    (*(HyperCongoTaskCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef CONGO_TASK_POST
#define CONGO_TASK_POST(task) \
    (*(HyperCongoTaskCallback volatile *)((char *)(task) + 0x10))
#endif
#ifndef CONGO_CALLBACK_IS_ENABLED
#define CONGO_CALLBACK_IS_ENABLED(callback) \
    ((callback) && (((unsigned long)(callback) & TASK_CALLBACK_DISABLED_BIT) == 0))
#endif

typedef struct
{
    void *task;
    void *object;
    unsigned short actor_id;
    unsigned char generation;
    unsigned char padding;
} HyperCongoIdentity;

extern unsigned short D_800C7AB2;
extern unsigned char *D_8015C5C8_15D1C8;
extern void *D_8016DAB4_16E6B4;
extern void *func_8021DDE8_5D92B8(
    void *owner, HyperCongoTaskCallback initializer, unsigned char task_kind,
    float x, float y, float z, int angle);
extern void *func_800141C4_14DC4(unsigned int file_id);
extern void func_08000DCC_6B406C(void *task, void *object);

static HyperCongoIdentity s_tracked_congo;
static unsigned short s_runtime_room;
static unsigned char s_runtime_active;
static unsigned char s_replay_reported;
static unsigned char s_defeated;
static unsigned char s_breath_clock_active;
static unsigned char s_breath_phase;
static HyperCongoIdentity s_flames[CONGO_FLAME_CAPACITY];
static unsigned int s_next_flame;
static HyperCongoIdentity s_parts[CONGO_PART_COUNT];

static void clear_congo_tracking(void)
{
    unsigned int index;

    s_tracked_congo.task = 0;
    s_replay_reported = 0;
    s_defeated = 0;
    s_breath_clock_active = 0;
    s_next_flame = 0;
    for (index = 0; index < CONGO_FLAME_CAPACITY; index++)
        s_flames[index].task = 0;
    for (index = 0; index < CONGO_PART_COUNT; index++)
        s_parts[index].task = 0;
}

static int refresh_congo_runtime_state(void)
{
    if (!extra_options_save_is_loaded())
    {
        if (s_runtime_active)
            clear_congo_tracking();
        s_runtime_active = 0;
        return 0;
    }

    if (!s_runtime_active || s_runtime_room != D_800C7AB2)
    {
        clear_congo_tracking();
        s_runtime_room = D_800C7AB2;
        s_runtime_active = 1;
    }
    return 1;
}

static int identity_matches(const HyperCongoIdentity *identity, void *task)
{
    return identity->task == task && task &&
           identity->object == TASK_OBJECT(task) &&
           identity->actor_id == TASK_ACTOR_ID(task) &&
           identity->generation == TASK_GENERATION(task);
}

static void bind_identity(HyperCongoIdentity *identity, void *task)
{
    identity->task = task;
    identity->object = TASK_OBJECT(task);
    identity->actor_id = TASK_ACTOR_ID(task);
    identity->generation = TASK_GENERATION(task);
}

static int hyper_congo_is_live(void *task)
{
    /* A228 plus entity 0x323 identifies the native boss.  Bind his actual
     * room and invalidate on transitions instead of guessing it from a
     * debug teleport destination (0x01A precedes the fight in room 0x016). */
    return refresh_congo_runtime_state() && task &&
           !s_defeated && identity_matches(&s_tracked_congo, task) &&
           TASK_ENTITY_ID(task) == ENTITY_CONGO &&
           (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) == 0 &&
           TASK_HEALTH(task) > 0;
}

/* A228 is Congo's exact health-bearing root callback.  His animation parts
 * and flames are tracked separately through their own native constructors. */
RECOMP_HOOK("func_0800A228_6BD4C8")
void extra_options_track_hyper_congo(void *actor)
{
    void *object;

    if (!actor || TASK_ENTITY_ID(actor) != ENTITY_CONGO ||
        !refresh_congo_runtime_state())
    {
        return;
    }

    object = TASK_OBJECT(actor);
    if (!object)
        return;

    if (!identity_matches(&s_tracked_congo, actor))
    {
        clear_congo_tracking();
        recomp_printf("[Extra Options] Congo tracked: room=0x%03X, Hyper Enemies=%s.\n",
                      (unsigned int)D_800C7AB2,
                      recomp_get_config_u32("hyper_enemies") == 0
                          ? "enabled" : "disabled");
    }

    bind_identity(&s_tracked_congo, actor);

    /* A228 restores HP to one before scheduling the victory sequence.
     * Remember its actual zero-HP branch so that sequence stays native. */
    if ((TASK_CONGO_FLAGS(actor) & CONGO_HEALTH_CONTROLLER) &&
        TASK_HEALTH(actor) == 0)
    {
        s_defeated = 1;
    }
}

static void track_congo_part(unsigned int part)
{
    void *task = D_8016DAB4_16E6B4;

    if (!task || !hyper_congo_is_live(TASK_PART_OWNER(task)) ||
        !TASK_OBJECT(task) || TASK_OBJECT(task) == s_tracked_congo.object)
    {
        return;
    }
    bind_identity(&s_parts[part], task);
}

/* Register only after each constructor installs its recurring AI.  All six
 * have a separate animated object and a root link at +0xDC, not +0xD0. */
#define TRACK_CONGO_PART(function, part) \
    RECOMP_HOOK_RETURN(function) \
    void extra_options_track_congo_part_##part(void) { track_congo_part(part); }

TRACK_CONGO_PART("func_080066B0_6B9950", 0)
TRACK_CONGO_PART("func_080068B8_6B9B58", 1)
TRACK_CONGO_PART("func_08006AC0_6B9D60", 2)
TRACK_CONGO_PART("func_08006CC8_6B9F68", 3)
TRACK_CONGO_PART("func_08006ED0_6BA170", 4)
TRACK_CONGO_PART("func_080070D8_6BA378", 5)

static int congo_callback_is_enabled(HyperCongoTaskCallback callback)
{
    return CONGO_CALLBACK_IS_ENABLED(callback);
}

static int congo_part_matches(const HyperCongoIdentity *identity, void *root)
{
    return identity_matches(identity, identity->task) &&
           TASK_PART_OWNER(identity->task) == root &&
           (TASK_STATUS(identity->task) & TASK_STATUS_REMOVE_PENDING) == 0;
}

static void advance_congo_parts(void *root)
{
    unsigned int part;
    unsigned short room = D_800C7AB2;

    if (D_8016DAB4_16E6B4 != root || !hyper_congo_is_live(root))
        return;

    /* The native scheduler runs these six parts after their root.  Consume
     * each preceding root tick before the next extra AI can clear the
     * one-tick animation signals (6/7/8).  The normal scheduler supplies the
     * fourth part update after the final extra root tick.  The outer shared
     * guard prevents their common post hooks from recursively replaying. */
    for (part = 0; part < CONGO_PART_COUNT; part++)
    {
        HyperCongoIdentity identity = s_parts[part];
        HyperCongoTaskCallback ai;
        HyperCongoTaskCallback post;

        if (D_800C7AB2 != room || !hyper_congo_is_live(root))
            break;
        if (!congo_part_matches(&identity, root))
            continue;

        ai = CONGO_TASK_AI(identity.task);
        post = CONGO_TASK_POST(identity.task);
        if (!congo_callback_is_enabled(ai) || !congo_callback_is_enabled(post))
            continue;

        D_8016DAB4_16E6B4 = identity.task;
        ai(identity.task, identity.object);
        if (D_800C7AB2 == room && D_8016DAB4_16E6B4 == identity.task &&
            congo_part_matches(&identity, root) &&
            CONGO_TASK_POST(identity.task) == post)
        {
            post(identity.task, identity.object);
        }
        D_8016DAB4_16E6B4 = root;
    }
}

/* DCC is the native flame constructor, not one of Congo's model children.
 * It sets entity 0x7E before the first common post pass.  Retain exact task
 * identities so unrelated projectiles or recycled task slots cannot match. */
RECOMP_HOOK("func_08000DCC_6B406C")
void extra_options_track_hyper_congo_flame(void *task)
{
    if (!task || !hyper_congo_is_live(TASK_OWNER(task)) || !TASK_OBJECT(task))
        return;

    bind_identity(&s_flames[s_next_flame], task);
    s_next_flame = (s_next_flame + 1) % CONGO_FLAME_CAPACITY;
}

int extra_options_hyper_congo_child_is_live(void *task)
{
    unsigned int index;

    if (!task || TASK_ENTITY_ID(task) != ENTITY_CONGO_FLAME ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        !hyper_congo_is_live(TASK_OWNER(task)))
    {
        return 0;
    }

    for (index = 0; index < CONGO_FLAME_CAPACITY; index++)
    {
        if (identity_matches(&s_flames[index], task))
            return 1;
    }
    return 0;
}

/* The native emitter uses global frame % 15 at phases 0, 5 and 10.
 * Repeating it on the same real frame would stack identical flames.  Give
 * only the live Congo root a private phase, advancing once per AI call;
 * the four simulated calls then preserve the native three-flame sequence
 * at 4x cadence without changing the world's clock. */
RECOMP_PATCH void func_0800A04C_6BD2EC(void *task)
{
    unsigned int phase = *(volatile unsigned short *)(
        D_8015C5C8_15D1C8 + 0x3ADCE) % 15u;
    unsigned int flame_flags;
    float forward_offset;
    void *child;

    if (recomp_get_config_u32("hyper_enemies") == 0 &&
        hyper_congo_is_live(task))
    {
        if (!s_breath_clock_active)
        {
            s_breath_phase = (unsigned char)phase;
            s_breath_clock_active = 1;
        }
        phase = s_breath_phase;
        s_breath_phase = (unsigned char)((phase + 1) % 15u);
    }
    else
    {
        s_breath_clock_active = 0;
    }

    switch (phase)
    {
    case 0:
        forward_offset = 60.0f;
        flame_flags = 0x10;
        break;
    case 5:
        forward_offset = 55.0f;
        flame_flags = 0x20;
        break;
    case 10:
        forward_offset = 58.0f;
        flame_flags = 0x40;
        break;
    default:
        return;
    }

    child = func_8021DDE8_5D92B8(
        task, func_08000DCC_6B406C, 10, 0.0f, 35.0f, forward_offset, 0);
    if (!child)
        return;

    TASK_RESOURCE_ID(child) = 0x1D;
    TASK_RESOURCE(child) = func_800141C4_14DC4(0x1D);
    TASK_OWNER(child) = task;
    TASK_CONGO_FLAGS(child) = flame_flags;
}

RECOMP_HOOK("func_0800000C_6B32AC")
void extra_options_capture_hyper_congo_actor(void *task)
{
    extra_options_hyper_capture_from_post(task, hyper_congo_is_live);
}

RECOMP_HOOK_RETURN("func_0800000C_6B32AC")
void extra_options_run_hyper_congo_tick(void)
{
    unsigned int ticks =
        extra_options_hyper_run_captured_tick(hyper_congo_is_live,
                                             advance_congo_parts);

    /* Report actual completed replays, not merely that a hook was entered.
     * Nested post hooks return zero under the shared replay guard. */
    if (ticks == 3 && !s_replay_reported)
    {
        s_replay_reported = 1;
        recomp_printf("[Extra Options] Hyper Congo active: room=0x%03X, "
                      "3 extra AI/movement/animation ticks (4x).\n",
                      (unsigned int)D_800C7AB2);
    }
}
