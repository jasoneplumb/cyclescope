#pragma once

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>
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

namespace detail {
// Trivially destructible, so reading it stays defined even after this
// thread's nontrivial thread_locals (including the buffer handle) are
// gone. Set during TLS teardown; record() then drops late events.
inline thread_local bool tls_shutdown = false;

// True while collector internals run on this thread. Instrumentation
// hooks check it and stand down, so a hook firing inside record() or
// write_json() can never re-enter a lock this thread already holds.
inline thread_local bool tls_suppress = false;

// The constructor and destructor are forced inline: an out-of-line copy
// emitted by an instrumented translation unit could be selected by the
// linker, and its own entry would fire a hook before the flag is set,
// recursing without bound in unoptimized builds.
#if defined(_MSC_VER)
#define CYCLESCOPE_ALWAYS_INLINE __forceinline
#else
#define CYCLESCOPE_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

struct suppress_scope {
  bool previous;
  CYCLESCOPE_ALWAYS_INLINE suppress_scope() noexcept
      : previous(tls_suppress) {
    tls_suppress = true;
  }
  suppress_scope(const suppress_scope&) = delete;
  suppress_scope& operator=(const suppress_scope&) = delete;
  CYCLESCOPE_ALWAYS_INLINE ~suppress_scope() { tls_suppress = previous; }
};
}  // namespace detail

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
  // Deliberately immortal (never destroyed): instrumentation hooks and
  // traced destructors can run during process teardown, and a destroyed
  // collector would turn those into use-after-free. The buffers are
  // reclaimed by the operating system at process exit.
  static collector& instance() {
    static collector* self = new collector();
    return *self;
  }

  void set_capacity_per_thread(std::size_t capacity) {
    capacity_.store(capacity, std::memory_order_relaxed);
  }

  // Hot path: records into the calling thread's buffer. Never throws:
  // trace_scope destructors call this, and an allocation failure counts
  // as a dropped event rather than terminating the program.
  void record(const char* name, std::uint64_t start_ns,
              std::uint64_t duration_ns) noexcept {
    if (detail::tls_shutdown) {
      // A traced destructor ran after this thread's buffer handle was
      // destroyed; the event is dropped rather than touching dead TLS.
      return;
    }
    detail::suppress_scope suppress;
    thread_buffer& mine = local_buffer();
    std::lock_guard guard(mine.mutex);
    if (mine.events.size() >= capacity_.load(std::memory_order_relaxed)) {
      ++mine.dropped;
      return;
    }
    try {
      mine.events.push_back({name, start_ns, duration_ns});
    } catch (const std::bad_alloc&) {
      ++mine.dropped;
    }
  }

  // Writes every buffer as chrome trace-event JSON. Returns false when the
  // file cannot be opened or written. `dropped_total`, when non-null, is
  // always written with the number of events lost to capacity limits,
  // including when the function returns false.
  //
  // No lock is held across file I/O: the registry and each buffer are
  // snapshotted first (a transient copy of the event data), so recording
  // threads and thread registration never stall behind a slow disk.
  bool write_json(const char* path, std::uint64_t* dropped_total = nullptr) {
    detail::suppress_scope suppress;
    std::vector<std::shared_ptr<thread_buffer>> registered;
    {
      std::lock_guard registry_guard(registry_mutex_);
      registered = buffers_;
    }

    struct buffer_snapshot {
      std::vector<trace_event> events;
      std::uint64_t tid = 0;
    };
    std::vector<buffer_snapshot> snapshots;
    snapshots.reserve(registered.size());
    std::uint64_t dropped = 0;
    for (const auto& buffer : registered) {
      std::lock_guard buffer_guard(buffer->mutex);
      dropped += buffer->dropped;
      snapshots.push_back({buffer->events, buffer->tid});
    }
    if (dropped_total != nullptr) {
      *dropped_total = dropped;
    }

    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
      return false;
    }
    const bool header_ok = std::fputs("{\"traceEvents\":[", out) >= 0;
    bool ok = header_ok;
    if (header_ok) {
      bool first = true;
      for (const auto& snapshot : snapshots) {
        for (const trace_event& event : snapshot.events) {
          const std::string name = escape(event.name);
          ok = ok &&
               std::fprintf(
                   out,
                   "%s\n{\"name\":\"%s\",\"cat\":\"scope\",\"ph\":\"X\","
                   "\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%" PRIu64 "}",
                   first ? "" : ",", name.c_str(),
                   static_cast<double>(event.start_ns) / 1e3,
                   static_cast<double>(event.duration_ns) / 1e3,
                   snapshot.tid) >= 0;
          first = false;
        }
      }
      // Whenever the header made it out, the footer is written even after
      // an event-write failure, so the file on disk stays syntactically
      // complete JSON if the filesystem allows.
      const bool footer_ok = std::fputs("\n]}\n", out) >= 0;
      ok = ok && footer_ok;
    }
    ok = (std::fclose(out) == 0) && ok;
    return ok;
  }

  // Empties every buffer and drop counter, and prunes buffers whose
  // threads have exited. Live buffers stay registered. Call only while no
  // thread is recording. Buffers of exited threads are retained until this
  // runs so their events still reach the trace; long-running programs with
  // thread churn should clear() after each flush.
  void clear() {
    detail::suppress_scope suppress;
    std::lock_guard registry_guard(registry_mutex_);
    std::vector<std::shared_ptr<thread_buffer>> kept;
    kept.reserve(buffers_.size());
    for (const auto& buffer : buffers_) {
      std::lock_guard buffer_guard(buffer->mutex);
      buffer->events.clear();
      buffer->dropped = 0;
      if (buffer->alive) {
        kept.push_back(buffer);
      }
    }
    buffers_.swap(kept);
  }

 private:
  struct thread_buffer {
    std::mutex mutex;
    std::vector<trace_event> events;
    std::uint64_t dropped = 0;
    std::uint64_t tid = 0;
    bool alive = true;  // Guarded by mutex; false once the thread exits.
  };

  // Marks the buffer dead when its thread exits, so clear() can prune it,
  // and flags TLS teardown so later record() calls bail out safely.
  struct buffer_handle {
    std::shared_ptr<thread_buffer> buffer;
    ~buffer_handle() {
      detail::tls_shutdown = true;
      if (buffer != nullptr) {
        std::lock_guard guard(buffer->mutex);
        buffer->alive = false;
      }
    }
  };

  collector() = default;

  // Registered once per thread; the registry keeps the buffer alive after
  // the thread exits so its events still reach the trace. Tids come from a
  // monotonic counter, so pruning never recycles them.
  thread_buffer& local_buffer() {
    thread_local buffer_handle handle = [this] {
      auto buffer = std::make_shared<thread_buffer>();
      std::lock_guard guard(registry_mutex_);
      buffer->tid = next_tid_++;
      buffers_.push_back(buffer);
      return buffer_handle{buffer};
    }();
    return *handle.buffer;
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
  std::uint64_t next_tid_ = 1;  // Guarded by registry_mutex_; never wraps.
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
