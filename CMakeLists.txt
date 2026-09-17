cmake_minimum_required(VERSION 3.24)

project(cyclescope VERSION 0.1.0 LANGUAGES CXX)

add_library(cyclescope INTERFACE)
add_library(cyclescope::cyclescope ALIAS cyclescope)

target_include_directories(cyclescope INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(cyclescope INTERFACE cxx_std_20)

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  option(CYCLESCOPE_BUILD_TESTS "Build cyclescope tests" ON)
  set(CYCLESCOPE_SANITIZE "" CACHE STRING "Sanitizer for tests: thread, address, undefined, or empty")
  if(CYCLESCOPE_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
  endif()
endif()
