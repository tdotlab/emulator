#include "scheduler.h"

#include <stdio.h>
#include <string.h>

#define PHASE_ONE 1000000000ULL
#define MAX_CPU_HZ 60000u

void chip8_scheduler_init(Chip8Scheduler *scheduler, uint32_t cpu_hz) {
    memset(scheduler, 0, sizeof(*scheduler));
    chip8_scheduler_set_cpu_hz(scheduler, cpu_hz);
}

void chip8_scheduler_set_cpu_hz(Chip8Scheduler *scheduler, uint32_t cpu_hz) {
    scheduler->cpu_hz = cpu_hz > MAX_CPU_HZ ? MAX_CPU_HZ : cpu_hz;
}

static uint64_t ns_until_event(uint64_t phase, uint32_t rate) {
    uint64_t remaining = PHASE_ONE - phase;
    return (remaining + rate - 1) / rate;
}

static int step_cpu(Chip8 *chip8, char *error, size_t error_capacity) {
    Chip8StepResult result = chip8_step(chip8, error, error_capacity);
    return result != CHIP8_STEP_ERROR;
}

static void tick_timer(Chip8 *chip8, Chip8TimerCallback callback, void *userdata) {
    chip8_tick_timers(chip8);
    if (callback) callback(chip8, userdata);
}

int chip8_scheduler_advance(Chip8 *chip8, Chip8Scheduler *scheduler,
                            uint64_t elapsed_ns, int paused,
                            Chip8TimerCallback timer_callback, void *userdata,
                            char *error, size_t error_capacity) {
    if (paused) {
        scheduler->cpu_phase = 0;
        return 1;
    }

    if (chip8->key_wait_active) {
        Chip8StepResult result = chip8_step(chip8, error, error_capacity);
        if (result == CHIP8_STEP_ERROR) return 0;
        if (result == CHIP8_STEP_WAITING_FOR_KEY) scheduler->cpu_phase = 0;
    }

    if (elapsed_ns > CHIP8_MAX_FRAME_DELTA_NS) {
        elapsed_ns = CHIP8_MAX_FRAME_DELTA_NS;
    }

    while (elapsed_ns > 0) {
        int cpu_running = scheduler->cpu_hz > 0 && !chip8->key_wait_active;
        uint64_t cpu_wait = cpu_running
            ? ns_until_event(scheduler->cpu_phase, scheduler->cpu_hz)
            : UINT64_MAX;
        uint64_t timer_wait = ns_until_event(scheduler->timer_phase, CHIP8_TIMER_HZ);
        uint64_t step_ns = elapsed_ns;
        if (cpu_wait < step_ns) step_ns = cpu_wait;
        if (timer_wait < step_ns) step_ns = timer_wait;

        if (cpu_running) scheduler->cpu_phase += step_ns * scheduler->cpu_hz;
        scheduler->timer_phase += step_ns * CHIP8_TIMER_HZ;
        elapsed_ns -= step_ns;

        /* CPU instructions run before a timer tick at the same instant. */
        if (cpu_running && scheduler->cpu_phase >= PHASE_ONE) {
            scheduler->cpu_phase -= PHASE_ONE;
            if (!step_cpu(chip8, error, error_capacity)) return 0;
            if (chip8->key_wait_active) scheduler->cpu_phase = 0;
        }
        if (scheduler->timer_phase >= PHASE_ONE) {
            scheduler->timer_phase -= PHASE_ONE;
            tick_timer(chip8, timer_callback, userdata);
        }
        if (step_ns == 0) return 0;
    }
    if (chip8->key_wait_active) scheduler->cpu_phase = 0;
    return 1;
}

int chip8_scheduler_run_cycles(Chip8 *chip8, Chip8Scheduler *scheduler,
                               uint32_t cycles, Chip8TimerCallback timer_callback,
                               void *userdata, char *error, size_t error_capacity) {
    uint32_t cpu_hz = scheduler->cpu_hz ? scheduler->cpu_hz : CHIP8_DEFAULT_CPU_HZ;
    for (uint32_t cycle = 0; cycle < cycles; cycle++) {
        if (!step_cpu(chip8, error, error_capacity)) return 0;
        scheduler->timer_cycle_phase += CHIP8_TIMER_HZ;
        if (scheduler->timer_cycle_phase >= cpu_hz) {
            scheduler->timer_cycle_phase -= cpu_hz;
            tick_timer(chip8, timer_callback, userdata);
        }
    }
    return 1;
}
