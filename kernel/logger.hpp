#pragma once

enum LogLevel {
  kError = 3,
  kWarn  = 4,
  kInfo  = 6,
  kDebug = 7,
};

void SetLogLevel(enum LogLevel level);
void InitializeLogFile();
int Log(enum LogLevel level, const char* format, ...);
