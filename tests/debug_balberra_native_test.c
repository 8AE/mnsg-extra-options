/*
 * Native-backed regression for Balberra's debug one-hit hook.
 *
 * tests/debug_balberra_native_test.sh extracts the exact generated functions
 * named below from a local Goemon64Recomp checkout.  No generated game code is
 * copied into this repository.  This fixture then executes the generated
 * 8020451C damage helper and 8020407C body finalizer around the production
 * src/debug.c entry hook.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

/* Generated projects make these externally interposable.  This single-TU
 * fixture deliberately keeps its extracted copies private. */
#undef RECOMP_FUNC
#define RECOMP_FUNC static __attribute__((noinline))

#ifndef EXTRA_OPTIONS_DEBUG
#define EXTRA_OPTIONS_DEBUG 0
#endif

#define RDRAM_SIZE 0x800000u
#define ROOT_ADDR 0x80110000u
#define PART_ADDR 0x80111000u
#define MODEL_ADDR 0x80112000u
#define HIT_ADDR 0x80113000u
#define BATTLE_ADDR 0x80114000u
#define SCRATCH_ADDR 0x80115000u
#define STACK_ADDR 0x807FF000u
#define BATTLE_SLOT_ADDR 0x8020EED0u
#define SCRATCH_SLOT_ADDR 0x8020EF40u

static _Alignas(16) uint8_t s_rdram[RDRAM_SIZE];
static unsigned short s_encounter;

static gpr guest_gpr(uint32_t address)
{
    assert(address >= 0x80000000u);
    assert(address - 0x80000000u < RDRAM_SIZE);
    return (gpr)(int64_t)(int32_t)address;
}

static uint8_t *guest_host(uint32_t address)
{
    assert(address >= 0x80000000u);
    assert(address - 0x80000000u < RDRAM_SIZE);
    return s_rdram + (address - 0x80000000u);
}

static uint32_t host_guest(const void *pointer)
{
    ptrdiff_t offset = (const uint8_t *)pointer - s_rdram;

    assert(offset >= 0);
    assert((size_t)offset < RDRAM_SIZE);
    return 0x80000000u + (uint32_t)offset;
}

static int32_t *guest_word(uint32_t address, unsigned int offset)
{
    assert((offset & 3u) == 0);
    assert(address - 0x80000000u + offset + sizeof(int32_t) <= RDRAM_SIZE);
    return (int32_t *)(void *)(guest_host(address) + offset);
}

static uint8_t *guest_byte(uint32_t address, unsigned int offset)
{
    unsigned int native_offset = offset ^ 3u;

    assert(address - 0x80000000u + native_offset < RDRAM_SIZE);
    return guest_host(address) + native_offset;
}

static uint8_t read_guest_byte(uint32_t address, unsigned int offset)
{
    return *guest_byte(address, offset);
}

static void write_guest_byte(uint32_t address, unsigned int offset,
                             uint8_t value)
{
    *guest_byte(address, offset) = value;
}

static void write_guest_pointer(uint32_t address, unsigned int offset,
                                uint32_t target)
{
    *guest_word(address, offset) = (int32_t)target;
}

#if EXTRA_OPTIONS_DEBUG
static void *debug_guest_pointer(void *base, unsigned int offset)
{
    uint32_t address;

    memcpy(&address, (uint8_t *)base + offset, sizeof(address));
    if (address == 0)
        return NULL;
    return guest_host(address);
}
#endif

#define __MODDING_H__
#define RECOMP_HOOK(name)
#if EXTRA_OPTIONS_DEBUG
#define DEBUG_BALBERRA_PTR(base, offset) debug_guest_pointer(base, offset)
#endif
#define DEBUG_IMPACT_ENCOUNTER s_encounter
#include "../src/debug.c"

void *D_8020EED0_63A2B0;
unsigned char *D_8015C5C8_15D1C8;

#define TRACE_ENTRY()
#define TRACE_RETURN()

void switch_error(const char *function, uint32_t vram, uint32_t table)
{
    fprintf(stderr, "unexpected generated switch in %s at %08X (%08X)\n",
            function, vram, table);
    abort();
}

/* The generated pure attack-class lookup used by both the production hook
 * bridge and the generated 8020451C helper. */
#define func_801D36CC_5FEAAC native_func_801D36CC_5FEAAC
#include "native_801D36CC.inc"
#undef func_801D36CC_5FEAAC

static recomp_context new_context(void)
{
    recomp_context context;

    memset(&context, 0, sizeof(context));
    context.r29 = guest_gpr(STACK_ADDR);
    return context;
}

int func_801D36CC_5FEAAC(void *task)
{
    recomp_context context = new_context();

    context.r4 = guest_gpr(host_guest(task));
    native_func_801D36CC_5FEAAC(s_rdram, &context);
    return (int)(int32_t)context.r2;
}

#define func_801D3894_5FEC74 native_func_801D3894_5FEC74
#include "native_801D3894.inc"
#undef func_801D3894_5FEC74

#define func_801D36CC_5FEAAC native_func_801D36CC_5FEAAC
#define func_801D3894_5FEC74 native_func_801D3894_5FEC74
#define func_8020451C_62F8FC native_func_8020451C_62F8FC
#include "native_8020451C.inc"
#undef func_8020451C_62F8FC
#undef func_801D3894_5FEC74
#undef func_801D36CC_5FEAAC

static void hooked_func_8020451C_62F8FC(uint8_t *rdram,
                                        recomp_context *context)
{
#if EXTRA_OPTIONS_DEBUG
    extra_options_debug_balberra_one_hit(
        guest_host((uint32_t)context->r4));
#endif
    native_func_8020451C_62F8FC(rdram, context);
}

/* These three generated leaf functions select Balberra's native collision
 * flags before its body damage finalizer runs. */
#define func_802045E8_62F9C8 native_func_802045E8_62F9C8
#include "native_802045E8.inc"
#undef func_802045E8_62F9C8

#define func_802045F4_62F9D4 native_func_802045F4_62F9D4
#include "native_802045F4.inc"
#undef func_802045F4_62F9D4

#define func_80204600_62F9E0 native_func_80204600_62F9E0
#include "native_80204600.inc"
#undef func_80204600_62F9E0

static unsigned int s_set_current_calls;
static uint32_t s_set_current_callback;
static unsigned int s_set_task_calls;
static uint32_t s_set_task_target;
static uint32_t s_set_task_callback;
static unsigned int s_effect_calls;

static void stub_func_8003521C_35E1C(uint8_t *rdram,
                                     recomp_context *context)
{
    (void)rdram;
    s_set_current_calls++;
    s_set_current_callback = (uint32_t)context->r4;
    context->r2 = 0;
}

static void stub_func_8003522C_35E2C(uint8_t *rdram,
                                     recomp_context *context)
{
    (void)rdram;
    s_set_task_calls++;
    s_set_task_target = (uint32_t)context->r4;
    s_set_task_callback = (uint32_t)context->r5;
    context->r2 = 0;
}

static void stub_func_801E1958_60CD38(uint8_t *rdram,
                                      recomp_context *context)
{
    (void)rdram;
    s_effect_calls++;
    /* A null effect makes the generated finalizer skip only the optional
     * cosmetic initializer while retaining all defeat control flow. */
    context->r2 = 0;
}

static void stub_func_801D5F34_601314(uint8_t *rdram,
                                      recomp_context *context)
{
    (void)rdram;
    (void)context;
    assert(!"null effect stubs must bypass the cosmetic initializer");
}

#define func_8003521C_35E1C stub_func_8003521C_35E1C
#define func_8003522C_35E2C stub_func_8003522C_35E2C
#define func_801D5F34_601314 stub_func_801D5F34_601314
#define func_801E1958_60CD38 stub_func_801E1958_60CD38
#define func_8020451C_62F8FC hooked_func_8020451C_62F8FC
#define func_802045E8_62F9C8 native_func_802045E8_62F9C8
#define func_802045F4_62F9D4 native_func_802045F4_62F9D4
#define func_80204600_62F9E0 native_func_80204600_62F9E0
#define func_8020407C_62F45C native_func_8020407C_62F45C
#include "native_8020407C.inc"
#undef func_8020407C_62F45C
#undef func_80204600_62F9E0
#undef func_802045F4_62F9D4
#undef func_802045E8_62F9C8
#undef func_8020451C_62F8FC
#undef func_801E1958_60CD38
#undef func_801D5F34_601314
#undef func_8003522C_35E2C
#undef func_8003521C_35E1C

static void reset_fixture(uint8_t attack)
{
    memset(s_rdram, 0, sizeof(s_rdram));
    s_encounter = 3;
    D_8020EED0_63A2B0 = guest_host(BATTLE_ADDR);
    D_8015C5C8_15D1C8 = s_rdram;

    write_guest_pointer(BATTLE_SLOT_ADDR, 0, BATTLE_ADDR);
    write_guest_pointer(SCRATCH_SLOT_ADDR, 0, SCRATCH_ADDR);
    write_guest_pointer(BATTLE_ADDR, 0x1D8, ROOT_ADDR);
    write_guest_pointer(BATTLE_ADDR, 0x1E0, MODEL_ADDR);
    write_guest_pointer(ROOT_ADDR, 0x18, MODEL_ADDR);
    write_guest_pointer(ROOT_ADDR, 0x38, HIT_ADDR);
    write_guest_pointer(PART_ADDR, 0x18, MODEL_ADDR);
    write_guest_pointer(PART_ADDR, 0x38, HIT_ADDR);

    /* debug.c reads the ID as a host halfword.  8020407C itself does not
     * inspect this field, so no N64 halfword-lane alias is needed here. */
    *(uint16_t *)(void *)(guest_host(ROOT_ADDR) + 0x5C) = 0x78;
    *(uint16_t *)(void *)(guest_host(PART_ADDR) + 0x5C) = 0x98;
    *guest_word(ROOT_ADDR, 0xAC) = 1000;
    *guest_word(PART_ADDR, 0xAC) = 150;
    *guest_word(BATTLE_ADDR, 0x60) = 1000;
    write_guest_byte(HIT_ADDR, 0x4C, attack);

    s_set_current_calls = 0;
    s_set_current_callback = 0;
    s_set_task_calls = 0;
    s_set_task_target = 0;
    s_set_task_callback = 0;
    s_effect_calls = 0;
}

static int run_native_body_finalizer(void)
{
    recomp_context context = new_context();

    context.r4 = guest_gpr(ROOT_ADDR);
    native_func_8020407C_62F45C(s_rdram, &context);
    return (int)(int32_t)context.r2;
}

static unsigned int run_native_local_damage(uint32_t task_address)
{
    recomp_context context = new_context();

    context.r4 = guest_gpr(task_address);
    hooked_func_8020451C_62F8FC(s_rdram, &context);
    return (unsigned int)(uint32_t)context.r2;
}

static void test_ordinary_body_hit_uses_native_defeat(void)
{
    reset_fixture(0x32);

    int defeated = run_native_body_finalizer();
#if EXTRA_OPTIONS_DEBUG
    assert(defeated == 1);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 0);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 0);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 0x50);
    assert(s_set_task_calls == 1);
    assert(s_set_task_target == ROOT_ADDR);
    assert(s_set_task_callback == 0x80201AD4u);
    assert(s_effect_calls == 5);
#else
    assert(defeated == 0);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 995);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 995);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 0);
    assert(s_set_task_calls == 0);
    assert(s_effect_calls == 0);
#endif
    assert(*guest_word(ROOT_ADDR, 0x38) == 0);
    assert(*guest_word(ROOT_ADDR, 0xB0) == 40);
}

static void test_special_hit_keeps_native_reaction_priority(void)
{
    reset_fixture(0x5A);

    int defeated = run_native_body_finalizer();
#if EXTRA_OPTIONS_DEBUG
    assert(defeated == 0);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 0);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 0);
    assert(s_set_current_calls == 1);
    assert(s_set_current_callback == 0x802007D4u);
    assert(s_set_task_calls == 0);
    assert(*guest_word(ROOT_ADDR, 0xB0) == 200);

    assert(run_native_body_finalizer() == 1);
    assert(s_set_task_calls == 1);
    assert(s_set_task_callback == 0x80201AD4u);
#else
    assert(defeated == 0);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 600);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 600);
    assert(s_set_current_calls == 1);
    assert(s_set_current_callback == 0x802007D4u);
    assert(s_set_task_calls == 0);
#endif
}

static void test_scratch_reaction_defers_but_does_not_cancel_defeat(void)
{
    reset_fixture(0x32);
    write_guest_byte(SCRATCH_ADDR, 0x4, 1);

    assert(run_native_body_finalizer() == 0);
    assert(s_set_current_calls == 1);
    assert(s_set_current_callback == 0x802006BCu);
    assert(read_guest_byte(SCRATCH_ADDR, 0x4) == 0x51);
#if EXTRA_OPTIONS_DEBUG
    assert(*guest_word(ROOT_ADDR, 0xAC) == 0);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 1000);
    assert(run_native_body_finalizer() == 1);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 0);
    assert(s_set_task_calls == 1);
#else
    assert(*guest_word(ROOT_ADDR, 0xAC) == 995);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 1000);
    assert(run_native_body_finalizer() == 0);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 995);
    assert(s_set_task_calls == 0);
#endif
}

static void test_part_hit_preserves_part_damage_then_defeats_body(void)
{
    reset_fixture(0x32);
    /* Only the struck component owns this hit record in the native task
     * topology; the body finalizer sees no independent hit this frame. */
    write_guest_pointer(ROOT_ADDR, 0x38, 0);

    assert(run_native_local_damage(PART_ADDR) == 0x32);
    assert(*guest_word(PART_ADDR, 0xAC) == 145);
    assert(*guest_word(PART_ADDR, 0xB0) == 40);
    assert(*guest_word(PART_ADDR, 0x38) == 0);
#if EXTRA_OPTIONS_DEBUG
    assert(*guest_word(ROOT_ADDR, 0xAC) == 0);
    assert(run_native_body_finalizer() == 1);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 0);
    assert(s_set_task_calls == 1);
#else
    assert(*guest_word(ROOT_ADDR, 0xAC) == 1000);
    assert(run_native_body_finalizer() == 0);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 1000);
    assert(s_set_task_calls == 0);
#endif
}

static void test_native_rejections_do_not_trigger_debug_defeat(void)
{
    reset_fixture(0x64); /* Native block class: clear hit, zero damage. */
    assert(run_native_body_finalizer() == 0);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 1000);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 1000);
    assert(*guest_word(ROOT_ADDR, 0x38) == 0);
    assert(s_set_task_calls == 0);

    reset_fixture(0x32);
    /* scratch +6 == 3 makes native 80204600 select flags 0x1042 before
     * 8020451C, so both the production hook and native damage reject it. */
    write_guest_byte(SCRATCH_ADDR, 0x6, 3);
    assert(run_native_body_finalizer() == 0);
    assert(*guest_word(ROOT_ADDR, 0x64) == 0x1042);
    assert(*guest_word(ROOT_ADDR, 0xAC) == 1000);
    assert(*guest_word(BATTLE_ADDR, 0x60) == 1000);
    assert(*guest_word(ROOT_ADDR, 0x38) == 0);
    assert(s_set_task_calls == 0);
}

int main(void)
{
    test_ordinary_body_hit_uses_native_defeat();
    test_special_hit_keeps_native_reaction_priority();
    test_scratch_reaction_defers_but_does_not_cancel_defeat();
    test_part_hit_preserves_part_damage_then_defeats_body();
    test_native_rejections_do_not_trigger_debug_defeat();
    printf("Passed native Balberra damage/finalizer regression: "
           "EXTRA_OPTIONS_DEBUG=%d.\n", EXTRA_OPTIONS_DEBUG);
    return 0;
}
