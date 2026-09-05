#ifndef EXTRA_OPTIONS_DEBUG
#define EXTRA_OPTIONS_DEBUG 0
#endif

#if EXTRA_OPTIONS_DEBUG != 0 && EXTRA_OPTIONS_DEBUG != 1
#error EXTRA_OPTIONS_DEBUG must be 0 or 1
#endif

#define DEBUG_IMPACT_BOSS_HP(state) \
    (*(volatile signed int *)((char *)(state) + 0x60))
#define DEBUG_IMPACT_COMBAT_PAUSED(state) \
    (*(volatile unsigned char *)((char *)(state) + 0x2C0))

#if EXTRA_OPTIONS_DEBUG
#include "modding.h"

extern void *D_8020EED0_63A2B0;

/* file_13's shared boss-damage helper has the native signature
 *   int func_801D3954_5FED34(int damage);
 * It subtracts damage from battle +0x60, clamps HP to zero, and returns HP.
 * Verified against USA ROM 5FED34..5FED5C (VRAM 801D3954..801D397C).
 * Kashiwagi, Thaisamba, and the later Impact boss dispatchers call it only
 * after their hit/block checks. Player damage uses a different helper.
 *
 * Change HP on entry, not on return: callers must receive native zero HP
 * immediately to take their normal defeat branches. No death callback,
 * progression flag, animation, or task deletion is forced by this flag.
 * The normal build contains neither this hook nor its native state import.
 */
RECOMP_HOOK("func_801D3954_5FED34")
void extra_options_debug_impact_boss_one_hit(int damage)
{
    void *state;

    if (damage <= 0)
        return;

    /* This exact overlay hook establishes Impact context; no ordinary-save
     * HP gate is needed, including when entering from title-menu boss rush. */
    state = D_8020EED0_63A2B0;
    if (!state || DEBUG_IMPACT_COMBAT_PAUSED(state) ||
        DEBUG_IMPACT_BOSS_HP(state) <= 0)
        return;

    DEBUG_IMPACT_BOSS_HP(state) = 1;
}
#endif
