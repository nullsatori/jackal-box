cmake_minimum_required(VERSION 3.25)

project(JackalBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(_jackal_app_default ON)
else()
    set(_jackal_app_default OFF)
endif()

option(JACKAL_BUILD_APP "Build the JackalBox GUI app (JUCE)" ${_jackal_app_default})
option(JACKAL_BUILD_TESTS "Build headless engine tests" ON)
set(JACKAL_ASIO_SDK_DIR "$ENV{ASIO_SDK_DIR}" CACHE PATH "Path to a local Steinberg ASIO SDK (folder that contains common/iasiodrv.h)")

# ---------------------------------------------------------------------------
# Engine: pure C++20, no JUCE
# ---------------------------------------------------------------------------
add_library(jackal_engine STATIC
    Source/Engine/EngineTypes.h
    Source/Engine/CommandQueue.h
    Source/Engine/SpscQueue.h
    Source/Engine/ParamIds.h
    Source/Engine/PatternData.h
    Source/Engine/Sample.h
    Source/Engine/SampleBank.h
    Source/Engine/StepHash.h
    Source/Engine/Voice.h
    Source/Engine/Voice.cpp
    Source/Engine/TrackPlayer.h
    Source/Engine/TrackPlayer.cpp
    Source/Engine/Sequencer.h
    Source/Engine/Sequencer.cpp
    Source/Engine/GlitchProcessor.h
    Source/Engine/GlitchProcessor.cpp
    Source/Engine/LevelMeter.h
    Source/Engine/Transport.h
    Source/Engine/Transport.cpp
    Source/Engine/Metronome.h
    Source/Engine/Metronome.cpp
    Source/Engine/InputMonitor.h
    Source/Engine/InputMonitor.cpp
    Source/Engine/EngineCore.h
    Source/Engine/EngineCore.cpp)

target_include_directories(jackal_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

if(MSVC)
    target_compile_options(jackal_engine PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(jackal_engine PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion)
endif()

# ---------------------------------------------------------------------------
# Model: project, JSON, pattern ops, mutation, demo kit, slicer. No JUCE.
# ---------------------------------------------------------------------------
include(cmake/FetchJson.cmake)

add_library(jackal_model STATIC
    Source/Model/Project.h
    Source/Model/ParamSpec.h
    Source/Model/ProjectJson.h
    Source/Model/ProjectJson.cpp
    Source/Model/PatternOps.h
    Source/Model/PatternOps.cpp
    Source/Model/Mutation.h
    Source/Model/Mutation.cpp
    Source/Model/DemoKit.h
    Source/Model/DemoKit.cpp
    Source/Model/Slicer.h
    Source/Model/Slicer.cpp)

target_link_libraries(jackal_model PUBLIC jackal_engine)
# nlohmann as a SYSTEM include so its warnings never break -Werror
get_target_property(_json_inc nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(jackal_model SYSTEM PRIVATE ${_json_inc})

if(MSVC)
    target_compile_options(jackal_model PRIVATE /W4 /WX /permissive- /bigobj)
else()
    target_compile_options(jackal_model PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(JACKAL_BUILD_TESTS)
    include(cmake/FetchCatch2.cmake)
    enable_testing()
    add_subdirectory(Tests)
endif()

# ---------------------------------------------------------------------------
# App (JUCE). JUCE is fetched only here, so a Linux test build never pulls it.
# ---------------------------------------------------------------------------
if(JACKAL_BUILD_APP)
    include(cmake/FetchJuce.cmake)
    include(cmake/AsioSdk.cmake)

    juce_add_gui_app(JackalBox
        PRODUCT_NAME "JackalBox"
        COMPANY_NAME "nullsatori"
        VERSION ${PROJECT_VERSION})

    target_sources(JackalBox PRIVATE
        Source/App/Main.cpp
        Source/App/AudioEngine.cpp
        Source/App/AppSettings.cpp
        Source/App/JackalLookAndFeel.cpp
        Source/App/MainComponent.cpp
        Source/App/TransportBar.cpp
        Source/App/MonitorPanel.cpp
        Source/App/LatencyDisplay.cpp
        Source/App/DeviceSettingsPanel.cpp)

    target_compile_definitions(JackalBox PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="JackalBox"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1)

    jackal_configure_asio(JackalBox)

    target_link_libraries(JackalBox
        PRIVATE
            jackal_engine
            jackal_model
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)

    if(MSVC)
        target_compile_options(JackalBox PRIVATE /W4)
    endif()
endif()
