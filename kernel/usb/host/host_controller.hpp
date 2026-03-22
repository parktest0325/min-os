#pragma once
#include "error.hpp"
#include "usb/setup_packet.hpp"

namespace usb {
  class HostController {
  public:
    virtual ~HostController() = default;
    virtual Error Initialize() = 0;
    virtual Error ControlTransfer(uint8_t slot_id,
                                  const SetupPacket& setup,
                                  void* buf, uint16_t len) = 0;
  };
}
