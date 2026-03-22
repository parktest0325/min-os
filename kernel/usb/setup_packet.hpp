#pragma once
#include <cstdint>

namespace usb {

  struct SetupPacket {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
  } __attribute__((packed));
  static_assert(sizeof(SetupPacket) == 8);

  // bmRequestType 방향
  constexpr uint8_t REQUEST_DIR_OUT = 0x00;  // Host → Device
  constexpr uint8_t REQUEST_DIR_IN  = 0x80;  // Device → Host

  // Standard bRequest 코드
  constexpr uint8_t REQUEST_GET_STATUS        = 0;
  constexpr uint8_t REQUEST_CLEAR_FEATURE     = 1;
  constexpr uint8_t REQUEST_SET_FEATURE       = 3;
  constexpr uint8_t REQUEST_SET_ADDRESS       = 5;
  constexpr uint8_t REQUEST_GET_DESCRIPTOR    = 6;
  constexpr uint8_t REQUEST_SET_DESCRIPTOR    = 7;
  constexpr uint8_t REQUEST_GET_CONFIGURATION = 8;
  constexpr uint8_t REQUEST_SET_CONFIGURATION = 9;

  // Descriptor Type (wValue 상위 바이트)
  constexpr uint8_t DESC_TYPE_DEVICE        = 1;
  constexpr uint8_t DESC_TYPE_CONFIGURATION = 2;
  constexpr uint8_t DESC_TYPE_STRING        = 3;
  constexpr uint8_t DESC_TYPE_INTERFACE     = 4;
  constexpr uint8_t DESC_TYPE_ENDPOINT      = 5;

}  // namespace usb
