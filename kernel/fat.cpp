#include "fat.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <utility>

namespace {

std::pair<const char*, bool> NextPathElement(const char* path, char* path_elem) {
  const char* next_slash = strchr(path, '/');
  if (next_slash == nullptr) {
    strcpy(path_elem, path);
    return { nullptr, false };
  }

  const auto elem_len = next_slash - path;
  strncpy(path_elem, path, elem_len);
  path_elem[elem_len] = '\0';
  return { &next_slash[1], true };
}

} // namespace


namespace fat {

BPB* boot_volume_image;
unsigned long bytes_per_cluster;

void Initialize(void* volume_image) {
  boot_volume_image = reinterpret_cast<fat::BPB*>(volume_image);
  bytes_per_cluster =
    static_cast<unsigned long>(boot_volume_image->bytes_per_sector) *
    boot_volume_image->sectors_per_cluster;
}

uintptr_t GetClusterAddr(unsigned long cluster) {
  unsigned long sector_num =
    boot_volume_image->reserved_sector_count +
    boot_volume_image->num_fats * boot_volume_image->fat_size_32 +
    (cluster - 2) * boot_volume_image->sectors_per_cluster;
  uintptr_t offset = sector_num * boot_volume_image->bytes_per_sector;
  return reinterpret_cast<uintptr_t>(boot_volume_image) + offset;
}

void ReadName(const DirectoryEntry& entry, char* base, char* ext) {
  memcpy(base, &entry.name[0], 8);
  base[8] = 0;
  for (int i = 7; i >= 0 && base[i] == 0x20; --i) {
    base[i] = 0;
  }

  memcpy(ext, &entry.name[8], 3);
  ext[3] = 0;
  for (int i = 2; i >= 0 && ext[i] == 0x20; --i) {
    ext[i] = 0;
  }
}

void FormatName(const DirectoryEntry& entry, char* dest) {
  char ext[5] = ".";
  ReadName(entry, dest, &ext[1]);
  if (ext[1]) {
    strcat(dest, ext);
  }
}


unsigned long NextCluster(unsigned long cluster) {
  uint32_t next = GetFAT()[cluster];
  if (IsEndOfClusterchain(next)) {
    return kEndOfClusterchain;
  }
  return next;
}

std::pair<DirectoryEntry*, bool> FindFile(const char* path, unsigned long directory_cluster) {
  if (path[0] == '/') {
    directory_cluster = boot_volume_image->root_cluster;
    ++path;
  } else if (directory_cluster == 0) {
    directory_cluster = boot_volume_image->root_cluster;
  }

  char path_elem[13];
  const auto [ next_path, post_slash ] = NextPathElement(path, path_elem);
  const bool path_last = next_path == nullptr || next_path[0] == '\0';

  while (directory_cluster != kEndOfClusterchain) {
    auto dir = GetSectorByCluster<DirectoryEntry>(directory_cluster);
    for (int i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0x00) {
        goto not_found;
      } else if (!NameIsEqual(dir[i], path_elem)) {
        continue;
      }

      if (dir[i].attr == Attribute::kDirectory && !path_last) {
        return FindFile(next_path, dir[i].FirstCluster());
      } else {
        return { &dir[i], post_slash };
      }
    }
    directory_cluster = NextCluster(directory_cluster);
  }

not_found:
  return { nullptr, post_slash };
}

bool NameIsEqual(const DirectoryEntry& entry, const char* name) {
  unsigned char name83[11];
  memset(name83, 0x20, sizeof(name83));

  int i = 0;
  int i83 = 0;
  for (; name[i] != 0 && i83 < sizeof(name83); ++i, ++i83) {
    if (name[i] == '.') {
      i83 = 7;
      continue;
    }
    name83[i83] = toupper(name[i]);
  }
  return memcmp(entry.name, name83, sizeof(name83)) == 0;
}

size_t LoadFile(void* buf, size_t len, const DirectoryEntry& entry) {
  auto is_valid_cluster = [](uint32_t c) {
    return c != 0 && c != fat::kEndOfClusterchain;
  };
  auto cluster = entry.FirstCluster();

  const auto buf_uint8 = reinterpret_cast<uint8_t*>(buf);
  const auto buf_end = buf_uint8 + len;
  auto p = buf_uint8;

  while (is_valid_cluster(cluster)) {
    if (bytes_per_cluster >= buf_end - p) {
      memcpy(p, GetSectorByCluster<uint8_t>(cluster), buf_end - p);
      return len;
    }
    memcpy(p, GetSectorByCluster<uint8_t>(cluster), bytes_per_cluster);
    p += bytes_per_cluster;
    cluster = NextCluster(cluster);
  }
  return p - buf_uint8;
}


bool IsEndOfClusterchain(unsigned long cluster) {
  return cluster >= 0x0ffffff8ul;
}

uint32_t* GetFAT() {
  uintptr_t fat_offset =
    boot_volume_image->reserved_sector_count *
    boot_volume_image->bytes_per_sector;
  return reinterpret_cast<uint32_t*>(
    reinterpret_cast<uintptr_t>(boot_volume_image) + fat_offset);
}

unsigned long ExtendCluster(unsigned long eoc_cluster, size_t n) {
  // FAT 영역에 클러스터 체인의 정보가 있다.
  uint32_t* fat = GetFAT();
  // 현재 디렉터리의 클러스터 체인 끝(마지막 유효 클러스터)을 찾아간다.
  while (!IsEndOfClusterchain(fat[eoc_cluster])) {
    eoc_cluster = fat[eoc_cluster];
  }

  size_t num_allocated = 0;
  auto current = eoc_cluster;
  // FAT에서 첫번째 인덱스부터 빈곳(값이 0인곳) 을 찾아간다
  for (unsigned long candidate = 2; num_allocated < n; ++candidate) {
    if (fat[candidate] != 0) {
      continue;
    }
    // fat[current]는 원래 EndOfClusterchain을 가리키고 있었지만, 위에서 찾은 빈 클러스터를 가리키게 한다.
    fat[current] = candidate;
    current = candidate;
    // 할당량까지 반복
    ++num_allocated;
  }
  // 빈 클러스터의 다음 클러스터는 없기 때문에 EOC 값을 집어넣는다. 
  fat[current] = kEndOfClusterchain;
  return current;
}


DirectoryEntry* AllocateEntry(unsigned long dir_cluster) {
  // 현재 디렉터리의 모든 클러스터에서 엔트리 배열을 전부 뒤져본다.
  while (true) {
    auto dir = GetSectorByCluster<DirectoryEntry>(dir_cluster);
    // 삭제되거나 빈 엔트리가 있다면 그 주소를 리턴
    for (int i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0 || dir[i].name[0] == 0xe5) {
        return &dir[i];
      }
    }
    auto next = NextCluster(dir_cluster);
    if (next == kEndOfClusterchain) {
      break;
    }
    dir_cluster = next;
  }
  // 없다면 클러스터를 하나 늘리고, 첫번째 엔트리 주소를 리턴
  dir_cluster = ExtendCluster(dir_cluster, 1);
  auto dir = GetSectorByCluster<DirectoryEntry>(dir_cluster);
  memset(dir, 0, bytes_per_cluster);
  return &dir[0];
}

void SetFileName(DirectoryEntry& entry, const char* name) {
  const char* dot_pos = strrchr(name, '.');
  memset(entry.name, ' ', 8+3);
  if (dot_pos) {
    for (int i = 0; i < 8 && i < dot_pos - name; ++i) {
      entry.name[i] = toupper(name[i]);
    }
    for (int i = 0; i < 3 && dot_pos[i + 1]; ++i) {
      entry.name[8 + i] = toupper(dot_pos[i + 1]);
    }
  } else {
    for (int i = 0; i < 8 && name[i]; ++i) {
      entry.name[i] = toupper(name[i]);
    }
  }
}

WithError<DirectoryEntry*> CreateFile(const char* path) {
  auto parent_dir_cluster = fat::boot_volume_image->root_cluster;
  const char* filename = path;

  // '/' 문자 기준으로 파일명과 나머지(파일이 포함된 디렉터리 전체 경로)를 분리
  if (const char* slash_pos = strrchr(path, '/')) {
    // 뒤에서부터 '/' 의 인덱스를 찾으면 다음 글자는 파일명 시작부분이다.
    filename = &slash_pos[1];
    if (slash_pos[1] == '\0') {
      return { nullptr, MAKE_ERROR(Error::kIsDirectory) };
    }

    // slash 주소에서 path의 시작 주소를 빼서 폴더 전체경로의 길이를 찾아온다.
    char parent_dir_name[slash_pos - path + 1];
    strncpy(parent_dir_name, path, slash_pos - path);
    parent_dir_name[slash_pos - path] = '\0';

    // 디렉터리의 클러스터를 찾아온다.
    if (parent_dir_name[0] != '\0') {
      auto [ parent_dir, post_slash2 ] = fat::FindFile(parent_dir_name);
      if (parent_dir == nullptr) {
        return { nullptr, MAKE_ERROR(Error::kNoSuchEntry) };
      }
      parent_dir_cluster = parent_dir->FirstCluster();
    }
  }

  // 엔트리 하나를 만들고
  auto dir = fat::AllocateEntry(parent_dir_cluster);
  if (dir == nullptr) {
    return { nullptr, MAKE_ERROR(Error::kNoEnoughMemory) };
  }
  // 엔트리에 Short Name만 지정
  fat::SetFileName(*dir, filename);
  dir->file_size = 0;
  return { dir, MAKE_ERROR(Error::kSuccess) };
}



FileDescriptor::FileDescriptor(DirectoryEntry& fat_entry)
    : fat_entry_{fat_entry} {
}

size_t FileDescriptor::Read(void* buf, size_t len) {
  if (rd_cluster_ == 0) {
    rd_cluster_ = fat_entry_.FirstCluster();
  }
  uint8_t* buf8 = reinterpret_cast<uint8_t*>(buf);
  len = std::min(len, fat_entry_.file_size - rd_off_);

  size_t total = 0;
  while (total < len) {
    uint8_t* sec = GetSectorByCluster<uint8_t>(rd_cluster_);
    size_t n = std::min(len - total, bytes_per_cluster - rd_cluster_off_);
    memcpy(&buf8[total], &sec[rd_cluster_off_], n);
    total += n;

    rd_cluster_off_ += n;
    if (rd_cluster_off_ == bytes_per_cluster) {
      rd_cluster_ = NextCluster(rd_cluster_);
      rd_cluster_off_ = 0;
    }
  }

  rd_off_ += total;
  return total;
}



} // namespace fat