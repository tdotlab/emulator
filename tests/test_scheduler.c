#include "chip8.h"
#include "scheduler.h"

#include <assert.h>
#include <stdio.h>

static void count_tick(Chip8 *chip8, void *userdata) {
    (void)chip8;
    (*(unsigned *)userdata)++;
}

static void load_loop(Chip8 *chip8) {
    static const uint8_t loop[] = {0x12, 0x00};
    char error[128];
    chip8_init(chip8, 1);
    assert(chip8_load_bytes(chip8, loop, sizeof(loop), error, sizeof(error)));
}

static void test_timer_cadence_independent_of_frames(void) {
    Chip8 a, b;
    Chip8Scheduler sa, sb;
    load_loop(&a);
    load_loop(&b);
    a.delay_timer = b.delay_timer = 120;
    a.sound_timer = b.sound_timer = 90;
    chip8_scheduler_init(&sa, CHIP8_DEFAULT_CPU_HZ);
    chip8_scheduler_init(&sb, CHIP8_DEFAULT_CPU_HZ);

    char error[128];
    for (int i = 0; i < 100; i++) {
        assert(chip8_scheduler_advance(&a, &sa, 10000000, 0, NULL, NULL,
                                       error, sizeof(error)));
    }
    assert(chip8_scheduler_advance(&b, &sb, 250000000, 0, NULL, NULL,
                                   error, sizeof(error)));
    assert(chip8_scheduler_advance(&b, &sb, 250000000, 0, NULL, NULL,
                                   error, sizeof(error)));
    assert(chip8_scheduler_advance(&b, &sb, 250000000, 0, NULL, NULL,
                                   error, sizeof(error)));
    assert(chip8_scheduler_advance(&b, &sb, 250000000, 0, NULL, NULL,
                                   error, sizeof(error)));
    assert(a.delay_timer == 60 && a.sound_timer == 30);
    assert(a.delay_timer == b.delay_timer && a.sound_timer == b.sound_timer);
    assert(sa.timer_phase == sb.timer_phase);
}

static void test_pause_discards_elapsed_time(void) {
    Chip8 chip8;
    Chip8Scheduler scheduler;
    load_loop(&chip8);
    chip8.delay_timer = 60;
    chip8_scheduler_init(&scheduler, CHIP8_DEFAULT_CPU_HZ);
    char error[128];
    assert(chip8_scheduler_advance(&chip8, &scheduler, 1000000000, 1,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8.delay_timer == 60);
    assert(chip8_scheduler_advance(&chip8, &scheduler, 250000000, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8_scheduler_advance(&chip8, &scheduler, 250000000, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8.delay_timer == 30);
}

static void test_live_stall_catchup_is_capped(void) {
    Chip8 chip8;
    Chip8Scheduler scheduler;
    load_loop(&chip8);
    chip8.delay_timer = 60;
    chip8_scheduler_init(&scheduler, CHIP8_DEFAULT_CPU_HZ);
    char error[128];
    assert(chip8_scheduler_advance(&chip8, &scheduler, 1000000000, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8.delay_timer == 45);
}

static void test_key_wait_releases_and_timers_continue(void) {
    static const uint8_t program[] = {
        0x60, 0x78,       /* V0 = 120 */
        0xF0, 0x15,       /* delay timer = V0 */
        0xF1, 0x0A,       /* wait for key, then release */
        0x12, 0x04        /* loop */
    };
    Chip8 chip8;
    Chip8Scheduler scheduler;
    char error[128];
    chip8_init(&chip8, 1);
    assert(chip8_load_bytes(&chip8, program, sizeof(program), error, sizeof(error)));
    chip8_scheduler_init(&scheduler, CHIP8_DEFAULT_CPU_HZ);
    assert(chip8_scheduler_advance(&chip8, &scheduler, 50000000, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8.key_wait_active && chip8.delay_timer == 117);
    for (int i = 0; i < 4; i++) {
        assert(chip8_scheduler_advance(&chip8, &scheduler, 250000000, 0,
                                       NULL, NULL, error, sizeof(error)));
    }
    assert(chip8.delay_timer == 57);
    assert(chip8.registers[1] == 0);

    chip8_set_key(&chip8, 0xA, 1);
    assert(chip8_scheduler_advance(&chip8, &scheduler, 0, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(chip8.key_wait_active && chip8.registers[1] == 0);
    chip8_set_key(&chip8, 0xA, 0);
    assert(chip8_scheduler_advance(&chip8, &scheduler, 0, 0,
                                   NULL, NULL, error, sizeof(error)));
    assert(!chip8.key_wait_active && chip8.registers[1] == 0xA);
}

static void test_headless_cycles_tick_at_sixty_hertz(void) {
    Chip8 chip8;
    Chip8Scheduler scheduler;
    unsigned ticks = 0;
    char error[128];
    load_loop(&chip8);
    chip8.delay_timer = 120;
    chip8.sound_timer = 120;
    chip8_scheduler_init(&scheduler, CHIP8_DEFAULT_CPU_HZ);
    assert(chip8_scheduler_run_cycles(&chip8, &scheduler, 600, count_tick,
                                      &ticks, error, sizeof(error)));
    assert(ticks == 60);
    assert(chip8.delay_timer == 60 && chip8.sound_timer == 60);
}

int main(void) {
    test_timer_cadence_independent_of_frames();
    test_pause_discards_elapsed_time();
    test_live_stall_catchup_is_capped();
    test_key_wait_releases_and_timers_continue();
    test_headless_cycles_tick_at_sixty_hertz();
    puts("scheduler tests passed");
    return 0;
}
