# Tests CMakeLists

# Collect test sources
set(TEST_SOURCES
    test_sanity.cpp
    test_event_bias.cpp
    test_recording_paths.cpp
    gtest_registration_shim.cpp
)

add_executable(unit_tests ${TEST_SOURCES})

# If we need to test code in library form, we would factor common sources into a library.
# For now, link against gtest and pthread (and any include dirs needed for headers under test)

target_include_directories(unit_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${METAVISION_INCLUDE_DIR}
    ${IDS_PEAK_INCLUDE_DIR}
    ${IDS_PEAK_IPL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/external/openeb/sdk/modules/base/cpp/include
    ${CMAKE_SOURCE_DIR}/external/openeb/build/generated/include
    ${CMAKE_SOURCE_DIR}/external/openeb/sdk/modules/core/cpp/include
    ${CMAKE_SOURCE_DIR}/external/openeb/sdk/modules/stream/cpp/include
    ${CMAKE_SOURCE_DIR}/external/openeb/hal/cpp/include
    ${CMAKE_SOURCE_DIR}/external/openeb/sdk/modules/ui/cpp/include
    /usr/include/ids_peak-1.11.1
    /usr/include/ids_peak_ipl-1.16.1
)

# Link with GoogleTest
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_link_libraries(unit_tests PRIVATE GTest::gtest GTest::gtest_main Threads::Threads)

# Discover tests
include(GoogleTest)
gtest_discover_tests(unit_tests)
