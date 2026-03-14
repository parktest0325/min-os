#include "usb/host/xhci/xhci.hpp"
#include "logger.hpp"

namespace usb::xhci {

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
        max_ports_{0}, max_slots_{0},
        db_off_{0}, rts_off_{0} {}

  Error XhciController::Initialize() {
    intel::RouteToXhci(dev_);
    ConfigurePCI();

    auto [bar, err] = pci::ReadBar(dev_, 0);
    if (err) return err;

    Log(kInfo, "xHCI BAR0: 0x%016lx\n", bar);
    cap_ = reinterpret_cast<volatile CapabilityRegisters*>(bar);

    return ReadCapabilityRegisters();
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

    db_off_  = cap_->db_off & ~0x3u;
    rts_off_ = cap_->rts_off & ~0x1Fu;

    Log(kInfo, "xHCI v%x.%x, CAPLENGTH=%u\n",
        version >> 8, version & 0xFF, cap_length);
    Log(kInfo, "  MaxSlots=%u, MaxIntrs=%u, MaxPorts=%u\n",
        max_slots_, max_intrs, max_ports_);
    Log(kInfo, "  AC64=%d, ContextSize=%dB, ErstMax=%u, Scratchpad=%u\n",
        ac64, csz ? 64 : 32, 1u << erst_max, max_scratchpad);
    Log(kInfo, "  DBOFF=0x%x, RTSOFF=0x%x\n", db_off_, rts_off_);

    uint32_t sts = op_->usb_sts;
    Log(kInfo, "  USBSTS=0x%08x (HCH=%d, CNR=%d)\n",
        sts, !!(sts & USBSTS_HCH), !!(sts & USBSTS_CNR));

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
