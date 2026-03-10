#include "usb.hpp"

#include <vector>

#include "pci.hpp"
#include "logger.hpp"

namespace usb {

  namespace intel {
    // Intel xHCI PCI config registers
    constexpr uint8_t kXusb2Pr   = 0xD0;
    constexpr uint8_t kUsb2Prm   = 0xD4;
    constexpr uint8_t kUsb3Pssen = 0xD8;
    constexpr uint8_t kUsb3Prm   = 0xDC;

    bool IsSwitchableIntelXhci(uint16_t vendor_id, uint16_t device_id) {
      if (vendor_id != 0x8086) return false;
      
      switch (device_id) {
        case 0x1E31: // Intel 7 Series (Panther Point)
        case 0x8C31: // Intel 8 Series (Lynx Point)
        case 0x8CB1: // Intel 9 Series (Wildcat Point)
        case 0x9C31: // Intel 8 Series ULV (Lynx Point-LP)
        case 0x9CB1: // Intel Broadwell ULV (Wildcat Point-LP)
          return true;
        default:
          return false; // 그 외의 인텔 칩셋(최신 등)은 라우팅 안 함!
      }
    }

    bool RouteToXhci(const pci::Device& xhci) {
      const uint32_t usb2_switchable_mask = pci::ReadConfReg(xhci, kUsb2Prm);
      const uint32_t usb3_switchable_mask = pci::ReadConfReg(xhci, kUsb3Prm);

      Log(kDebug, "Intel xHCI handoff: USB2PRM = 0x%08x, USB3PRM = 0x%08x\n",
          usb2_switchable_mask, usb3_switchable_mask);

      pci::WriteConfReg(xhci, kXusb2Pr, usb2_switchable_mask);
      pci::WriteConfReg(xhci, kUsb3Pssen, usb3_switchable_mask);

      const uint32_t usb2_routed = pci::ReadConfReg(xhci, kXusb2Pr);
      const uint32_t usb3_enabled = pci::ReadConfReg(xhci, kUsb3Pssen);

      Log(kInfo,
          "Intel xHCI handoff done: XUSB2PR = 0x%08x, USB3_PSSEN = 0x%08x\n",
          usb2_routed, usb3_enabled);

      return true;
    }
  }  // namespace intel


  void Initialize() {
    pci::Device* xhc = nullptr;

    for (auto& dev : pci::devices) {
      if (dev.class_code.Match(0x0c, 0x03, 0x30)) {
        if (xhc == nullptr) xhc = &dev;
      }
    }

    if (xhc == nullptr) {
      Log(kDebug, "xHCI controller not found\n");
      return;
    }

    if (intel::IsSwitchableIntelXhci(pci::ReadVendorId(*xhc), pci::ReadDeviceId(*xhc))) {
      intel::RouteToXhci(*xhc);
    }

    // XhciController controller(*xhc);
    // if (!controller.Initialize()) {
    //   Log("xHCI init failed");
    //   return;
    // }

    Log(kDebug, "xHCI initialized\n");
  }

} // namespace usb