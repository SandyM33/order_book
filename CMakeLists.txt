cmake_minimum_required(VERSION 3.20)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer -DNDEBUG")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_ob_sanitize_default ON)
else()
  set(_ob_sanitize_default OFF)
endif()
option(OB_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" ${_ob_sanitize_default})
option(OB_TSAN     "Build with ThreadSanitizer (cannot be combined with OB_SANITIZE)" OFF)
option(OB_NATIVE   "Compile with -march=native" ON)
option(OB_WERROR   "Treat warnings as errors" OFF)
option(OB_BUILD_TESTS "Build unit tests" ON)
option(OB_BUILD_BENCH "Build benchmarks (Google Benchmark)" ON)

# ---------------------------------------------------------------------------
# Shared compile options for all first-party targets (not applied to gtest).
# ---------------------------------------------------------------------------
add_library(ob_options INTERFACE)
target_compile_options(ob_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  $<$<BOOL:${OB_WERROR}>:-Werror>)

if(OB_SANITIZE AND OB_TSAN)
  message(FATAL_ERROR "OB_SANITIZE (ASan/UBSan) and OB_TSAN cannot be used together; use the 'debug' or 'tsan' preset")
endif()

if(OB_NATIVE AND NOT OB_SANITIZE AND NOT OB_TSAN)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-march=native OB_HAS_MARCH_NATIVE)
  if(OB_HAS_MARCH_NATIVE)
    target_compile_options(ob_options INTERFACE -march=native)
  endif()
endif()

if(OB_SANITIZE)
  set(_san -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_compile_options(ob_options INTERFACE ${_san})
  target_link_options(ob_options INTERFACE ${_san})
endif()

if(OB_TSAN)
  set(_tsan -fsanitize=thread -fno-omit-frame-pointer)
  target_compile_options(ob_options INTERFACE ${_tsan})
  target_link_options(ob_options INTERFACE ${_tsan})
endif()

# ---------------------------------------------------------------------------
# Order book library.
# ---------------------------------------------------------------------------
add_library(ob STATIC src/map_order_book.cpp
  src/spsc_queue.cpp
  src/matching_engine.cpp)
add_library(ob::ob ALIAS ob)
target_include_directories(ob PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ob PUBLIC cxx_std_17)
target_link_libraries(ob PRIVATE ob_options)

if(OB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(OB_BUILD_BENCH)
  add_subdirectory(bench)
endif()
