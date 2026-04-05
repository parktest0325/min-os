#include "usb/host/xhci/xhci.hpp"
#include "usb/host/xhci/xhci_context.hpp"
#include "usb/usb_device.hpp"
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
        erst_{nullptr}, dcbaa_{nullptr}, slot_data_{} {}

  Error XhciController::Initialize() {
    intel::RouteToXhci(dev_);
    ConfigurePCI();

    auto [bar, err] = pci::ReadBar(dev_, 0);
    if (err) return err;

    Log(kInfo, "xHCI BAR0: 0x%016lx\n", bar);
    cap_ = reinterpret_cast<volatile CapabilityRegisters*>(bar);

    if (auto err = ReadCapabilityRegisters()) return err;
    if (auto err = ResetController()) return err;
    if (auto err = StartController()) return err;
    return ScanPorts();
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


  volatile PortRegisterSet* XhciController::PortAt(uint8_t port_num) {
    // port_num은 1 ~ max_ports_
    uintptr_t base = reinterpret_cast<uintptr_t>(op_) + 0x400 + (port_num - 1) * 0x10;
    return reinterpret_cast<volatile PortRegisterSet*>(base);
  }

  void XhciController::PushCommand(uint32_t param0, uint32_t param1, uint32_t status, uint32_t control) {
    TRB& trb = cmd_ring_[cmd_ring_enqueue_];
    trb.parameter[0] = param0;
    trb.parameter[1] = param1;
    trb.status = status;
    trb.control = control | (cmd_ring_cycle_ ? TRB_CYCLE : 0);

    ++cmd_ring_enqueue_;
    if (cmd_ring_enqueue_ == cmd_ring_size_ - 1) {
      // Link TRB에 도달 — cycle bit 세팅 후 wrap
      TRB& link = cmd_ring_[cmd_ring_enqueue_];
      link.control = (link.control & ~TRB_CYCLE) | (cmd_ring_cycle_ ? TRB_CYCLE : 0);
      cmd_ring_cycle_ = !cmd_ring_cycle_;
      cmd_ring_enqueue_ = 0;
    }

    // Host Controller Doorbell (slot 0, target 0)
    doorbell_base_[0] = 0;
  }

  constexpr uint32_t kEp0RingSize = 16;

  void XhciController::PushTransfer(uint8_t slot_id, uint8_t dci, uint32_t param0, uint32_t param1, uint32_t status, uint32_t control) {
    auto& ep = slot_data_[slot_id].ep[dci - 1];
    TRB& trb = ep.ring[ep.enqueue];
    trb.parameter[0] = param0;
    trb.parameter[1] = param1;
    trb.status = status;
    trb.control = control | (ep.cycle ? TRB_CYCLE : 0);

    ++ep.enqueue;
    if (ep.enqueue == kEp0RingSize - 1) {
      TRB& link = ep.ring[ep.enqueue];
      link.control = (link.control & ~TRB_CYCLE) | (ep.cycle ? TRB_CYCLE : 0);
      ep.cycle = !ep.cycle;
      ep.enqueue = 0;
    }
  }

  TRB* XhciController::WaitEvent() {
    while (true) {
      TRB* event = &event_ring_[event_ring_dequeue_];
      // Cycle bit가 기대값과 일치할 때까지 대기
      while ((event->control & TRB_CYCLE) != (event_ring_cycle_ ? 1u : 0u)) {}

      ++event_ring_dequeue_;
      if (event_ring_dequeue_ == event_ring_size_) {
        event_ring_dequeue_ = 0;
        event_ring_cycle_ = !event_ring_cycle_;
      }

      // ERDP 업데이트
      uintptr_t erdp = reinterpret_cast<uintptr_t>(&event_ring_[event_ring_dequeue_]);
      primary_interrupter_->erdp_lo = static_cast<uint32_t>(erdp) | (1u << 3);
      primary_interrupter_->erdp_hi = static_cast<uint32_t>(erdp >> 32);

      // Port Status Change Event는 스킵 (포트 스캔 중에 자연 발생)
      uint8_t type = TRB_Type(event->control);
      if (type == TRB_TYPE_PORT_STATUS_CHG) {
        uint8_t port_id = (event->parameter[0] >> 24) & 0xFF;
        Log(kDebug, "xHCI: Port Status Change Event (port=%u), skipping\n", port_id);
        continue;
      }

      return event;
    }
  }


  Error XhciController::ScanPorts() {
    for (uint8_t port = 1; port <= max_ports_; ++port) {
      volatile PortRegisterSet* pr = PortAt(port);
      uint32_t portsc = pr->portsc;
      // PORTSC_CCS: 장치가 물리적으로 연결되어 있다면 set (Read-Only)
      if (!(portsc & PORTSC_CCS)) continue;

      Log(kInfo, "xHCI: Port %u connected (PORTSC=0x%08x, Speed=%u)\n",
          port, portsc, PORTSC_Speed(portsc));

      // 1. Port Reset
      pr->portsc = (portsc & ~PORTSC_W1C_BITS) | PORTSC_PR;
      while (!(pr->portsc & PORTSC_PRC)) {}
      // PRC 클리어
      pr->portsc = (pr->portsc & ~PORTSC_W1C_BITS) | PORTSC_PRC;

      uint8_t speed = PORTSC_Speed(pr->portsc);
      Log(kInfo, "xHCI: Port %u reset done (Speed=%u, Enabled=%d)\n",
          port, speed, !!(pr->portsc & PORTSC_PED));

      if (!(pr->portsc & PORTSC_PED)) {
        Log(kWarn, "xHCI: Port %u not enabled after reset\n", port);
        continue;
      }

      // 2. Enable Slot Command
      PushCommand(0, 0, 0, TRB_SetType(TRB_TYPE_ENABLE_SLOT_CMD));
      TRB* event = WaitEvent();

      uint8_t cc = TRB_CompletionCode(event->status);
      uint8_t slot_id = TRB_SlotId(event->control);

      if (cc != TRB_COMPLETION_SUCCESS) {
        Log(kError, "xHCI: Enable Slot failed (code=%u)\n", cc);
        continue;
      }
      Log(kInfo, "xHCI: Slot %u enabled\n", slot_id);

      // 3. Address Device
      if (auto err = AddressDevice(slot_id, port, speed)) {
        Log(kError, "xHCI: Address Device failed for slot %u: %s\n", slot_id, err.Name());
        continue;
      }

      // 4. UsbDevice를 통해 GET_DESCRIPTOR + SET_CONFIGURATION
      usb::UsbDevice dev(this, slot_id, port, speed);

      DeviceDescriptor desc{};
      if (auto err = dev.GetDescriptor(usb::DESC_TYPE_DEVICE, 0, &desc, sizeof(desc))) {
        Log(kError, "xHCI: GET_DESCRIPTOR failed for slot %u: %s\n", slot_id, err.Name());
        continue;
      }
      Log(kInfo, "USB Device: VendorID=%04x, ProductID=%04x, Class=%02x, bcdUSB=%04x\n",
          desc.idVendor, desc.idProduct, desc.bDeviceClass, desc.bcdUSB);
      Log(kInfo, "  MaxPacketSize0=%u, NumConfigurations=%u\n",
          desc.bMaxPacketSize0, desc.bNumConfigurations);

      if (auto err = dev.SetConfiguration(1)) {
        Log(kError, "xHCI: SET_CONFIGURATION failed for slot %u: %s\n", slot_id, err.Name());
        continue;
      }
      Log(kInfo, "xHCI: Slot %u configured\n", slot_id);
    }
    return MAKE_ERROR(Error::kSuccess);
  }

  Error XhciController::AddressDevice(uint8_t slot_id, uint8_t port, uint8_t speed) {
    // Device Context 생성
    auto* dev_ctx = reinterpret_cast<DeviceContext*>(
        AllocAlignedZeroed(sizeof(DeviceContext), 64));
    if (!dev_ctx) return MAKE_ERROR(Error::kNoEnoughMemory);

    // DCBAA에 등록
    dcbaa_[slot_id] = reinterpret_cast<uintptr_t>(dev_ctx);

    // EP0 Transfer Ring 할당
    auto* ep0_ring = reinterpret_cast<TRB*>(
        AllocAlignedZeroed(kEp0RingSize * sizeof(TRB), 64));
    if (!ep0_ring) return MAKE_ERROR(Error::kNoEnoughMemory);

    // EP0 Ring의 Link TRB
    uintptr_t ep0_ring_phys = reinterpret_cast<uintptr_t>(ep0_ring);
    TRB& link = ep0_ring[kEp0RingSize - 1];
    link.parameter[0] = static_cast<uint32_t>(ep0_ring_phys);
    link.parameter[1] = static_cast<uint32_t>(ep0_ring_phys >> 32);
    link.control = TRB_SetType(TRB_TYPE_LINK) | LINK_TRB_TOGGLE_CYCLE;

    slot_data_[slot_id] = {};
    slot_data_[slot_id].ep[0] = {ep0_ring, 0, true};

    // Input Context 구성
    auto* input = reinterpret_cast<InputContext*>(
        AllocAlignedZeroed(sizeof(InputContext), 64));
    if (!input) return MAKE_ERROR(Error::kNoEnoughMemory);

    // Add Context Flags: Slot (bit 0) + EP0 (bit 1)
    input->control.add_flags = (1u << 0) | (1u << 1);

    // Slot Context
    SC_SetSpeed(input->slot, speed);
    SC_SetContextEntries(input->slot, 1);  // EP0만
    SC_SetRootHubPortNumber(input->slot, port);

    // EP0 Context (DCI 1 = ep[0])
    uint16_t max_packet = DefaultMaxPacketSize(speed);
    EP_SetEPType(input->ep[0], EP_TYPE_CONTROL);
    EP_SetMaxPacketSize(input->ep[0], max_packet);
    EP_SetCErr(input->ep[0], 3);
    EP_SetTRDequeuePointer(input->ep[0], ep0_ring_phys, true);
    EP_SetAvgTRBLength(input->ep[0], 8);

    // Address Device Command
    uintptr_t input_phys = reinterpret_cast<uintptr_t>(input);
    PushCommand(
        static_cast<uint32_t>(input_phys),
        static_cast<uint32_t>(input_phys >> 32),
        0,
        TRB_SetType(TRB_TYPE_ADDRESS_DEV_CMD) | (static_cast<uint32_t>(slot_id) << 24));

    TRB* event = WaitEvent();
    uint8_t cc = TRB_CompletionCode(event->status);
    if (cc != TRB_COMPLETION_SUCCESS) {
      Log(kError, "xHCI: Address Device completion code=%u\n", cc);
      return MAKE_ERROR(Error::kNotImplemented);
    }

    Log(kInfo, "xHCI: Slot %u addressed (Speed=%u, MaxPacket=%u)\n",
        slot_id, speed, max_packet);
    return MAKE_ERROR(Error::kSuccess);
  }

  Error XhciController::ControlTransfer(uint8_t slot_id,
                                         const usb::SetupPacket& setup,
                                         void* buf, uint16_t len) {
    // Setup Stage TRB (Immediate Data)
    uint32_t setup_lo = setup.bmRequestType
                      | (static_cast<uint32_t>(setup.bRequest) << 8)
                      | (static_cast<uint32_t>(setup.wValue) << 16);
    uint32_t setup_hi = setup.wIndex
                      | (static_cast<uint32_t>(setup.wLength) << 16);

    bool has_data = (len > 0 && buf != nullptr);
    bool is_in = (setup.bmRequestType & 0x80) != 0;

    uint32_t trt = has_data ? (is_in ? SETUP_TRT_IN : SETUP_TRT_OUT)
                            : SETUP_TRT_NO_DATA;
    PushTransfer(slot_id, 1, setup_lo, setup_hi, 8,
        TRB_SetType(TRB_TYPE_SETUP_STAGE) | TRB_IDT | trt);

    // Data Stage TRB (있을 때만)
    if (has_data) {
      uintptr_t buf_phys = reinterpret_cast<uintptr_t>(buf);
      uint32_t dir = is_in ? TRB_DIR_IN : TRB_DIR_OUT;
      PushTransfer(slot_id, 1,
          static_cast<uint32_t>(buf_phys),
          static_cast<uint32_t>(buf_phys >> 32),
          len, TRB_SetType(TRB_TYPE_DATA_STAGE) | dir);
    }

    // Status Stage TRB (방향은 Data의 반대)
    uint32_t status_dir = (!has_data || is_in) ? TRB_DIR_OUT : TRB_DIR_IN;
    PushTransfer(slot_id, 1, 0, 0, 0,
        TRB_SetType(TRB_TYPE_STATUS_STAGE) | TRB_IOC | status_dir);

    // Doorbell: EP0 = DCI 1
    doorbell_base_[slot_id] = 1;

    // Transfer Event 대기
    TRB* event = WaitEvent();
    uint8_t cc = TRB_CompletionCode(event->status);
    if (cc != TRB_COMPLETION_SUCCESS && cc != TRB_COMPLETION_SHORT_PKT) {
      Log(kError, "xHCI: ControlTransfer completion code=%u\n", cc);
      return MAKE_ERROR(Error::kNotImplemented);
    }

    return MAKE_ERROR(Error::kSuccess);
  }

}  // namespace usb::xhci
