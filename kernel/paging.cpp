#include "paging.hpp"
#include "memory_manager.hpp"
#include "task.hpp"
#include "logger.hpp"

#include "asmfunc.h"

#include <array>

namespace {
  const uint64_t kPageSize4K = 4096;
  const uint64_t kPageSize2M = 512 * kPageSize4K;
  const uint64_t kPageSize1G = 512 * kPageSize2M;

  alignas(kPageSize4K) std::array<uint64_t, 512> pml4_table;
  alignas(kPageSize4K) std::array<uint64_t, 512> pdp_table;
  alignas(kPageSize4K) std::array<std::array<uint64_t, 512>, kPageDirectoryCount> page_directory;
}

void SetupIdentityPageTable() {
  pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x0003;
  for (int i_pdpt = 0; i_pdpt < page_directory.size(); ++i_pdpt) {
    pdp_table[i_pdpt] = reinterpret_cast<uint64_t>(&page_directory[i_pdpt]) | 0x003;
    for (int i_pd = 0; i_pd < 512; ++i_pd) {
      page_directory[i_pdpt][i_pd] = i_pdpt * kPageSize1G + i_pd * kPageSize2M | 0x083;
    }
  }
  ResetCR3();
  SetCR0(GetCR0() & 0xfffeffff); // Clear WP
}

void ResetCR3() {
  SetCR3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}

void InitializePaging() {
  SetupIdentityPageTable();
}


namespace {

WithError<PageMapEntry*> SetNewPageMapIfNotPresent(PageMapEntry& entry) {
  if (entry.bits.present) {
    return { entry.Pointer(), MAKE_ERROR(Error::kSuccess) };
  }

  auto [ child_map, err ] = NewPageMap();
  if (err) {
    return { nullptr, err };
  }

  entry.SetPointer(child_map);
  entry.bits.present = 1;

  return { child_map, MAKE_ERROR(Error::kSuccess) };
}

WithError<size_t> SetupPageMap(
    PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr,
    size_t num_4kpages, bool writable) {
  while (num_4kpages > 0) {
    const auto entry_index = addr.Part(page_map_level);

    auto [ child_map, err ] = SetNewPageMapIfNotPresent(page_map[entry_index]);
    if (err) {
      return { num_4kpages, err };
    }
    page_map[entry_index].bits.user = 1;

    if (page_map_level == 1) {
      page_map[entry_index].bits.writable = writable;
      --num_4kpages;
    } else {
      page_map[entry_index].bits.writable = true;

      auto [ num_remain_pages, err ] =
          SetupPageMap(child_map, page_map_level - 1, addr, num_4kpages, writable);
      if (err) {
        return { num_4kpages, err };
      }
      num_4kpages = num_remain_pages;
    }

    if (entry_index == 511) {
      break;
    }

    addr.SetPart(page_map_level, entry_index + 1);
    for (int level = page_map_level - 1; level >= 1; --level) {
      addr.SetPart(level, 0);
    }
  }
  return { num_4kpages, MAKE_ERROR(Error::kSuccess) };
}

Error CleanPageMap(PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr) {
  for (int i = addr.Part(page_map_level); i < 512; ++i) {
    auto entry = page_map[i];
    if (!entry.bits.present) {
      continue;
    }

    if (page_map_level > 1) {
      if (auto err = CleanPageMap(entry.Pointer(), page_map_level - 1, addr)) {
        return err;
      }
    }

    if (entry.bits.writable) {
      const auto entry_addr = reinterpret_cast<uintptr_t>(entry.Pointer());
      const FrameID map_frame{entry_addr / kBytesPerFrame};
      if (auto err = memory_manager->Free(map_frame, 1)) {
        return err;
      }
    }
    page_map[i].data = 0;
  }

  return MAKE_ERROR(Error::kSuccess);
}


// 페이지테이블을 재귀적으로 타고들어가면서 실제 물리주소를 content로 세팅하는 함수
// writable 도 1로 세팅
Error SetPageContent(PageMapEntry* table, int part,
                     LinearAddress4Level addr, PageMapEntry* content) {
  if (part == 1) {
    const auto i = addr.Part(part);
    table[i].SetPointer(content);
    table[i].bits.writable = 1;
    InvalidateTLB(addr.value);
    return MAKE_ERROR(Error::kSuccess);
  }

  const auto i = addr.Part(part);
  return SetPageContent(table[i].Pointer(), part - 1, addr, content);
}


Error CopyOnePage(uint64_t causal_addr) {
  auto [ p, err ] = NewPageMap();
  if (err) {
    return err;
  }
  // #PF 발생한 주소에 해당하는 페이지 찾음
  const auto aligned_addr = causal_addr & 0xffff'ffff'ffff'f000;
  // 새로운 페이지(=프레임)에 내용 전부 복사
  memcpy(p, reinterpret_cast<const void*>(aligned_addr), 4096);
  // 프레임을 현재 페이지테이블 말단에 세팅하면서 writable=1 로 세팅
  return SetPageContent(reinterpret_cast<PageMapEntry*>(GetCR3()), 4,
                        LinearAddress4Level{causal_addr}, p);
}


} // namespace


const FileMapping* FindFileMapping(const std::vector<FileMapping>& fmaps,
                                   uint64_t causal_vaddr) {
  for (const FileMapping& m : fmaps) {
    if (m.vaddr_begin <= causal_vaddr && causal_vaddr < m.vaddr_end) {
      return &m;
    }
  }
  return nullptr;
}

Error PreparePageCache(FileDescriptor& fd, const FileMapping& m,
                       uint64_t causal_vaddr) {
  LinearAddress4Level page_vaddr{causal_vaddr};
  // PF가 발생한 페이지 세팅 및 프레임 할당 (오프셋 값을 0으로세팅)
  page_vaddr.parts.offset = 0;
  if (auto err = SetupPageMaps(page_vaddr, 1)) {
    return err;
  }

  // 복사할 파일에서의 오프셋 구함  (접근한페이지주소 - 접근한주소의시작)
  const long file_offset = page_vaddr.value - m.vaddr_begin;
  void* page_cache = reinterpret_cast<void*>(page_vaddr.value);
  fd.Load(page_cache, 4096, file_offset);
  return MAKE_ERROR(Error::kSuccess);
}


WithError<PageMapEntry*> NewPageMap() {
  auto frame = memory_manager->Allocate(1);
  if (frame.error) {
    return { nullptr, frame.error };
  }

  auto e = reinterpret_cast<PageMapEntry*>(frame.value.Frame());
  memset(e, 0, sizeof(uint64_t) * 512);
  return { e, MAKE_ERROR(Error::kSuccess) };
}


Error FreePageMap(PageMapEntry* table) {
  const FrameID frame{reinterpret_cast<uintptr_t>(table) / kBytesPerFrame};
  return memory_manager->Free(frame, 1);
}


Error SetupPageMaps(LinearAddress4Level addr, size_t num_4kpages, bool writable) {
  auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
  return SetupPageMap(pml4_table, 4, addr, num_4kpages, writable).error;
}


Error CleanPageMaps(LinearAddress4Level addr) {
  auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
  return CleanPageMap(pml4_table, 4, addr);
}


Error CopyPageMaps(PageMapEntry* dest, PageMapEntry* src, int part, int start) {
  // page_table[0:512] 세팅
  if (part == 1) {
    for (int i = start; i < 512; ++i) {
      if (!src[i].bits.present) {
        continue;
      }
      dest[i] = src[i];            // 물리주소 복사
      dest[i].bits.writable = 0;   // readonly 세팅
    }
    return MAKE_ERROR(Error::kSuccess);
  }

  // 처음들어오면 (part==4) pml4_table[256:512] 세팅
  // 재귀적으로 들어오면 pdpt_table[0:512], pd_table[0:512] 세팅
  for (int i = start; i < 512; ++i) {
    if (!src[i].bits.present) {
      continue;
    }
    // page_table(part==1)이 아니라면 새로운 페이지를 생성해서 연결
    auto [ table, err ] = NewPageMap();
    if (err) {
      return err;
    }
    dest[i] = src[i];          // src에서 속성까지 전부 복사
    dest[i].SetPointer(table); // 방금만든 페이지 물리 주소를 저장
    if (auto err = CopyPageMaps(table, src[i].Pointer(), part - 1, 0)) {
      return err;
    }
  }
  return MAKE_ERROR(Error::kSuccess);
}


Error HandlePageFault(uint64_t error_code, uint64_t causal_addr) {
  auto& task = task_manager->CurrentTask();
  const bool present = (error_code >> 0) & 1;
  const bool rw      = (error_code >> 1) & 1;
  const bool user    = (error_code >> 2) & 1;

  if (present && rw && user) {
    // 페이지가 있지만, write 로 권한에러 발생, user 앱인 경우. 페이지복사해줌
    return CopyOnePage(causal_addr);
  } else if (present) {
    // 페이지레벨 권한 위반 예외. 이건 디맨드페이징 처리를 하면 안된다.
    return MAKE_ERROR(Error::kAlreadyAllocated);
  }

  if (task.DPagingBegin() <= causal_addr && causal_addr < task.DPagingEnd()) {
    return SetupPageMaps(LinearAddress4Level{causal_addr}, 1);
  }
  if (auto m = FindFileMapping(task.FileMaps(), causal_addr)) {
    return PreparePageCache(*task.Files()[m->fd], *m, causal_addr);
  }
  return MAKE_ERROR(Error::kIndexOutOfRange);
}