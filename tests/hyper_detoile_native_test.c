/*
 * Native-backed smoke test for D'Etoile's 4x replay contract.
 *
 * hyper_detoile_native_test.sh extracts these exact generated functions from
 * a local Goemon64Recomp checkout at test time; generated game code is not
 * copied into this repository.  This test establishes what one versus four
 * native callback invocations do.  Production hook/lifecycle behavior stays
 * covered by the ordinary hyper_detoile host fixture.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

#undef RECOMP_FUNC
#define RECOMP_FUNC static __attribute__((noinline))
#define TRACE_ENTRY()
#define TRACE_RETURN()

#define RDRAM_SIZE 0x800000u
#define ROOT_ADDR 0x80110000u
#define MODEL_ADDR 0x80111000u
#define SCRATCH_ADDR 0x80112000u
#define EFFECT_ADDR 0x80113000u
#define STACK_ADDR 0x807FF000u
#define SCRATCH_SLOT_ADDR 0x8020EF40u
#define EFFECT_SLOT_ADDR 0x8020EF08u
#define PROJECTILE_CONSTANT_ADDR 0x8020EAA0u

static _Alignas(16) uint8_t s_rdram[RDRAM_SIZE];

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

static uint16_t *guest_half(uint32_t address, unsigned int offset)
{
    unsigned int native_offset = offset ^ 2u;

    assert((native_offset & 1u) == 0);
    assert(address - 0x80000000u + native_offset + sizeof(uint16_t) <=
           RDRAM_SIZE);
    return (uint16_t *)(void *)(guest_host(address) + native_offset);
}

static float read_float(uint32_t address, unsigned int offset)
{
    float value;

    memcpy(&value, guest_word(address, offset), sizeof(value));
    return value;
}

static void write_float(uint32_t address, unsigned int offset, float value)
{
    memcpy(guest_word(address, offset), &value, sizeof(value));
}

static recomp_context new_context(void)
{
    recomp_context context;

    memset(&context, 0, sizeof(context));
    context.r29 = guest_gpr(STACK_ADDR);
    return context;
}

void switch_error(const char *function, uint32_t vram, uint32_t table)
{
    fprintf(stderr, "unexpected generated switch in %s at %08X (%08X)\n",
            function, vram, table);
    abort();
}

/* Use the game's actual animation-frame and linked-model motion helpers. */
#define func_801D2F40_5FE320 native_func_801D2F40_5FE320
#include "native_801D2F40.inc"
#undef func_801D2F40_5FE320

#define func_801D614C_60152C native_func_801D614C_60152C
#include "native_801D614C.inc"
#undef func_801D614C_60152C

static unsigned int s_animation_state_changes;
static unsigned int s_callback_changes;
static uint32_t s_last_callback;

static void stub_func_801D31CC_5FE5AC(uint8_t *rdram,
                                      recomp_context *context)
{
    (void)rdram;
    (void)context;
    s_animation_state_changes++;
}

static void stub_func_8003521C_35E1C(uint8_t *rdram,
                                     recomp_context *context)
{
    (void)rdram;
    s_callback_changes++;
    s_last_callback = (uint32_t)context->r4;
}

#define func_801D2F40_5FE320 native_func_801D2F40_5FE320
#define func_801D31CC_5FE5AC stub_func_801D31CC_5FE5AC
#define func_801D614C_60152C native_func_801D614C_60152C
#define func_8003521C_35E1C stub_func_8003521C_35E1C
#define func_801FCD1C_6280FC native_func_801FCD1C_6280FC
#include "native_801FCD1C.inc"
#undef func_801FCD1C_6280FC
#undef func_8003521C_35E1C
#undef func_801D614C_60152C
#undef func_801D31CC_5FE5AC
#undef func_801D2F40_5FE320

/* 801E4194 writes four 16-bit parameters to the global effect-control
 * object (+90..+9C) and raises its +84 update flag.  In 801FD30C the first
 * three parameters are 0, 0, 0x4C and scratch +817 supplies parameter four.
 * Track that neutral fourth-channel cue; it is not D'Etoile pose history. */
#define func_801E4194_60F574 native_func_801E4194_60F574
#include "native_801E4194.inc"
#undef func_801E4194_60F574

static unsigned int s_effect_updates;
static uint8_t s_effect_fourth_channel[4];
static unsigned int s_random_calls;
static unsigned int s_projectile_calls;
static uint32_t s_projectile_owner;
static unsigned int s_unexpected_phase_calls;

static void stub_func_801D641C_6017FC(uint8_t *rdram,
                                      recomp_context *context)
{
    (void)rdram;
    (void)context;
}

static void tracked_func_801E4194_60F574(uint8_t *rdram,
                                         recomp_context *context)
{
    assert(s_effect_updates < sizeof(s_effect_fourth_channel));
    s_effect_fourth_channel[s_effect_updates++] = (uint8_t)context->r7;
    native_func_801E4194_60F574(rdram, context);
}

static void stub_func_8000DD58_E958(uint8_t *rdram,
                                    recomp_context *context)
{
    (void)rdram;
    s_random_calls++;
    context->r2 = 0x123;
}

static void stub_func_8000E630_F230(uint8_t *rdram,
                                    recomp_context *context)
{
    /* Deterministic native output slots consumed as projectile arguments. */
    MEM_W(0, context->r6) = 0;
    MEM_W(0, context->r7) = 0;
}

static void stub_func_80207838_632C18(uint8_t *rdram,
                                      recomp_context *context)
{
    (void)rdram;
    s_projectile_calls++;
    s_projectile_owner = (uint32_t)context->r4;
}

static void stub_unexpected_phase_helper(uint8_t *rdram,
                                         recomp_context *context)
{
    (void)rdram;
    (void)context;
    s_unexpected_phase_calls++;
}

#define func_801D641C_6017FC stub_func_801D641C_6017FC
#define func_801E4194_60F574 tracked_func_801E4194_60F574
#define func_8000DD58_E958 stub_func_8000DD58_E958
#define func_8000E630_F230 stub_func_8000E630_F230
#define func_80207838_632C18 stub_func_80207838_632C18
#define func_80206ED8_6322B8 stub_unexpected_phase_helper
#define func_80038BC8_397C8 stub_unexpected_phase_helper
#define func_801FC3FC_6277DC stub_unexpected_phase_helper
#define func_801FD30C_6286EC native_func_801FD30C_6286EC
#include "native_801FD30C.inc"
#undef func_801FD30C_6286EC
#undef func_801FC3FC_6277DC
#undef func_80038BC8_397C8
#undef func_80206ED8_6322B8
#undef func_80207838_632C18
#undef func_8000E630_F230
#undef func_8000DD58_E958
#undef func_801E4194_60F574
#undef func_801D641C_6017FC

static void reset_movement_fixture(void)
{
    memset(s_rdram, 0, sizeof(s_rdram));
    *guest_word(ROOT_ADDR, 0x18) = (int32_t)MODEL_ADDR;
    *guest_word(ROOT_ADDR, 0x7C) = 4;
    write_float(ROOT_ADDR, 0x70, 1.5f);
    write_float(ROOT_ADDR, 0x74, -2.0f);
    write_float(ROOT_ADDR, 0x78, 3.0f);
    write_float(MODEL_ADDR, 0x8, 10.0f);
    write_float(MODEL_ADDR, 0xC, 20.0f);
    write_float(MODEL_ADDR, 0x10, 30.0f);
    /* Ended animation: native 801D2F40 returns zero, allowing 801FCD1C's
     * movement/yaw/timer phase to execute. */
    write_float(MODEL_ADDR, 0x28, 0.0f);
    write_float(MODEL_ADDR, 0x94, 1.0f);
    *guest_half(MODEL_ADDR, 0x16) = 0x100;
    s_animation_state_changes = 0;
    s_callback_changes = 0;
    s_last_callback = 0;
}

static void run_movement_ticks(unsigned int ticks)
{
    recomp_context context = new_context();

    for (unsigned int tick = 0; tick < ticks; ++tick) {
        context.r4 = guest_gpr(ROOT_ADDR);
        context.r5 = guest_gpr(MODEL_ADDR);
        native_func_801FCD1C_6280FC(s_rdram, &context);
    }
}

static void test_native_movement_yaw_and_timer(void)
{
    reset_movement_fixture();
    run_movement_ticks(1);
    assert(read_float(MODEL_ADDR, 0x8) == 11.5f);
    assert(read_float(MODEL_ADDR, 0xC) == 18.0f);
    assert(read_float(MODEL_ADDR, 0x10) == 33.0f);
    assert(*guest_half(MODEL_ADDR, 0x16) == 0x110);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 3);
    assert(s_animation_state_changes == 0);
    assert(s_callback_changes == 0);

    reset_movement_fixture();
    run_movement_ticks(4);
    assert(read_float(MODEL_ADDR, 0x8) == 16.0f);
    assert(read_float(MODEL_ADDR, 0xC) == 12.0f);
    assert(read_float(MODEL_ADDR, 0x10) == 42.0f);
    assert(*guest_half(MODEL_ADDR, 0x16) == 0x140);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 0);
    assert(s_animation_state_changes == 1);
    assert(s_callback_changes == 1);
    assert(s_last_callback == 0x801FCC64u);
}

static void test_native_skeletal_animation(void)
{
    reset_movement_fixture();
    write_float(MODEL_ADDR, 0x28, 0.0f);
    write_float(MODEL_ADDR, 0x94, 100.0f);
    run_movement_ticks(1);
    assert(read_float(MODEL_ADDR, 0x28) == 5.0f);
    assert(read_float(MODEL_ADDR, 0x8) == 10.0f);
    assert(*guest_half(MODEL_ADDR, 0x16) == 0x100);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 4);

    reset_movement_fixture();
    write_float(MODEL_ADDR, 0x28, 0.0f);
    write_float(MODEL_ADDR, 0x94, 100.0f);
    run_movement_ticks(4);
    assert(read_float(MODEL_ADDR, 0x28) == 20.0f);
    assert(read_float(MODEL_ADDR, 0x8) == 10.0f);
    assert(*guest_half(MODEL_ADDR, 0x16) == 0x100);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 4);
}

static void reset_attack_fixture(void)
{
    memset(s_rdram, 0, sizeof(s_rdram));
    *guest_word(SCRATCH_SLOT_ADDR, 0) = (int32_t)SCRATCH_ADDR;
    *guest_word(EFFECT_SLOT_ADDR, 0) = (int32_t)EFFECT_ADDR;
    write_float(PROJECTILE_CONSTANT_ADDR, 0, 45.0f);
    *guest_byte(SCRATCH_ADDR, 0x817) = 3;
    *guest_word(ROOT_ADDR, 0x90) = 4;
    *guest_word(ROOT_ADDR, 0x80) = 20;
    *guest_word(ROOT_ADDR, 0x7C) = 100;
    *guest_word(ROOT_ADDR, 0x94) = 100;
    s_effect_updates = 0;
    memset(s_effect_fourth_channel, 0, sizeof(s_effect_fourth_channel));
    s_random_calls = 0;
    s_projectile_calls = 0;
    s_projectile_owner = 0;
    s_unexpected_phase_calls = 0;
}

static void run_attack_ticks(unsigned int ticks)
{
    recomp_context context = new_context();

    for (unsigned int tick = 0; tick < ticks; ++tick) {
        context.r4 = guest_gpr(ROOT_ADDR);
        context.r5 = guest_gpr(MODEL_ADDR);
        native_func_801FD30C_6286EC(s_rdram, &context);
    }
}

static void test_native_attack_clocks_and_projectile_boundary(void)
{
    reset_attack_fixture();
    run_attack_ticks(1);
    assert(*guest_word(ROOT_ADDR, 0x90) == 3);
    assert(*guest_word(ROOT_ADDR, 0x80) == 19);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 99);
    assert(s_effect_updates == 1 && s_effect_fourth_channel[0] == 3);
    assert(*guest_word(EFFECT_ADDR, 0x90) == 0);
    assert(*guest_word(EFFECT_ADDR, 0x94) == 0);
    assert(*guest_word(EFFECT_ADDR, 0x98) == 0x4C);
    assert(*guest_word(EFFECT_ADDR, 0x9C) == 3);
    assert(*guest_word(EFFECT_ADDR, 0x84) == 1);
    assert(s_random_calls == 0);
    assert(s_projectile_calls == 0);
    assert(s_unexpected_phase_calls == 0);

    reset_attack_fixture();
    run_attack_ticks(4);
    assert(*guest_word(ROOT_ADDR, 0x90) == 50);
    assert(*guest_word(ROOT_ADDR, 0x80) == 16);
    assert(*guest_word(ROOT_ADDR, 0x7C) == 96);
    assert(s_effect_updates == 4);
    for (unsigned int tick = 0; tick < 4; ++tick)
        assert(s_effect_fourth_channel[tick] == 3);
    assert(s_random_calls == 1);
    assert(s_projectile_calls == 1);
    assert(s_projectile_owner == ROOT_ADDR);
    assert(s_unexpected_phase_calls == 0);
}

int main(void)
{
    test_native_movement_yaw_and_timer();
    test_native_skeletal_animation();
    test_native_attack_clocks_and_projectile_boundary();
    puts("Passed native D'Etoile 1x/4x callback smoke tests.");
    return 0;
}
