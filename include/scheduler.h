#ifndef NACRE_SCHEDULER_H
#define NACRE_SCHEDULER_H

#include "chip8.h"

#include <stddef.h>
#include <stdint.h>

#define CHIP8_TIMER_HZ 60u
#define CHIP8_DEFAULT_CPU_HZ 600u
#define CHIP8_MAX_FRAME_DELTA_NS 250000000u

typedef struct {
    uint32_t cpu_hz;
    uint64_t cpu_phase;
    uint64_t timer_phase;
    uint64_t timer_cycle_phase;
} Chip8Scheduler;

typedef void (*Chip8TimerCallback)(Chip8 *chip8, void *userdata);

void chip8_scheduler_init(Chip8Scheduler *scheduler, uint32_t cpu_hz);
void chip8_scheduler_set_cpu_hz(Chip8Scheduler *scheduler, uint32_t cpu_hz);
int chip8_scheduler_advance(Chip8 *chip8, Chip8Scheduler *scheduler,
                            uint64_t elapsed_ns, int paused,
                            Chip8TimerCallback timer_callback, void *userdata,
                            char *error, size_t error_capacity);
int chip8_scheduler_run_cycles(Chip8 *chip8, Chip8Scheduler *scheduler,
                               uint32_t cycles, Chip8TimerCallback timer_callback,
                               void *userdata, char *error, size_t error_capacity);

#endif
