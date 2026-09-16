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

The Ezurio overlay maps the ADS1292R to the DVK's mikroBUS-style pins:

| ADS1292R signal | BL54L15u DVK pin |
| --- | --- |
| SCK | SCK / P2.01 |
| DIN / MOSI | MOSI / P2.02 |
| DOUT / MISO | MISO / P2.04 |
| CS | GPIO / P1.11 |
| DRDY | GPIO / P1.12 |
| PWDN/RESET | MB_RX / P1.08 |
| START | MB_TX / P1.09 |

The overlay keeps the onboard SPI flash on spi00 chip select 0 and adds the
ADS1292R on chip select 1.





