#pragma once
#include <cstdint>

namespace usb::xhci {

  // ---- Capability Registers (BAR0 base, read-only) ----

  struct CapabilityRegisters {
    uint32_t cap_length_and_version; // 0x00: [7:0] CAPLENGTH, [31:16] HCIVERSION
    uint32_t hcs_params1;            // 0x04: Structural Parameters 1
    uint32_t hcs_params2;            // 0x08: Structural Parameters 2
    uint32_t hcs_params3;            // 0x0C: Structural Parameters 3
    uint32_t hcc_params1;            // 0x10: Capability Parameters 1
    uint32_t db_off;                 // 0x14: Doorbell Offset
    uint32_t rts_off;                // 0x18: Runtime Register Space Offset
    uint32_t hcc_params2;            // 0x1C: Capability Parameters 2 (xHCI 1.1+)
  };

  // cap_length_and_version fields
  inline uint8_t  CAP_CapLength(uint32_t v)   { return v & 0xFF; }
  inline uint16_t CAP_HciVersion(uint32_t v)  { return (v >> 16) & 0xFFFF; }

  // HCSPARAMS1 fields
  inline uint8_t  HCSPARAMS1_MaxSlots(uint32_t v) { return v & 0xFF; }
  inline uint16_t HCSPARAMS1_MaxIntrs(uint32_t v) { return (v >> 8) & 0x7FF; }
  inline uint8_t  HCSPARAMS1_MaxPorts(uint32_t v) { return (v >> 24) & 0xFF; }

  // HCSPARAMS2 fields
  inline uint8_t HCSPARAMS2_ErstMax(uint32_t v) { return (v >> 4) & 0x0F; }
  inline uint32_t HCSPARAMS2_MaxScratchpadBufs(uint32_t v) {
    uint32_t hi = (v >> 21) & 0x1F;
    uint32_t lo = (v >> 27) & 0x1F;
    return (hi << 5) | lo;
  }

  // HCCPARAMS1 fields
  inline bool HCCPARAMS1_AC64(uint32_t v) { return v & 1; }
  inline bool HCCPARAMS1_CSZ(uint32_t v)  { return (v >> 2) & 1; }
  inline uint16_t HCCPARAMS1_xECP(uint32_t v) { return (v >> 16) & 0xFFFF; }

  // ---- Operational Registers (BAR0 + CAPLENGTH) ----

  struct OperationalRegisters {
    uint32_t usb_cmd;           // 0x00: USB Command
    uint32_t usb_sts;           // 0x04: USB Status
    uint32_t page_size;         // 0x08: Page Size
    uint32_t reserved1[2];      // 0x0C-0x10
    uint32_t dn_ctrl;           // 0x14: Device Notification Control
    uint32_t cr_cr_lo;          // 0x18: Command Ring Control (low)
    uint32_t cr_cr_hi;          // 0x1C: Command Ring Control (high)
    uint32_t reserved2[4];      // 0x20-0x2C
    uint32_t dcbaap_lo;         // 0x30: DCBAAP (low)
    uint32_t dcbaap_hi;         // 0x34: DCBAAP (high)
    uint32_t config;            // 0x38: Configure
  };

  // USBCMD bits
  constexpr uint32_t USBCMD_RUN_STOP = 1u << 0;
  constexpr uint32_t USBCMD_HCRST    = 1u << 1;
  constexpr uint32_t USBCMD_INTE     = 1u << 2;

  // USBSTS bits
  constexpr uint32_t USBSTS_HCH = 1u << 0;   // HC Halted
  constexpr uint32_t USBSTS_HSE = 1u << 2;   // Host System Error
  constexpr uint32_t USBSTS_CNR = 1u << 11;  // Controller Not Ready

  // ---- Runtime Registers (BAR0 + RTSOFF) ----
  //   0x00: MFINDEX
  //   0x20 + interrupter * 0x20: Interrupter Register Set

  struct InterrupterRegisterSet {
    uint32_t iman;              // 0x00: Interrupter Management
    uint32_t imod;              // 0x04: Interrupter Moderation
    uint32_t erstsz;            // 0x08: Event Ring Segment Table Size
    uint32_t reserved;          // 0x0C
    uint32_t erstba_lo;         // 0x10: ERST Base Address (low)
    uint32_t erstba_hi;         // 0x14: ERST Base Address (high)
    uint32_t erdp_lo;           // 0x18: Event Ring Dequeue Pointer (low)
    uint32_t erdp_hi;           // 0x1C: Event Ring Dequeue Pointer (high)
  };

  // IMAN bits
  constexpr uint32_t IMAN_IP = 1u << 0;  // Interrupt Pending
  constexpr uint32_t IMAN_IE = 1u << 1;  // Interrupt Enable

  // ---- Doorbell Register (BAR0 + DBOFF + slot*4) ----
  //   slot 0 = Host Controller (Command Ring)
  //   slot N = Device Slot N

  // ---- Port Register Set (BAR0 + CAPLENGTH + 0x400 + port*0x10) ----

  struct PortRegisterSet {
    uint32_t portsc;            // 0x00: Port Status and Control
    uint32_t portpmsc;          // 0x04: Port Power Management Status and Control
    uint32_t portli;            // 0x08: Port Link Info
    uint32_t porthlpmc;         // 0x0C: Port Hardware LPM Control
  };

  // PORTSC bits
  constexpr uint32_t PORTSC_CCS  = 1u << 0;   // Current Connect Status
  constexpr uint32_t PORTSC_PED  = 1u << 1;   // Port Enabled/Disabled
  constexpr uint32_t PORTSC_PR   = 1u << 4;   // Port Reset
  constexpr uint32_t PORTSC_PP   = 1u << 9;   // Port Power
  inline uint8_t PORTSC_Speed(uint32_t v) { return (v >> 10) & 0x0F; }
  constexpr uint32_t PORTSC_CSC  = 1u << 17;  // Connect Status Change
  constexpr uint32_t PORTSC_PRC  = 1u << 21;  // Port Reset Change

  // PORTSC의 W1C (Write-1-to-Clear) 비트들 — 쓰기 시 실수로 클리어하지 않기 위해 마스킹
  constexpr uint32_t PORTSC_W1C_BITS =
      PORTSC_CSC | (1u << 18) | (1u << 19) | (1u << 20) | PORTSC_PRC | (1u << 22) | (1u << 23);

}  // namespace usb::xhci
