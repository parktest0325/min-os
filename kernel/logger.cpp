#include "logger.hpp"

#include <cstddef>
#include <cstdio>

#include "console.hpp"
#include "fat.hpp"

namespace {
  LogLevel log_level = kWarn;
  fat::FileDescriptor* log_fd = nullptr;
}

extern Console* console;

void SetLogLevel(LogLevel level) {
  log_level = level;
}

void InitializeLogFile() {
  auto [ entry, post_slash ] = fat::FindFile("kernlog.txt");
  if (!entry) {
    auto [ new_entry, err ] = fat::CreateFile("kernlog.txt");
    if (err) return;
    entry = new_entry;
  }
  static fat::FileDescriptor fd(*entry);
  log_fd = &fd;
}

int Log(LogLevel level, const char* format, ...) {
  if (level > log_level) {
    return 0;
  }

  va_list ap;
  int result;
  char s[1024];

  va_start(ap, format);
  result = vsprintf(s, format, ap);
  va_end(ap);

  console->PutString(s);
  if (log_fd) {
    log_fd->Write(s, result);
  }
  return result;
}
