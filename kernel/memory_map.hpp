#pragma once

#include <stdint.h>

struct MemoryMap {
  unsigned long long buffer_size;
  void* buffer;
  unsigned long long map_size;
  unsigned long long map_key;
  unsigned long long descriptor_size;
  uint32_t descriptor_version;
};

struct MemoryDescriptor {
  uint32_t type;
  uintptr_t physical_start;
  uintptr_t virtual_start;
  uint64_t number_of_pages;
  uint64_t attribute;
};

#ifdef __cplusplus
enum class MemoryType {
  kEfiReservedMemoryType,
  kEfiLoaderCode,
  kEfiLoaderData,
  kEfiBootServicesCode,
  kEfiBootServicesData,
  kEfiRuntimeServicesCode,
  kEfiRuntimeServicesData,
  kEfiConventionalMemory,
  kEfiUnusableMemory,
  kEfiACPIReclaimMemory,
  kEfiACPIMemoryNVS,
  kEfiMemoryMappedIO,
  kEfiMemoryMappedIOPortSpace,
  kEfiPalCode,
  kEfiPersistentMemory,
  kEfiMaxMemoryType
};

inline bool operator==(uint32_t lhs, MemoryType rhs) {
  return lhs == static_cast<uint32_t>(rhs);
}

inline bool operator==(MemoryType lhs, uint32_t rhs) {
  return static_cast<uint32_t>(lhs) == rhs;
}

inline bool IsAvailable(MemoryType memory_type) {
  return
    memory_type == MemoryType::kEfiBootServicesCode ||
    memory_type == MemoryType::kEfiBootServicesData ||
    memory_type == MemoryType::kEfiConventionalMemory;
}

inline void SwapMemoryDescriptor(MemoryDescriptor& a, MemoryDescriptor& b) {
  MemoryDescriptor temp = a;
  a = b;
  b = temp;
}

inline void SortMemoryMapInPlace(MemoryMap& memory_map) {
  const auto desc_count = memory_map.map_size / memory_map.descriptor_size;
  const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);

  MemoryDescriptor *mem_a, *mem_b;
  for (size_t i = 0; i < desc_count; ++i) {
    for (size_t j = i; j < desc_count; ++j) {
      mem_a = reinterpret_cast<MemoryDescriptor*>(memory_map_base + memory_map.descriptor_size * i);
      mem_b = reinterpret_cast<MemoryDescriptor*>(memory_map_base + memory_map.descriptor_size * j);
      if (mem_a->physical_start > mem_b->physical_start) {
        SwapMemoryDescriptor(*mem_a, *mem_b);
      }
    }
  }
}

const int kUEFIPageSize = 4096;
#endif