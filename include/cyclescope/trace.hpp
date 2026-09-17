#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Scope tracing core. Threads record complete events (name, start,
// duration) into their own buffers; a collector owns every buffer and
// writes the whole trace as chrome trace-event JSON, which the Perfetto UI
// and chrome://tracing both load.
//
// Contracts:
// - Event names must outlive the collector (string literals, __func__, or
//   otherwise static storage); the buffer stores the pointer, not a copy.
// - Recording is safe from any thread. Each thread locks only its own
//   buffer's mutex, which is uncontended except while a flush snapshots it.
// - Buffers are bounded (set_capacity_per_thread); overflow drops events
//   and counts the drops, and write_json reports the total dropped.

namespace cyclescope {

struct trace_event {
  const char* name;
  std::uint64_t start_ns;
  std::uint64_t duration_ns;
};

// Nanoseconds since the first use in the process; monotonic.
inline std::uint64_t trace_now_ns() {
  static const auto anchor = std::chrono::steady_clock::now();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - anchor)
          .count());
}

class collector {
 public:
  static collector& instance() {
    static collector self;
    return self;
  }

  void set_capacity_per_thread(std::size_t capacity) {
    capacity_.store(capacity, std::memory_order_relaxed);
  }

  // Hot path: records into the calling thread's buffer.
  void record(const char* name, std::uint64_t start_ns,
              std::uint64_t duration_ns) {
    thread_buffer& mine = local_buffer();
    std::lock_guard guard(mine.mutex);
    if (mine.events.size() >= capacity_.load(std::memory_order_relaxed)) {
      ++mine.dropped;
      return;
    }
    mine.events.push_back({name, start_ns, duration_ns});
  }

  // Writes every buffer as chrome trace-event JSON. Returns false when the
  // file cannot be opened or written. `dropped_total`, when non-null,
  // receives the number of events lost to capacity limits.
  bool write_json(const char* path, std::uint64_t* dropped_total = nullptr) {
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
      return false;
    }
    bool ok = std::fputs("{\"traceEvents\":[", out) >= 0;
    std::uint64_t dropped = 0;
    bool first = true;

    std::lock_guard registry_guard(registry_mutex_);
    for (const auto& buffer : buffers_) {
      std::lock_guard buffer_guard(buffer->mutex);
      dropped += buffer->dropped;
      for (const trace_event& event : buffer->events) {
        const std::string name = escape(event.name);
        ok = ok &&
             std::fprintf(
                 out,
                 "%s\n{\"name\":\"%s\",\"cat\":\"scope\",\"ph\":\"X\","
                 "\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%u}",
                 first ? "" : ",", name.c_str(),
                 static_cast<double>(event.start_ns) / 1e3,
                 static_cast<double>(event.duration_ns) / 1e3,
                 buffer->tid) >= 0;
        first = false;
      }
    }
    ok = ok && std::fputs("\n]}\n", out) >= 0;
    ok = (std::fclose(out) == 0) && ok;
    if (dropped_total != nullptr) {
      *dropped_total = dropped;
    }
    return ok;
  }

  // Empties every buffer and drop counter; buffers stay registered.
  // Call only while no thread is recording.
  void clear() {
    std::lock_guard registry_guard(registry_mutex_);
    for (const auto& buffer : buffers_) {
      std::lock_guard buffer_guard(buffer->mutex);
      buffer->events.clear();
      buffer->dropped = 0;
    }
  }

 private:
  struct thread_buffer {
    std::mutex mutex;
    std::vector<trace_event> events;
    std::uint64_t dropped = 0;
    std::uint32_t tid = 0;
  };

  collector() = default;

  // Registered once per thread; the registry keeps the buffer alive after
  // the thread exits so its events still reach the trace.
  thread_buffer& local_buffer() {
    thread_local std::shared_ptr<thread_buffer> mine = [this] {
      auto buffer = std::make_shared<thread_buffer>();
      std::lock_guard guard(registry_mutex_);
      buffer->tid = static_cast<std::uint32_t>(buffers_.size() + 1);
      buffers_.push_back(buffer);
      return buffer;
    }();
    return *mine;
  }

  static std::string escape(const char* raw) {
    std::string out;
    for (const char* p = raw != nullptr ? raw : "(null)"; *p != '\0'; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c == '"' || c == '\\') {
        out += '\\';
        out += static_cast<char>(c);
      } else if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
    return out;
  }

  std::mutex registry_mutex_;
  std::vector<std::shared_ptr<thread_buffer>> buffers_;
  std::atomic<std::size_t> capacity_{std::size_t{1} << 20};
};

// RAII scope: records one complete event covering its lifetime.
class trace_scope {
 public:
  explicit trace_scope(const char* name)
      : name_(name), start_ns_(trace_now_ns()) {}
  trace_scope(const trace_scope&) = delete;
  trace_scope& operator=(const trace_scope&) = delete;
  ~trace_scope() {
    collector::instance().record(name_, start_ns_,
                                 trace_now_ns() - start_ns_);
  }

 private:
  const char* name_;
  std::uint64_t start_ns_;
};

}  // namespace cyclescope

#define CYCLESCOPE_CONCAT_INNER(a, b) a##b
#define CYCLESCOPE_CONCAT(a, b) CYCLESCOPE_CONCAT_INNER(a, b)

// Names one scope explicitly; the argument must have static lifetime.
#define CYCLESCOPE_SCOPE(name) \
  ::cyclescope::trace_scope CYCLESCOPE_CONCAT(cs_scope_, __LINE__)(name)

// Names the scope after the enclosing function.
#define CYCLESCOPE_SCOPE_FUNC() CYCLESCOPE_SCOPE(__func__)
