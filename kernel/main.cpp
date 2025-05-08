#include <cstdint>
#include <cstddef>
#include <cstdio>

#include <numeric>
#include <vector>
#include <deque>
#include <limits>

#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "font.hpp"
#include "console.hpp"
#include "pci.hpp"
#include "logger.hpp"
#include "mouse.hpp"
#include "interrupt.hpp"
#include "memory_map.hpp"
#include "segment.hpp"
#include "paging.hpp"
#include "memory_manager.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "message.hpp"
#include "timer.hpp"
#include "acpi.hpp"
#include "keyboard.hpp"

#include "asmfunc.h"

#include "usb/xhci/xhci.hpp"

int printk(const char* format, ...) {
  va_list ap;
  int result;
  char s[1024];

  va_start(ap, format);
  result = vsprintf(s, format, ap);
  va_end(ap);

  console->PutString(s);
  return result;
}

std::shared_ptr<Window> main_window;
unsigned int main_window_layer_id;
void InitializeMainWindow() {
  main_window = std::make_shared<Window>(
    260, 100, screen_config.pixel_format);
  DrawWindow(*main_window->Writer(), "Hello Window");
  WriteString(*main_window->Writer(), {24, 28}, "Welcome to", {0, 0, 0});
  WriteString(*main_window->Writer(), {24, 44}, " MinOS World!!", {0, 0, 0});

  main_window_layer_id = layer_manager->NewLayer()
    .SetWindow(main_window)
    .SetDraggable(true)
    .Move({300, 100})
    .ID();

  layer_manager->UpDown(main_window_layer_id, std::numeric_limits<int>::max());
}

std::deque<Message>* main_queue;
alignas(16) uint8_t kernel_main_stack[1024 * 1024];

extern "C" void KernelMainNewStack(const FrameBufferConfig& frame_buffer_config_ref,
                            const MemoryMap& memory_map_ref,
                            const acpi::RSDP& acpi_table) {
  // 포인터를 전달받았지만 스택으로 이동
  MemoryMap memory_map{memory_map_ref};
  InitializeGraphics(frame_buffer_config_ref);
  InitializeConsole();

  printk("Welcome to MikanOS!\n");
  printk("Welcome to Min-OS!\n");
  SetLogLevel(kWarn);

  InitializeSegmentation();
  InitializePaging();
  printk("memory_map: %p\n", &memory_map);
  InitializeMemoryManager(memory_map);

  ::main_queue = new std::deque<Message>(32);
  InitializeInterrupt(main_queue);

  InitializePCI();
  usb::xhci::Initialize();
  InitializeLayer();
  InitializeMainWindow();
  InitializeMouse();
  InitializeKeyboard(*main_queue);

  layer_manager->Draw({{0, 0}, ScreenSize()});

  acpi::Initialize(acpi_table);
  InitializeLAPICTimer(*main_queue);
  timer_manager->AddTimer(Timer(100, 1));

  char str[128];

  // event loop
  while (true) {
    __asm__("cli");
    const auto tick = timer_manager->CurrentTick();
    __asm__("sti");

    sprintf(str, "%010lu", tick);
    FillRectangle(*main_window->Writer(), {24, 68}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
    WriteString(*main_window->Writer(), {24, 68}, str, {0, 0, 0});
    layer_manager->Draw(main_window_layer_id);

    __asm__("cli");
    if (main_queue->size() == 0) {
      __asm__("sti\n\thlt");
      continue;
    }

    Message msg = main_queue->front();
    main_queue->pop_front();
    __asm__("sti");

    switch (msg.type) {
    case Message::kInterruptXHCI:
      usb::xhci::ProcessEvents();
      break;
    case Message::kTimerTimeout:
      printk("Timer: timeout = %lu, value = %d\n",
        msg.arg.timer.timeout, msg.arg.timer.value);
      if (msg.arg.timer.value > 0) {
        timer_manager->AddTimer(Timer(
          msg.arg.timer.timeout + 1000 * 60, msg.arg.timer.value + 1));
      }
      break;
    case Message::kKeyPush:
      if (msg.arg.keyboard.ascii != 0) {
        printk("%c", msg.arg.keyboard.ascii);
      }
      break;
    default:
      Log(kError, "Unknown message type: %d\n", msg.type);
    }
  }
}

extern "C" void __cxa_pure_virtual() {
  while (1) __asm__("hlt");
}