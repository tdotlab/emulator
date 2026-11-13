/* chip8.c - minimal Chip-8 interpreter (core + CLI) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#define MEM_SIZE 4096
#define DISP_W 64
#define DISP_H 32
#define STACK_SIZE 16
#define NUM_REGS 16

static unsigned char memory[MEM_SIZE];
static unsigned char V[NUM_REGS];
static unsigned short I;
static unsigned short pc;
static unsigned short stack[STACK_SIZE];
static int sp;
static unsigned char delay_timer;
static unsigned char sound_timer;
static unsigned char display[DISP_W * DISP_H];
static unsigned char keys[16];
static int draw_flag;
static int key_timer[16];
static int pause_state = 0;
static int pause_lock = 0;

/* sound output: -1 unset, 0=off, 1=terminal bell, 2=wav file */
static int sound_mode = -1;
static const char *sound_wav_path = "chip8.wav";
static FILE *wav_file = NULL;
static unsigned long wav_sample_count = 0;
static int wav_phase = 0;
#define WAV_RATE 8000
#define WAV_FREQ 440
#define WAV_AMP 12000

/* embedded self-test ROM: clear + draw digit 0 at (0,0) */
static const unsigned char selftest_rom[] = {
    0x00,0xE0, 0x60,0x00, 0x61,0x00, 0xA2,0x0A, 0xD0,0x15,
    0xF0,0x90,0x90,0x90,0xF0, 0x12,0x00
};
static int selftest_mode = 0;

/* font data for digits 0-9, 5 bytes each, starting at 0x050 */
static const unsigned char fontset[80] = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0
};

static void init(void) {
    memset(memory, 0, MEM_SIZE);
    memset(V, 0, NUM_REGS);
    memset(display, 0, DISP_W * DISP_H);
    memset(keys, 0, 16);
    memset(key_timer, 0, sizeof(key_timer));
    I = 0; pc = 0x200; sp = 0;
    delay_timer = 0; sound_timer = 0; draw_flag = 0;
    memcpy(memory + 0x050, fontset, sizeof(fontset));
    srand((unsigned)time(NULL));
}

static int load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (MEM_SIZE - 0x200)) { fclose(f); return 0; }
    size_t n = fread(memory + 0x200, 1, sz, f);
    fclose(f);
    return (int)n == sz;
}

static void clear_display(void) { memset(display, 0, DISP_W * DISP_H); draw_flag = 1; }

static void draw_sprite(int x, int y, int n) {
    V[0xF] = 0;
    for (int row = 0; row < n; row++) {
        unsigned char byte = memory[I + row];
        for (int col = 0; col < 8; col++) {
            if (byte & (0x80 >> col)) {
                int px = (x + col) % DISP_W;
                int py = (y + row) % DISP_H;
                int idx = py * DISP_W + px;
                if (display[idx]) V[0xF] = 1;
                display[idx] ^= 1;
            }
        }
    }
    draw_flag = 1;
}

static void dump_display(const char *path) {
    FILE *f;
    if (path && strcmp(path, "-") != 0) f = fopen(path, "w");
    else f = stdout;
    if (!f) return;
    for (int y = 0; y < DISP_H; y++) {
        for (int x = 0; x < DISP_W; x++) fputc(display[y*DISP_W+x] ? '#' : '.', f);
        fputc('\n', f);
    }
    if (f != stdout) fclose(f);
}

static int step(void) {
    if (pc > MEM_SIZE - 2) return -1;
    unsigned short opcode = (memory[pc] << 8) | memory[pc + 1];
    pc += 2;
    int x = (opcode >> 8) & 0x0F;
    int y = (opcode >> 4) & 0x0F;
    int n = opcode & 0x0F;
    int kk = opcode & 0xFF;
    int nnn = opcode & 0xFFF;

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) clear_display();
            else if (opcode == 0x00EE) { if (sp > 0) pc = stack[--sp]; }
            break;
        case 0x1000: pc = nnn; break;
        case 0x2000: if (sp < STACK_SIZE) stack[sp++] = pc; pc = nnn; break;
        case 0x3000: if (V[x] == kk) pc += 2; break;
        case 0x4000: if (V[x] != kk) pc += 2; break;
        case 0x5000: if (V[x] == V[y]) pc += 2; break;
        case 0x6000: V[x] = kk; break;
        case 0x7000: V[x] += kk; break;
        case 0x8000:
            switch (n) {
                case 0x0: V[x] = V[y]; break;
                case 0x1: V[x] |= V[y]; break;
                case 0x2: V[x] &= V[y]; break;
                case 0x3: V[x] ^= V[y]; break;
                case 0x4: { int s = V[x] + V[y]; V[x] = s & 0xFF; V[0xF] = (s > 255); } break;
                case 0x5: { int borrow = (V[x] >= V[y]); V[x] = (V[x] - V[y]) & 0xFF; V[0xF] = borrow; } break;
                case 0x6: { int lsb = V[x] & 1; V[x] >>= 1; V[0xF] = lsb; } break;
                case 0x7: { int borrow = (V[y] >= V[x]); V[x] = (V[y] - V[x]) & 0xFF; V[0xF] = borrow; } break;
                case 0xE: { int msb = (V[x] >> 7) & 1; V[x] <<= 1; V[0xF] = msb; } break;
            }
            break;
        case 0x9000: if (V[x] != V[y]) pc += 2; break;
        case 0xA000: I = nnn; break;
        case 0xB000: pc = (V[0] + nnn) & 0xFFF; break;
        case 0xC000: V[x] = (rand() & 0xFF) & kk; break;
        case 0xD000: draw_sprite(V[x], V[y], n); break;
        case 0xE000:
            if (kk == 0x9E) { if (keys[V[x] & 0xF]) pc += 2; }
            else if (kk == 0xA1) { if (!keys[V[x] & 0xF]) pc += 2; }
            break;
        case 0xF000:
            switch (kk) {
                case 0x07: V[x] = delay_timer; break;
                case 0x0A: {
                    /* wait for key */
                    int pressed = -1;
                    for (int i = 0; i < 16; i++) if (keys[i]) { pressed = i; break; }
                    if (pressed < 0) { pc -= 2; } /* repeat */
                    else V[x] = pressed;
                    break;
                }
                case 0x15: delay_timer = V[x]; break;
                case 0x18: sound_timer = V[x]; break;
                case 0x1E: I = (I + V[x]) & 0xFFF; break;
                case 0x29: I = 0x050 + (V[x] & 0x0F) * 5; break;
                case 0x33:
                    memory[I] = V[x] / 100;
                    memory[I+1] = (V[x] / 10) % 10;
                    memory[I+2] = V[x] % 10;
                    break;
                case 0x55:
                    for (int i = 0; i <= x; i++) memory[I + i] = V[i];
                    break;
                case 0x65:
                    for (int i = 0; i <= x; i++) V[i] = memory[I + i];
                    break;
            }
            break;
    }
    return 0;
}

static struct termios orig_termios;
static int termios_set = 0;

static void restore_terminal(void) {
    if (termios_set) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_set = 0;
    }
    /* show cursor */
    fputs("\x1b[?25h", stdout);
    fflush(stdout);
}

static void setup_terminal(void) {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    termios_set = 1;
    atexit(restore_terminal);
    fputs("\x1b[?25l", stdout); /* hide cursor */
    fflush(stdout);
}

static int keymap(int c) {
    /* returns chip8 key 0-15 or -1 */
    switch (c) {
        case '1': return 0x1; case '2': return 0x2; case '3': return 0x3; case '4': return 0xC;
        case 'q': case 'Q': return 0x4; case 'w': case 'W': return 0x5; case 'e': case 'E': return 0x6; case 'r': case 'R': return 0xD;
        case 'a': case 'A': return 0x7; case 's': case 'S': return 0x8; case 'd': case 'D': return 0x9; case 'f': case 'F': return 0xE;
        case 'z': case 'Z': return 0xA; case 'x': case 'X': return 0x0; case 'c': case 'C': return 0xB; case 'v': case 'V': return 0xF;
        default: return -1;
    }
}

static int render_half = 1;

static void render_display(void) {
    if (render_half) {
        /* 64x16 using half-block characters */
        for (int y = 0; y < DISP_H; y += 2) {
            for (int x = 0; x < DISP_W; x++) {
                int top = display[y*DISP_W+x];
                int bot = display[(y+1)*DISP_W+x];
                if (top && bot) fputs("\xe2\x96\x88", stdout); /* █ */
                else if (top) fputs("\xe2\x96\x80", stdout);   /* ▀ */
                else if (bot) fputs("\xe2\x96\x84", stdout);   /* ▄ */
                else fputs(" ", stdout);
            }
            fputc('\n', stdout);
        }
    } else {
        for (int y = 0; y < DISP_H; y++) {
            for (int x = 0; x < DISP_W; x++) {
                if (display[y*DISP_W+x]) fputs("\xe2\x96\x88\xe2\x96\x88", stdout); /* ██ */
                else fputs("  ", stdout);
            }
            fputc('\n', stdout);
        }
    }
    fflush(stdout);
}

static void wav_put_u16(FILE *f, unsigned int v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}

static void wav_put_u32(FILE *f, unsigned long v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

static void wav_write_header(FILE *f, unsigned long data_bytes) {
    fputs("RIFF", f);
    wav_put_u32(f, 36 + data_bytes);
    fputs("WAVE", f);
    fputs("fmt ", f);
    wav_put_u32(f, 16);
    wav_put_u16(f, 1);            /* PCM */
    wav_put_u16(f, 1);            /* mono */
    wav_put_u32(f, WAV_RATE);
    wav_put_u32(f, WAV_RATE * 2); /* byte rate */
    wav_put_u16(f, 2);            /* block align */
    wav_put_u16(f, 16);           /* bits per sample */
    fputs("data", f);
    wav_put_u32(f, data_bytes);
}

/* Called once per 60 Hz timer tick while the sound timer is active. */
static void sound_tick(int sounding) {
    if (!sounding) return;
    if (sound_mode == 1) {
        fputc('\a', stdout);
    } else if (sound_mode == 2 && wav_file) {
        int period = WAV_RATE / WAV_FREQ; /* samples per square cycle */
        int half = period / 2;
        int n = WAV_RATE / 60;            /* samples per 60 Hz tick */
        for (int i = 0; i < n; i++) {
            short s = (wav_phase < half) ? WAV_AMP : -WAV_AMP;
            wav_put_u16(wav_file, (unsigned short)s);
            wav_sample_count++;
            wav_phase = (wav_phase + 1) % period;
        }
    }
}

static void sound_open(void) {
    if (sound_mode != 2) return;
    wav_file = fopen(sound_wav_path, "wb");
    if (!wav_file) {
        fprintf(stderr, "failed to open sound file: %s\n", sound_wav_path);
        sound_mode = 0;
        return;
    }
    wav_write_header(wav_file, 0);
}

static void sound_close(void) {
    if (!wav_file) return;
    fseek(wav_file, 0, SEEK_SET);
    wav_write_header(wav_file, wav_sample_count * 2);
    fclose(wav_file);
    wav_file = NULL;
}

static void play(const char *rom, int cpm) {
    if (rom) {
        if (!load_rom(rom)) { fprintf(stderr, "failed to load rom: %s\n", rom); return; }
    }
    setup_terminal();
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);

    int running = 1;
    int cycles_per_frame = cpm > 0 ? cpm : 10;
    int reset = 0;
    while (running) {
        /* key timers: decay */
        for (int i = 0; i < 16; i++) {
            if (key_timer[i] > 0) key_timer[i]--;
            keys[i] = (key_timer[i] > 0) ? 1 : 0;
        }

        /* input */
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        while (select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv) > 0) {
            char buf[64];
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            for (int i = 0; i < n; i++) {
                char c = buf[i];
                if (c == 27) { running = 0; break; } /* ESC quits */
                if (c == 'p' || c == 'P') {
                    if (pause_lock <= 0) { pause_state = !pause_state; pause_lock = 10; }
                } else if (c == 'Q') {
                    running = 0; break;
                } else if (c == 'R') {
                    reset = 1;
                } else if (c == '+' || c == '=') {
                    if (cycles_per_frame < 100) cycles_per_frame++;
                } else if (c == '-' || c == '_') {
                    if (cycles_per_frame > 1) cycles_per_frame--;
                } else if (c == 'm' || c == 'M') {
                    sound_mode = (sound_mode == 0) ? 1 : 0;
                } else {
                    int k = keymap(c);
                    if (k >= 0) key_timer[k] = 6;
                }
            }
            if (!running) break;
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 0;
        }
        if (pause_lock > 0) pause_lock--;

        if (reset) {
            init();
            if (rom) load_rom(rom);
            else if (selftest_mode) memcpy(memory + 0x200, selftest_rom, sizeof(selftest_rom));
            reset = 0;
            pause_state = 0;
            pause_lock = 0;
        }

        /* render header */
        fputs("\x1b[H", stdout);
        if (pause_state) {
            fputs("  *** PAUSED (p resume, R reset, ESC quit) ***\n", stdout);
        } else {
            fprintf(stdout, "  speed %d  sound %s  (p pause  R reset  +/- speed  m mute  ESC quit)\n",
                    cycles_per_frame, sound_mode ? "on" : "off");
        }

        if (!pause_state) {
            for (int c = 0; c < cycles_per_frame; c++) {
                if (step() < 0) { running = 0; break; }
            }
            sound_tick(sound_timer > 0);
            if (delay_timer > 0) delay_timer--;
            if (sound_timer > 0) sound_timer--;
        }

        render_display();

        struct timespec ts = {0, 16000000};
        nanosleep(&ts, NULL);
    }
    restore_terminal();
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

int main(int argc, char **argv) {
    init();
    int cycles = 2000;
    const char *rom = NULL;
    const char *dump = NULL;
    int selftest = 0;
    int play_mode = 0;
    int speed = 10;
    const char *sound_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cycles") && i+1 < argc) { cycles = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--dump") && i+1 < argc) { dump = argv[++i]; }
        else if (!strcmp(argv[i], "--selftest")) { selftest = 1; }
        else if (!strcmp(argv[i], "--play")) { play_mode = 1; }
        else if (!strcmp(argv[i], "--speed") && i+1 < argc) { speed = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--sound") && i+1 < argc) { sound_arg = argv[++i]; }
        else if (!strcmp(argv[i], "--sound-file") && i+1 < argc) { sound_wav_path = argv[++i]; }
        else if (!strcmp(argv[i], "--render") && i+1 < argc) {
            const char *r = argv[++i];
            if (!strcmp(r, "half")) render_half = 1;
            else if (!strcmp(r, "full")) render_half = 0;
            else { fprintf(stderr, "unknown --render mode: %s (use half or full)\n", r); return 1; }
        }
        else { rom = argv[i]; }
    }

    if (sound_arg) {
        if (!strcmp(sound_arg, "off")) sound_mode = 0;
        else if (!strcmp(sound_arg, "bell")) sound_mode = 1;
        else if (!strcmp(sound_arg, "wav")) sound_mode = 2;
        else { fprintf(stderr, "unknown --sound mode: %s (use off, bell or wav)\n", sound_arg); return 1; }
    } else {
        sound_mode = play_mode ? 1 : 0; /* bell in play mode, silent headless */
    }
    sound_open();
    atexit(sound_close);

    if (selftest) {
        selftest_mode = 1;
        memcpy(memory + 0x200, selftest_rom, sizeof(selftest_rom));
    }

    if (play_mode) {
        play(rom, speed);
        return 0;
    }

    if (!selftest) {
        if (rom) {
            if (!load_rom(rom)) { fprintf(stderr, "failed to load rom: %s\n", rom); return 1; }
        } else {
            fprintf(stderr, "usage: %s rom.ch8 [--cycles N] [--dump path|-] [--selftest] [--play] [--speed N] [--sound off|bell|wav] [--sound-file PATH] [--render half|full]\n", argv[0]);
            return 1;
        }
    }

    int timer_accum = 0;
    for (int c = 0; c < cycles; c++) {
        if (step() < 0) break;
        timer_accum++;
        if (timer_accum >= 17) { /* ~60Hz at ~1000 cps */
            sound_tick(sound_timer > 0);
            if (delay_timer > 0) delay_timer--;
            if (sound_timer > 0) sound_timer--;
            timer_accum = 0;
        }
    }
    dump_display(dump ? dump : "-");
    return 0;
}
