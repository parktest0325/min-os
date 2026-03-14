#pragma once
#include <cstdint>

namespace usb::xhci {

  // 모든 TRB는 16바이트
  struct TRB {
    uint32_t parameter[2];
    uint32_t status;
    uint32_t control;
  };
  static_assert(sizeof(TRB) == 16);

  // control 필드 공통
  constexpr uint32_t TRB_CYCLE       = 1u << 0;
  constexpr uint32_t TRB_CHAIN       = 1u << 4;  // Chain bit (Transfer TRB)
  constexpr uint32_t TRB_IOC         = 1u << 5;  // Interrupt On Completion
  inline uint8_t TRB_Type(uint32_t control) { return (control >> 10) & 0x3F; }
  inline uint32_t TRB_SetType(uint8_t type) { return static_cast<uint32_t>(type) << 10; }

  // TRB Type IDs
  constexpr uint8_t TRB_TYPE_NORMAL           = 1;
  constexpr uint8_t TRB_TYPE_SETUP_STAGE      = 2;
  constexpr uint8_t TRB_TYPE_DATA_STAGE       = 3;
  constexpr uint8_t TRB_TYPE_STATUS_STAGE     = 4;
  constexpr uint8_t TRB_TYPE_LINK             = 6;
  constexpr uint8_t TRB_TYPE_ENABLE_SLOT_CMD  = 9;
  constexpr uint8_t TRB_TYPE_ADDRESS_DEV_CMD  = 11;
  constexpr uint8_t TRB_TYPE_NOOP_CMD         = 23;
  constexpr uint8_t TRB_TYPE_TRANSFER_EVENT   = 32;
  constexpr uint8_t TRB_TYPE_CMD_COMPLETION   = 33;
  constexpr uint8_t TRB_TYPE_PORT_STATUS_CHG  = 34;

  // Link TRB: control bit 1 = Toggle Cycle
  constexpr uint32_t LINK_TRB_TOGGLE_CYCLE = 1u << 1;

  // Event Ring Segment Table Entry
  struct ERSTEntry {
    uint32_t ring_segment_base_lo;
    uint32_t ring_segment_base_hi;
    uint32_t ring_segment_size;   // TRB 개수
    uint32_t reserved;
  };
  static_assert(sizeof(ERSTEntry) == 16);

}  // namespace usb::xhci
