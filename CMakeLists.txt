cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(eVRT-Firmware)

target_sources(app PRIVATE
    app/main.c
    app/evrt_app.c
    modules/ads1292r/ads1292r.c
)

target_include_directories(app PRIVATE
    app
    modules/ads1292r
)
