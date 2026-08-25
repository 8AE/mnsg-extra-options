#include "modding.h"
#include "extra_options.h"
#include "recompconfig.h"

#define CURRENT_HP_OFFSET (-0x21)
#define CURRENT_HP_READ() \
    (*(volatile unsigned char *)((char *)D_8015C608_15D208 + CURRENT_HP_OFFSET))

#define RYO_LOSS_PER_HIT (-50)
#define RYO_POPUP_FILE_ID 0x3B
#define RYO_POPUP_MODEL_ID 0x2C4
#define RYO_POPUP_TASK_KIND 8
#define RYO_POPUP_LIFETIME 20

#define TASK_WRITE16(task, offset, value) \
    (*(volatile unsigned short *)((char *)(task) + (offset)) = \
         (unsigned short)(value))
#define TASK_READ16(task, offset) \
    (*(volatile unsigned short *)((char *)(task) + (offset)))
#define TASK_WRITE32(task, offset, value) \
    (*(volatile unsigned int *)((char *)(task) + (offset)) = \
         (unsigned int)(value))

typedef struct
{
    unsigned char padding[8];
    float x;
    float y;
    float z;
} ExtraOptionsRyoPopupObject;

extern void *D_801FC604_5B8514;
extern ExtraOptionsRyoPopupObject *D_801FC60C_5B851C;
extern unsigned char D_8020CBF0_5C8B00[];

extern int func_801DCCF0_598C00(short delta);
extern void *func_80013B14_14714(unsigned int file_id);
extern void *func_800141C4_14DC4(unsigned int file_id);
extern void *func_802171A8_5D2678(
    void *owner, void (*initializer)(void *, void *), unsigned char task_kind);
extern void func_80216E1C_5D22EC(void *task, int mode);
extern void func_8021A310_5D57E0(void *task);
extern void func_8003521C_35E1C(void (*update)(void *, void *));
extern void func_80034ED4_35AD4(void);
extern void func_8000F420_10020(unsigned short sound_id, void *sound_state,
                                void *object, float radius);

static int loose_ryo_on_hit_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("loose_ryo_on_hit") == 0;
}

static int is_rdram_pointer(const void *pointer)
{
    unsigned int address = (unsigned int)(unsigned long)pointer;
    unsigned int physical = address & 0x1FFFFFFFu;

    return physical >= 0x1000u && physical < 0x800000u;
}

static int is_loaded_resource(const void *resource)
{
    return resource &&
           resource != (const void *)(unsigned long)0xFFFFFFFFu;
}

static void extra_options_ryo_popup_update(void *task, void *object_pointer)
{
    ExtraOptionsRyoPopupObject *object =
        (ExtraOptionsRyoPopupObject *)object_pointer;
    unsigned short timer;

    if (!is_rdram_pointer(task) || !is_rdram_pointer(object))
    {
        func_80034ED4_35AD4();
        return;
    }

    object->y += 1.0f;
    timer = TASK_READ16(task, 0x8A);
    TASK_WRITE16(task, 0x8A, timer - 1);
    if (timer == 0)
        func_80034ED4_35AD4();
}

static void extra_options_ryo_popup_initialize(void *task, void *object_pointer)
{
    ExtraOptionsRyoPopupObject *object =
        (ExtraOptionsRyoPopupObject *)object_pointer;
    ExtraOptionsRyoPopupObject *player_object = D_801FC60C_5B851C;

    if (!is_rdram_pointer(task) || !is_rdram_pointer(object) ||
        !is_rdram_pointer(player_object))
    {
        func_80034ED4_35AD4();
        return;
    }

    /* This is the native file_59 setup for the floating "-50 Ryo" object. */
    TASK_WRITE32(task, 0x60, 0x220);
    TASK_WRITE16(task, 0x5E, RYO_POPUP_MODEL_ID);
    func_80216E1C_5D22EC(task, 2);
    TASK_WRITE16(task, 0x8A, RYO_POPUP_LIFETIME);
    func_8021A310_5D57E0(task);

    object->x = player_object->x;
    object->y = player_object->y + 20.0f;
    object->z = player_object->z;
    func_8003521C_35E1C(extra_options_ryo_popup_update);
}

static void spawn_ryo_loss_popup(void)
{
    void *owner = D_801FC604_5B8514;
    ExtraOptionsRyoPopupObject *player_object = D_801FC60C_5B851C;
    void *resource;
    void *popup_task;

    if (!is_rdram_pointer(owner) || !is_rdram_pointer(player_object))
        return;

    resource = func_800141C4_14DC4(RYO_POPUP_FILE_ID);
    if (!is_loaded_resource(resource))
        return;

    func_8000F420_10020(0x2B2, D_8020CBF0_5C8B00,
                        player_object, 400.0f);

    popup_task = func_802171A8_5D2678(
        owner, extra_options_ryo_popup_initialize, RYO_POPUP_TASK_KIND);
    if (!is_rdram_pointer(popup_task))
        return;

    /* The initializer runs on the next task update, after these are set. */
    TASK_WRITE16(popup_task, 0x28, RYO_POPUP_FILE_ID);
    TASK_WRITE32(popup_task, 0x2C, resource);
}

/* Normal stage resources have finished loading when this hook runs. Load the
 * small native robbery-effect file into the same scene registry it expects. */
RECOMP_HOOK_RETURN("func_8020D6BC_5C8B8C")
void extra_options_load_ryo_popup_resource(void)
{
    void *resource;

    if (!loose_ryo_on_hit_is_enabled())
        return;

    resource = func_800141C4_14DC4(RYO_POPUP_FILE_ID);
    if (!is_loaded_resource(resource))
        func_80013B14_14714(RYO_POPUP_FILE_ID);
}

/* Ghidra shows every applied player-damage path passing a negative signed
 * delta through this HP adjuster. Healing is positive and is ignored. */
RECOMP_HOOK("func_801DCD48_598C58")
void extra_options_loose_ryo_on_hit_hook(signed char hp_delta)
{
    if (hp_delta >= 0 || !loose_ryo_on_hit_is_enabled() ||
        !extra_options_save_is_loaded() || CURRENT_HP_READ() == 0)
    {
        return;
    }

    func_801DCCF0_598C00(RYO_LOSS_PER_HIT);
    spawn_ryo_loss_popup();
}
