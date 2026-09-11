# Simple bitbanged SPI example
This example communicates with the ADS1292R board via GPIO bitbanging, bypassing the Xiao nRF54L15 board's SPI bus. This allows for very slow communication with the board to remove issues concerning clock speed, isolating just the command sequence itself.

# Hardware Details
XIAO D8  -> ADS SCK
XIAO D10 -> ADS MOSI
XIAO D9  -> ADS MISO
XIAO D1  -> ADS CS
XIAO D2  -> ADS DRDY
XIAO D3  -> ADS START
XIAO D4  -> ADS PWDN/RESET

LOGIC_SEL -> 3V3
CLKSEL -> right-hand pad / internal clock selection

# How to Run
1. From the overall project root (eVRT-Firmware/), run:
west build -b "xiao_nRF54l15/nrf54l15/cpuapp" -d build_bb -p always examples/ads1292r_bitbang_id_test -- "-DBOARD_ROOT=C:/Users/teri-/Documents/eVRT-Firmware"

2. To flash, run:
west flash -d build_bb --runner openocd

# Expected output
ADS1292R bitbang RX high-phase: 00 00 73; low-phase: 00 00 73
ADS1292R bitbang RX high-phase: 02 00 73; low-phase: 02 00 73
ADS1292R bitbang RX high-phase: 02 00 73; low-phase: 02 00 73
...
...
...

