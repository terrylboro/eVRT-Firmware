# Board-specific ADS1292R overlays

The ADS1292R driver and application code are board-neutral.  Select the wiring
for a specific development board by passing one of these overlays at build time.

## Seeed XIAO nRF54L15

```powershell
west build -b "xiao_nRF54l15/nrf54l15/cpuapp" -d build_xiao_ads -p always -- "-DBOARD_ROOT=C:/Users/teri-/Documents/eVRT-Firmware" "-DDTC_OVERLAY_FILE=overlays/xiao_nrf54l15_ads1292r.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_xiao_ads
```

This uses the pin mapping that was validated during ADS1292R bring-up on the
XIAO board.

## Seeed XIAO BLE nRF52840

```powershell
west build -b "xiao_ble/nrf52840" -d build_xiao52840_ads -p always -- "-DDTC_OVERLAY_FILE=overlays/xiao_nrf52840_ads1292r.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_xiao52840_ads
```

For the Sense variant, use `xiao_ble/nrf52840/sense` as the board target.
This overlay uses the same physical XIAO ADS wiring as the nRF54L15 overlay:
SPI on D8/D9/D10, CS on D1, DRDY on D2, START on D3, and /PWDN/RESET on D4.

## Ezurio BL54L15u DVK, part 453-00223-K1

For nRF Connect SDK 3.1.x the board target is expected to be:

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_ads -p always -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk_ads1292r.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_bl54l15u_ads
```

In newer Zephyr versions the `bl54l15u_dvk` board was consolidated into
`bl54l15_dvk`.  If your SDK no longer has `bl54l15u_dvk`, use:

```powershell
west build -b "bl54l15_dvk/nrf54l15/cpuapp" -d build_bl54l15u_ads -p always -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk_ads1292r.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_bl54l15u_ads
```

The Ezurio overlay currently maps the ADS1292R to the DVK pins that matched the
audio SD bring-up:

| ADS1292R signal | BL54L15u DVK pin |
| --- | --- |
| SCK | P1.04 |
| DIN / MOSI | P1.05 |
| DOUT / MISO | P1.06 |
| CS | P1.07 |
| DRDY | GPIO / P1.12 |
| PWDN/RESET | P1.13 |
| START | MB_TX / P1.09 |

The overlay uses `spi21` for ADS testing.

## Nordic nRF54L15 DK

Use the GPIO bitbang ID test first on the Nordic DK:

```powershell
west build -b "nrf54l15dk/nrf54l15/cpuapp" examples/ads1292r_bitbang_id_test -d build_nrf54l15dk_ads_bitbang -p always -- "-DDTC_OVERLAY_FILE=C:/Users/teri-/Documents/eVRT-Firmware/overlays/nrf54l15dk_ads1292r_bitbang.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_nrf54l15dk_ads_bitbang
```

After the ID test returns `0x73`, build the full internal-test-signal app from
the repository root:

```powershell
west build -b "nrf54l15dk/nrf54l15/cpuapp" -d build_nrf54l15dk_ads -p always -- "-DDTC_OVERLAY_FILE=C:/Users/teri-/Documents/eVRT-Firmware/overlays/nrf54l15dk_ads1292r.overlay" "-DCONF_FILE=prj.conf"
west flash -d build_nrf54l15dk_ads
```

Before connecting the ADS1292R logic pins, use Nordic Board Programmer to set
the nRF54L15 DK target voltage to 3V3.

| ADS1292R signal | nRF54L15 DK pin |
| --- | --- |
| SCK | P1.08 |
| DIN / MOSI | P1.09 |
| DOUT / MISO | P1.10 |
| CS | P1.11 |
| START | P1.12 |
| PWDN/RESET | P1.13 |
| DRDY | P1.14 |

This overlay keeps the ADS logic wiring on Port P1 and avoids the DK's default
UART20 console pins on P1.04/P1.05/P1.06/P1.07. It disables the overlapping
button, LED, and PWM peripheral nodes on P1.08/P1.09/P1.10/P1.13/P1.14 while
ADS test firmware is used.





