# Tests CMakeLists

# Collect test sources
set(TEST_SOURCES
    test_sanity.cpp
    test_event_bias.cpp
    test_recording_paths.cpp
    # test_recording_manager_advanced.cpp  # Temporarily disabled due to heap corruption investigation
    # New logic-focused tests
    test_extract_frame_index.cpp
    test_recording_manager_config.cpp
    test_cvMatToQImage.cpp
    test_recording_manager_output_dir.cpp
)

add_executable(unit_tests ${TEST_SOURCES})

# If we need to test code in library form, we would factor common sources into a library.
# For now, link against gtest and pthread (and any include dirs needed for headers under test)

# Only include project public headers explicitly; rest come transitively from ebv_core
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Link with GoogleTest
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Qt6 COMPONENTS Core Gui REQUIRED)
target_link_libraries(unit_tests PRIVATE ebv_core GTest::gtest GTest::gmock GTest::gmock_main Qt6::Core Qt6::Gui Threads::Threads)

target_compile_definitions(unit_tests PRIVATE UNIT_TEST=1)

# Add implementation sources needed by tests (QObject-based classes & utilities)

# Qt component library for test reuse (ensures proper AUTOMOC for QObject classes)
add_library(ebv_utils_qt STATIC 
    ${CMAKE_SOURCE_DIR}/src/utils_qt.cpp 
    ${CMAKE_SOURCE_DIR}/include/utils_qt.h
    ${CMAKE_SOURCE_DIR}/src/extract_frame_index.cpp
)
target_link_libraries(ebv_utils_qt PUBLIC Qt6::Core Qt6::Gui ${OpenCV_LIBS})
target_include_directories(ebv_utils_qt PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(unit_tests PRIVATE ebv_utils_qt)

# Discover tests
include(GoogleTest)
gtest_discover_tests(unit_tests)
