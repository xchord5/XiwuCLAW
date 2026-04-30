cmake_minimum_required(VERSION 3.22)

project(JuceGainVST3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")

if(EXISTS "${JUCE_LOCAL_PATH}/CMakeLists.txt")
  message(STATUS "Using local JUCE at: ${JUCE_LOCAL_PATH}")
  add_subdirectory("${JUCE_LOCAL_PATH}" JUCE)
else()
  message(FATAL_ERROR "JUCE not found at ${JUCE_LOCAL_PATH}. Put JUCE sources there first.")
endif()

juce_add_plugin(GainPlugin
  COMPANY_NAME "Xiwuclaw"
  BUNDLE_ID "com.xiwuclaw.instrument"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE XwCl
  PLUGIN_CODE Xi01
  FORMATS VST3 Standalone
  PRODUCT_NAME "XiwuCLAW"
  VST3_CATEGORIES "Instrument|Synth"
)

# ==============================================================================
# Probe Plugin - MIDI Effect for sending probe data to main plugin via NamedPipe
# ==============================================================================
juce_add_plugin(ProbePlugin
  COMPANY_NAME "Xiwuclaw"
  BUNDLE_ID "com.xiwuclaw.probe"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT TRUE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE XwCl
  PLUGIN_CODE Xi02
  FORMATS VST3 Standalone
  PRODUCT_NAME "XiwuCLAW Probe"
  VST3_CATEGORIES "Tools"
)

file(GLOB XIWU_TONE_JSON CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tones/factory/*.json"
)

juce_add_binary_data(XiwuToneData
  NAMESPACE XiwuToneData
  SOURCES
    ${XIWU_TONE_JSON}
)

juce_add_binary_data(XiwuAssetData
  NAMESPACE XiwuAssetData
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/asset/banner.png
)

target_sources(GainPlugin
  PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
)

target_sources(ProbePlugin
  PRIVATE
    src-probe/PluginProcessor.cpp
    src-probe/PluginProcessor.h
    src-probe/PluginEditor.cpp
    src-probe/PluginEditor.h
)

target_compile_definitions(GainPlugin
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_compile_definitions(ProbePlugin
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(GainPlugin
  PRIVATE
    juce::juce_audio_utils
    juce::juce_opengl
    XiwuToneData
    XiwuAssetData
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_link_libraries(ProbePlugin
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(GainPlugin)
juce_generate_juce_header(ProbePlugin)
