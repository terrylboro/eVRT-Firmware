# Audio Playback Module

Status: initial port from the known-good `wav-zephyr-control` nRF52840/XIAO
baseline.

The module streams 16-bit PCM WAV files from a filesystem path to a Zephyr I2S
TX device. It is intended first for the Adafruit Audio BFF MAX98357 I2S amp and
MicroSD board.

Current API:

```c
int audio_playback_init(void);
int audio_playback_play_file(const char *path);
int audio_playback_stop(void);
int audio_playback_pause(void);
int audio_playback_resume(void);
int audio_playback_set_volume(uint8_t volume_percent);
```

Storage is intentionally path-based. The first example uses `/SD:` from the BFF
MicroSD card, but the playback API can later read from `/lfs` or another mount
without changing the I2S layer.

Initial BL54L15u I2S pinout:

| BL54L15u pin | MAX98357A signal |
| --- | --- |
| P1.08 | BCLK |
| P1.09 | LRC / WS |
| P1.10 | DIN |
| 3V3 | VIN |
| GND | GND |

The BFF MicroSD SPI pins are example-overlay-specific and may need adjustment
to match the physical wiring used on the DVK.
