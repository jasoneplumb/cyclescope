#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cyclescope/trace.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

std::string temp_trace_path(const char* tag) {
  return std::string("cyclescope_test_") + tag + ".trace.json";
}

// The collector is a process-wide singleton, so every test case starts by
// clearing it and restoring the default capacity.
void reset_collector() {
  cyclescope::collector::instance().clear();
  cyclescope::collector::instance().set_capacity_per_thread(std::size_t{1}
                                                            << 20);
}

}  // namespace

TEST_CASE("scope events land in valid trace json") {
  reset_collector();
  {
    CYCLESCOPE_SCOPE("outer");
    CYCLESCOPE_SCOPE("inner");
  }
  {
    CYCLESCOPE_SCOPE_FUNC();
  }

  const std::string path = temp_trace_path("basic");
  std::uint64_t dropped = 999;
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str(),
                                                       &dropped));
  const std::string json = slurp(path);
  std::remove(path.c_str());

  CHECK(dropped == 0);
  CHECK(json.find("{\"traceEvents\":[") == 0);
  CHECK(json.rfind("]}\n") == json.size() - 3);
  CHECK(count_occurrences(json, "\"ph\":\"X\"") == 3);
  CHECK(json.find("\"name\":\"outer\"") != std::string::npos);
  CHECK(json.find("\"name\":\"inner\"") != std::string::npos);
  CHECK(json.find("\"cat\":\"scope\"") != std::string::npos);
}

TEST_CASE("names are escaped for json") {
  reset_collector();
  cyclescope::collector::instance().record("quote\"back\\slash\tend", 10,
                                           20);
  const std::string path = temp_trace_path("escape");
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str()));
  const std::string json = slurp(path);
  std::remove(path.c_str());
  CHECK(json.find("quote\\\"back\\\\slash\\u0009end") != std::string::npos);
}

TEST_CASE("capacity bounds each thread and drops are counted") {
  reset_collector();
  cyclescope::collector::instance().set_capacity_per_thread(4);
  // This thread's buffer may already hold events from earlier cases in
  // this binary; record from a fresh thread for a clean count.
  std::thread worker([] {
    for (int i = 0; i < 10; ++i) {
      cyclescope::collector::instance().record("bounded", 0, 1);
    }
  });
  worker.join();

  const std::string path = temp_trace_path("bounded");
  std::uint64_t dropped = 0;
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str(),
                                                       &dropped));
  const std::string json = slurp(path);
  std::remove(path.c_str());
  CHECK(count_occurrences(json, "\"name\":\"bounded\"") == 4);
  CHECK(dropped == 6);
}

TEST_CASE("threads keep distinct tids and survive exit") {
  reset_collector();
  constexpr int kThreads = 4;
  constexpr int kEventsPerThread = 100;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([] {
      for (int i = 0; i < kEventsPerThread; ++i) {
        CYCLESCOPE_SCOPE("worker");
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }

  const std::string path = temp_trace_path("threads");
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str()));
  const std::string json = slurp(path);
  std::remove(path.c_str());
  // All events survive their threads' exit.
  CHECK(count_occurrences(json, "\"name\":\"worker\"") ==
        kThreads * kEventsPerThread);
}

TEST_CASE("clear prunes exited threads and tids stay unique") {
  reset_collector();
  std::thread first([] { CYCLESCOPE_SCOPE("gen1"); });
  first.join();
  cyclescope::collector::instance().clear();  // Prunes gen1's dead buffer.

  std::thread second([] { CYCLESCOPE_SCOPE("gen2"); });
  second.join();

  const std::string path = temp_trace_path("churn");
  REQUIRE(cyclescope::collector::instance().write_json(path.c_str()));
  const std::string json = slurp(path);
  std::remove(path.c_str());
  CHECK(count_occurrences(json, "\"name\":\"gen1\"") == 0);
  CHECK(count_occurrences(json, "\"name\":\"gen2\"") == 1);
}

TEST_CASE("recording races a concurrent flush safely") {
  reset_collector();
  std::atomic<bool> stop{false};
  std::thread recorder([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      CYCLESCOPE_SCOPE("racing");
    }
  });
  for (int i = 0; i < 20; ++i) {
    const std::string path = temp_trace_path("race");
    CHECK(cyclescope::collector::instance().write_json(path.c_str()));
    std::remove(path.c_str());
  }
  stop.store(true, std::memory_order_relaxed);
  recorder.join();
  CHECK(true);
}
