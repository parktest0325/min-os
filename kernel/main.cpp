#include <cstdint>
#include <cstddef>
#include <cstdio>

#include <numeric>
#include <vector>

#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "font.hpp"
#include "console.hpp"
#include "pci.hpp"
#include "logger.hpp"
#include "mouse.hpp"
#include "interrupt.hpp"
#include "queue.hpp"
#include "memory_map.hpp"
#include "segment.hpp"
#include "paging.hpp"
#include "memory_manager.hpp"
#include "window.hpp"
#include "layer.hpp"

#include "asmfunc.h"

#include "usb/memory.hpp"
#include "usb/device.hpp"
#include "usb/classdriver/mouse.hpp"
#include "usb/xhci/xhci.hpp"
#include "usb/xhci/trb.hpp"

char console_buf[sizeof(Console)];
Console* console;

char memory_manager_buf[sizeof(BitmapMemoryManager)];
BitmapMemoryManager* memory_manager;

char pixel_writer_buf[sizeof(RGBResv8BitPerColorPixelWriter)];
PixelWriter* pixel_writer;

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

unsigned int mouse_layer_id;
Vector2D<int> screen_size;
Vector2D<int> mouse_position;

void MouseObserver(int8_t displacement_x, int8_t displacement_y) {
  auto newpos = mouse_position + Vector2D<int>{displacement_x, displacement_y};
  newpos = ElementMin(newpos, screen_size + Vector2D<int>{-1, -1});
  mouse_position = ElementMax(newpos,  {0, 0});

  layer_manager->Move(mouse_layer_id, mouse_position);
}

void SwitchEhci2Xhci(const pci::Device& xhc_dev) {
  bool intel_ehc_exist = false;
  for (int i = 0; i < pci::num_device; ++i) {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) /* EHCI */ &&
        0x8086 == pci::ReadVendorId(pci::devices[i])) {
      intel_ehc_exist = true;
      break;
    }
  }
  if (!intel_ehc_exist) {
    return;
  }

  uint32_t superspeed_ports = pci::ReadConfReg(xhc_dev, 0xdc); // USB3PRM
  pci::WriteConfReg(xhc_dev, 0xd8, superspeed_ports); // USB3_PSSEN
  uint32_t ehci2xhci_ports = pci::ReadConfReg(xhc_dev, 0xd4); // XUSB2PRM
  pci::WriteConfReg(xhc_dev, 0xd0, ehci2xhci_ports); // XUSB2PR
  Log(kDebug, "SwitchEhci2Xhci: SS = %02, xHCI = %02x\n",
      superspeed_ports, ehci2xhci_ports);
}

usb::xhci::Controller* xhc;

struct Message {
  enum Type {
    kInterruptXHCI,
  } type;
};

ArrayQueue<Message>* main_queue;

__attribute__((interrupt))
void IntHandlerXHCI(InterruptFrame* frame) {
  main_queue->Push(Message{Message::kInterruptXHCI});
  NotifyEndOfInterrupt();
}

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

extern "C" void KernelMainNewStack(const FrameBufferConfig& frame_buffer_config_ref,
                           const MemoryMap& memory_map_ref) {
  // 포인터를 전달받았지만 스택으로 이동
  FrameBufferConfig frame_buffer_config{frame_buffer_config_ref};
  MemoryMap memory_map{memory_map_ref};
  
  switch (frame_buffer_config.pixel_format) {
    case kPixelRGBResv8BitPerColor:
      pixel_writer = new(pixel_writer_buf)
        RGBResv8BitPerColorPixelWriter{frame_buffer_config};
      break;
    case kPixelBGRResv8BitPerColor:
      pixel_writer = new(pixel_writer_buf)
        BGRResv8BitPerColorPixelWriter{frame_buffer_config};
      break;
  }

  DrawDesktop(*pixel_writer);

  console = new(console_buf) Console {
    kDesktopFGColor, kDesktopBGColor
  };
  console->SetWriter(pixel_writer);

  printk("Welcome to MikanOS!\n");
  printk("Welcome to Min-OS!\n");
  SetLogLevel(kWarn);

  SetupSegments();
  const uint16_t kernel_cs = 1 << 3;    // GDT의 인덱스가 8byte 단위라서 << 3 함
  const uint16_t kernel_ss = 2 << 3;
  SetDSAll(0);
  SetCSSS(kernel_cs, kernel_ss);
  SetupIdentityPageTable();

  printk("memory_map: %p\n", &memory_map);

  ::memory_manager = new(memory_manager_buf) BitmapMemoryManager;
  // memory_map을 physical_addr 오름차순으로 InPlace 정렬
  SortMemoryMapInPlace(memory_map);

  const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);
  uintptr_t available_end = 0;
  for (uintptr_t iter = memory_map_base;
      iter < memory_map_base + memory_map.map_size;
      iter += memory_map.descriptor_size) {

    auto desc = reinterpret_cast<const MemoryDescriptor*>(iter);
    if (available_end < desc->physical_start) {
      memory_manager->MarkAllocated(
        FrameID{available_end / kBytesPerFrame},
        (desc->physical_start - available_end) / kBytesPerFrame
      );
    }

    const auto physical_end = desc->physical_start + desc->number_of_pages * kUEFIPageSize;

    if (IsAvailable(static_cast<MemoryType>(desc->type))){
      available_end = physical_end;
    } else {
      memory_manager->MarkAllocated(
        FrameID{desc->physical_start / kBytesPerFrame},
        desc->number_of_pages * kUEFIPageSize / kBytesPerFrame
      );
      // printk("type = %u, phys = %08lx - %08lx, virs = %08lx, pages = %lu, attr = %08lx\n",
      //     desc->type,
      //     desc->physical_start,
      //     physical_end,
      //     desc->virtual_start,
      //     desc->number_of_pages,
      //     desc->attribute
      // );
    }
  }

  memory_manager->SetMemoryRange(FrameID{1}, FrameID{available_end / kBytesPerFrame});

  if (auto err = InitializeHeap(*memory_manager)) {
    Log(kError, "failed to allocate pages: %s at %s:%d\n",
        err.Name(), err.File(), err.Line());
    exit(1);
  }

  // initial main_queue
  std::array<Message, 32> main_queue_data;
  ArrayQueue<Message> main_queue{main_queue_data};
  ::main_queue = &main_queue;

  auto err = pci::ScanAllBus();
  printk("ScanAllBus: %s\n", err.Name());

  for (int i = 0; i < pci::num_device; ++i) {
    const auto& dev = pci::devices[i];
    auto vendor_id = pci::ReadVendorId(dev.bus, dev.device, dev.function);
    auto class_code = pci::ReadClassCode(dev.bus, dev.device, dev.function);
    printk("%02d) %d.%d.%d: vend %04x, class %02x %02x %02x, head %02x\n",
        i, dev.bus, dev.device, dev.function,
        vendor_id, class_code.base, class_code.sub, class_code.interface, dev.header_type);
  }

  pci::Device* xhc_dev = nullptr;
  for (int i = 0; i < pci::num_device; ++i) {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x30u)) {
      xhc_dev = &pci::devices[i];
      if (0x8086 == pci::ReadVendorId(*xhc_dev)) {
        break;
      }
    }
  }

  if (xhc_dev) {
    Log(kInfo, "xHC has been found: %d:%d.%d\n",
        xhc_dev->bus, xhc_dev->device, xhc_dev->function);
  }

  SetIDTEntry(idt[InterruptVector::kXHCI], MakeIDTAttr(DescriptorType::kInterruptGate, 0),
              reinterpret_cast<uint64_t>(IntHandlerXHCI), kernel_cs);
  LoadIDT(sizeof(idt) - 1, reinterpret_cast<uintptr_t>(&idt[0]));

  uint8_t bsp_local_apic_id =
    *reinterpret_cast<const uint32_t*>(0xfee00020) >> 24;
  pci::ConfigureMSIFixedDestination(
    *xhc_dev, bsp_local_apic_id,
    pci::MSITriggerMode::kLevel, pci::MSIDeliveryMode::kFixed,
    InterruptVector::kXHCI, 0);

  const WithError<uint64_t> xhc_bar = pci::ReadBar(*xhc_dev, 0);
  Log(kDebug, "ReadBar: %s(%d)\n", xhc_bar.error.Name(), xhc_bar.value);
  uint64_t xhc_mmio_base = xhc_bar.value & ~static_cast<uint64_t>(0xf);
  Log(kDebug, "xHC mmio_base = %08lx\n", xhc_mmio_base);

  usb::xhci::Controller xhc{xhc_mmio_base};
  if (0x8086 == pci::ReadVendorId(*xhc_dev)) {
    SwitchEhci2Xhci(*xhc_dev);
  }
  {
    auto err = xhc.Initialize();
    Log(kDebug, "xhc.Initialize: %s\n", err.Name());
  }

  Log(kInfo, "xHC starting\n");
  xhc.Run();

  ::xhc = &xhc;

  usb::HIDMouseDriver::default_observer = MouseObserver;
  for (int i = 1; i <= xhc.MaxPorts(); ++i) {
    auto port = xhc.PortAt(i);
    Log(kDebug, "Port %d: IsConnected=%d\n", i, port.IsConnected());

    if (port.IsConnected()) {
      if (auto err = ConfigurePort(xhc, port)) {
        Log(kError, "failed to configure port: %s at %s:%d\n",
            err.Name(), err.File(), err.Line());
        continue;
      }
    }
  }

  printk("> bsp_local_apic_id(0): 0x%x", bsp_local_apic_id);

  screen_size.x = frame_buffer_config.horizontal_resolution;
  screen_size.y = frame_buffer_config.vertical_resolution;

  auto bgwindow = std::make_shared<Window>(
    screen_size.x, screen_size.y, frame_buffer_config.pixel_format);
  auto bgwriter = bgwindow->Writer();

  DrawDesktop(*bgwriter);

  auto mouse_window = std::make_shared<Window>(
    kMouseCursorWidth, kMouseCursorHeight, frame_buffer_config.pixel_format);
  // mouse_window->SetTransparentColor(kMouseTransparentColor);
  DrawMouseCursor(mouse_window->Writer(), {0, 0});
  mouse_position = {200, 200};

  auto main_window = std::make_shared<Window>(
    260, 100, frame_buffer_config.pixel_format);
  DrawWindow(*main_window->Writer(), "Hello Window");
  WriteString(*main_window->Writer(), {24, 28}, "Welcome to", {0, 0, 0});
  WriteString(*main_window->Writer(), {24, 44}, " MinOS World!!", {0, 0, 0});

  auto console_window = std::make_shared<Window>(
    Console::kColumns * 8, Console::kRows * 16, frame_buffer_config.pixel_format);
  console->SetWindow(console_window);

  FrameBuffer screen;
  if (auto err = screen.Initialize(frame_buffer_config)) {
    Log(kError, "failed to initialize frame buffer: %s at %s:%d\n",
      err.Name(), err.File(), err.Line());
  }

  layer_manager = new LayerManager;
  layer_manager->SetWriter(&screen);

  auto bglayer_id = layer_manager->NewLayer()
    .SetWindow(bgwindow)
    .Move({0, 0})
    .ID();

  console->SetLayerID(layer_manager->NewLayer()
    .SetWindow(console_window)
    .Move({0, 0})
    .ID());

  mouse_layer_id = layer_manager->NewLayer()
    .SetWindow(mouse_window)
    .Move(mouse_position)
    .ID();

  auto main_window_layer_id = layer_manager->NewLayer()
    .SetWindow(main_window)
    .Move({300, 100})
    .ID();

  layer_manager->UpDown(bglayer_id, 0);
  layer_manager->UpDown(console->LayerID(), 1);
  layer_manager->UpDown(main_window_layer_id, 2);
  layer_manager->UpDown(mouse_layer_id, 3);
  layer_manager->Draw({{0, 0}, screen_size});

  char str[128];
  unsigned int count = 0;

  // event loop
  while (true) {
    ++count;
    sprintf(str, "%010u", count);
    FillRectangle(*main_window->Writer(), {24, 68}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
    WriteString(*main_window->Writer(), {24, 68}, str, {0, 0, 0});
    layer_manager->Draw(main_window_layer_id);

    __asm__("cli");
    if (main_queue.Count() == 0) {
      __asm__("sti");
      continue;
    }

    Message msg = main_queue.Front();
    main_queue.Pop();
    __asm__("sti");

    switch (msg.type) {
    case Message::kInterruptXHCI:
      while (xhc.PrimaryEventRing()->HasFront()) {
        if (auto err = ProcessEvent(xhc)) {
          Log(kError, "Error while ProcessEvent: %s at %s:%d\n",
                err.Name(), err.File(), err.Line());
        }
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
