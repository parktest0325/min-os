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
    Error ControlTransfer(uint8_t slot_id,
                          const usb::SetupPacket& setup,
                          void* buf, uint16_t len) override;

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

    // 슬롯별 EP0 Transfer Ring 상태
    struct SlotData {
      TRB* ep0_ring;
      uint32_t ep0_enqueue;
      bool ep0_cycle;
    };
    static constexpr int kMaxSlots = 256;
    SlotData slot_data_[kMaxSlots];

    void ConfigurePCI();
    Error ReadCapabilityRegisters();
    Error ResetController();
    Error StartController();

    // Ring 조작
    volatile PortRegisterSet* PortAt(uint8_t port_num);
    void PushCommand(uint32_t param0, uint32_t param1, uint32_t status, uint32_t control);
    void PushTransfer(uint8_t slot_id, uint32_t param0, uint32_t param1, uint32_t status, uint32_t control);
    TRB* WaitEvent();

    // 포트 감지 + Enumeration
    Error ScanPorts();
    Error AddressDevice(uint8_t slot_id, uint8_t port, uint8_t speed);
  };
}
