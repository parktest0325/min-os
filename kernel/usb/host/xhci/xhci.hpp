#pragma once
#include "error.hpp"
#include "pci.hpp"
#include "usb/host/host_controller.hpp"
#include "usb/host/xhci/xhci_registers.hpp"

namespace usb::xhci {
  class XhciController : public HostController {
  public:
    XhciController(const pci::Device& dev);
    Error Initialize() override;

  private:
    const pci::Device& dev_;
    volatile CapabilityRegisters* cap_;
    volatile OperationalRegisters* op_;
    uint8_t  max_ports_;
    uint8_t  max_slots_;
    uint32_t db_off_;
    uint32_t rts_off_;

    void ConfigurePCI();
    Error ReadCapabilityRegisters();
  };
}
