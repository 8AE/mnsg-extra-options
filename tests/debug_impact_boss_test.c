/* Host contract tests, not native gameplay certification.
 * xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *   -fsanitize=undefined -DEXTRA_OPTIONS_DEBUG=1 tests/debug_impact_boss_test.c \
 *   -o /tmp/debug_impact_boss_test && /tmp/debug_impact_boss_test
 * Repeat with -DEXTRA_OPTIONS_DEBUG=0 for the normal-build contract.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define __MODDING_H__
#define RECOMP_HOOK(name)

#include "../src/debug.c"

void *D_8020EED0_63A2B0;
static _Alignas(max_align_t) unsigned char s_battle[0x300];

static void run_debug_hook(int damage)
{
#if EXTRA_OPTIONS_DEBUG
    extra_options_debug_impact_boss_one_hit(damage);
#else
    /* No hook exists in the normal build. */
    (void)damage;
#endif
}

/* Reproduce the verified native subtraction/clamp/return after the entry
 * hook. These small test inputs do not overflow signed arithmetic. */
static int native_damage_with_hook(int damage)
{
    int hp;
    run_debug_hook(damage);
    hp = DEBUG_IMPACT_BOSS_HP(s_battle) - damage;
    if (hp <= 0)
        hp = 0;
    DEBUG_IMPACT_BOSS_HP(s_battle) = hp;
    return hp;
}

static void reset_battle(void)
{
    memset(s_battle, 0, sizeof(s_battle));
    D_8020EED0_63A2B0 = s_battle;
    DEBUG_IMPACT_BOSS_HP(s_battle) = 2000;
    *(int *)(s_battle + 0x68) = 500; /* Player Impact HP. */
    *(int *)(s_battle + 0x6C) = 42;  /* Separate battle gauge. */
}

int main(void)
{
    const int damaging_hits[] = {1, 5, 10, 20, 30, 70, 80, 100, 400, 3000};
    unsigned int i;
    unsigned char expected[sizeof(s_battle)];

    for (i = 0; i < sizeof(damaging_hits) / sizeof(damaging_hits[0]); i++)
    {
        reset_battle();
        memcpy(expected, s_battle, sizeof(expected));
        int remaining = EXTRA_OPTIONS_DEBUG ? 0 : 2000 - damaging_hits[i];
        if (remaining < 0)
            remaining = 0;
        memcpy(expected + 0x60, &remaining, sizeof(remaining));
        assert(native_damage_with_hook(damaging_hits[i]) == remaining);
        /* No player HP, gauge, pause, or other battle state changes. */
        assert(memcmp(expected, s_battle, sizeof(expected)) == 0);
    }

    reset_battle();
    assert(native_damage_with_hook(0) == 2000);
    run_debug_hook(-10);
    assert(DEBUG_IMPACT_BOSS_HP(s_battle) == 2000);

    reset_battle();
    DEBUG_IMPACT_COMBAT_PAUSED(s_battle) = 1;
    assert(native_damage_with_hook(5) == 1995);

    reset_battle();
    DEBUG_IMPACT_BOSS_HP(s_battle) = 0;
    run_debug_hook(5);
    assert(DEBUG_IMPACT_BOSS_HP(s_battle) == 0);
    DEBUG_IMPACT_BOSS_HP(s_battle) = -1;
    run_debug_hook(5);
    assert(DEBUG_IMPACT_BOSS_HP(s_battle) == -1);

    D_8020EED0_63A2B0 = NULL;
    run_debug_hook(5); /* No stale pointer/cache. */

    printf("Passed Impact boss tests: EXTRA_OPTIONS_DEBUG=%d (host only).\n",
           EXTRA_OPTIONS_DEBUG);
    return 0;
}
