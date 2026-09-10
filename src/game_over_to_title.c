#include "modding.h"
#include "recompconfig.h"

/* func_80002F54_3B54 is the native game-over step handler. Its first substep
 * only marks the state; the player's "goodbye" choice runs the native
 * func_8000383C_443C() teardown followed by func_80003728_4328(3), which
 * returns to the title sequence.
 *
 * Redirect the goodbye branch as soon as the game-over step is dispatched so
 * the player never sees the continue/goodbye menu. The hook runs after the
 * step handler on the main-step dispatcher context, so the teardown executes
 * from the same context the native menu exit uses. On the next frame the
 * dispatcher starts the title step with a cleared substep. */

extern void func_8000383C_443C(void);
extern void func_80003728_4328(int step);

#define EXTRA_OPTIONS_TITLE_STEP 3

static int game_over_to_title_is_enabled(void)
{
    /* Enabled is the first enum entry in mod.toml, so its value is zero. */
    return recomp_get_config_u32("game_over_to_title") == 0;
}

RECOMP_HOOK_RETURN("func_80002F54_3B54")
void extra_options_game_over_to_title_hook(void)
{
    if (!game_over_to_title_is_enabled())
        return;

    func_8000383C_443C();
    func_80003728_4328(EXTRA_OPTIONS_TITLE_STEP);
}
