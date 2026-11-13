#include "chip8.h"

#include <assert.h>
#include <stdio.h>

static void load_program(Chip8 *chip8, const uint8_t *program, size_t length) {
    char error[128];
    chip8_init(chip8, 1);
    assert(chip8_load_bytes(chip8, program, length, error, sizeof(error)));
}

static void run_instruction(Chip8 *chip8) {
    char error[128];
    assert(chip8_step(chip8, error, sizeof(error)) == CHIP8_STEP_EXECUTED);
}

static void test_arithmetic_and_flag_aliasing(void) {
    static const uint8_t program[] = {
        0x6F, 0xF0,       /* VF = F0 */
        0x60, 0x30,       /* V0 = 30 */
        0x8F, 0x04,       /* VF = VF + V0 */
        0x6F, 0xF0,
        0x60, 0x20,
        0x80, 0xF4,       /* V0 = V0 + VF */
        0x61, 0x05,
        0x62, 0x05,
        0x81, 0x25        /* V1 = V1 - V2; equal means no borrow */
    };
    Chip8 chip8;
    load_program(&chip8, program, sizeof(program));
    for (int i = 0; i < 9; i++) run_instruction(&chip8);
    assert(chip8.registers[0xF] == 1);
    assert(chip8.registers[0] == 0x10);
    assert(chip8.registers[1] == 0);
}

static void test_call_and_return(void) {
    static const uint8_t program[] = {
        0x22, 0x06,       /* call subroutine */
        0x12, 0x04,       /* loop */
        0x00, 0x00,
        0x60, 0x2A,
        0x00, 0xEE
    };
    Chip8 chip8;
    load_program(&chip8, program, sizeof(program));
    run_instruction(&chip8);
    assert(chip8.stack_depth == 1 && chip8.pc == 0x206);
    run_instruction(&chip8);
    run_instruction(&chip8);
    assert(chip8.registers[0] == 0x2A);
    assert(chip8.stack_depth == 0 && chip8.pc == 0x202);
}

static void test_call_stack_overflow_is_reported(void) {
    static const uint8_t recursive_call[] = {0x22, 0x00};
    Chip8 chip8;
    char error[128];
    load_program(&chip8, recursive_call, sizeof(recursive_call));
    for (int i = 0; i < 16; i++) {
        assert(chip8_step(&chip8, error, sizeof(error)) == CHIP8_STEP_EXECUTED);
    }
    assert(chip8_step(&chip8, error, sizeof(error)) == CHIP8_STEP_ERROR);
}

static void test_memory_bounds_and_rom_sizes(void) {
    static const uint8_t bcd_at_end[] = {0xAF, 0xFF, 0x60, 0x63, 0xF0, 0x33};
    static const uint8_t tiny[] = {0x12, 0x00};
    static const uint8_t oversized[CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START + 1] = {0};
    Chip8 chip8;
    char error[128];
    load_program(&chip8, bcd_at_end, sizeof(bcd_at_end));
    run_instruction(&chip8);
    run_instruction(&chip8);
    assert(chip8_step(&chip8, error, sizeof(error)) == CHIP8_STEP_ERROR);
    assert(!chip8_load_bytes(&chip8, NULL, 2, error, sizeof(error)));
    assert(!chip8_load_bytes(&chip8, tiny, 0, error, sizeof(error)));
    assert(!chip8_load_bytes(&chip8, oversized, sizeof(oversized), error, sizeof(error)));
}

static void test_sprite_wrap_and_collision(void) {
    static const uint8_t program[] = {
        0x60, 0x3F, 0x61, 0x1F, 0xA2, 0x0A,
        0xD0, 0x12, 0xD0, 0x12, 0x80, 0x80
    };
    Chip8 chip8;
    load_program(&chip8, program, sizeof(program));
    for (int i = 0; i < 4; i++) run_instruction(&chip8);
    assert(chip8.display[31 * CHIP8_DISPLAY_WIDTH + 63] == 1);
    assert(chip8.display[63] == 1);
    run_instruction(&chip8);
    assert(chip8.registers[0xF] == 1);
    assert(chip8.display[31 * CHIP8_DISPLAY_WIDTH + 63] == 0);
    assert(chip8.display[63] == 0);
}

int main(void) {
    test_arithmetic_and_flag_aliasing();
    test_call_and_return();
    test_call_stack_overflow_is_reported();
    test_memory_bounds_and_rom_sizes();
    test_sprite_wrap_and_collision();
    puts("core tests passed");
    return 0;
}
