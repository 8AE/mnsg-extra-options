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
extern unsigned char *D_8015C5C8_15D1C8;

/* file_13's shared boss-damage helper has the native signature
 *   int func_801D3954_5FED34(int damage);
 * It subtracts damage from battle +0x60, clamps HP to zero, and returns HP.
 * Verified against USA ROM 5FED34..5FED5C (VRAM 801D3954..801D397C).
 * Kashiwagi, Taisamba, and D'Etoile call it after their hit/block checks.
 * Balberra uses task-local HP instead and is handled separately below.
 * Player damage uses a different helper.
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

#define DEBUG_BALBERRA_LOCAL_HP(task) \
    (*(volatile signed int *)((char *)(task) + 0xAC))
#define DEBUG_BALBERRA_FLAGS(task) \
    (*(volatile unsigned int *)((char *)(task) + 0x64))
#define DEBUG_BALBERRA_ID(task) \
    (*(volatile unsigned short *)((char *)(task) + 0x5C))

/* Pointer slots are overridable for the 64-bit host regression fixture. */
#ifndef DEBUG_BALBERRA_PTR
#define DEBUG_BALBERRA_PTR(base, offset) \
    (*(void *volatile *)((char *)(base) + (offset)))
#endif
#ifndef DEBUG_IMPACT_ENCOUNTER
#define DEBUG_IMPACT_ENCOUNTER \
    (*(volatile unsigned short *)(D_8015C5C8_15D1C8 + 0x3ADF4))
#endif

extern int func_801D36CC_5FEAAC(void *task);

/* file_13 ROM 62F8FC..62F9C8: 8020451C accepts a hit at task +38 unless
 * flags +64 contain 0x1000, then subtracts 801D36CC(task) from local +AC.
 * 801D36CC is a read-only attack-class lookup (including zero for blocks).
 * Balberra's body is ID 78, encounter 3. Its 8020407C finalizer mirrors
 * local HP to battle +60 and selects the native 80201AD4 defeat sequence.
 * The only callers of 8020451C are that body finalizer and part finalizer
 * 802043E4 (from 80202264/02598/02EA4/03154/03424). This exact overlay hook
 * establishes body/part context without an ordinary-room or save-HP gate.
 *
 * A valid hit on ANY owned vulnerable part defeats the boss, not merely
 * that part. Keep normal part damage, hit reactions, and destruction flags;
 * only body HP becomes zero. Do not publish HUD HP or force callbacks: the
 * next native body pass handles death and gives special-hit reactions their
 * usual priority. The subsequent D'Etoile form changes body ID to 64 and
 * continues to use the shared damage hook above.
 */
RECOMP_HOOK("func_8020451C_62F8FC")
void extra_options_debug_balberra_one_hit(void *task)
{
    void *state;
    void *root;

    if (!task || !D_8015C5C8_15D1C8 || DEBUG_IMPACT_ENCOUNTER != 3)
        return;
    state = D_8020EED0_63A2B0;
    if (!state || DEBUG_IMPACT_COMBAT_PAUSED(state))
        return;
    root = DEBUG_BALBERRA_PTR(state, 0x1D8);
    if (!root || DEBUG_BALBERRA_ID(root) != 0x78 ||
        !DEBUG_BALBERRA_PTR(root, 0x18) ||
        !DEBUG_BALBERRA_PTR(state, 0x1E0) ||
        DEBUG_BALBERRA_LOCAL_HP(root) <= 0)
        return;
    if (DEBUG_BALBERRA_LOCAL_HP(task) <= 0 ||
        !DEBUG_BALBERRA_PTR(task, 0x38) ||
        (DEBUG_BALBERRA_FLAGS(task) & 0x1000u) ||
        func_801D36CC_5FEAAC(task) <= 0)
        return;

    DEBUG_BALBERRA_LOCAL_HP(root) = task == root ? 1 : 0;
}
#endif
