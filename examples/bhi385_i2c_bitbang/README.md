# BHI385/BHI360 GPIO bitbang I2C diagnostic

This example uses GPIO on the Ezurio BL54L15u DVK to bitbang a slow I2C bus on
the same pins used by the BHI385/BHI360 hardware I2C examples:

| Shuttle signal | BL54L15u DVK pin |
| --- | --- |
| VDD / VDDIO | 1V8 / VDD_nRF |
| GND | GND |
| SDA | P1.12 / MikroE_SDA |
| SCL | P1.11 / MikroE_SCL |

It drives the pins in an open-drain style: low is driven actively, high is a
released input with pull-up. This is intended only as a hardware bring-up
diagnostic before using the Zephyr TWIM/I2C controller.

## Build and flash

```powershell
west build -b "bl54l15u_dvk/nrf54l15/cpuapp" -d build_bl54l15u_bhi385_bitbang -p always examples/bhi385_i2c_bitbang -- "-DDTC_OVERLAY_FILE=../bhi385_i2c_scan/overlays/bl54l15u_dvk.overlay"
west flash -d build_bl54l15u_bhi385_bitbang
```

Open `VCOM0` at `115200 8N1`.
