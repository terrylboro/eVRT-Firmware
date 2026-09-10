# ADS1292R SPI bring-up

The application-local transport uses Zephyr's synchronous SPI API. The custom
devicetree binding supplies pin/configuration metadata; it is not a Zephyr ADC
API driver.

## Wiring

| Signal | XIAO label | GPIO |
| --- | --- | --- |
| SCLK | D8 | P2.01 |
| DIN / MOSI | D10 | P2.02 |
| DOUT / MISO | D9 | P2.04 |
| CS | D1 | P1.05 |
| DRDY | D2 | P1.06 |
| START | D3 | P1.07 |
| PWDN/RESET | D4 | P1.10 |

PWDN/RESET must no longer be tied directly to 3V3. Connect common ground and
power the breakout according to its revision. Transport timing assumes a running
nominal 512 kHz ADS clock. Console UART20 remains on P1.09/P1.08.

## Build and run

From an nRF Connect SDK terminal in this folder:

```powershell
west build -d build_spi -b xiao_nRF54l15/nrf54l15/cpuapp . -- "-DBOARD_ROOT=C:/Users/teri-/Documents/ads1292-Firmware" "-DCONF_FILE=prj.conf"
west flash -d build_spi --runner openocd --openocd "C:/ProgramData/chocolatey/lib/openocd/tools/install/bin/openocd.exe"
```

Open the XIAO COM port at 115200 baud, 8N1, no flow control.
Expect `ADS1292R ID OK: 0x73; registers: ...` every two seconds.
An SPI return value of zero only confirms the controller completed a transfer;
the ID check establishes that a plausible ADS1292R response was received.

## Transport behavior

- SPI mode 1, 8-bit words, MSB first, 1.28 MHz, GPIO-controlled active-low CS.
- Ten-microsecond CS setup/hold and post-transfer delays.
- Register access sends opcode, register count minus one, and register data as
  separate SPI transfers while manual GPIO control holds CS active across the
  whole register command at the nRF54L15's valid SPIM00 rate.
- Zero transmit bytes clock out register data during reads.
- Startup waits one second, resets high-low-high, toggles START low-high-low,
  sends START, STOP, then SDATAC before register access.
- Register access is serialized with a mutex; functions are thread-context only.
- Writes accept caller-supplied register values: reserved bits must follow TI's
  register definitions. ID writes and invalid register ranges are rejected.
- DRDY is configured as an input. Interrupts and sample acquisition are not yet
  implemented; init follows the ProtoCentral START low-high-low sequence.

## Hardware checks

With a logic analyser, verify SCLK idles low, data are sampled on falling edges,
and CS stays low for an entire operation. A read of all 12 registers transmits
`20 0b` followed by 12 zero bytes. The first meaningful received byte is `73`.
Repeated `00` or `ff` responses warrant checking power, ground, pin labels,
reset level, clock and SPI connections before adding acquisition.

Next validate writes by saving CONFIG1, writing a legal alternate data-rate
value, reading it back, and restoring the saved value with readback. Add
internal-test signal acquisition only after transport validation succeeds.

Primary reference: TI ADS1292R SBAS502C, sections 6.6, 8.5 and 10.1:
https://www.ti.com/lit/ds/symlink/ads1292r.pdf
