# BHI385 / BHI360 I2C scan

This example is a minimal baseline test for a Bosch BHI385 shuttle board, or a
BHI360 shuttle with an auxiliary BMM350 magnetometer. It only checks I2C bus
presence; it does not upload Bosch firmware or read virtual sensor data yet.

## Hardware

Ezurio BL54L15u DVK wiring:

| Shuttle signal | BL54L15u DVK pin |
| --- | --- |
| VDD / VDDIO | 1V8 / VDD_nRF |
| GND | GND |
| SDA | P1.12 / MikroE_SDA |
| SCL | P1.11 / MikroE_SCL |

The BHI device is expected to acknowledge at `0x28` or `0x29`, depending on the
board address strap.

This example should be used without the ADS1292R overlay because that overlay
uses `P1.11` and `P1.12` for ADS `CS` and `DRDY`.

## Build and flash

From the project root:

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi_scan -p always examples/bhi385_i2c_scan -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_bhi_scan
```

If your SDK uses the consolidated board name:

```powershell
west build -b "bl54l15_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi_scan -p always examples/bhi385_i2c_scan -- "-DDTC_OVERLAY_FILE=overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_bhi_scan
```

Open the DVK's normal debug serial/VCOM port at `115200 8N1`.

## Expected output

```text
BHI385/BHI360 shuttle I2C scan: P1.12 SDA, P1.11 SCL
I2C bus ready: i2c@c8000
Checking expected BHI addresses...
  0x28: ACK
  0x29: no response
Scanning 0x20..0x2f for nearby devices...
  found device at 0x28
BHI shuttle detected. Next step: Bosch SensorAPI firmware upload.
```
