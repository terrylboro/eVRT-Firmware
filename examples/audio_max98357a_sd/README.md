# MAX98357A SD WAV Playback Baseline

This example ports the working `wav-zephyr-control` playback path to the
eVRT-Firmware layout. It plays a 16-bit PCM WAV file from the Adafruit Audio BFF
MicroSD card through the MAX98357A I2S amplifier.

## Wiring

I2S amp wiring:

| BL54L15u DVK | MAX98357A BFF |
| --- | --- |
| P1.08 | BCLK |
| P1.09 | LRC / WS |
| P1.10 | DIN |
| 3V3 | VIN |
| GND | GND |

The example overlay also defines a MicroSD SPI mapping using default-connected
header pins:

| BL54L15u DVK | BFF MicroSD |
| --- | --- |
| P1.04 | SCK |
| P1.05 | MOSI |
| P1.06 | MISO |
| P1.07 | CS |

Adjust the SPI pins in `overlays/bl54l15u_dvk.overlay` if your wiring differs.
The SD SPI clock is intentionally limited to 250 kHz for wired bring-up.
The overlay disables `uart20` and moves the console to `uart30` because the
board default UART20 pins are reused for SD SPI in this baseline.

## SD Card

Format the card as FAT/FAT32 and place a 16-bit PCM WAV file named:

```text
INSTRU~1.WAV
```

Mono and stereo files are supported. Mono files are duplicated to stereo before
I2S output.

## Build and Flash

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_audio -p always examples/audio_max98357a_sd -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_audio
```

Open the second virtual COM port, usually `VCOM1`, at `115200 8N1`.

## Expected Output

```text
MAX98357A SD WAV playback baseline
Audio playback ready on i2s@dd000
Audio volume: 40%
Audio SD mounted at /SD:
Audio play: /SD:/INSTRU~1.WAV
WAV: ... Hz, ... channel(s), 16-bit, ... data bytes
Audio playback complete
```
