#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifdef CHIP8_HAVE_SDL2
#include <SDL.h>
#endif

#define AUDIO_RATE 48000
#define TONE_FREQUENCY 440.0
#define TONE_VOLUME 0.20
#define RAMP_SECONDS 0.005
#define PI 3.14159265358979323846

static FILE *wav_file;
static uint64_t wav_sample_count;
static int sound_active;
static double phase;
static double envelope;

#ifdef CHIP8_HAVE_SDL2
static SDL_AudioDeviceID audio_device;
static SDL_atomic_t live_sound_active;
#endif

static int16_t next_sample(int active) {
    double target = active ? 1.0 : 0.0;
    double ramp = 1.0 / (AUDIO_RATE * RAMP_SECONDS);
    if (envelope < target) {
        envelope += ramp;
        if (envelope > target) envelope = target;
    } else if (envelope > target) {
        envelope -= ramp;
        if (envelope < target) envelope = target;
    }

    double value = sin(phase * 2.0 * PI) * envelope * TONE_VOLUME;
    phase += TONE_FREQUENCY / AUDIO_RATE;
    if (phase >= 1.0) phase -= 1.0;
    return (int16_t)(value * 32767.0);
}

static void write_u16(FILE *file, uint16_t value) {
    fputc(value & 0xFF, file);
    fputc((value >> 8) & 0xFF, file);
}

static void write_u32(FILE *file, uint32_t value) {
    fputc(value & 0xFF, file);
    fputc((value >> 8) & 0xFF, file);
    fputc((value >> 16) & 0xFF, file);
    fputc((value >> 24) & 0xFF, file);
}

static void write_wav_header(FILE *file, uint32_t data_size) {
    fputs("RIFF", file);
    write_u32(file, 36u + data_size);
    fputs("WAVEfmt ", file);
    write_u32(file, 16);
    write_u16(file, 1);                  /* PCM */
    write_u16(file, 1);                  /* mono */
    write_u32(file, AUDIO_RATE);
    write_u32(file, AUDIO_RATE * 2);      /* bytes per second */
    write_u16(file, 2);                  /* block size */
    write_u16(file, 16);                 /* bits per sample */
    fputs("data", file);
    write_u32(file, data_size);
}

#ifdef CHIP8_HAVE_SDL2
static void audio_callback(void *userdata, Uint8 *stream, int length) {
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int count = length / (int)sizeof(*samples);
    int active = SDL_AtomicGet(&live_sound_active) != 0;
    for (int i = 0; i < count; i++) {
        samples[i] = next_sample(active);
    }
    if (length % (int)sizeof(*samples) != 0) {
        stream[length - 1] = 0;
    }
}
#endif

int chip8_audio_open_live(char *error, size_t error_capacity) {
#ifdef CHIP8_HAVE_SDL2
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        if (error && error_capacity > 0) {
            snprintf(error, error_capacity, "could not initialize SDL audio: %s", SDL_GetError());
        }
        return 0;
    }

    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq = AUDIO_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples = 512;
    wanted.callback = audio_callback;
    SDL_AtomicSet(&live_sound_active, 0);
    audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio_device) {
        if (error && error_capacity > 0) {
            snprintf(error, error_capacity, "could not open an audio device: %s", SDL_GetError());
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_PauseAudioDevice(audio_device, 0);
    return 1;
#else
    if (error && error_capacity > 0) {
        snprintf(error, error_capacity,
                 "this build has no SDL2 audio support; install SDL2 development files and rebuild");
    }
    return 0;
#endif
}

int chip8_audio_open_wav(const char *path, char *error, size_t error_capacity) {
    wav_file = fopen(path, "wb");
    if (!wav_file) {
        if (error && error_capacity > 0) {
            snprintf(error, error_capacity, "cannot open WAV output '%s'", path);
        }
        return 0;
    }
    wav_sample_count = 0;
    sound_active = 0;
    phase = 0.0;
    envelope = 0.0;
    write_wav_header(wav_file, 0);
    return 1;
}

void chip8_audio_set_active(int active) {
    sound_active = active != 0;
#ifdef CHIP8_HAVE_SDL2
    SDL_AtomicSet(&live_sound_active, sound_active);
#endif
}

void chip8_audio_tick(void) {
    if (!wav_file) return;
    const int samples_per_tick = AUDIO_RATE / 60;
    for (int i = 0; i < samples_per_tick; i++) {
        uint16_t sample = (uint16_t)next_sample(sound_active);
        fputc(sample & 0xFF, wav_file);
        fputc((sample >> 8) & 0xFF, wav_file);
        wav_sample_count++;
    }
}

void chip8_audio_close(void) {
    chip8_audio_set_active(0);
    if (wav_file) {
        uint64_t max_samples = (UINT32_MAX - 36u) / 2u;
        if (wav_sample_count <= max_samples) {
            if (fseek(wav_file, 0, SEEK_SET) == 0) {
                write_wav_header(wav_file, (uint32_t)(wav_sample_count * 2u));
            }
        }
        fclose(wav_file);
        wav_file = NULL;
    }
#ifdef CHIP8_HAVE_SDL2
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif
}
