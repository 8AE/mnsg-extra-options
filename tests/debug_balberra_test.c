/* Host contracts for the production debug hook, not gameplay certification.
 * xcrun clang -std=c11 -Wall -Wextra -Werror -I include \
 *   -fsanitize=undefined -DEXTRA_OPTIONS_DEBUG=1 tests/debug_balberra_test.c \
 *   -o /tmp/debug_balberra_test && /tmp/debug_balberra_test
 * Repeat with EXTRA_OPTIONS_DEBUG=0 to verify the normal-build behavior.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define __MODDING_H__
#define RECOMP_HOOK(name)
static void *test_pointer(void *base, unsigned int offset);
static unsigned short s_encounter;
#define DEBUG_BALBERRA_PTR(base, offset) test_pointer(base, offset)
#define DEBUG_IMPACT_ENCOUNTER s_encounter
#include "../src/debug.c"

typedef struct
{
    _Alignas(max_align_t) unsigned char bytes[0x100];
    void *object;
    void *hit;
} TestTask;

static _Alignas(max_align_t) unsigned char s_battle[0x300];
static unsigned char s_system;
static unsigned char s_hit[0x50];
static TestTask s_root, s_part;
static void *s_state_root;
static void *s_state_model;
void *D_8020EED0_63A2B0;
unsigned char *D_8015C5C8_15D1C8;

#define LOCAL_HP(task) (*(int *)((char *)(task) + 0xAC))
#define FLAGS(task) (*(unsigned int *)((char *)(task) + 0x64))
#define TASK_ID(task) (*(unsigned short *)((char *)(task) + 0x5C))
#define REACTION_TIMER(task) (*(int *)((char *)(task) + 0xB0))

static void *test_pointer(void *base, unsigned int offset)
{
    if (base == s_battle)
    {
        assert(offset == 0x1D8 || offset == 0x1E0);
        return offset == 0x1D8 ? s_state_root : s_state_model;
    }
    assert(base == &s_root || base == &s_part);
    TestTask *task = base;
    assert(offset == 0x18 || offset == 0x38);
    return offset == 0x18 ? task->object : task->hit;
}

/* The ROM's pure 801D36CC attack-class table. */
int func_801D36CC_5FEAAC(void *task)
{
    unsigned char *hit = test_pointer(task, 0x38);
    if (!hit)
        return 0;
    switch (hit[0x4C])
    {
    case 0x32: return 5;
    case 0x33: return 10;
    case 0x3C: return 30;
    case 0x3D: return 80;
    case 0x3E: return 10;
    case 0x46: return 100;
    case 0x50: return 70;
    case 0x51: return 20;
    case 0x5A: return 400;
    case 0x5B: return 100;
    default: return 0;
    }
}

static void run_debug_hook(void *task)
{
#if EXTRA_OPTIONS_DEBUG
    extra_options_debug_balberra_one_hit(task);
#else
    (void)task;
#endif
}

/* Native 8020451C control flow after hook entry: preserve its return class,
 * damage clamp, reaction timer, immunity check, and hit consumption. */
static unsigned int native_local_damage(TestTask *task)
{
    run_debug_hook(task);
    unsigned char *hit = task->hit;
    unsigned int attack = hit ? hit[0x4C] : 0;
    if (!hit || (FLAGS(task) & 0x1000) || attack == 0x64)
    {
        task->hit = NULL;
        return 0;
    }
    int damage = func_801D36CC_5FEAAC(task);
    if (damage > 0)
        REACTION_TIMER(task) = attack == 0x5A ? 200 : 40;
    LOCAL_HP(task) -= damage;
    if (LOCAL_HP(task) < 1)
        LOCAL_HP(task) = 0;
    task->hit = NULL;
    return attack;
}

static void reset(void)
{
    memset(s_battle, 0, sizeof(s_battle));
    memset(&s_root, 0, sizeof(s_root));
    memset(&s_part, 0, sizeof(s_part));
    memset(s_hit, 0, sizeof(s_hit));
    D_8020EED0_63A2B0 = s_battle;
    D_8015C5C8_15D1C8 = &s_system;
    s_state_root = &s_root;
    s_root.object = s_state_model = &s_system;
    s_part.object = &s_system;
    s_root.hit = s_part.hit = s_hit;
    s_encounter = 3;
    s_hit[0x4C] = 0x32;
    TASK_ID(&s_root) = 0x78;
    TASK_ID(&s_part) = 0x98;
    LOCAL_HP(&s_root) = DEBUG_IMPACT_BOSS_HP(s_battle) = 1000;
    LOCAL_HP(&s_part) = 150;
    *(int *)(s_battle + 0x68) = 500;
    *(int *)(s_battle + 0x6C) = 42;
}

static void test_all_hits(void)
{
    for (unsigned int attack = 0; attack <= 0xFF; attack++)
    {
        reset();
        s_hit[0x4C] = attack;
        int damage = func_801D36CC_5FEAAC(&s_root);
        unsigned int returned = native_local_damage(&s_root);
        assert(returned == (attack == 0x64 ? 0 : attack));
        assert(LOCAL_HP(&s_root) ==
               (EXTRA_OPTIONS_DEBUG && damage ? 0 : 1000 - damage));
        assert(!s_root.hit);
        assert(REACTION_TIMER(&s_root) ==
               (damage ? (attack == 0x5A ? 200 : 40) : 0));

        reset();
        s_hit[0x4C] = attack;
        unsigned char before[sizeof(s_battle)];
        memcpy(before, s_battle, sizeof(before));
        returned = native_local_damage(&s_part);
        assert(returned == (attack == 0x64 ? 0 : attack));
        int expected_part_hp = damage > 150 ? 0 : 150 - damage;
        assert(LOCAL_HP(&s_part) == expected_part_hp);
        assert(LOCAL_HP(&s_root) ==
               (EXTRA_OPTIONS_DEBUG && damage ? 0 : 1000));
        assert(!s_part.hit);
        /* No early HUD publication, player damage, phase flags or scripts. */
        assert(memcmp(before, s_battle, sizeof(before)) == 0);
    }
}

static void test_rejected_contexts(void)
{
    for (unsigned int rejection = 0; rejection < 12; rejection++)
    {
        reset();
        void *target = &s_part;
        switch (rejection)
        {
        case 0: D_8020EED0_63A2B0 = NULL; break;
        case 1: D_8015C5C8_15D1C8 = NULL; break;
        case 2: s_encounter = 2; break;
        case 3: TASK_ID(&s_root) = 0x64; break;
        case 4: DEBUG_IMPACT_COMBAT_PAUSED(s_battle) = 1; break;
        case 5: s_state_root = NULL; break;
        case 6: s_state_model = NULL; break;
        case 7: s_root.object = NULL; break;
        case 8: s_part.hit = NULL; break;
        case 9: FLAGS(&s_part) = 0x1000; break;
        case 10: LOCAL_HP(&s_part) = 0; break;
        case 11: target = NULL; break;
        }
        TestTask before_root = s_root, before_part = s_part;
        run_debug_hook(target);
        assert(memcmp(&before_root, &s_root, sizeof(s_root)) == 0);
        assert(memcmp(&before_part, &s_part, sizeof(s_part)) == 0);
    }
    reset();
    LOCAL_HP(&s_root) = 0;
    run_debug_hook(&s_part);
    assert(LOCAL_HP(&s_root) == 0 && LOCAL_HP(&s_part) == 150);
    reset();
    FLAGS(&s_root) = 0x1000; /* Initial armor protects body, not open parts. */
    assert(native_local_damage(&s_root) == 0);
    assert(LOCAL_HP(&s_root) == 1000);
    native_local_damage(&s_part);
    assert(LOCAL_HP(&s_root) == (EXTRA_OPTIONS_DEBUG ? 0 : 1000));
}

int main(void)
{
    test_all_hits();
    test_rejected_contexts();
    printf("Passed Balberra debug contracts: EXTRA_OPTIONS_DEBUG=%d (host only).\n",
           EXTRA_OPTIONS_DEBUG);
    return 0;
}
