#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include <stdint.h>
#include <stddef.h>

#define AUDIO_PLAYBACK_MAX_PATH 64

int audio_playback_init(void);
int audio_playback_play_file(const char *path);
int audio_playback_play_buffer(const uint8_t *data, size_t len);
int audio_playback_play_embedded(void);
int audio_playback_stop(void);
int audio_playback_pause(void);
int audio_playback_resume(void);
int audio_playback_set_volume(uint8_t volume_percent);

#endif
