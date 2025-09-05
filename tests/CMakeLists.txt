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

# Only include project public headers explicitly; rest come transitively from ebv_core
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Link with GoogleTest
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_link_libraries(unit_tests PRIVATE ebv_core GTest::gtest GTest::gtest_main Threads::Threads)

# Discover tests
include(GoogleTest)
gtest_discover_tests(unit_tests)
