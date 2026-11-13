#include "chip8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t font[16][5] = {
    {0xF0, 0x90, 0x90, 0x90, 0xF0},
    {0x20, 0x60, 0x20, 0x20, 0x70},
    {0xF0, 0x10, 0xF0, 0x80, 0xF0},
    {0xF0, 0x10, 0xF0, 0x10, 0xF0},
    {0x90, 0x90, 0xF0, 0x10, 0x10},
    {0xF0, 0x80, 0xF0, 0x10, 0xF0},
    {0xF0, 0x80, 0xF0, 0x90, 0xF0},
    {0xF0, 0x10, 0x20, 0x40, 0x40},
    {0xF0, 0x90, 0xF0, 0x90, 0xF0},
    {0xF0, 0x90, 0xF0, 0x10, 0xF0},
    {0xF0, 0x90, 0xF0, 0x90, 0x90},
    {0xE0, 0x90, 0xE0, 0x90, 0xE0},
    {0xF0, 0x80, 0x80, 0x80, 0xF0},
    {0xE0, 0x90, 0x90, 0x90, 0xE0},
    {0xF0, 0x80, 0xF0, 0x80, 0xF0},
    {0xF0, 0x80, 0xF0, 0x80, 0x80}
};

static void set_error(char *error, size_t capacity, const char *message) {
    if (error && capacity > 0) {
        snprintf(error, capacity, "%s", message);
    }
}

static int fail_opcode(uint16_t address, uint16_t opcode,
                       char *error, size_t capacity) {
    if (error && capacity > 0) {
        snprintf(error, capacity, "unsupported opcode %04X at %03X",
                 opcode, address);
    }
    return 0;
}

void chip8_init(Chip8 *chip8, uint32_t seed) {
    memset(chip8, 0, sizeof(*chip8));
    chip8->pc = CHIP8_PROGRAM_START;
    chip8->random_state = seed ? seed : 0xA341316Cu;
    for (size_t digit = 0; digit < 16; digit++) {
        memcpy(chip8->memory + 0x50 + digit * 5, font[digit], 5);
    }
}

int chip8_load_bytes(Chip8 *chip8, const uint8_t *bytes, size_t length,
                     char *error, size_t error_capacity) {
    if (!bytes || length == 0 || length > CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START) {
        set_error(error, error_capacity, "ROM must contain 1 to 3584 bytes");
        return 0;
    }

    uint32_t seed = chip8->random_state;
    chip8_init(chip8, seed);
    memcpy(chip8->memory + CHIP8_PROGRAM_START, bytes, length);
    return 1;
}

int chip8_load_rom(Chip8 *chip8, const char *path,
                   char *error, size_t error_capacity) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error && error_capacity > 0) {
            snprintf(error, error_capacity, "cannot open ROM '%s'", path);
        }
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(error, error_capacity, "cannot seek in ROM file");
        return 0;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || file_size > CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START) {
        fclose(file);
        set_error(error, error_capacity, "ROM must contain 1 to 3584 bytes");
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error, error_capacity, "cannot rewind ROM file");
        return 0;
    }

    size_t length = (size_t)file_size;
    uint8_t *bytes = malloc(length);
    if (!bytes) {
        fclose(file);
        set_error(error, error_capacity, "not enough memory to read ROM");
        return 0;
    }
    size_t read_count = fread(bytes, 1, length, file);
    int read_failed = ferror(file);
    fclose(file);
    if (read_failed || read_count != length) {
        free(bytes);
        set_error(error, error_capacity, "could not read the entire ROM file");
        return 0;
    }

    int loaded = chip8_load_bytes(chip8, bytes, length, error, error_capacity);
    free(bytes);
    return loaded;
}

static uint32_t random_next(Chip8 *chip8) {
    uint32_t value = chip8->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    chip8->random_state = value;
    return value;
}

static int memory_range_valid(uint16_t start, size_t length) {
    return start < CHIP8_MEMORY_SIZE && length <= (size_t)CHIP8_MEMORY_SIZE - start;
}

static int draw_sprite(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t height,
                       char *error, size_t error_capacity) {
    if (!memory_range_valid(chip8->index, height)) {
        set_error(error, error_capacity, "sprite extends past the end of memory");
        return 0;
    }

    chip8->registers[0xF] = 0;
    for (uint8_t row = 0; row < height; row++) {
        uint8_t sprite = chip8->memory[chip8->index + row];
        for (uint8_t column = 0; column < 8; column++) {
            if ((sprite & (0x80u >> column)) == 0) {
                continue;
            }
            size_t px = (x + column) % CHIP8_DISPLAY_WIDTH;
            size_t py = (y + row) % CHIP8_DISPLAY_HEIGHT;
            size_t pixel = py * CHIP8_DISPLAY_WIDTH + px;
            if (chip8->display[pixel]) {
                chip8->registers[0xF] = 1;
            }
            chip8->display[pixel] ^= 1;
        }
    }
    return 1;
}

int chip8_step(Chip8 *chip8, char *error, size_t error_capacity) {
    if (chip8->pc > CHIP8_MEMORY_SIZE - 2) {
        if (error && error_capacity > 0) {
            snprintf(error, error_capacity, "program counter %03X is outside memory", chip8->pc);
        }
        return 0;
    }

    uint16_t instruction_address = chip8->pc;
    uint16_t opcode = (uint16_t)((chip8->memory[chip8->pc] << 8) |
                                 chip8->memory[chip8->pc + 1]);
    chip8->pc += 2;
    uint8_t x = (opcode >> 8) & 0x0F;
    uint8_t y = (opcode >> 4) & 0x0F;
    uint8_t n = opcode & 0x0F;
    uint8_t kk = opcode & 0xFF;
    uint16_t nnn = opcode & 0x0FFF;
    uint8_t vx = chip8->registers[x];
    uint8_t vy = chip8->registers[y];

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) {
                memset(chip8->display, 0, sizeof(chip8->display));
            } else if (opcode == 0x00EE) {
                if (chip8->stack_depth == 0) {
                    set_error(error, error_capacity, "return executed with an empty stack");
                    return 0;
                }
                chip8->pc = chip8->stack[--chip8->stack_depth];
            }
            /* 0nnn system calls are ignored by modern CHIP-8 interpreters. */
            break;

        case 0x1000:
            chip8->pc = nnn;
            break;

        case 0x2000:
            if (chip8->stack_depth == sizeof(chip8->stack) / sizeof(chip8->stack[0])) {
                set_error(error, error_capacity, "call stack overflow");
                return 0;
            }
            chip8->stack[chip8->stack_depth++] = chip8->pc;
            chip8->pc = nnn;
            break;

        case 0x3000:
            if (vx == kk) chip8->pc += 2;
            break;

        case 0x4000:
            if (vx != kk) chip8->pc += 2;
            break;

        case 0x5000:
            if (n != 0) return fail_opcode(instruction_address, opcode, error, error_capacity);
            if (vx == vy) chip8->pc += 2;
            break;

        case 0x6000:
            chip8->registers[x] = kk;
            break;

        case 0x7000:
            chip8->registers[x] = (uint8_t)(vx + kk);
            break;

        case 0x8000:
            switch (n) {
                case 0x0: chip8->registers[x] = vy; break;
                case 0x1: chip8->registers[x] = (uint8_t)(vx | vy); break;
                case 0x2: chip8->registers[x] = (uint8_t)(vx & vy); break;
                case 0x3: chip8->registers[x] = (uint8_t)(vx ^ vy); break;
                case 0x4: {
                    uint16_t sum = (uint16_t)vx + vy;
                    chip8->registers[x] = (uint8_t)sum;
                    chip8->registers[0xF] = sum > 0xFF;
                    break;
                }
                case 0x5:
                    chip8->registers[x] = (uint8_t)(vx - vy);
                    chip8->registers[0xF] = vx >= vy;
                    break;
                case 0x6:
                    chip8->registers[x] = vx >> 1;
                    chip8->registers[0xF] = vx & 1;
                    break;
                case 0x7:
                    chip8->registers[x] = (uint8_t)(vy - vx);
                    chip8->registers[0xF] = vy >= vx;
                    break;
                case 0xE:
                    chip8->registers[x] = (uint8_t)(vx << 1);
                    chip8->registers[0xF] = (vx >> 7) & 1;
                    break;
                default:
                    return fail_opcode(instruction_address, opcode, error, error_capacity);
            }
            break;

        case 0x9000:
            if (n != 0) return fail_opcode(instruction_address, opcode, error, error_capacity);
            if (vx != vy) chip8->pc += 2;
            break;

        case 0xA000:
            chip8->index = nnn;
            break;

        case 0xB000:
            chip8->pc = (uint16_t)((chip8->registers[0] + nnn) & 0x0FFF);
            break;

        case 0xC000:
            chip8->registers[x] = (uint8_t)(random_next(chip8) & kk);
            break;

        case 0xD000:
            if (!draw_sprite(chip8, vx, vy, n, error, error_capacity)) return 0;
            break;

        case 0xE000:
            if (kk == 0x9E) {
                if (chip8->keys[vx & 0x0F]) chip8->pc += 2;
            } else if (kk == 0xA1) {
                if (!chip8->keys[vx & 0x0F]) chip8->pc += 2;
            } else {
                return fail_opcode(instruction_address, opcode, error, error_capacity);
            }
            break;

        case 0xF000:
            switch (kk) {
                case 0x07:
                    chip8->registers[x] = chip8->delay_timer;
                    break;
                case 0x0A: {
                    int pressed = -1;
                    for (int key = 0; key < 16; key++) {
                        if (chip8->keys[key]) { pressed = key; break; }
                    }
                    if (pressed < 0) chip8->pc = instruction_address;
                    else chip8->registers[x] = (uint8_t)pressed;
                    break;
                }
                case 0x15:
                    chip8->delay_timer = vx;
                    break;
                case 0x18:
                    chip8->sound_timer = vx;
                    break;
                case 0x1E:
                    chip8->index = (uint16_t)((chip8->index + vx) & 0x0FFF);
                    break;
                case 0x29:
                    chip8->index = (uint16_t)(0x50 + (vx & 0x0F) * 5);
                    break;
                case 0x33:
                    if (!memory_range_valid(chip8->index, 3)) {
                        set_error(error, error_capacity, "BCD write extends past the end of memory");
                        return 0;
                    }
                    chip8->memory[chip8->index] = vx / 100;
                    chip8->memory[chip8->index + 1] = (vx / 10) % 10;
                    chip8->memory[chip8->index + 2] = vx % 10;
                    break;
                case 0x55:
                    if (!memory_range_valid(chip8->index, (size_t)x + 1)) {
                        set_error(error, error_capacity, "register save extends past the end of memory");
                        return 0;
                    }
                    for (uint8_t reg = 0; reg <= x; reg++) {
                        chip8->memory[chip8->index + reg] = chip8->registers[reg];
                    }
                    break;
                case 0x65:
                    if (!memory_range_valid(chip8->index, (size_t)x + 1)) {
                        set_error(error, error_capacity, "register load extends past the end of memory");
                        return 0;
                    }
                    for (uint8_t reg = 0; reg <= x; reg++) {
                        chip8->registers[reg] = chip8->memory[chip8->index + reg];
                    }
                    break;
                default:
                    return fail_opcode(instruction_address, opcode, error, error_capacity);
            }
            break;
    }
    return 1;
}

void chip8_tick_timers(Chip8 *chip8) {
    if (chip8->delay_timer > 0) chip8->delay_timer--;
    if (chip8->sound_timer > 0) chip8->sound_timer--;
}

void chip8_set_key(Chip8 *chip8, uint8_t key, int pressed) {
    if (key < 16) chip8->keys[key] = (uint8_t)(pressed != 0);
}
