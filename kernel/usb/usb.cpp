#include "usb.hpp"

#include "pci.hpp"
#include "logger.hpp"
#include "usb/host/host_controller.hpp"
#include "usb/host/xhci/xhci.hpp"
#include "usb/host/ehci/ehci.hpp"

namespace usb {

  HostController* controllers[8];
  int num_controllers = 0;

  void Initialize() {
    // xHCI
    for (int i = 0; i < pci::num_device; ++i) {
      auto& dev = pci::devices[i];
      if (dev.class_code.Match(0x0c, 0x03, 0x30)) {
        auto* hc = new xhci::XhciController(dev);
        if (auto err = hc->Initialize()) {
          Log(kError, "xHCI init failed: %s\n", err.Name());
          delete hc;
        } else {
          controllers[num_controllers++] = hc;
        }
      }
    }

    // EHCI
    for (int i = 0; i < pci::num_device; ++i) {
      if (num_controllers >= 8) break;
      auto& dev = pci::devices[i];
      if (dev.class_code.Match(0x0c, 0x03, 0x20)) {
        auto* hc = new ehci::EhciController(dev);
        if (auto err = hc->Initialize()) {
          Log(kError, "EHCI init failed: %s\n", err.Name());
          delete hc;
        } else {
          controllers[num_controllers++] = hc;
        }
      }
    }

    Log(kInfo, "USB: %d controller(s) initialized\n", num_controllers);
  }

} // namespace usb
