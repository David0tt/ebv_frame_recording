cmake_minimum_required(VERSION 3.16)
project(
    ebv_frame_recording
    VERSION 0.1.0
    DESCRIPTION "Capturing data from multiple frame and EBV cameras"
    LANGUAGES CXX)



# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automoc for any Qt based executables we add (mockup player)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Add compiler flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

option(EBV_BUILD_TESTS "Build unit tests" ON)

# ----------------------------
# Testing (GoogleTest) Setup (gated by EBV_BUILD_TESTS)
# ----------------------------
if(EBV_BUILD_TESTS)
    set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/.fcdeps) # persist dependencies outside build dir
    include(FetchContent)
    set(BUILD_GMOCK OFF CACHE BOOL "Disable GMock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable gtest install" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()


# Find required packages
find_package(Threads REQUIRED)

# OpenCV after manual installation
set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/external/opencv/build)
find_package(OpenCV REQUIRED)

# IDS Peak
find_package(ids_peak REQUIRED)

find_package(ids_peak_ipl REQUIRED)


# Metavision (OpenEB build tree provides generated config files, not a system install)
set(MetavisionSDK_DIR "${CMAKE_SOURCE_DIR}/external/openeb/build/generated/share/cmake/MetavisionSDKCMakePackagesFilesDir")
set(MetavisionHAL_DIR "${CMAKE_SOURCE_DIR}/external/openeb/build/generated/share/cmake/MetavisionHALCMakePackagesFilesDir")
set(hdf5_ecf_DIR "${CMAKE_SOURCE_DIR}/external/openeb/build/sdk/modules/stream/cpp/3rdparty/hdf5_ecf")

find_package(MetavisionSDK REQUIRED COMPONENTS base core stream ui)

# Metavision HAL + SDK
find_package(MetavisionHAL REQUIRED)


# # TODO use target_include_directories instead of include_directories if possible

# # Include directories
# include_directories(${CMAKE_SOURCE_DIR}/include)

# include_directories(${OpenCV_INCLUDE_DIRS})

# include_directories(${IDS_PEAK_INCLUDE_DIR})

# include_directories(${IDS_PEAK_IPL_INCLUDE_DIR})

# include_directories(${METAVISION_INCLUDE_DIR})

# Source files
# set(SOURCES
#     src/ebv_frame_recording.cpp
#     src/frame_camera_manager.cpp
#     src/event_camera_manager.cpp
#     src/utils.cpp
# )

set(CLI11_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/CLI11)

## ----------------------------------------------------------------------------------
## Core library (non-Qt, reusable for executables and tests)
## ----------------------------------------------------------------------------------
add_library(ebv_core STATIC
    src/frame_camera_manager.cpp
    src/event_camera_manager.cpp
    src/recording_manager.cpp
    src/utils.cpp
)

target_include_directories(ebv_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${IDS_PEAK_INCLUDE_DIR}
    ${IDS_PEAK_IPL_INCLUDE_DIR}
    ${METAVISION_INCLUDE_DIR}
    ${CLI11_INCLUDE_DIR}
)

target_link_libraries(ebv_core PUBLIC
    Threads::Threads
    ids_peak
    ids_peak_ipl
    ${OpenCV_LIBS}
    MetavisionSDK::base
    MetavisionSDK::core
    MetavisionSDK::stream
    MetavisionSDK::ui
    Metavision::HAL
)

# Main recording executable (small target only containing the entry point)
add_executable(ebv_frame_recording
    src/ebv_frame_recording.cpp
)

target_link_libraries(ebv_frame_recording PRIVATE ebv_core)

set(CLI11_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/CLI11)

target_include_directories(ebv_frame_recording PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)




# Set output directory
set_target_properties(ebv_frame_recording PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --------------------------------------------------------------------------------------------------
# Mockup video player (Qt Widgets) providing required 2x2 layout + play bar + transport controls
# This is a standalone mockup UI (no real data wiring yet)
# --------------------------------------------------------------------------------------------------

# Try Qt6 first, fall back to Qt5 (Metavision UI today often still uses Qt5)
find_package(Qt6 COMPONENTS Widgets REQUIRED)
set(QT_WIDGETS_LIB Qt6::Widgets)


# find_package(Qt6 COMPONENTS Widgets QUIET)
# if (Qt6_FOUND)
#     message(STATUS "Configuring mockup player with Qt6")
#     set(QT_WIDGETS_LIB Qt6::Widgets)
# else()
#     find_package(Qt5 COMPONENTS Widgets REQUIRED)
#     message(STATUS "Configuring mockup player with Qt5")
#     set(QT_WIDGETS_LIB Qt5::Widgets)
# endif()

add_executable(video_player
    src/video_player.cpp
    src/player_window.cpp
    src/recording_loader.cpp
    src/utils_qt.cpp
    src/recording_buffer.cpp
    src/cached_timeline_slider.cpp
    include/player_window.h
    include/recording_loader.h
    include/utils_qt.h
    include/recording_buffer.h
    include/cached_timeline_slider.h
)

target_include_directories(video_player PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${IDS_PEAK_INCLUDE_DIR}
    ${IDS_PEAK_IPL_INCLUDE_DIR}
    ${METAVISION_INCLUDE_DIR}
)

target_link_libraries(video_player PRIVATE 
    ebv_core
    ${QT_WIDGETS_LIB}
    ${OpenCV_LIBS}
)

set_target_properties(video_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)



#  // Copy DLL to output directory
# # Generic c++
# ids_peak_deploy(${SAMPLE_TARGET_NAME})

if(EBV_BUILD_TESTS)
    add_subdirectory(tests)
endif()