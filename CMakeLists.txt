cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
project(UBTune VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- JUCE: local checkout (-DJUCE_PATH=...) or fetched from GitHub ----
set(JUCE_PATH "" CACHE PATH "Path to a JUCE checkout")
if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(UBTUNE_FORMATS VST3 AU Standalone)
else()
    set(UBTUNE_FORMATS VST3 Standalone)
endif()

juce_add_plugin(UBTune
    PRODUCT_NAME "UBTune"
    COMPANY_NAME "UNIQUEBABY"
    COMPANY_WEBSITE "https://ubtune.uniquebaby.de"
    COMPANY_EMAIL "support@uniquebaby.de"
    COMPANY_COPYRIGHT "(c) 2026 unique & Mbezza - Gelo Santino Mares, Meron Negash"
    BUNDLE_ID de.uniquebaby.ubtune
    PLUGIN_MANUFACTURER_CODE Unqb
    PLUGIN_CODE UbTn
    DESCRIPTION "UBTune - realtime pitch correction by unique & Mbezza"
    FORMATS ${UBTUNE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TRUE
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "UBTune braucht das Mikrofon, um deine Stimme live zu tunen."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/branding/icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/branding/icon-256.png")

juce_add_binary_data(UBTuneData SOURCES
    plugin/resources/ub-logo.png
    plugin/resources/fonts/InstrumentSans-Regular.ttf
    plugin/resources/fonts/InstrumentSans-SemiBold.ttf
    plugin/resources/fonts/InstrumentSans-Bold.ttf
    plugin/resources/fonts/JetBrainsMono-Regular.ttf
    plugin/resources/fonts/JetBrainsMono-Medium.ttf)

target_sources(UBTune PRIVATE
    plugin/source/PluginProcessor.cpp
    plugin/source/PluginEditor.cpp
    plugin/source/ui/Theme.cpp
    plugin/source/ui/ParticleRing.cpp
    plugin/source/ui/PianoKeys.cpp)

target_include_directories(UBTune PRIVATE plugin/source)

target_compile_definitions(UBTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    UBTUNE_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(UBTune
    PRIVATE
        UBTuneData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- headless UI snapshot tool (used for website screenshots / CI smoke test) ----
option(UBTUNE_BUILD_SNAPSHOT "Build the UI snapshot tool" OFF)
if(UBTUNE_BUILD_SNAPSHOT)
    juce_add_console_app(UBTuneSnapshot PRODUCT_NAME "UBTuneSnapshot")
    set(UBTUNE_TEST_MAIN tests/snapshot.cpp CACHE STRING "test main")
    target_sources(UBTuneSnapshot PRIVATE
        ${UBTUNE_TEST_MAIN}
        plugin/source/PluginProcessor.cpp
        plugin/source/PluginEditor.cpp
        plugin/source/ui/Theme.cpp
        plugin/source/ui/ParticleRing.cpp
        plugin/source/ui/PianoKeys.cpp)
    target_include_directories(UBTuneSnapshot PRIVATE plugin/source)
    target_compile_definitions(UBTuneSnapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 UBTUNE_SNAPSHOT=1
        JucePlugin_Name="UBTune" JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        UBTUNE_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(UBTuneSnapshot PRIVATE UBTuneData juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
