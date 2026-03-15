#include "usb/host/xhci/xhci.hpp"
#include "logger.hpp"
#include <cstdlib>
#include <cstring>

namespace usb::xhci {

  void* AllocAligned(size_t size, size_t alignment) {
    void* ptr = malloc(size + alignment);
    if (!ptr) return nullptr;
    uintptr_t addr = (reinterpret_cast<uintptr_t>(ptr) + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void*>(addr);
  }

  void* AllocAlignedZeroed(size_t size, size_t alignment) {
    void* ptr = AllocAligned(size, alignment);
    if (ptr) memset(ptr, 0, size);
    return ptr;
  }

  namespace intel {
    // Intel xHCI PCI config registers
    constexpr uint8_t kXusb2Pr   = 0xD0;
    constexpr uint8_t kUsb2Prm   = 0xD4;
    constexpr uint8_t kUsb3Pssen = 0xD8;
    constexpr uint8_t kUsb3Prm   = 0xDC;

    bool IsSwitchableIntelXhci(uint16_t vendor_id, uint16_t device_id) {
      if (vendor_id != 0x8086) return false;

      switch (device_id) {
        case 0x1E31: // Intel 7 Series (Panther Point)
        case 0x8C31: // Intel 8 Series (Lynx Point)
        case 0x8CB1: // Intel 9 Series (Wildcat Point)
        case 0x9C31: // Intel 8 Series ULV (Lynx Point-LP)
        case 0x9CB1: // Intel Broadwell ULV (Wildcat Point-LP)
          return true;
        default:
          return false; // 그 외의 인텔 칩셋은 라우팅 안 함
      }
    }

    bool RouteToXhci(const pci::Device& dev) {
      uint16_t vendor_id = pci::ReadVendorId(dev);
      uint16_t device_id = pci::ReadDeviceId(dev);

      Log(kInfo, "xHCI found: %d.%d.%d (vend=%04x, dev=%04x)\n",
          dev.bus, dev.device, dev.function, vendor_id, device_id);

      if (!IsSwitchableIntelXhci(vendor_id, device_id)) {
        return false;
      }

      const uint32_t usb2_switchable_mask = pci::ReadConfReg(dev, kUsb2Prm);
      const uint32_t usb3_enable_mask     = pci::ReadConfReg(dev, kUsb3Prm);

      Log(kDebug, "Intel xHCI handoff: USB2PRM = 0x%08x, USB3PRM = 0x%08x\n",
          usb2_switchable_mask, usb3_enable_mask);

      pci::WriteConfReg(dev, kXusb2Pr, usb2_switchable_mask);
      pci::WriteConfReg(dev, kUsb3Pssen, usb3_enable_mask);

      const uint32_t usb2_routed  = pci::ReadConfReg(dev, kXusb2Pr);
      const uint32_t usb3_enabled = pci::ReadConfReg(dev, kUsb3Pssen);

      Log(kInfo, "Intel xHCI handoff done: XUSB2PR = 0x%08x, USB3_PSSEN = 0x%08x\n",
          usb2_routed, usb3_enabled);

      return true;
    }
  }  // namespace intel

  XhciController::XhciController(const pci::Device& dev)
      : dev_{dev},
        cap_{nullptr}, op_{nullptr},
        primary_interrupter_{nullptr}, doorbell_base_{nullptr},
        max_ports_{0}, max_slots_{0},
        cmd_ring_{nullptr}, cmd_ring_size_{0}, cmd_ring_enqueue_{0}, cmd_ring_cycle_{true},
        event_ring_{nullptr}, event_ring_size_{0}, event_ring_dequeue_{0}, event_ring_cycle_{true},
        erst_{nullptr}, dcbaa_{nullptr} {}

  Error XhciController::Initialize() {
    intel::RouteToXhci(dev_);
    ConfigurePCI();

    auto [bar, err] = pci::ReadBar(dev_, 0);
    if (err) return err;

    Log(kInfo, "xHCI BAR0: 0x%016lx\n", bar);
    cap_ = reinterpret_cast<volatile CapabilityRegisters*>(bar);

    if (auto err = ReadCapabilityRegisters()) return err;
    if (auto err = ResetController()) return err;
    return StartController();
  }

  Error XhciController::ReadCapabilityRegisters() {
    uint32_t dword0 = cap_->cap_length_and_version;
    uint8_t cap_length = CAP_CapLength(dword0);
    uint16_t version = CAP_HciVersion(dword0);
    if (version == 0xFFFF || cap_length == 0) {
      Log(kError, "xHCI: invalid capability registers (version=0x%04x, caplength=%u)\n",
          version, cap_length);
      return MAKE_ERROR(Error::kNotImplemented);
    }

    uintptr_t bar = reinterpret_cast<uintptr_t>(cap_);
    op_ = reinterpret_cast<volatile OperationalRegisters*>(bar + cap_length);

    uint32_t hcs1 = cap_->hcs_params1;
    max_slots_ = HCSPARAMS1_MaxSlots(hcs1);
    uint16_t max_intrs = HCSPARAMS1_MaxIntrs(hcs1);
    max_ports_ = HCSPARAMS1_MaxPorts(hcs1);

    uint32_t hcs2 = cap_->hcs_params2;
    uint32_t max_scratchpad = HCSPARAMS2_MaxScratchpadBufs(hcs2);
    uint8_t erst_max = HCSPARAMS2_ErstMax(hcs2);

    uint32_t hcc1 = cap_->hcc_params1;
    bool ac64 = HCCPARAMS1_AC64(hcc1);
    bool csz  = HCCPARAMS1_CSZ(hcc1);

    uint32_t db_off  = cap_->db_off & ~0x3u;
    uint32_t rts_off = cap_->rts_off & ~0x1Fu;

    doorbell_base_ = reinterpret_cast<volatile uint32_t*>(bar + db_off);
    // Interrupter 0 = Runtime base + 0x20
    primary_interrupter_ = reinterpret_cast<volatile InterrupterRegisterSet*>(bar + rts_off + 0x20);

    Log(kInfo, "xHCI v%x.%x, CAPLENGTH=%u\n",
        version >> 8, version & 0xFF, cap_length);
    Log(kInfo, "  MaxSlots=%u, MaxIntrs=%u, MaxPorts=%u\n",
        max_slots_, max_intrs, max_ports_);
    Log(kInfo, "  AC64=%d, ContextSize=%dB, ErstMax=%u, Scratchpad=%u\n",
        ac64, csz ? 64 : 32, 1u << erst_max, max_scratchpad);
    Log(kInfo, "  DBOFF=0x%x, RTSOFF=0x%x\n", db_off, rts_off);

    uint32_t sts = op_->usb_sts;
    Log(kInfo, "  USBSTS=0x%08x (HCH=%d, CNR=%d)\n",
        sts, !!(sts & USBSTS_HCH), !!(sts & USBSTS_CNR));

    return MAKE_ERROR(Error::kSuccess);
  }

  Error XhciController::ResetController() {
    // 1. Halt: USBCMD.Run/Stop = 0
    op_->usb_cmd &= ~USBCMD_RUN_STOP;
    // USBSTS.HCH == 0 이 될때(정지)까지 대기한다. 
    while (!(op_->usb_sts & USBSTS_HCH)) {}
    Log(kInfo, "xHCI: controller halted\n");

    // 2. Reset: USBCMD.HCRST = 1
    op_->usb_cmd |= USBCMD_HCRST;
    // HCRST == 0 (리셋 완료)
    while (op_->usb_cmd & USBCMD_HCRST) {}
    // CNR == 0 (컨트롤러 준비 완료)
    while (op_->usb_sts & USBSTS_CNR) {}
    Log(kInfo, "xHCI: controller reset done\n");

    // 3. CONFIG.MaxSlotsEn 설정
    op_->config = max_slots_;
    Log(kInfo, "xHCI: MaxSlotsEn=%u\n", max_slots_);

    return MAKE_ERROR(Error::kSuccess);
  }

  constexpr uint32_t kCmdRingSize   = 256;
  constexpr uint32_t kEventRingSize = 256;

  Error XhciController::StartController() {
    // 1. DCBAA 할당: (MaxSlots + 1) * 8 바이트, 64바이트 정렬
    size_t dcbaa_bytes = (max_slots_ + 1) * sizeof(uint64_t);
    dcbaa_ = reinterpret_cast<uint64_t*>(AllocAlignedZeroed(dcbaa_bytes, 64));
    if (!dcbaa_) return MAKE_ERROR(Error::kNoEnoughMemory);

    uintptr_t dcbaa_phys = reinterpret_cast<uintptr_t>(dcbaa_);
    op_->dcbaap_lo = static_cast<uint32_t>(dcbaa_phys);
    op_->dcbaap_hi = static_cast<uint32_t>(dcbaa_phys >> 32);
    Log(kInfo, "xHCI: DCBAA @ 0x%016lx\n", dcbaa_phys);

    // 2. Command Ring 할당: kCmdRingSize TRB, 마지막은 Link TRB
    cmd_ring_size_ = kCmdRingSize;
    cmd_ring_ = reinterpret_cast<TRB*>(AllocAlignedZeroed(cmd_ring_size_ * sizeof(TRB), 64));
    if (!cmd_ring_) return MAKE_ERROR(Error::kNoEnoughMemory);

    TRB& link = cmd_ring_[cmd_ring_size_ - 1];
    uintptr_t cmd_ring_phys = reinterpret_cast<uintptr_t>(cmd_ring_);
    link.parameter[0] = static_cast<uint32_t>(cmd_ring_phys);
    link.parameter[1] = static_cast<uint32_t>(cmd_ring_phys >> 32);
    link.control = TRB_SetType(TRB_TYPE_LINK) | LINK_TRB_TOGGLE_CYCLE;

    cmd_ring_enqueue_ = 0;
    cmd_ring_cycle_ = true;

    uintptr_t crcr_val = cmd_ring_phys | 1;  // Cycle State = 1
    op_->cr_cr_lo = static_cast<uint32_t>(crcr_val);
    op_->cr_cr_hi = static_cast<uint32_t>(crcr_val >> 32);
    Log(kInfo, "xHCI: Command Ring @ 0x%016lx (%u TRBs)\n", cmd_ring_phys, cmd_ring_size_);

    // 3. Event Ring 할당
    event_ring_size_ = kEventRingSize;
    event_ring_ = reinterpret_cast<TRB*>(AllocAlignedZeroed(event_ring_size_ * sizeof(TRB), 64));
    if (!event_ring_) return MAKE_ERROR(Error::kNoEnoughMemory);

    event_ring_dequeue_ = 0;
    event_ring_cycle_ = true;

    // ERST 할당
    erst_ = reinterpret_cast<ERSTEntry*>(AllocAlignedZeroed(sizeof(ERSTEntry), 64));
    if (!erst_) return MAKE_ERROR(Error::kNoEnoughMemory);

    uintptr_t event_ring_phys = reinterpret_cast<uintptr_t>(event_ring_);
    erst_->ring_segment_base_lo = static_cast<uint32_t>(event_ring_phys);
    erst_->ring_segment_base_hi = static_cast<uint32_t>(event_ring_phys >> 32);
    erst_->ring_segment_size = event_ring_size_;

    // Interrupter 0 설정
    uintptr_t erst_phys = reinterpret_cast<uintptr_t>(erst_);
    primary_interrupter_->erstsz = 1;
    // ERDP
    primary_interrupter_->erdp_lo = static_cast<uint32_t>(event_ring_phys);
    primary_interrupter_->erdp_hi = static_cast<uint32_t>(event_ring_phys >> 32);
    // ERSTBA
    primary_interrupter_->erstba_lo = static_cast<uint32_t>(erst_phys);
    primary_interrupter_->erstba_hi = static_cast<uint32_t>(erst_phys >> 32);
    Log(kInfo, "xHCI: Event Ring @ 0x%016lx (%u TRBs), ERST @ 0x%016lx\n",
        event_ring_phys, event_ring_size_, erst_phys);

    // Interrupter 0 활성화
    primary_interrupter_->iman = IMAN_IP | IMAN_IE;

    // 4. 컨트롤러 시작
    op_->usb_cmd |= USBCMD_RUN_STOP | USBCMD_INTE;
    while (op_->usb_sts & USBSTS_HCH) {}
    Log(kInfo, "xHCI: controller running\n");

    return MAKE_ERROR(Error::kSuccess);
  }

  void XhciController::ConfigurePCI() {
    uint32_t cmd = pci::ReadConfReg(dev_, 0x04);
    cmd |= (1u << 1) | (1u << 2);  // Memory Space + Bus Master
    pci::WriteConfReg(dev_, 0x04, cmd);
    Log(kInfo, "PCI Command: 0x%04x (MemSpace=%d, BusMaster=%d)\n",
        cmd & 0xffff, (cmd >> 1) & 1, (cmd >> 2) & 1);
  }

}  // namespace usb::xhci
