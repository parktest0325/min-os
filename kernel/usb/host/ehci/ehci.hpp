#pragma once
#include "error.hpp"
#include "pci.hpp"
#include "usb/host/host_controller.hpp"

namespace usb::ehci {
  class EhciController : public HostController {
  public:
    EhciController(const pci::Device& dev);
    Error Initialize() override;

  private:
    const pci::Device& dev_;
    uint64_t mmio_base_;

    void ConfigurePCI();
  };
}
