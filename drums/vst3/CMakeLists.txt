cmake_minimum_required(VERSION 3.22)

project(BytebeatDrumsVST3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/JUCE" CACHE PATH "JUCE source directory")
set(BYTEBEAT_CORE "${CMAKE_CURRENT_SOURCE_DIR}/../../mpc/src")
set(DRUM_SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../mpc/src")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${BYTEBEAT_CORE}/BytebeatEngine.h")
    message(FATAL_ERROR "BytebeatEngine.h not found at ${BYTEBEAT_CORE}")
endif()
if(NOT EXISTS "${DRUM_SHARED}/DrumKits.h")
    message(FATAL_ERROR "DrumKits.h not found at ${DRUM_SHARED}")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(BytebeatDrums
    COMPANY_NAME "bokontep"
    PRODUCT_NAME "Bytebeat Drums"
    PLUGIN_MANUFACTURER_CODE Boko
    PLUGIN_CODE Bbt2
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Instrument|Drum"
    BUNDLE_ID "com.bokontep.bytebeatdrums"
)

juce_generate_juce_header(BytebeatDrums)

target_sources(BytebeatDrums PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(BytebeatDrums PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_include_directories(BytebeatDrums PRIVATE
    "${BYTEBEAT_CORE}"
    "${DRUM_SHARED}"
    "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
)

if(NOT MSVC)
    target_compile_options(BytebeatDrums PRIVATE -Wno-float-equal -Wno-switch-enum)
endif()

target_link_libraries(BytebeatDrums PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

enable_testing()
juce_add_console_app(BytebeatDrumsTest PRODUCT_NAME "Bytebeat Drums Test")
juce_generate_juce_header(BytebeatDrumsTest)
target_sources(BytebeatDrumsTest PRIVATE
    tests/drum_vst3_tests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_definitions(BytebeatDrumsTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)
target_include_directories(BytebeatDrumsTest PRIVATE
    Source
    "${BYTEBEAT_CORE}"
    "${DRUM_SHARED}"
)
if(NOT MSVC)
    target_compile_options(BytebeatDrumsTest PRIVATE -Wno-float-equal -Wno-switch-enum)
endif()
target_link_libraries(BytebeatDrumsTest PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME BytebeatDrumsTest COMMAND BytebeatDrumsTest)
