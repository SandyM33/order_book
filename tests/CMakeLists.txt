find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(ob_tests
  map_order_book_test.cpp
  spsc_queue_test.cpp
  matching_engine_test.cpp)
target_link_libraries(ob_tests PRIVATE ob::ob ob_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ob_tests DISCOVERY_MODE PRE_TEST)
