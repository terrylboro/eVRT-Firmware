# `app/main.c`

This file is the Zephyr application entry point for the combined eVRT firmware
bring-up image (in app/main.c). `main()` prints the firmware
flow identifier, calls `evrt_app_run()`, and returns only if the application
runner exits.

## Startup flow

The implementation of the runtime flow is in [`evrt_app.c`](evrt_app.c):

1. Initialize audio playback, set volume to 100%, and initialize BLE control.
2. Play the embedded `assets/INST.WAV` test file.
3. Initialize the ADS1292R over SPI and print samples for 20 seconds.
4. Stop ADS1292R sampling and initialize the BNO055 IMU.
5. Start the IMU polling and ADS print worker threads.
6. Stay in the BLE command loop.

The `main()` log line is:

```text
eVRT firmware build: audio-then-ads-test-flow v1
```

The process depends on the board-specific devicetree overlay for the connected
hardware. The root `CMakeLists.txt` embeds `assets/INST.WAV` when that file is
present; without it, boot-time audio playback fails with `-ENOENT`.

## Current BLE commands

Commands are received as text and passed to the application queue:

| Command | Behavior |
| --- | --- |
| `PLAY:INST.WAV` | Play the embedded test file |
| `PLAY:embedded INST.WAV` | Play the embedded test file |
| `VOL:0` through `VOL:100` | Set playback volume |
| `PAUSE` | Pause playback |
| `RESUME` | Resume playback |
| `STOP` | Stop playback |
| `ADS_START` | Start ADS1292R acquisition and sample printing |
| `ADS_STOP` | Stop ADS1292R acquisition and sample printing |

Only the embedded `INST.WAV` asset is available in this build; arbitrary file
paths are rejected.

## Build and flash

From an nRF Connect SDK terminal at the repository root, the current Nordic
nRF54L15 DK build is:

```powershell
west build -b "nrf54l15dk/nrf54l15/cpuapp" . -d build_nrf54l15dk_evrt_combined -p always -- "-DDTC_OVERLAY_FILE=overlays/nrf54l15dk.overlay"
west flash -d build_nrf54l15dk_evrt_combined
```

The latest bring-up session used the same nRF54L15 application target for the
BNO055 probe and completed with exit code 0. Use the overlay documentation for
board-specific ADS1292R wiring and alternate targets.

## Console output

The project enables SEGGER RTT as the console and disables the UART console.
Connect with an RTT viewer and watch for these milestones:

```text
Audio playback complete; starting ADS1292R internal test signal
ADS printing deliberately stopped after 20 seconds
BNO055 initialized: chip_id=0xa0
BLE control ready. Commands: PLAY:<file>, VOL:<0-100>, ADS_START, ADS_STOP
```

The BNO055 chip ID line is expected only when the IMU is connected and responds
successfully. Initialization errors are logged and the application continues to
the BLE command loop where possible.

## Related files

- [`evrt_app.c`](evrt_app.c): orchestration, worker threads, and command handling
- [`evrt_app.h`](evrt_app.h): public application-runner declaration
- [`../assets/README.md`](../assets/README.md): embedded audio asset requirements
- [`../overlays/README.md`](../overlays/README.md): board overlays and wiring
- [`../SPI_BRINGUP.md`](../SPI_BRINGUP.md): ADS1292R SPI transport details