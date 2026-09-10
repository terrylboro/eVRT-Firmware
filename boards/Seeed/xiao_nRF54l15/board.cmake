# Locate Chocolatey's installation even when the IDE has an older PATH.
if(CMAKE_HOST_WIN32 AND NOT OPENOCD)
  find_program(OPENOCD NAMES openocd
    HINTS "C:/ProgramData/chocolatey/lib/openocd/tools/install/bin")
endif()

if (CONFIG_SOC_NRF54L15_CPUAPP)
  board_runner_args(openocd "--cmd-load=nrf54l-load" -c "targets nrf54l.cpu")
  board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")
elseif (CONFIG_SOC_NRF54L15_CPUFLPR)
  board_runner_args(openocd "--cmd-load=nrf54l-load" -c "targets nrf54l.aux")
  board_runner_args(jlink "--device=nRF54L15_RV32" "--speed=4000")
endif()

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
