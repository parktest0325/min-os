#include "usb/host/ehci/ehci.hpp"
#include "logger.hpp"

namespace usb::ehci {

  EhciController::EhciController(const pci::Device& dev)
      : dev_{dev}, mmio_base_{0} {}

  Error EhciController::Initialize() {
    uint16_t vendor_id = pci::ReadVendorId(dev_);
    uint16_t device_id = pci::ReadDeviceId(dev_);
    Log(kInfo, "EHCI found: %d.%d.%d (vend=%04x, dev=%04x)\n",
        dev_.bus, dev_.device, dev_.function, vendor_id, device_id);

    ConfigurePCI();

    auto [bar, err] = pci::ReadBar(dev_, 0);
    if (err) return err;
    mmio_base_ = bar;

    Log(kInfo, "EHCI BAR0: 0x%016lx\n", mmio_base_);
    return MAKE_ERROR(Error::kSuccess);
  }

  void EhciController::ConfigurePCI() {
    uint32_t cmd = pci::ReadConfReg(dev_, 0x04);
    cmd |= (1u << 1) | (1u << 2);  // Memory Space + Bus Master
    pci::WriteConfReg(dev_, 0x04, cmd);
    Log(kInfo, "PCI Command: 0x%04x (MemSpace=%d, BusMaster=%d)\n",
        cmd & 0xffff, (cmd >> 1) & 1, (cmd >> 2) & 1);
  }

  Error EhciController::ControlTransfer(uint8_t /*slot_id*/,
                                         const usb::SetupPacket& /*setup*/,
                                         void* /*buf*/, uint16_t /*len*/) {
    return MAKE_ERROR(Error::kNotImplemented);
  }

}  // namespace usb::ehci
