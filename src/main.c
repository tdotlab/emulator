#define _POSIX_C_SOURCE 200809L
#include "audio.h"
#include "chip8.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CYCLES 2000
#define DEFAULT_SPEED 10
#define FRAME_NANOSECONDS 16666667L
#define KEY_HOLD_FRAMES 5

typedef enum {
    SOUND_OFF,
    SOUND_LIVE,
    SOUND_WAV
} SoundMode;

typedef struct {
    const char *rom_path;
    const char *dump_path;
    const char *sound_file;
    uint32_t seed;
    int cycles;
    int speed;
    int play;
    int selftest;
    int render_full;
    int sound_specified;
    int sound_file_specified;
    SoundMode sound_mode;
} Options;

typedef struct {
    struct termios original;
    int active;
} Terminal;

static const uint8_t selftest_rom[] = {
    0x00, 0xE0, 0x60, 0x00, 0x61, 0x00, 0xA2, 0x0C, 0xD0, 0x15,
    0x12, 0x00, 0xF0, 0x90, 0x90, 0x90, 0xF0
};

static volatile sig_atomic_t stop_requested;
static Terminal terminal;

static void request_stop(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void terminal_restore(void) {
    if (!terminal.active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &terminal.original);
    terminal.active = 0;
    fputs("\033[?25h\n", stdout);
    fflush(stdout);
}

static int terminal_open(char *error, size_t error_capacity) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        snprintf(error, error_capacity, "interactive mode needs a terminal for input and display");
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &terminal.original) != 0) {
        snprintf(error, error_capacity, "cannot read terminal settings: %s", strerror(errno));
        return 0;
    }

    struct termios raw = terminal.original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        snprintf(error, error_capacity, "cannot set terminal input mode: %s", strerror(errno));
        return 0;
    }

    terminal.active = 1;
    atexit(terminal_restore);
    fputs("\033[?25l\033[2J\033[H", stdout);
    fflush(stdout);
    return 1;
}

static int parse_number(const char *text, long minimum, long maximum, long *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
        "Nacre\nUsage: %s [ROM] [options]\n"
        "  --play                 run with terminal controls and live audio\n"
        "  --cycles N             instruction count in headless mode (default 2000)\n"
        "  --speed N              instructions per frame in play mode (default 10)\n"
        "  --sound on|off|wav     choose live audio, silence, or WAV capture\n"
        "  --sound-file PATH      WAV output path (default nacre.wav)\n"
        "  --dump PATH            write the framebuffer to a file instead of stdout\n"
        "  --render half|full     choose terminal pixel rendering (default half)\n"
        "  --seed N               set the random-number seed\n"
        "  --selftest             run the built-in framebuffer demo\n"
        "  -h, --help             show this help\n",
        program);
}

static int parse_options(int argc, char **argv, Options *options, int *show_help) {
    memset(options, 0, sizeof(*options));
    options->cycles = DEFAULT_CYCLES;
    options->speed = DEFAULT_SPEED;
    options->sound_mode = SOUND_OFF;
    options->sound_file = "nacre.wav";
    options->seed = (uint32_t)time(NULL) ^ ((uint32_t)getpid() * 2654435761u);
    *show_help = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        long number;
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            *show_help = 1;
        } else if (!strcmp(arg, "--play")) {
            options->play = 1;
        } else if (!strcmp(arg, "--selftest")) {
            options->selftest = 1;
        } else if (!strcmp(arg, "--cycles") || !strcmp(arg, "--speed") ||
                   !strcmp(arg, "--seed") || !strcmp(arg, "--dump") ||
                   !strcmp(arg, "--sound") || !strcmp(arg, "--sound-file") ||
                   !strcmp(arg, "--render")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", arg);
                return 0;
            }
            const char *value = argv[++i];
            if (!strcmp(arg, "--cycles")) {
                if (!parse_number(value, 1, 10000000, &number)) {
                    fprintf(stderr, "--cycles must be between 1 and 10000000\n");
                    return 0;
                }
                options->cycles = (int)number;
            } else if (!strcmp(arg, "--speed")) {
                if (!parse_number(value, 1, 1000, &number)) {
                    fprintf(stderr, "--speed must be between 1 and 1000\n");
                    return 0;
                }
                options->speed = (int)number;
            } else if (!strcmp(arg, "--seed")) {
                if (!parse_number(value, 1, UINT32_MAX, &number)) {
                    fprintf(stderr, "--seed must be between 1 and 4294967295\n");
                    return 0;
                }
                options->seed = (uint32_t)number;
            } else if (!strcmp(arg, "--dump")) {
                options->dump_path = value;
            } else if (!strcmp(arg, "--sound")) {
                options->sound_specified = 1;
                if (!strcmp(value, "on")) options->sound_mode = SOUND_LIVE;
                else if (!strcmp(value, "off")) options->sound_mode = SOUND_OFF;
                else if (!strcmp(value, "wav")) options->sound_mode = SOUND_WAV;
                else {
                    fprintf(stderr, "unknown sound mode '%s' (choose on, off, or wav)\n", value);
                    return 0;
                }
            } else if (!strcmp(arg, "--sound-file")) {
                options->sound_file = value;
                options->sound_file_specified = 1;
            } else if (!strcmp(arg, "--render")) {
                if (!strcmp(value, "half")) options->render_full = 0;
                else if (!strcmp(value, "full")) options->render_full = 1;
                else {
                    fprintf(stderr, "unknown render mode '%s' (choose half or full)\n", value);
                    return 0;
                }
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "unknown option '%s'\n", arg);
            return 0;
        } else if (!options->rom_path) {
            options->rom_path = arg;
        } else {
            fprintf(stderr, "only one ROM path may be supplied\n");
            return 0;
        }
    }

    if (*show_help) return 1;
    if (!options->sound_specified) options->sound_mode = options->play ? SOUND_LIVE : SOUND_OFF;
    if (options->selftest && options->rom_path) {
        fprintf(stderr, "--selftest cannot be combined with a ROM path\n");
        return 0;
    }
    if (options->play && !options->rom_path && !options->selftest) {
        fprintf(stderr, "play mode needs a ROM path (or --selftest)\n");
        return 0;
    }
    if (!options->play && !options->rom_path && !options->selftest) {
        fprintf(stderr, "provide a ROM path or use --selftest\n");
        return 0;
    }
    if (options->sound_mode == SOUND_LIVE && !options->play) {
        fprintf(stderr, "live sound needs --play; use --sound wav for headless capture\n");
        return 0;
    }
    if (options->sound_file_specified && options->sound_mode != SOUND_WAV) {
        fprintf(stderr, "--sound-file only applies to --sound wav\n");
        return 0;
    }
    return 1;
}

static int load_machine(Chip8 *chip8, const Options *options, char *error, size_t error_capacity) {
    if (options->selftest) {
        return chip8_load_bytes(chip8, selftest_rom, sizeof(selftest_rom), error, error_capacity);
    }
    return chip8_load_rom(chip8, options->rom_path, error, error_capacity);
}

static int write_framebuffer(const Chip8 *chip8, const char *path) {
    FILE *file = stdout;
    if (path && strcmp(path, "-") != 0) {
        file = fopen(path, "w");
        if (!file) {
            fprintf(stderr, "cannot open framebuffer output '%s': %s\n", path, strerror(errno));
            return 0;
        }
    }

    for (int y = 0; y < CHIP8_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < CHIP8_DISPLAY_WIDTH; x++) {
            size_t pixel = (size_t)y * CHIP8_DISPLAY_WIDTH + x;
            fputc(chip8->display[pixel] ? '#' : '.', file);
        }
        fputc('\n', file);
    }
    int success = ferror(file) == 0;
    if (file != stdout && fclose(file) != 0) success = 0;
    if (!success) fprintf(stderr, "failed while writing framebuffer output\n");
    return success;
}

static int key_from_char(char character) {
    switch (character) {
        case '1': return 0x1; case '2': return 0x2; case '3': return 0x3; case '4': return 0xC;
        case 'q': return 0x4; case 'w': return 0x5;
        case 'e': return 0x6; case 'r': return 0xD;
        case 'a': return 0x7; case 's': return 0x8;
        case 'd': return 0x9; case 'f': return 0xE;
        case 'z': return 0xA; case 'x': return 0x0;
        case 'c': return 0xB; case 'v': return 0xF;
        default: return -1;
    }
}

static void render_display(const Chip8 *chip8, int full) {
    if (full) {
        for (int y = 0; y < CHIP8_DISPLAY_HEIGHT; y++) {
            for (int x = 0; x < CHIP8_DISPLAY_WIDTH; x++) {
                size_t pixel = (size_t)y * CHIP8_DISPLAY_WIDTH + x;
                fputs(chip8->display[pixel] ? "██" : "  ", stdout);
            }
            fputc('\n', stdout);
        }
        return;
    }

    for (int y = 0; y < CHIP8_DISPLAY_HEIGHT; y += 2) {
        for (int x = 0; x < CHIP8_DISPLAY_WIDTH; x++) {
            size_t upper = (size_t)y * CHIP8_DISPLAY_WIDTH + x;
            size_t lower = (size_t)(y + 1) * CHIP8_DISPLAY_WIDTH + x;
            int top = chip8->display[upper] != 0;
            int bottom = chip8->display[lower] != 0;
            if (top && bottom) fputs("█", stdout);
            else if (top) fputs("▀", stdout);
            else if (bottom) fputs("▄", stdout);
            else fputc(' ', stdout);
        }
        fputc('\n', stdout);
    }
}

static void set_key_for_frames(Chip8 *chip8, int key, int frames, int durations[16]) {
    if (key < 0 || key >= 16) return;
    durations[key] = frames;
    chip8_set_key(chip8, (uint8_t)key, 1);
}

static void poll_input(Chip8 *chip8, int durations[16], int *running,
                       int *paused, int *speed, int *sound_enabled,
                       SoundMode sound_mode, int *reset_requested) {
    struct pollfd input = { STDIN_FILENO, POLLIN, 0 };
    for (;;) {
        int ready = poll(&input, 1, 0);
        if (ready <= 0) break;
        if (input.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            *running = 0;
            break;
        }
        if (!(input.revents & POLLIN)) break;

        char buffer[64];
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; i++) {
            char key = buffer[i];
            if (key == 27) {
                *running = 0;
                break;
            } else if (key == 'p' || key == 'P') {
                *paused = !*paused;
            } else if (key == 'R') {
                *reset_requested = 1;
            } else if (key == '+' || key == '=') {
                if (*speed < 1000) (*speed)++;
            } else if (key == '-' || key == '_') {
                if (*speed > 1) (*speed)--;
            } else if (key == 'm' || key == 'M') {
                if (sound_mode != SOUND_OFF) *sound_enabled = !*sound_enabled;
            } else {
                set_key_for_frames(chip8, key_from_char(key), KEY_HOLD_FRAMES, durations);
            }
        }
        if (!*running) break;
        input.revents = 0;
    }
}

static void draw_status(int speed, int paused, int sound_enabled, SoundMode mode) {
    if (paused) {
        fputs("Nacre  PAUSED  [p resume, R reset, Esc quit]\n", stdout);
    } else {
        const char *sound = mode == SOUND_OFF ? "off" : (sound_enabled ? "on" : "muted");
        fprintf(stdout,
                "Nacre  %d cycles/frame  sound %s  [p pause, R reset, +/- speed, m mute, Esc quit]\n",
                speed, sound);
    }
}

static void add_frame_duration(struct timespec *deadline) {
    deadline->tv_nsec += FRAME_NANOSECONDS;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static void wait_for_frame(const struct timespec *deadline) {
    struct timespec now;
    for (;;) {
        if (stop_requested || clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
        struct timespec remaining = {
            deadline->tv_sec - now.tv_sec,
            deadline->tv_nsec - now.tv_nsec
        };
        if (remaining.tv_nsec < 0) {
            remaining.tv_sec--;
            remaining.tv_nsec += 1000000000L;
        }
        if (remaining.tv_sec < 0 || (remaining.tv_sec == 0 && remaining.tv_nsec == 0)) return;
        if (nanosleep(&remaining, NULL) == 0) return;
        if (errno != EINTR) return;
    }
}

static int run_interactive(Chip8 *chip8, const Options *options, int *sound_enabled,
                           char *error, size_t error_capacity) {
    if (!terminal_open(error, error_capacity)) return 0;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    int running = 1;
    int paused = 0;
    int speed = options->speed;
    int key_durations[16] = {0};
    int reset_requested = 0;
    int instruction_error = 0;
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);

    while (running && !stop_requested) {
        poll_input(chip8, key_durations, &running, &paused, &speed,
                   sound_enabled, options->sound_mode, &reset_requested);
        if (!running) break;

        for (int key = 0; key < 16; key++) {
            if (key_durations[key] > 0) key_durations[key]--;
            chip8_set_key(chip8, (uint8_t)key, key_durations[key] > 0);
        }

        if (reset_requested) {
            if (!load_machine(chip8, options, error, error_capacity)) {
                instruction_error = 1;
                break;
            }
            memset(key_durations, 0, sizeof(key_durations));
            reset_requested = 0;
            paused = 0;
        }

        if (!paused) {
            for (int cycle = 0; cycle < speed; cycle++) {
                if (!chip8_step(chip8, error, error_capacity)) {
                    instruction_error = 1;
                    running = 0;
                    break;
                }
            }
        }

        chip8_audio_set_active(!paused && *sound_enabled && chip8->sound_timer > 0);
        chip8_audio_tick();
        if (!paused) chip8_tick_timers(chip8);

        fputs("\033[H", stdout);
        draw_status(speed, paused, *sound_enabled, options->sound_mode);
        render_display(chip8, options->render_full);
        fflush(stdout);

        add_frame_duration(&deadline);
        wait_for_frame(&deadline);
    }

    chip8_audio_set_active(0);
    terminal_restore();
    return !instruction_error;
}

static int run_headless(Chip8 *chip8, const Options *options) {
    int timer_counter = 0;
    for (int cycle = 0; cycle < options->cycles; cycle++) {
        char error[160];
        if (!chip8_step(chip8, error, sizeof(error))) {
            fprintf(stderr, "emulation stopped: %s\n", error);
            return 0;
        }
        if (++timer_counter == 17) {
            chip8_audio_set_active(chip8->sound_timer > 0);
            chip8_audio_tick();
            chip8_tick_timers(chip8);
            timer_counter = 0;
        }
    }
    return write_framebuffer(chip8, options->dump_path);
}

int main(int argc, char **argv) {
    Options options;
    int show_help;
    if (!parse_options(argc, argv, &options, &show_help)) {
        print_usage(stderr, argv[0]);
        return 2;
    }
    if (show_help) {
        print_usage(stdout, argv[0]);
        return 0;
    }

    Chip8 chip8;
    chip8_init(&chip8, options.seed);
    char error[192];
    if (!load_machine(&chip8, &options, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    int sound_enabled = options.sound_mode != SOUND_OFF;
    if (options.sound_mode == SOUND_LIVE) {
        if (!chip8_audio_open_live(error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
    } else if (options.sound_mode == SOUND_WAV) {
        if (!chip8_audio_open_wav(options.sound_file, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
    }
    atexit(chip8_audio_close);

    int success;
    if (options.play) {
        success = run_interactive(&chip8, &options, &sound_enabled, error, sizeof(error));
        if (!success) fprintf(stderr, "%s\n", error);
    } else {
        success = run_headless(&chip8, &options);
    }
    chip8_audio_close();
    return success ? 0 : 1;
}
