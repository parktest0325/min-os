#pragma once
#include <cstdint>

namespace usb::xhci {

  // 32-byte context (CSZ=0)
  struct SlotContext {
    uint32_t dword[8];
  };
  static_assert(sizeof(SlotContext) == 32);

  struct EndpointContext {
    uint32_t dword[8];
  };
  static_assert(sizeof(EndpointContext) == 32);

  // Output Device Context (controller가 쓰고 드라이버가 읽음)
  struct DeviceContext {
    union {
      struct {
        SlotContext slot;
        EndpointContext ep[31];
      };
      uint32_t dci[32][8];  // dci[0]=Slot, dci[1..31]=EP
    };
  };
  static_assert(sizeof(DeviceContext) == 1024);

  // Input Control Context
  struct InputControlContext {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
  };
  static_assert(sizeof(InputControlContext) == 32);

  // Input Context (Address Device Command에 전달)
  struct InputContext {
    InputControlContext control;
    union {
      struct {
        SlotContext slot;            // DCI 0
        EndpointContext ep[31];      // ep[0]=DCI 1, ep[1]=DCI 2, ...
      };
      uint32_t dci[32][8];          // dci[0]=Slot, dci[1..31]=EP
    };
  };
  static_assert(sizeof(InputContext) == 1056);

  // -- Slot Context helpers --
  inline void SC_SetSpeed(SlotContext& sc, uint8_t speed) {
    sc.dword[0] = (sc.dword[0] & ~(0xFu << 20)) | ((speed & 0xFu) << 20);
  }
  inline void SC_SetContextEntries(SlotContext& sc, uint8_t entries) {
    sc.dword[0] = (sc.dword[0] & ~(0x1Fu << 27)) | ((entries & 0x1Fu) << 27);
  }
  inline void SC_SetRootHubPortNumber(SlotContext& sc, uint8_t port) {
    sc.dword[1] = (sc.dword[1] & ~(0xFFu << 16)) | ((port & 0xFFu) << 16);
  }

  // -- Endpoint Context helpers --
  constexpr uint8_t EP_TYPE_CONTROL = 4;

  inline void EP_SetEPType(EndpointContext& ep, uint8_t type) {
    ep.dword[1] = (ep.dword[1] & ~(0x7u << 3)) | ((type & 0x7u) << 3);
  }
  inline void EP_SetMaxPacketSize(EndpointContext& ep, uint16_t size) {
    ep.dword[1] = (ep.dword[1] & ~(0xFFFFu << 16)) | ((size & 0xFFFFu) << 16);
  }
  inline void EP_SetCErr(EndpointContext& ep, uint8_t cerr) {
    ep.dword[1] = (ep.dword[1] & ~(0x3u << 1)) | ((cerr & 0x3u) << 1);
  }
  inline void EP_SetTRDequeuePointer(EndpointContext& ep, uintptr_t addr, bool dcs) {
    ep.dword[2] = static_cast<uint32_t>(addr & ~0xFu) | (dcs ? 1u : 0u);
    ep.dword[3] = static_cast<uint32_t>(addr >> 32);
  }
  inline void EP_SetAvgTRBLength(EndpointContext& ep, uint16_t len) {
    ep.dword[4] = (ep.dword[4] & ~0xFFFFu) | (len & 0xFFFFu);
  }

  // -- USB Device Descriptor --
  struct DeviceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
  } __attribute__((packed));
  static_assert(sizeof(DeviceDescriptor) == 18);

  // Speed별 EP0 Max Packet Size
  inline uint16_t DefaultMaxPacketSize(uint8_t speed) {
    switch (speed) {
      case 2: return 8;    // Low Speed
      case 1: return 8;    // Full Speed
      case 3: return 64;   // High Speed
      case 4: return 512;  // Super Speed
      case 5: return 512;  // Super Speed Plus
      default: return 8;
    }
  }

}  // namespace usb::xhci
