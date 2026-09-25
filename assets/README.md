# Audio Assets

Place the boot-time WAV file at:

```text
assets/INST.WAV
```

When present, the root application build embeds this file into the firmware
image and `audio_playback_play_embedded()` streams it directly from flash.
