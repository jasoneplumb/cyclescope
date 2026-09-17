include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG 1da23a3e8119ec5cce4f9388e91b065e20bf06f5 # v2.4.12
)
FetchContent_MakeAvailable(doctest)

function(cyclescope_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE cyclescope::cyclescope
    doctest::doctest Threads::Threads)
  target_compile_options(${name} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
  )
  if(CYCLESCOPE_SANITIZE)
    if(MSVC)
      message(WARNING "CYCLESCOPE_SANITIZE is not supported with MSVC; ignoring")
    else()
      target_compile_options(${name} PRIVATE
        -fsanitize=${CYCLESCOPE_SANITIZE} -g -fno-omit-frame-pointer)
      target_link_options(${name} PRIVATE -fsanitize=${CYCLESCOPE_SANITIZE})
    endif()
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

cyclescope_add_test(log_test)
cyclescope_add_test(trace_test)

# The instrumentation backend test: only the fixture translation unit is
# compiled with -finstrument-functions, so the assertions run over a known
# call set instead of every doctest internal. ENABLE_EXPORTS lets dladdr
# resolve the fixture symbols from the executable on Linux.
if(NOT MSVC)
  add_executable(instrument_test instrument_test.cpp instrument_fixture.cpp)
  set_source_files_properties(instrument_fixture.cpp PROPERTIES
    COMPILE_OPTIONS "-finstrument-functions")
  target_link_libraries(instrument_test PRIVATE cyclescope::cyclescope
    cyclescope::instrument doctest::doctest Threads::Threads)
  target_compile_options(instrument_test PRIVATE -Wall -Wextra)
  set_target_properties(instrument_test PROPERTIES ENABLE_EXPORTS ON)
  if(CYCLESCOPE_SANITIZE)
    target_compile_options(instrument_test PRIVATE
      -fsanitize=${CYCLESCOPE_SANITIZE} -g -fno-omit-frame-pointer)
    target_link_options(instrument_test PRIVATE
      -fsanitize=${CYCLESCOPE_SANITIZE})
  endif()
  add_test(NAME instrument_test COMMAND instrument_test)
  # A suppression regression deadlocks rather than fails; bound it.
  set_tests_properties(instrument_test PROPERTIES TIMEOUT 120)
endif()
