cmake_minimum_required(VERSION 3.15)
project(DaisySandbox)

set(CMAKE_CXX_STANDARD 17)
set(JUCE_DIR ../JUCE) 
set(DAISYSP_DIR ../DaisySP)

add_compile_definitions(BUILDING_FOR_EMULATOR)

add_subdirectory(${JUCE_DIR} juce)
add_subdirectory(${DAISYSP_DIR} DaisySP)  # <-- brings in real DaisySP

juce_add_gui_app(DaisySandbox VERSION 0.0.0)

juce_generate_juce_header(DaisySandbox)

set_target_properties(DaisySandbox PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/Info_debug.plist
)

target_sources(DaisySandbox PRIVATE
    Main.cpp
    FakeDaisyPetal.h
    SandboxedPedal.h
)

target_include_directories(DaisySandbox PRIVATE
    ../cenote          # your pedal sources
    ../flib            # add this so cenote.cpp can find flib headers
    ../Terrarium       # if you also need the Terrarium headers
)

target_link_libraries(DaisySandbox PRIVATE
    DaisySP
    juce::juce_gui_basics
    juce::juce_audio_utils
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_events
)
