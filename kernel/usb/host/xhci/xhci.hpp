#pragma once
#include "error.hpp"
#include "pci.hpp"
#include "usb/host/host_controller.hpp"
#include "usb/host/xhci/xhci_registers.hpp"
#include "usb/host/xhci/xhci_trb.hpp"

namespace usb::xhci {

  // 정렬 메모리 할당 (한번 할당하면 해제 안 함)
  void* AllocAligned(size_t size, size_t alignment);

  // 정렬 + 제로 초기화
  void* AllocAlignedZeroed(size_t size, size_t alignment);

  class XhciController : public HostController {
  public:
    XhciController(const pci::Device& dev);
    Error Initialize() override;

  private:
    const pci::Device& dev_;
    volatile CapabilityRegisters* cap_;
    volatile OperationalRegisters* op_;
    volatile InterrupterRegisterSet* primary_interrupter_;
    volatile uint32_t* doorbell_base_;
    uint8_t  max_ports_;
    uint8_t  max_slots_;

    // Command Ring
    TRB*     cmd_ring_;
    uint32_t cmd_ring_size_;
    uint32_t cmd_ring_enqueue_;
    bool     cmd_ring_cycle_;

    // Event Ring
    TRB*     event_ring_;
    uint32_t event_ring_size_;
    uint32_t event_ring_dequeue_;
    bool     event_ring_cycle_;
    ERSTEntry* erst_;

    // DCBAA
    uint64_t* dcbaa_;

    void ConfigurePCI();
    Error ReadCapabilityRegisters();
    Error ResetController();
  };
}
