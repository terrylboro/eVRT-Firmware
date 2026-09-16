cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bhi360_firmware_boot)

set(BHI360_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/bhi360/vendor/BHI360_SensorAPI)
option(BHI360_BOOT_BMM350 "Load the BHI360+BMM350 NDOF firmware image" OFF)

target_sources(app PRIVATE
    src/main.c
    ../../modules/bhi360/bhi360_zephyr.c
    ${BHI360_VENDOR_DIR}/source/bhi360.c
    ${BHI360_VENDOR_DIR}/source/bhi360_hif.c
    ${BHI360_VENDOR_DIR}/source/bhi360_parse.c
    ${BHI360_VENDOR_DIR}/source/bhi360_event_data.c
    ${BHI360_VENDOR_DIR}/source/bhi360_system_param.c
    ${BHI360_VENDOR_DIR}/source/bhi360_virtual_sensor_conf_param.c
    ${BHI360_VENDOR_DIR}/source/bhi360_virtual_sensor_info_param.c
)

target_include_directories(app PRIVATE
    ../../modules/bhi360
    ${BHI360_VENDOR_DIR}/source
    ${BHI360_VENDOR_DIR}/firmware/bhi360
)

if(BHI360_BOOT_BMM350)
    target_compile_definitions(app PRIVATE BHI360_ZEPHYR_USE_BMM350_FW=1)
endif()
