cmake_minimum_required(VERSION 3.24)

project(cyclescope VERSION 0.1.0 LANGUAGES CXX)

add_library(cyclescope INTERFACE)
add_library(cyclescope::cyclescope ALIAS cyclescope)

target_include_directories(cyclescope INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(cyclescope INTERFACE cxx_std_20)

# Compiler-instrumentation backend: hooks for -finstrument-functions,
# compiled without instrumentation. Not available with MSVC.
if(NOT MSVC)
  add_library(cyclescope_instrument STATIC src/instrument.cpp)
  add_library(cyclescope::instrument ALIAS cyclescope_instrument)
  target_link_libraries(cyclescope_instrument
    PUBLIC cyclescope::cyclescope ${CMAKE_DL_LIBS})
  target_compile_features(cyclescope_instrument PUBLIC cxx_std_20)
  target_compile_options(cyclescope_instrument PRIVATE -Wall -Wextra)
endif()

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  option(CYCLESCOPE_BUILD_TESTS "Build cyclescope tests" ON)
  option(CYCLESCOPE_BUILD_EXAMPLES "Build cyclescope examples" ON)
  set(CYCLESCOPE_SANITIZE "" CACHE STRING "Sanitizer for tests: thread, address, undefined, or empty")
  if(CYCLESCOPE_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
  endif()
  if(CYCLESCOPE_BUILD_EXAMPLES AND NOT MSVC)
    add_subdirectory(examples)
  endif()
endif()
