cmake_minimum_required(VERSION 3.22)
project(MotifDesk VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
option(MOTIF_DESK_BUILD_GUI "Build the optional JUCE desktop app" OFF)
option(BUILD_TESTING "Build and register headless smoke tests" OFF)

# nix develop supplies this pinned source. Standalone CMake users can point
# to their own JUCE 8.0.12 source tree with -DJUCE_SOURCE_DIR=<path>.
set(JUCE_SOURCE_DIR "$ENV{JUCE_SOURCE_DIR}" CACHE PATH "Path to the JUCE source tree")
if(NOT EXISTS "${JUCE_SOURCE_DIR}/modules/juce_core/juce_core.cpp")
    message(FATAL_ERROR "Set JUCE_SOURCE_DIR to the JUCE 8.0.12 source tree, or run CMake from nix develop.")
endif()

# Deliberately bypass JUCE's GUI-based juceaide bootstrap and audio-backend
# pkg-config metadata. Neither the CLI nor the engine smoke test opens a
# device; an audio-device callback type is only part of the shared engine API.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The Motif Desk build currently targets Linux.")
endif()
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
set(juceModules "${JUCE_SOURCE_DIR}/modules")
add_library(MotifDeskHeadless STATIC
    Source/Engine.cpp
    Source/ProjectIO.cpp
    "${juceModules}/juce_core/juce_core.cpp"
    "${juceModules}/juce_core/juce_core_CompilationTime.cpp"
    "${juceModules}/juce_events/juce_events.cpp"
    "${juceModules}/juce_audio_basics/juce_audio_basics.cpp"
    "${juceModules}/juce_audio_devices/juce_audio_devices.cpp"
    "${juceModules}/juce_audio_formats/juce_audio_formats.cpp"
)
target_include_directories(MotifDeskHeadless PUBLIC Source "${juceModules}")
target_compile_features(MotifDeskHeadless PUBLIC cxx_std_17)
target_compile_definitions(MotifDeskHeadless PUBLIC
    LINUX=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_ALSA=0
    JUCE_JACK=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    $<$<CONFIG:Debug>:DEBUG=1>
)
target_link_libraries(MotifDeskHeadless PUBLIC Threads::Threads "${CMAKE_DL_LIBS}" rt)

add_executable(MotifDeskCli Source/Cli.cpp)
set_target_properties(MotifDeskCli PROPERTIES OUTPUT_NAME motif-desk)
target_link_libraries(MotifDeskCli PRIVATE MotifDeskHeadless)
install(TARGETS MotifDeskCli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIF_DESK_BUILD_GUI)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
    juce_add_gui_app(MotifDesk
        PRODUCT_NAME "Motif Desk"
        BUNDLE_ID "io.github.jihoo12.motifdesk"
        NEEDS_CURL FALSE
        NEEDS_WEB_BROWSER FALSE
    )
    set_target_properties(MotifDesk PROPERTIES OUTPUT_NAME motif-desk-gui)
    juce_generate_juce_header(MotifDesk)
    target_sources(MotifDesk PRIVATE Source/Main.cpp Source/Engine.cpp Source/ProjectIO.cpp)
    target_compile_features(MotifDesk PRIVATE cxx_std_17)
    target_compile_definitions(MotifDesk PRIVATE JUCE_JACK=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(MotifDesk PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
    )
    install(TARGETS MotifDesk RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(MotifDeskEngineTest tests/EngineTest.cpp)
    target_link_libraries(MotifDeskEngineTest PRIVATE MotifDeskHeadless)
    add_test(NAME motif-desk-engine COMMAND MotifDeskEngineTest)
    add_test(NAME motif-desk-cli
        COMMAND "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:MotifDeskCli>"
            "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/examples/first-sketch.motif"
            "-DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CliSmoke.cmake"
    )
endif()
