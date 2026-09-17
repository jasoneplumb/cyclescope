#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cyclescope/log.hpp>
#include <string>

TEST_CASE("format_line produces the clickable diagnostic shape") {
  const std::string line = cyclescope::detail::format_line(
      "src/widget.cpp", 42, "warning", "spline unreticulated");
  CHECK(line == "src/widget.cpp(42): warning: spline unreticulated");
}

TEST_CASE("warn and todo macros expand and continue") {
  CS_WARN("exercising the warning path");
  CS_TODO("exercising the todo path");
  CHECK(true);
}

TEST_CASE("format handles edge inputs") {
  CHECK(cyclescope::detail::format_line("f", 0, "todo", "") ==
        "f(0): todo: ");
  const std::string long_message(300, 'x');
  const std::string line = cyclescope::detail::format_line(
      "a/b/c.hpp", 9001, "error", long_message.c_str());
  CHECK(line.size() == std::string("a/b/c.hpp(9001): error: ").size() + 300);
}
