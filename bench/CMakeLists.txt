add_executable(overhead_bench overhead_bench.cpp overhead_fib.cpp)
set_source_files_properties(overhead_fib.cpp PROPERTIES
  COMPILE_OPTIONS "-finstrument-functions")
target_link_libraries(overhead_bench PRIVATE cyclescope::cyclescope
  cyclescope::instrument)
target_compile_options(overhead_bench PRIVATE -Wall -Wextra)
set_target_properties(overhead_bench PROPERTIES ENABLE_EXPORTS ON)
if(CYCLESCOPE_SANITIZE)
  target_compile_options(overhead_bench PRIVATE
    -fsanitize=${CYCLESCOPE_SANITIZE} -g -fno-omit-frame-pointer)
  target_link_options(overhead_bench PRIVATE
    -fsanitize=${CYCLESCOPE_SANITIZE})
endif()
