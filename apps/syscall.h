#ifdef __cplusplus
#include <cstdint>
#include <cstddef>

extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "../kernel/logger.hpp"

struct SyscallResult {
  uint64_t value;
  int error;
};

#define LAYER_NO_REDRAW (0x00000001ull << 32)
// assembly syscall wrapper function
struct SyscallResult SyscallLogString(enum LogLevel level, const char* message);
struct SyscallResult SyscallPutString(int fd, const char* s, size_t len);
void SyscallExit(int exit_code);
struct SyscallResult SyscallOpenWindow(int w, int h, int x, int y, const char* title);
struct SyscallResult SyscallWinWriteString(uint64_t layer_id_flags, int x, int y, uint32_t color, const char* s);
struct SyscallResult SyscallWinFillRectangle(uint64_t layer_id_flags, int x, int y, int w, int h, uint32_t color);
struct SyscallResult SyscallGetCurrentTick();
struct SyscallResult SyscallWinRedraw(uint64_t layer_id_flags);

#ifdef __cplusplus
} // extern "C"
#endif