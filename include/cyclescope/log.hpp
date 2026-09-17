#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

// Diagnostics in the path(line): message form that editors and IDE output
// panes turn into a clickable jump to source. Three levels: CS_ERROR prints
// and aborts (a diagnostic for states the program must not continue from),
// CS_WARN and CS_TODO print and continue. Each level compiles out with
// CYCLESCOPE_LOG_DISABLE or its per-level switch, leaving no trace in the
// binary.

namespace cyclescope::detail {

// Formatting is a plain function so tests can pin the exact output shape.
inline std::string format_line(const char* file, int line, const char* level,
                               const char* message) {
  std::string out;
  out.reserve(64);
  out += file;
  out += '(';
  out += std::to_string(line);
  out += "): ";
  out += level;
  out += ": ";
  out += message;
  return out;
}

inline void emit(const char* file, int line, const char* level,
                 const char* message) {
  std::fputs(format_line(file, line, level, message).c_str(), stderr);
  std::fputc('\n', stderr);
}

}  // namespace cyclescope::detail

#if defined(CYCLESCOPE_LOG_DISABLE) || defined(CYCLESCOPE_LOG_DISABLE_ERROR)
#define CS_ERROR(message) ((void)0)
#else
#define CS_ERROR(message)                                              \
  do {                                                                 \
    ::cyclescope::detail::emit(__FILE__, __LINE__, "error", (message)); \
    std::abort();                                                      \
  } while (0)
#endif

#if defined(CYCLESCOPE_LOG_DISABLE) || defined(CYCLESCOPE_LOG_DISABLE_WARN)
#define CS_WARN(message) ((void)0)
#else
#define CS_WARN(message) \
  ::cyclescope::detail::emit(__FILE__, __LINE__, "warning", (message))
#endif

#if defined(CYCLESCOPE_LOG_DISABLE) || defined(CYCLESCOPE_LOG_DISABLE_TODO)
#define CS_TODO(message) ((void)0)
#else
#define CS_TODO(message) \
  ::cyclescope::detail::emit(__FILE__, __LINE__, "todo", (message))
#endif
