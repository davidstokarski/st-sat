# Offline test / calibration tool for the ST Sat saturation engine.
#   StSatDspTests --test | --calibrate [file] | --render <dir> | --harmonics | --spectrum | --bench

juce_add_console_app(StSatDspTests PRODUCT_NAME "StSatDspTests")

file(GLOB STSAT_SATURATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../Source/dsp/Saturation*.cpp")

target_sources(StSatDspTests PRIVATE
    SaturationTests.cpp
    ${STSAT_SATURATION_SOURCES})

target_include_directories(StSatDspTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Source")

target_compile_features(StSatDspTests PRIVATE cxx_std_20)

target_compile_definitions(StSatDspTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    STSAT_CALIBRATION_FILE="${CMAKE_CURRENT_LIST_DIR}/../Source/dsp/SaturationCalibration.h")

target_link_libraries(StSatDspTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
