cmake_minimum_required(VERSION 3.22)

project(BytebeatMachineVST3 VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/JUCE" CACHE PATH "JUCE source directory")
set(BYTEBEAT_CORE "${CMAKE_CURRENT_SOURCE_DIR}/../mpc/src")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${BYTEBEAT_CORE}/BytebeatEngine.h")
    message(FATAL_ERROR "BytebeatEngine.h not found at ${BYTEBEAT_CORE}")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

# QuickJS powers the optional JavaScript engine. It is VST3-only: the MPC build
# uses the allocation-free native bytecode VM.
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/quickjs-ng"
                 "${CMAKE_BINARY_DIR}/quickjs" EXCLUDE_FROM_ALL)

juce_add_plugin(BytebeatMachine
    COMPANY_NAME "bokontep"
    PRODUCT_NAME "Bytebeat Machine"
    PLUGIN_MANUFACTURER_CODE Boko
    PLUGIN_CODE Bbt1
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Instrument|Synth"
    BUNDLE_ID "com.bokontep.bytebeatmachine"
)

juce_generate_juce_header(BytebeatMachine)

target_sources(BytebeatMachine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BytebeatJsEngine.cpp
)

target_compile_definitions(BytebeatMachine PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_include_directories(BytebeatMachine PRIVATE
    "${BYTEBEAT_CORE}"
    "${JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
)

if(NOT MSVC)
    target_compile_options(BytebeatMachine PRIVATE -Wno-float-equal -Wno-switch-enum)
endif()

target_link_libraries(BytebeatMachine PRIVATE
    qjs
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

enable_testing()
add_executable(BytebeatJsTest
    ../tests/js_engine_tests.cpp
    Source/BytebeatJsEngine.cpp
)
target_include_directories(BytebeatJsTest PRIVATE
    Source
    "${BYTEBEAT_CORE}"
)
target_link_libraries(BytebeatJsTest PRIVATE qjs)
target_compile_features(BytebeatJsTest PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(BytebeatJsTest PRIVATE -Wall -Wextra)
endif()
add_test(NAME BytebeatJsTest COMMAND BytebeatJsTest)
