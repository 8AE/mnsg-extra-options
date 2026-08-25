#include "modding.h"
#include "extra_options.h"
#include "recompconfig.h"

#define CURRENT_HP_OFFSET (-0x21)
#define CURRENT_HP_READ() \
    (*(volatile unsigned char *)((char *)D_8015C608_15D208 + CURRENT_HP_OFFSET))
#define CURRENT_HP_WRITE(value) \
    (*(volatile unsigned char *)((char *)D_8015C608_15D208 + CURRENT_HP_OFFSET) = \
         (unsigned char)(value))
#define CURRENT_CHARACTER_READ() \
    ((*(volatile unsigned int *)0x8015C5DC) & 0xFFu)

static unsigned char s_previous_hp;
static unsigned int s_previous_character;
static int s_hp_tracking_initialized;

static int no_hit_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("no_hit") == 0;
}

static void reset_hp_tracking(void)
{
    s_previous_hp = 0;
    s_previous_character = 0;
    s_hp_tracking_initialized = 0;
}

RECOMP_HOOK_RETURN("func_80002040_2C40")
void extra_options_no_hit_frame_hook(void)
{
    unsigned char current_hp;
    unsigned int current_character;

    if (!extra_options_save_is_loaded())
    {
        reset_hp_tracking();
        return;
    }

    current_hp = CURRENT_HP_READ();
    current_character = CURRENT_CHARACTER_READ();

    if (!s_hp_tracking_initialized ||
        current_character != s_previous_character)
    {
        s_previous_hp = current_hp;
        s_previous_character = current_character;
        s_hp_tracking_initialized = 1;
        return;
    }

    if (no_hit_is_enabled() &&
        current_hp < s_previous_hp && current_hp > 0)
    {
        CURRENT_HP_WRITE(0);
        current_hp = 0;
    }

    s_previous_hp = current_hp;
    s_previous_character = current_character;
}
