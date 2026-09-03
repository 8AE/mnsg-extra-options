#include "modding.h"
#include "extra_options.h"
#include "hyper_enemies.h"

/*
 * Room 0x155 is Koryuta's flight.  Its actor overlay owns a wave controller
 * plus two dynamically allocated enemy children:
 *
 *   entity 0x0FA - the winged bomb/Robot Bee
 *   entity 0x12D - the diving Flying Dragon Head
 *
 * The children inherit actor 0x1B0 from the root or 0x1B5/0x1B6 from the two
 * standalone generators, so the ordinary actor-ID roster cannot identify
 * them.  Track only their three native constructors and admit only their
 * recurring approach callbacks to Hyper's common replay.  The generators
 * themselves stay native so Hyper never changes how often enemies spawn.
 */

#define KORYUTA_ROOM 0x0155u
#define KORYUTA_ENEMY_CAPACITY 8u

#define ENTITY_ROBOT_BEE 0x00FAu
#define ENTITY_FLYING_DRAGON_HEAD 0x012Du

#define ACTOR_KORYUTA_ROOT 0x01B0u
#define ACTOR_KORYUTA_WAVE_CONTROLLER 0x01B5u
#define ACTOR_KORYUTA_BEE_SPAWNER 0x01B6u

#define KORYUTA_FAMILY_DRAGON_HEAD 1u
#define KORYUTA_FAMILY_WAVE_BEE 2u
#define KORYUTA_FAMILY_ATTACK_BEE 3u

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

#define TASK_STATUS_REMOVE_PENDING 0x00000002u

typedef void (*HyperKoryutaTaskCallback)(void *task, void *object);

/* Host tests replace target-ABI callback slots and overlay scratch globals. */
#ifndef KORYUTA_TASK_AI
#define KORYUTA_TASK_AI(task) \
    (*(HyperKoryutaTaskCallback volatile *)((char *)(task) + 0x0C))
#endif
#ifndef KORYUTA_TASK_POST
#define KORYUTA_TASK_POST(task) \
    (*(HyperKoryutaTaskCallback volatile *)((char *)(task) + 0x10))
#endif

typedef struct
{
    void *task;
    void *object;
    unsigned short actor_id;
    unsigned char generation;
    unsigned char family;
} HyperKoryutaIdentity;

extern unsigned short D_800C7AB2;
extern void *D_8016DAB4_16E6B4;
extern void func_80218F30_5D4400(void *task, void *object);
extern void func_08003B4C_704C4C(void *task, void *object);
extern void func_08003C90_704D90(void *task, void *object);
extern void func_08003D64_704E64(void *task, void *object);
extern void func_08003E60_704F60(void *task, void *object);
extern void func_08003F68_705068(void *task, void *object);
extern void func_08004034_705134(void *task, void *object);
extern void func_080044BC_7055BC(void *task, void *object);
extern void func_080045B8_7056B8(void *task, void *object);
extern void func_08004660_705760(void *task, void *object);

static HyperKoryutaIdentity s_enemies[KORYUTA_ENEMY_CAPACITY];
static HyperKoryutaIdentity s_pending_enemy;
static unsigned int s_next_enemy;
static unsigned short s_runtime_room;
static unsigned short s_pending_entity;
static unsigned char s_runtime_active;
static unsigned char s_pending_valid;
static unsigned char s_pending_family;

static void clear_koryuta_tracking(void)
{
    unsigned int index;

    s_pending_enemy.task = 0;
    s_pending_entity = 0;
    s_pending_family = 0;
    s_pending_valid = 0;
    s_next_enemy = 0;
    for (index = 0; index < KORYUTA_ENEMY_CAPACITY; index++)
        s_enemies[index].task = 0;
}

static int refresh_koryuta_runtime_state(void)
{
    if (!extra_options_save_is_loaded())
    {
        if (s_runtime_active)
            clear_koryuta_tracking();
        s_runtime_active = 0;
        return 0;
    }

    if (!s_runtime_active || s_runtime_room != D_800C7AB2)
    {
        clear_koryuta_tracking();
        s_runtime_room = D_800C7AB2;
        s_runtime_active = 1;
    }
    return 1;
}

static int identity_matches(const HyperKoryutaIdentity *identity,
                            void *task)
{
    return identity->task == task && task &&
           identity->object == TASK_OBJECT(task) &&
           identity->actor_id == TASK_ACTOR_ID(task) &&
           identity->generation == TASK_GENERATION(task);
}

static void bind_identity(HyperKoryutaIdentity *identity, void *task)
{
    identity->task = task;
    identity->object = TASK_OBJECT(task);
    identity->actor_id = TASK_ACTOR_ID(task);
    identity->generation = TASK_GENERATION(task);
}

static int is_recurring_koryuta_enemy_callback(
    void *task, unsigned char family)
{
    HyperKoryutaTaskCallback callback = KORYUTA_TASK_AI(task);

    if (family == KORYUTA_FAMILY_WAVE_BEE)
    {
        return TASK_ENTITY_ID(task) == ENTITY_ROBOT_BEE &&
               (callback == func_08003E60_704F60 ||
                callback == func_08003F68_705068 ||
                callback == func_08004034_705134);
    }

    if (family == KORYUTA_FAMILY_ATTACK_BEE)
    {
        return TASK_ENTITY_ID(task) == ENTITY_ROBOT_BEE &&
               (callback == func_080045B8_7056B8 ||
                callback == func_08004660_705760);
    }

    return family == KORYUTA_FAMILY_DRAGON_HEAD &&
           TASK_ENTITY_ID(task) == ENTITY_FLYING_DRAGON_HEAD &&
           callback == func_08003C90_704D90;
}

static int actor_matches_koryuta_family(void *task,
                                        unsigned char family)
{
    unsigned short actor_id = TASK_ACTOR_ID(task);

    if (family == KORYUTA_FAMILY_ATTACK_BEE)
        return actor_id == ACTOR_KORYUTA_BEE_SPAWNER;

    return (family == KORYUTA_FAMILY_WAVE_BEE ||
            family == KORYUTA_FAMILY_DRAGON_HEAD) &&
           (actor_id == ACTOR_KORYUTA_ROOT ||
            actor_id == ACTOR_KORYUTA_WAVE_CONTROLLER);
}

static void register_pending_koryuta_enemy(void)
{
    HyperKoryutaIdentity identity;
    unsigned short expected_entity;
    unsigned char family;
    unsigned int index;
    unsigned int empty_index = KORYUTA_ENEMY_CAPACITY;

    if (!s_pending_valid)
        return;

    identity = s_pending_enemy;
    expected_entity = s_pending_entity;
    family = s_pending_family;
    s_pending_valid = 0;
    s_pending_enemy.task = 0;
    s_pending_entity = 0;
    s_pending_family = 0;
    if (!refresh_koryuta_runtime_state() ||
        D_800C7AB2 != KORYUTA_ROOM ||
        !identity_matches(&identity, identity.task) ||
        TASK_ENTITY_ID(identity.task) != expected_entity ||
        !actor_matches_koryuta_family(identity.task, family) ||
        (TASK_STATUS(identity.task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        KORYUTA_TASK_POST(identity.task) != func_80218F30_5D4400 ||
        !is_recurring_koryuta_enemy_callback(identity.task, family))
    {
        return;
    }

    for (index = 0; index < KORYUTA_ENEMY_CAPACITY; index++)
    {
        if (s_enemies[index].task == identity.task)
        {
            s_enemies[index] = identity;
            return;
        }
        if (!s_enemies[index].task && empty_index == KORYUTA_ENEMY_CAPACITY)
            empty_index = index;
    }

    if (empty_index != KORYUTA_ENEMY_CAPACITY)
        s_enemies[empty_index] = identity;
    else
        s_enemies[s_next_enemy] = identity;

    s_next_enemy++;
    if (s_next_enemy == KORYUTA_ENEMY_CAPACITY)
        s_next_enemy = 0;
}

static void capture_koryuta_enemy(void *task,
                                  unsigned short expected_entity,
                                  unsigned char family)
{
    s_pending_valid = 0;
    s_pending_enemy.task = 0;
    s_pending_entity = 0;
    s_pending_family = 0;
    if (!task || task != D_8016DAB4_16E6B4 || !TASK_OBJECT(task) ||
        !refresh_koryuta_runtime_state() || D_800C7AB2 != KORYUTA_ROOM ||
        !actor_matches_koryuta_family(task, family) ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        KORYUTA_TASK_POST(task) != func_80218F30_5D4400)
    {
        return;
    }

    bind_identity(&s_pending_enemy, task);
    s_pending_enemy.family = family;
    s_pending_entity = expected_entity;
    s_pending_family = family;
    s_pending_valid = 1;
}

RECOMP_HOOK("func_08003B4C_704C4C")
void extra_options_capture_hyper_koryuta_dragon_head(void *task)
{
    capture_koryuta_enemy(task, ENTITY_FLYING_DRAGON_HEAD,
                          KORYUTA_FAMILY_DRAGON_HEAD);
}

RECOMP_HOOK_RETURN("func_08003B4C_704C4C")
void extra_options_track_hyper_koryuta_dragon_head(void)
{
    register_pending_koryuta_enemy();
}

RECOMP_HOOK("func_08003D64_704E64")
void extra_options_capture_hyper_koryuta_robot_bee(void *task)
{
    capture_koryuta_enemy(task, ENTITY_ROBOT_BEE,
                          KORYUTA_FAMILY_WAVE_BEE);
}

RECOMP_HOOK_RETURN("func_08003D64_704E64")
void extra_options_track_hyper_koryuta_robot_bee(void)
{
    register_pending_koryuta_enemy();
}

/* Standalone actor 0x1B6 uses a second Robot Bee constructor with a distinct
 * two-state flight loop.  It shares the room's active-child counter but not
 * the mixed-wave constructor. */
RECOMP_HOOK("func_080044BC_7055BC")
void extra_options_capture_hyper_koryuta_attack_bee(void *task)
{
    capture_koryuta_enemy(task, ENTITY_ROBOT_BEE,
                          KORYUTA_FAMILY_ATTACK_BEE);
}

RECOMP_HOOK_RETURN("func_080044BC_7055BC")
void extra_options_track_hyper_koryuta_attack_bee(void)
{
    register_pending_koryuta_enemy();
}

int extra_options_hyper_koryuta_enemy_is_live(void *task)
{
    unsigned int index;

    if (!task || !refresh_koryuta_runtime_state() ||
        D_800C7AB2 != KORYUTA_ROOM ||
        (TASK_STATUS(task) & TASK_STATUS_REMOVE_PENDING) != 0 ||
        KORYUTA_TASK_POST(task) != func_80218F30_5D4400)
    {
        return 0;
    }

    for (index = 0; index < KORYUTA_ENEMY_CAPACITY; index++)
        if (identity_matches(&s_enemies[index], task) &&
            is_recurring_koryuta_enemy_callback(
                task, s_enemies[index].family))
        {
            return 1;
        }
    return 0;
}
