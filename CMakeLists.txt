cmake_minimum_required(VERSION 3.16)
project(
    ebv_frame_recording
    VERSION 0.1.0
    DESCRIPTION "Capturing data from multiple frame and EBV cameras"
    LANGUAGES CXX)



# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

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


# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

include_directories(${OpenCV_INCLUDE_DIRS})

include_directories(${IDS_PEAK_INCLUDE_DIR})

include_directories(${IDS_PEAK_IPL_INCLUDE_DIR})

include_directories(${METAVISION_INCLUDE_DIR})

# Source files
set(SOURCES
    src/main.cpp
    src/frame_camera_manager.cpp
    src/event_camera_manager.cpp
    src/utils.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(
    ${PROJECT_NAME} PRIVATE
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




# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)



#  // Copy DLL to output directory
# # Generic c++
# ids_peak_deploy(${SAMPLE_TARGET_NAME})