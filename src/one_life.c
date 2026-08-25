#include "modding.h"
#include "extra_options.h"
#include "recompconfig.h"

#define SAVE_READ32(off) \
    (*(volatile signed int *)((char *)D_8015C608_15D208 + (off)))
#define SAVE_WRITE32(off, value) \
    (*(volatile signed int *)((char *)D_8015C608_15D208 + (off)) = \
         (signed int)(value))

#define CURRENT_HP_OFFSET (-0x21)
#define RUNTIME_LIVES_OFFSET (-0x1C)
#define SAVE_LIVES_OFFSET 0x78

#define CURRENT_HP_READ() \
    (*(volatile unsigned char *)((char *)D_8015C608_15D208 + CURRENT_HP_OFFSET))

static int one_life_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("one_life") == 0;
}

RECOMP_HOOK_RETURN("func_80002040_2C40")
void extra_options_one_life_frame_hook(void)
{
    if (!extra_options_save_is_loaded() ||
        !one_life_is_enabled() || CURRENT_HP_READ() == 0)
    {
        return;
    }

    if (SAVE_READ32(RUNTIME_LIVES_OFFSET) != 1 ||
        SAVE_READ32(SAVE_LIVES_OFFSET) != 1)
    {
        /* The game decrements the runtime value on death and game-overs when
         * it reaches zero. Mirror the save copy so reloads retain the rule. */
        SAVE_WRITE32(RUNTIME_LIVES_OFFSET, 1);
        SAVE_WRITE32(SAVE_LIVES_OFFSET, 1);
    }
}
