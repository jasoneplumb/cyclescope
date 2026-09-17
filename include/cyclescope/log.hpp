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
// Null inputs render as "(null)" rather than invoking undefined behavior.
inline std::string format_line(const char* file, int line, const char* level,
                               const char* message) {
  const auto safe = [](const char* s) { return s ? s : "(null)"; };
  std::string out;
  out.reserve(64);
  out += safe(file);
  out += '(';
  out += std::to_string(line);
  out += "): ";
  out += safe(level);
  out += ": ";
  out += safe(message);
  return out;
}

// One write call per line, so concurrent emitters cannot interleave a
// message with its newline.
inline void emit(const char* file, int line, const char* level,
                 const char* message) {
  std::string out = format_line(file, line, level, message);
  out += '\n';
  std::fputs(out.c_str(), stderr);
}

}  // namespace cyclescope::detail

#if defined(CYCLESCOPE_LOG_DISABLE) || defined(CYCLESCOPE_LOG_DISABLE_ERROR)
#define CS_ERROR(message) ((void)0)
#else
#define CS_ERROR(message)                                               \
  do {                                                                  \
    ::cyclescope::detail::emit(__FILE__, __LINE__, "error", (message)); \
    std::fflush(stderr); /* abort() need not flush redirected streams */ \
    std::abort();                                                       \
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
