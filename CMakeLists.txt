cmake_minimum_required(VERSION 3.22)
project(MotifDesk VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

# nix develop supplies the fixed-output JUCE source. CMake-only users can pass an
# existing JUCE source checkout or install JUCE's CMake package themselves.
set(JUCE_SOURCE_DIR "$ENV{JUCE_SOURCE_DIR}" CACHE PATH "Path to the JUCE source tree")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    find_package(JUCE 8.0.12 EXACT CONFIG REQUIRED)
endif()

juce_add_gui_app(MotifDesk
    PRODUCT_NAME "Motif Desk"
    BUNDLE_ID "io.github.jihoo12.motifdesk"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(MotifDesk PROPERTIES OUTPUT_NAME motif-desk)
endif()
juce_generate_juce_header(MotifDesk)

target_sources(MotifDesk PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/Engine.h
)
target_compile_features(MotifDesk PRIVATE cxx_std_17)
target_compile_definitions(MotifDesk PRIVATE
    JUCE_JACK=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)
target_link_libraries(MotifDesk PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
)

install(TARGETS MotifDesk
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION .
)

option(BUILD_TESTING "Build and register the headless engine smoke test" OFF)
include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(MotifDeskEngineTest PRODUCT_NAME MotifDeskEngineTest)
    juce_generate_juce_header(MotifDeskEngineTest)
    target_sources(MotifDeskEngineTest PRIVATE tests/EngineTest.cpp Source/Engine.cpp)
    target_include_directories(MotifDeskEngineTest PRIVATE Source)
    target_compile_features(MotifDeskEngineTest PRIVATE cxx_std_17)
    target_compile_definitions(MotifDeskEngineTest PRIVATE
        JUCE_JACK=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(MotifDeskEngineTest PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_recommended_config_flags
    )
    add_test(NAME motif-desk-engine COMMAND MotifDeskEngineTest)
endif()
