add_executable(cyclescope_demo demo.cpp)
target_link_libraries(cyclescope_demo PRIVATE cyclescope::cyclescope
  cyclescope::instrument)
target_compile_options(cyclescope_demo PRIVATE -finstrument-functions
  -Wall -Wextra)
set_target_properties(cyclescope_demo PROPERTIES ENABLE_EXPORTS ON)
