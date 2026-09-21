# MAX98357A SD WAV Playback Baseline

This example ports the working `wav-zephyr-control` playback path to the
eVRT-Firmware layout. It plays a 16-bit PCM WAV file from the Adafruit Audio BFF
MicroSD card through the MAX98357A I2S amplifier.

## Wiring

### Ezurio BL54L15u DVK

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

### Nordic nRF54L15 DK

Set the DK target voltage to 3V3 in Nordic Board Programmer before wiring the
audio module.

I2S amp wiring:

| nRF54L15 DK | MAX98357A BFF |
| --- | --- |
| P1.11 | BCLK |
| P1.12 | LRC / WS |
| P1.15 | DIN |
| 3V3 | VIN |
| GND | GND |

MicroSD SPI wiring:

| nRF54L15 DK | BFF MicroSD |
| --- | --- |
| P0.04 | SCK |
| P0.00 | MOSI |
| P0.01 | MISO |
| P0.02 | CS |

The Nordic DK overlay uses `spi30` for the SD card and `i2s20` on P1 because
I2S20 is tied to the P1 GPIO domain on nRF54L15. It disables UART30/I2C30 and
button3 while this audio test is running because those share the selected P0
pins.

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

For the Nordic nRF54L15 DK:

```powershell
west build -b "nrf54l15dk/nrf54l15/cpuapp" -d build_nrf54l15dk_audio -p always examples/audio_max98357a_sd -- "-DDTC_OVERLAY_FILE=overlays/nrf54l15dk.overlay"
west flash -d build_nrf54l15dk_audio
```

Open the DK serial console at `115200 8N1`.

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
