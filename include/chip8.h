#ifndef CHIP8_H
#define CHIP8_H

#include <stddef.h>
#include <stdint.h>

#define CHIP8_MEMORY_SIZE 4096
#define CHIP8_DISPLAY_WIDTH 64
#define CHIP8_DISPLAY_HEIGHT 32
#define CHIP8_REGISTER_COUNT 16
#define CHIP8_PROGRAM_START 0x200

typedef struct {
    uint8_t memory[CHIP8_MEMORY_SIZE];
    uint8_t registers[CHIP8_REGISTER_COUNT];
    uint16_t index;
    uint16_t pc;
    uint16_t stack[16];
    uint8_t stack_depth;
    uint8_t delay_timer;
    uint8_t sound_timer;
    uint8_t display[CHIP8_DISPLAY_WIDTH * CHIP8_DISPLAY_HEIGHT];
    uint8_t keys[16];
    uint32_t random_state;
} Chip8;

void chip8_init(Chip8 *chip8, uint32_t seed);
int chip8_load_bytes(Chip8 *chip8, const uint8_t *bytes, size_t length,
                     char *error, size_t error_capacity);
int chip8_load_rom(Chip8 *chip8, const char *path,
                   char *error, size_t error_capacity);
int chip8_step(Chip8 *chip8, char *error, size_t error_capacity);
void chip8_tick_timers(Chip8 *chip8);
void chip8_set_key(Chip8 *chip8, uint8_t key, int pressed);

#endif
