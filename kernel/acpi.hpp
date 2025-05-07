#pragma once

#include <cstdint>
#include <cstddef>

namespace acpi {

struct RSDP {
  char signature[8];        // "RSD PTR "
  uint8_t checksum;         // 상위 20byte의 체크섬
  char oem_id[6];           // oem 이름
  uint8_t revision;         // RSDP 구조체 버전 번호. ACPI 6.2에서 2
  uint32_t rsdt_address;    // RSDT 32bit 물리주소
  uint32_t length;          // RSDP 전체 바이트 수
  uint64_t xsdt_address;    // XSDT 64bit 물리주소 
  uint8_t extended_checksum;// 전체 36byte의 체크섬
  char reserved[3];
  
  bool IsValid() const;
} __attribute__((packed));

struct DescriptionHeader {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;

  bool IsValid(const char* expected_signature) const;
} __attribute__((packed));

struct XSDT {
  DescriptionHeader header;

  const DescriptionHeader& operator[](size_t i) const;
  size_t Count() const;
} __attribute__((packed));

struct FADT {
  DescriptionHeader header;

  char reserved1[76 - sizeof(header)];
  uint32_t pm_tmr_blk;
  char reserved2[112 - 80];
  uint32_t flags;
  char reserved3[276 - 116];
} __attribute__((packed));

extern const FADT* fadt;
const int kPMTimerFreq = 3579545;

void WaitMilliseconds(unsigned long msec);
void Initialize(const RSDP& rsdp);

}