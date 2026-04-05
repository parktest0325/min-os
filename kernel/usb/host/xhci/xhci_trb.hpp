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
  constexpr uint8_t TRB_TYPE_CONFIGURE_EP_CMD = 12;
  constexpr uint8_t TRB_TYPE_NOOP_CMD         = 23;
  constexpr uint8_t TRB_TYPE_TRANSFER_EVENT   = 32;
  constexpr uint8_t TRB_TYPE_CMD_COMPLETION   = 33;
  constexpr uint8_t TRB_TYPE_PORT_STATUS_CHG  = 34;

  constexpr uint32_t TRB_IDT = 1u << 6;  // Immediate Data

  // Setup Stage TRB: Transfer Type (TRT) [17:16]
  constexpr uint32_t SETUP_TRT_NO_DATA = 0u << 16;
  constexpr uint32_t SETUP_TRT_OUT     = 2u << 16;
  constexpr uint32_t SETUP_TRT_IN      = 3u << 16;

  // Data/Status Stage TRB: Direction [16]
  constexpr uint32_t TRB_DIR_OUT = 0u << 16;
  constexpr uint32_t TRB_DIR_IN  = 1u << 16;

  // Link TRB: control bit 1 = Toggle Cycle
  constexpr uint32_t LINK_TRB_TOGGLE_CYCLE = 1u << 1;

  // Completion Code
  constexpr uint8_t TRB_COMPLETION_SUCCESS    = 1;
  constexpr uint8_t TRB_COMPLETION_SHORT_PKT  = 13;
  inline uint8_t TRB_CompletionCode(uint32_t status) { return (status >> 24) & 0xFF; }
  inline uint8_t TRB_SlotId(uint32_t control) { return (control >> 24) & 0xFF; }

  // Event Ring Segment Table Entry
  struct ERSTEntry {
    uint32_t ring_segment_base_lo;
    uint32_t ring_segment_base_hi;
    uint32_t ring_segment_size;   // TRB 개수
    uint32_t reserved;
  };
  static_assert(sizeof(ERSTEntry) == 16);

}  // namespace usb::xhci
