cmake_minimum_required(VERSION 3.22)

project(MajorCircleDenoise VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(MCD_FORMATS AU VST3 Standalone)
else()
    set(MCD_FORMATS VST3 Standalone)
endif()

juce_add_plugin(MajorCircleDenoise
    COMPANY_NAME "majorcircle"
    BUNDLE_ID com.majorcircle.denoise
    PLUGIN_MANUFACTURER_CODE Mjcr
    PLUGIN_CODE Mdns
    PRODUCT_NAME "MajorCircle Denoise"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Restoration
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${MCD_FORMATS})

target_sources(MajorCircleDenoise PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MajorCircleDenoise PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MajorCircleDenoise
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# DSP unit tests (no JUCE needed): ctest
enable_testing()
add_executable(DenoiseCoreTest Tests/DenoiseCoreTest.cpp)
target_include_directories(DenoiseCoreTest PRIVATE Source)
add_test(NAME DenoiseCoreTest COMMAND DenoiseCoreTest)
