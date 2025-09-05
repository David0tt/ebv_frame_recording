# Project Overview

This project is a video player and recorder, that allows to jointly record and play back video from multiple frame cameras and multiple event cameras. 

## Folder Structure

- `/include`: Contains header files.
- `/src`: Contains the source code.
- `/tests`: Contains tests for the project using gtest.

### folders that are created to build the project but are not part of the repository

- `/build`: Directory for build files.
- `/external`: Contains external dependencies.

## Libraries and Frameworks

- OpenEB / MetavisionSDK: For event-based vision processing.
- OpenCV: For image processing and computer vision tasks.
- gtest: For unit testing.
- CMake: For build configuration.
- Ninja: As the build system.
- Qt6: For GUI components.
- CLI11: For command-line interface parsing.
- ids_peak: to interface with IDS frame cameras.

## Coding Standards

- Follow C++17 standards.
- Write unit tests for all new features and bug fixes.

## UI guidelines

- Follows Qt6 design principles.

## Building

- the project should be built using CMake and Ninja.

    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DEBV_BUILD_TESTS=ON -B build
    ninja -C build

- always run the test suite: 

    ctest --test-dir build --output-on-failure

or verbose with nicer output formatting:

    ./build/tests/unit_tests --gtest_color=yes

- Rebuild & rerun quickly after code changes:

    ninja -C build && ctest --test-dir build --output-on-failure
