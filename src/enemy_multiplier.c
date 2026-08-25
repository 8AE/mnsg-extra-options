#include "modding.h"
#include "extra_options.h"
#include "recompconfig.h"

/* Enemy duplication is configured directly through the mod menu. */

typedef struct
{
    short x;
    short y;
    short z;
} ExtraOptionsVec3s;

typedef struct
{
    ExtraOptionsVec3s position;
    ExtraOptionsVec3s rotation;
    void *definition;
    unsigned char is_spawned;
    unsigned char unk_11;
    unsigned char unk_12;
    unsigned char unk_13;
} ExtraOptionsActorInstance;

typedef struct
{
    unsigned short actor_id;
    unsigned short params;
    unsigned int data[3];
} ExtraOptionsActorDefinition;

extern int *func_80219CA0_5D5170(int *task, unsigned char actor_type);
extern int func_8003555C_3615C(int *owner, void *entry, int arg2, int flags,
                              int x, int y, int z, short pitch, short yaw,
                              short roll, float scale_x, float scale_y,
                              float scale_z, short arg13, short arg14,
                              short model_id);
extern void func_80218A54_5D3F24(int actor,
                                ExtraOptionsActorInstance *instance);

extern void *D_802287BC_5E3C8C[];
extern unsigned char D_802297D6_5E4CA6[];
extern float D_80239AFC_5F4FCC;
extern ExtraOptionsActorInstance *D_8015CDDC;
extern ExtraOptionsActorDefinition *D_8015CDE0;

#define ACTOR_CAP_TOTAL (*(volatile int *)0x8015CCFC)
#define ACTOR_CAP_KIND_8 (*(volatile int *)0x8015CCE0)
#define ACTOR_CAP_KIND_9 (*(volatile int *)0x8015CCF0)
#define ACTOR_CAP_KIND_10 (*(volatile int *)0x8015CCE4)
#define ACTOR_CAP_KIND_6 (*(volatile int *)0x8015CCE8)
#define ACTOR_CAP_KIND_12 (*(volatile int *)0x8015CCEC)
#define DUPLICATE_RANDOM_RADIUS 50

static int s_duplicate_enemy_guard;
static unsigned int s_duplicate_position_rng = 0xA341316Cu;

static int configured_enemy_multiplier(void)
{
    unsigned long option = recomp_get_config_u32("enemy_multiplier");

    if (option > 2)
        return 1;
    return (int)option + 1;
}

static void raise_enemy_actor_caps(void)
{
    if (ACTOR_CAP_TOTAL < 0x78)
        ACTOR_CAP_TOTAL = 0x78;
    if (ACTOR_CAP_KIND_8 < 0x40)
        ACTOR_CAP_KIND_8 = 0x40;
    if (ACTOR_CAP_KIND_9 < 0x40)
        ACTOR_CAP_KIND_9 = 0x40;
    if (ACTOR_CAP_KIND_10 < 0x30)
        ACTOR_CAP_KIND_10 = 0x30;
    if (ACTOR_CAP_KIND_6 < 0x40)
        ACTOR_CAP_KIND_6 = 0x40;
    if (ACTOR_CAP_KIND_12 < 0x10)
        ACTOR_CAP_KIND_12 = 0x10;
}

static int is_enemy_actor_id(unsigned short actor_id)
{
    switch (actor_id)
    {
    case 0x0CB: /* Ghost Robot Tsurami */
    case 0x0FA: /* Robot Bee */
    case 0x0FB: /* Pink Robot FB */
    case 0x0FC: /* Pink Robot FC */
    case 0x0FD: /* Green Robot FD */
    case 0x0FE: /* Green Robot FE */
    case 0x0FF: /* Big Swatting Robot */
    case 0x100: /* Blue Robot */
    case 0x103: /* Scarecrow Bot */
    case 0x104: /* Can-can Legs */
    case 0x105: /* Flatfish */
    case 0x106: /* Helicopter Kite */
    case 0x107: /* Bamboo Shooter */
    case 0x108: /* Jet Robot */
    case 0x109: /* Yellow Robot */
    case 0x10A: /* Big Swatting Robot Red */
    case 0x10B: /* Shrinking Robot */
    case 0x10C: /* Sword Robot */
    case 0x110: /* Pink Robot 110 */
    case 0x12C: /* Small Cannon */
    case 0x12D: /* Flying Dragon Head */
    case 0x12E: /* Seahorse */
    case 0x12F: /* Spiny Sea Urchin */
    case 0x130: /* Drum Robot */
    case 0x131: /* Triangle Robot */
    case 0x133: /* Paper Ghost */
    case 0x136: /* Red Eye Fish */
    case 0x13A: /* Tank */
    case 0x13B: /* Spiny Sea Urchin (Kii-Awaji) */
    case 0x13C: /* Giant Enemy Crab */
    case 0x13D: /* Samurai */
    case 0x13E: /* Rose Robot */
    case 0x13F: /* Hammer Thrower */
    case 0x140: /* Fire Stalker */
    case 0x141: /* Ninja */
    case 0x144: /* Burrowing Robot */
    case 0x145: /* Kite */
    case 0x147: /* Bomber Bird */
    case 0x148: /* Water Snake */
    case 0x190: /* Flying Tile */
    case 0x191: /* Unknown in vanilla export */
    case 0x198: /* Spike Chain */
    case 0x19A: /* Falling Barrel */
    case 0x19D: /* Slicer */
    case 0x1A6: /* Fox Mask */
    case 0x1A9: /* Falling Bomb Block */
    case 0x1AA: /* Jump Rope */
    case 0x1B0: /* Mind-Control Robot */
    case 0x1B6: /* Robot Bee Spawner */
    case 0x2F6: /* Chef */
    case 0x334: /* Sushi Spawner */
    case 0x362: /* Unknown in vanilla export */
    case 0x365: /* Giant Spinning Top */
    case 0x3CA: /* Spike Floor */
    case 0x3D1: /* Floating Ingredients */
    case 0x3DA: /* Boulder */
    case 0x3EF: /* Spiny Urchin Spawner */
    case 0x3FC: /* Floor Flamethrower */
        return 1;
    default:
        return 0;
    }
}

static short offset_coord(short value, short offset)
{
    int result = (int)value + (int)offset;

    if (result > 32767)
        return 32767;
    if (result < -32768)
        return -32768;
    return (short)result;
}

static unsigned int next_duplicate_random(void)
{
    s_duplicate_position_rng =
        s_duplicate_position_rng * 1664525u + 1013904223u;
    return s_duplicate_position_rng;
}

static short random_axis_offset(void)
{
    return (short)((int)(next_duplicate_random() %
                         (DUPLICATE_RANDOM_RADIUS * 2 + 1)) -
                   DUPLICATE_RANDOM_RADIUS);
}

static void random_duplicate_offsets(short *x_offset, short *z_offset)
{
    int attempt;
    int x;
    int z;

    for (attempt = 0; attempt < 8; attempt++)
    {
        x = random_axis_offset();
        z = random_axis_offset();
        if ((x != 0 || z != 0) &&
            x * x + z * z <=
                DUPLICATE_RANDOM_RADIUS * DUPLICATE_RANDOM_RADIUS)
        {
            *x_offset = (short)x;
            *z_offset = (short)z;
            return;
        }
    }

    *x_offset = (short)((int)(next_duplicate_random() %
                              (DUPLICATE_RANDOM_RADIUS * 2 + 1)) -
                        DUPLICATE_RANDOM_RADIUS);
    *z_offset = 0;
    if (*x_offset == 0)
        *x_offset = DUPLICATE_RANDOM_RADIUS;
}

RECOMP_HOOK("func_80218A54_5D3F24")
void extra_options_enemy_multiplier_spawn_hook(
    int actor, ExtraOptionsActorInstance *instance)
{
    ExtraOptionsActorInstance duplicate_instance;
    ExtraOptionsActorDefinition *definition;
    unsigned short actor_id;
    unsigned char actor_type;
    short model_id;
    int *owner;
    int duplicate;
    int multiplier;
    int duplicate_index;
    short base_x;
    short base_z;
    short x_offset;
    short z_offset;
    float scale;

    multiplier = configured_enemy_multiplier();
    if (s_duplicate_enemy_guard || multiplier <= 1 ||
        !extra_options_save_is_loaded() || !instance)
        return;

    definition = D_8015CDE0;
    if (D_8015CDDC != instance || !definition)
        return;

    actor_id = definition->actor_id;
    if (!is_enemy_actor_id(actor_id))
        return;

    raise_enemy_actor_caps();
    actor_type = D_802297D6_5E4CA6[actor_id + 0x80D];
    model_id = *(short *)&D_802297D6_5E4CA6[actor_id * 2];
    scale = D_80239AFC_5F4FCC;
    base_x = instance->position.x;
    base_z = instance->position.z;

    for (duplicate_index = 1; duplicate_index < multiplier;
         duplicate_index++)
    {
        owner = func_80219CA0_5D5170(0, actor_type);
        if (!owner)
            return;

        duplicate = func_8003555C_3615C(
            owner, D_802287BC_5E3C8C[actor_id], 0, 0xC006D920,
            0, 0, 0, 0, 0, 0, scale, scale, scale, 0, 0, model_id);
        if (!duplicate)
            return;

        duplicate_instance = *instance;
        random_duplicate_offsets(&x_offset, &z_offset);
        duplicate_instance.position.x = offset_coord(base_x, x_offset);
        duplicate_instance.position.z = offset_coord(base_z, z_offset);
        base_x = duplicate_instance.position.x;
        base_z = duplicate_instance.position.z;

        s_duplicate_enemy_guard = 1;
        func_80218A54_5D3F24(duplicate, &duplicate_instance);
        s_duplicate_enemy_guard = 0;
    }
}
