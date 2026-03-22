#include "usb/usb_device.hpp"
#include "logger.hpp"

namespace usb {

  UsbDevice::UsbDevice(HostController* hc, uint8_t slot_id, uint8_t port, uint8_t speed)
      : hc_{hc}, slot_id_{slot_id}, port_{port}, speed_{speed} {}

  Error UsbDevice::GetDescriptor(uint8_t desc_type, uint8_t index, void* buf, uint16_t len) {
    SetupPacket setup{};
    setup.bmRequestType = REQUEST_DIR_IN;
    setup.bRequest = REQUEST_GET_DESCRIPTOR;
    setup.wValue = (static_cast<uint16_t>(desc_type) << 8) | index;
    setup.wIndex = 0;
    setup.wLength = len;
    return hc_->ControlTransfer(slot_id_, setup, buf, len);
  }

  Error UsbDevice::SetConfiguration(uint8_t config_value) {
    SetupPacket setup{};
    setup.bmRequestType = REQUEST_DIR_OUT;
    setup.bRequest = REQUEST_SET_CONFIGURATION;
    setup.wValue = config_value;
    setup.wIndex = 0;
    setup.wLength = 0;
    return hc_->ControlTransfer(slot_id_, setup, nullptr, 0);
  }

}  // namespace usb
