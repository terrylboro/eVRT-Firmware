cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(audio_max98357a_sd)

target_sources(app PRIVATE
    src/main.c
    ../../modules/audio/audio_playback.c
)

target_include_directories(app PRIVATE
    ../../modules/audio
)
