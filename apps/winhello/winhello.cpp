#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "../syscall.h"

extern "C" void main(int argc, char** argv) {
  auto [layer_id, err_openwin] = SyscallOpenWindow(200, 100, 10, 10, argv[1]);
  if (err_openwin) {
    exit(err_openwin);
  }

  SyscallWinWriteString(layer_id, 7, 24, 0xc00000, argv[2]);
  SyscallWinWriteString(layer_id, 14, 40, 0x00c000, argv[2]);
  SyscallWinWriteString(layer_id, 21, 56, 0x0000c0, argv[2]);
  SyscallWinWriteString(layer_id, 7, 72, 0xc00000, argv[2]);

  AppEvent events[1];
  while (true) {
    auto [ n, err ] = SyscallReadEvent(events, 1);
    if (err) {
      printf("ReadEvent failed: %s\n", strerror(err));
    }

    if (events[0].type == AppEvent::kQuit) {
      break;
    } else if (events[0].type == AppEvent::kMouseMove ||
        events[0].type == AppEvent::kMouseButton ||
        events[0].type == AppEvent::kKeyPush) {
      // ignore
    } else {
      printf("unknown event: type = %d\n", events[0].type);
    }
  }

  SyscallCloseWindow(layer_id);
  exit(0);
}