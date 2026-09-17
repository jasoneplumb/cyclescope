#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cyclescope/trace.hpp>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
void cyclescope_fixture_parent(void);
int cyclescope_fixture_flush(const char* path);
}

namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::size_t count_occurrences(const std::string& text,
                              const std::string& needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("instrumented calls appear as named complete events") {
  cyclescope::collector::instance().clear();
  cyclescope_fixture_parent();
  cyclescope_fixture_parent();

  const std::string path = "cyclescope_test_instrument.trace.json";
  std::uint64_t dropped = 1;
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str(),
                                                       &dropped));
  const std::string json = slurp(path);
  std::remove(path.c_str());

  CHECK(dropped == 0);
  CHECK(count_occurrences(json, "\"name\":\"cyclescope_fixture_parent\"") ==
        2);
  CHECK(count_occurrences(json, "\"name\":\"cyclescope_fixture_leaf\"") == 6);
}

TEST_CASE("flushing from instrumented code does not deadlock") {
  cyclescope::collector::instance().clear();
  cyclescope_fixture_parent();
  const std::string path = "cyclescope_test_instrument_flush.trace.json";
  CHECK(cyclescope_fixture_flush(path.c_str()) == 1);
  const std::string json = slurp(path);
  std::remove(path.c_str());
  CHECK(count_occurrences(json, "\"name\":\"cyclescope_fixture_parent\"") ==
        1);
}
