# BHI360 + BMM350 firmware boot baseline

This example uses Bosch's BHI360 SensorAPI over Zephyr I2C. It first checks
whether the BMM350 at `0x14` is directly visible on the host I2C bus, then
verifies the BHI360 chip ID, uploads a Bosch firmware image, boots the BHI360
from RAM, enables accelerometer and gyroscope passthrough streams, and polls the
FIFO for a short baseline capture. By default it uses Bosch's BHI360-only
`bsxsam` firmware for a core boot baseline.

It is the next baseline after `examples/bhi385_i2c_scan`.

## Hardware

Ezurio BL54L15u DVK wiring:

| Shuttle signal | BL54L15u DVK pin |
| --- | --- |
| VDD / VDDIO | 1V8 / VDD_nRF |
| GND | GND |
| SDA | P1.12 / MikroE_SDA |
| SCL | P1.11 / MikroE_SCL |

## Build and flash

From the project root:

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi360_boot -p always examples/bhi360_firmware_boot -- "-DDTC_OVERLAY_FILE=../bhi385_i2c_scan/overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_bhi360_boot
```

Open `VCOM0` at `115200 8N1`.

To test the BHI360+BMM350 NDOF firmware image instead, add
`-DBHI360_BOOT_BMM350=ON` to the build arguments:

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi360_boot_bmm350 -p always examples/bhi360_firmware_boot -- "-DDTC_OVERLAY_FILE=../bhi385_i2c_scan/overlays/bl54l15u_dvk.overlay" "-DBHI360_BOOT_BMM350=ON"
west flash -d build_bl54l15u_bhi360_boot_bmm350
```

## Expected output

```text
BHI360 + BMM350 firmware boot baseline
BHI360 init on i2c@c8000
BMM350 host-bus chip probe 0x14/0x40: ret=... chip_id=...
BHI360 chip probe 0x28: ret=0 chip_id=0x7a
BHI360 chip probe 0x29: ret=... chip_id=0x00
BHI360 using I2C address 0x28
BHI360 detected: chip=0x7a product=0x89 revision=...
BHI360 loading BHI360_bsxsam firmware (... bytes)
BHI360 firmware upload: ...
BHI360 booting from RAM
BHI360 boot complete: kernel=...
BHI360 baseline passed: chip=0x7a product=0x89 kernel=...
BHI360 streaming enabled: accel pass + gyro pass at 50 Hz
IMU t=...ms acc_g=(...,...,...) gyro_dps=(...,...,...)
BHI360 streaming baseline complete
```
