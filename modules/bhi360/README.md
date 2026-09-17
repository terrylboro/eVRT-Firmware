# BHI360 / BHI385 Shuttle Bring-Up Notes

Status: working baseline on Ezurio BL54L15u DVK using Zephyr/NCS.

## Known-Good Hardware Setup

| BHI385/BHI360 shuttle | Ezurio BL54L15u DVK |
| --- | --- |
| VDD | 1V8 / VDD_nRF |
| VDDIO | 1V8 / VDD_nRF |
| GND | GND |
| SDA | P1.12 / MikroE_SDA |
| SCL | P1.11 / MikroE_SCL |

The BHI360 is detected on host I2C address `0x28`. The BMM350 is not directly
visible on the host I2C bus at `0x14` in the current wiring; it may be routed
behind the BHI360 auxiliary bus.

## Known-Good Build

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi360_boot -p always examples/bhi360_firmware_boot -- "-DDTC_OVERLAY_FILE=../bhi385_i2c_scan/overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_bhi360_boot
```

Open `VCOM0` at `115200 8N1`.

## Current Firmware State

The wrapper in `modules/bhi360/bhi360_zephyr.c` can:

- probe BHI360 chip ID `0x7a` at `0x28`
- upload Bosch's `BHI360_bsxsam` RAM firmware
- boot the BHI360 from RAM
- read kernel version `2380` on the tested board
- enable accelerometer and gyroscope passthrough at a requested sample rate
- process FIFO data
- expose the latest scaled sample through `bhi360_zephyr_get_latest()`

Current public API:

```c
int bhi360_zephyr_boot(struct bhi360_zephyr_info *info);
int bhi360_zephyr_start_streaming(float sample_rate_hz);
int bhi360_zephyr_process_fifo(void);
int bhi360_zephyr_get_latest(struct bhi360_imu_sample *sample);
```

`struct bhi360_imu_sample` contains accel in `g`, gyro in `dps`, a Zephyr uptime
timestamp in milliseconds, and validity flags for accel/gyro.

## Known-Good Output Shape

```text
BHI360 chip probe 0x28: ret=0 chip_id=0x7a
BHI360 using I2C address 0x28
BHI360 loading BHI360_bsxsam firmware (88488 bytes)
BHI360 boot complete: kernel=2380
BHI360 baseline passed: chip=0x7a product=0x89 kernel=2380
BHI360 streaming enabled: accel pass + gyro pass at 50 Hz
IMU t=...ms acc_g=(...,...,...) gyro_dps=(...,...,...)
BHI360 streaming baseline complete
```

Stationary samples should have accelerometer magnitude near `1 g` and gyro near
`0 dps`.

## Useful Diagnostics Kept In Repo

- `examples/bhi385_i2c_scan`: simple Zephyr I2C address probe.
- `examples/bhi385_i2c_bitbang`: GPIO bitbanged I2C diagnostic. This confirmed
  BHI360 register `0x2b` at address `0x28` returns chip ID `0x7a`.
- `examples/bhi360_firmware_boot`: current boot + simple streaming baseline.

## Next Steps

- Move FIFO servicing into a module-owned thread or interrupt-driven work item.
- Add a BHI360 interrupt GPIO to avoid polling.
- Add recovery/re-init behavior for runtime I2C/FIFO errors.
- Split boot-only and streaming examples if the baseline grows.
- Try BMM350 firmware variants, especially
  `Bosch_Shuttle3_BHI360_BMM350_Poll_bsxsam_ndof.fw.h`, once the aux routing is
  understood.
- Add magnetometer, rotation vector, and/or game rotation vector streams after
  BMM350 firmware is stable.
