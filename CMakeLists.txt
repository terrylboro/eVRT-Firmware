cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(eVRT-Firmware)

set(EVRT_AUDIO_WAV_ASSET "${CMAKE_CURRENT_SOURCE_DIR}/assets/INST.WAV")
set(EVRT_GENERATED_AUDIO_ASM "${CMAKE_CURRENT_BINARY_DIR}/evrt_embedded_audio_asset.S")

if(EXISTS "${EVRT_AUDIO_WAV_ASSET}")
    cmake_path(CONVERT "${EVRT_AUDIO_WAV_ASSET}" TO_CMAKE_PATH_LIST EVRT_AUDIO_WAV_ASSET)
    configure_file(
        modules/audio/embedded_audio_asset.S.in
        "${EVRT_GENERATED_AUDIO_ASM}"
        @ONLY
    )
    target_sources(app PRIVATE "${EVRT_GENERATED_AUDIO_ASM}")
    target_compile_definitions(app PRIVATE EVRT_HAS_EMBEDDED_AUDIO=1)
    message(STATUS "Embedding audio asset: ${EVRT_AUDIO_WAV_ASSET}")
else()
    message(WARNING "Audio asset not found: ${EVRT_AUDIO_WAV_ASSET}; embedded playback will return -ENOENT")
endif()

target_sources(app PRIVATE
    app/main.c
    app/evrt_app.c
    modules/ads1292r/ads1292r.c
    modules/audio/audio_playback.c
    modules/bluetooth/ble_control.c
)

target_include_directories(app PRIVATE
    app
    modules/ads1292r
    modules/audio
    modules/bluetooth
)
