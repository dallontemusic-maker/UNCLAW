cmake_minimum_required(VERSION 3.22)
project(WavetableSynth VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Не используем "master": у него меняется API (например, появился модуль
# juce_audio_processors_headless), и сборка может ломаться без изменений в вашем коде.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(WavetableSynth
    COMPANY_NAME "UNCLAW Audio"
    PLUGIN_NAME "WavetableSynth"
    BUNDLE_ID com.unclaw.wavetablesynth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Uncl
    PLUGIN_CODE Wsyn
    FORMATS ${PLUGIN_FORMATS}
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(WavetableSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/WavetableOscillator.h
    Source/OscilloscopeDisplay.h
    Source/WavetableLoader.h
    Source/BrowserSettings.h
    Source/HybridSource.h
    Source/HybridComponents.h
    Source/UnclawLookAndFeel.h
)

target_compile_definitions(WavetableSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(WavetableSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
)

# Проверки гибридного источника (по умолчанию не собираются): -DUNCLAW_BUILD_TESTS=ON
option(UNCLAW_BUILD_TESTS "Build UnclawHybridTests" OFF)
if(UNCLAW_BUILD_TESTS)
    juce_add_console_app(UnclawHybridTests PRODUCT_NAME "UnclawHybridTests")
    target_sources(UnclawHybridTests PRIVATE
        tests/HybridTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(UnclawHybridTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(UnclawHybridTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags)
endif()
