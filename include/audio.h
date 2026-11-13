#ifndef CHIP8_AUDIO_H
#define CHIP8_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define CHIP8_AUDIO_SAMPLE_RATE 48000

typedef struct {
    double phase;
    double envelope;
} Chip8AudioOscillator;

void chip8_audio_oscillator_reset(Chip8AudioOscillator *oscillator);
void chip8_audio_render_samples(Chip8AudioOscillator *oscillator,
                               int16_t *samples, size_t sample_count,
                               int active);

int chip8_audio_open_live(char *error, size_t error_capacity);
int chip8_audio_open_wav(const char *path, char *error, size_t error_capacity);
void chip8_audio_set_active(int active);
void chip8_audio_tick(void);
void chip8_audio_close(void);

#endif
