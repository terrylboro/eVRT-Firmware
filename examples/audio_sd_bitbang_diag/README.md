# Audio SD Bitbang Diagnostic

This example parks the manual SD-card SPI bring-up diagnostics used while
debugging wired MicroSD connections on the BL54L15u DVK.

## Wiring

| BL54L15u DVK | SD signal |
| --- | --- |
| P1.04 | SCK |
| P1.05 | DI / MOSI |
| P1.06 | DO / MISO |
| P1.07 | CS |
| 3V3 | VCC |
| GND | GND |

The diagnostic performs a manual low-speed command sequence before any Zephyr
filesystem access:

```text
CMD0
CMD8
CMD55 / ACMD41
CMD58
```

## Build and Flash

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_sd_bitbang -p always examples/audio_sd_bitbang_diag -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_sd_bitbang
```
