#ifndef CHIP8_AUDIO_H
#define CHIP8_AUDIO_H

#include <stddef.h>

int chip8_audio_open_live(char *error, size_t error_capacity);
int chip8_audio_open_wav(const char *path, char *error, size_t error_capacity);
void chip8_audio_set_active(int active);
void chip8_audio_tick(void);
void chip8_audio_close(void);

#endif
