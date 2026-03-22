#pragma once
#include "error.hpp"
#include "usb/setup_packet.hpp"
#include "usb/host/host_controller.hpp"
#include "usb/host/xhci/xhci_context.hpp"

namespace usb {

  class UsbDevice {
  public:
    UsbDevice(HostController* hc, uint8_t slot_id, uint8_t port, uint8_t speed);

    Error GetDescriptor(uint8_t desc_type, uint8_t index, void* buf, uint16_t len);
    Error SetConfiguration(uint8_t config_value);

    uint8_t SlotId() const { return slot_id_; }
    uint8_t Port() const { return port_; }
    uint8_t Speed() const { return speed_; }

  private:
    HostController* hc_;
    uint8_t slot_id_;
    uint8_t port_;
    uint8_t speed_;
  };

}  // namespace usb
