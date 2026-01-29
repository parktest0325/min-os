#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/BlockIo.h>
#include <Guid/FileInfo.h>

#include <Library/DebugLib.h>

#include "elf.hpp"
#include "frame_buffer_config.hpp"
#include "memory_map.hpp"

// #@@range_begin(check_cpu_mode)
void check_cpu_mode() {
  uint64_t cr0, cr4, efer;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
  __asm__ volatile("rdmsr" : "=A"(efer) : "c"(0xC0000080));  // IA32_EFER

  Print(L"CR0 = 0x%08llx\n", cr0);
  Print(L"CR4 = 0x%08llx\n", cr4);
  Print(L"EFER = 0x%08llx\n", efer);

  if (efer & (1 << 10)) {
    Print(L"=> Long Mode Active (LMA = 1)\n");
  }
}
// #@@range_end(check_cpu_mode)

// #@@range_begin(get_memory_map)
// main 의 스택에서 만든 map 변수는 하나로 두고 여러번 호출되는 경우 이전 버퍼는 반납한다.
// 처음에는 사이즈를 0으로 세팅하고 호출해서 필요한 크기를 알아온다.
// 해당 크기만큼(+여유분) 동적할당 후 한번 더 요청
EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
  EFI_STATUS status;
  if (map->buffer != NULL) {
    gBS->FreePool(map->buffer);
    map->buffer = NULL;
  }

  map->map_size = 0;
  gBS->GetMemoryMap(
      &map->map_size,
      NULL,
      &map->map_key,
      &map->descriptor_size,
      &map->descriptor_version);

  map->map_size += map->descriptor_size * 2;
  map->buffer_size = map->map_size;

  status = gBS->AllocatePool(EfiLoaderData, map->map_size, &map->buffer);
  if (EFI_ERROR(status)) return status;

  return gBS->GetMemoryMap(
      &map->map_size,
      (EFI_MEMORY_DESCRIPTOR*)map->buffer,
      &map->map_key,
      &map->descriptor_size,
      &map->descriptor_version);
}
// #@@range_end(get_memory_map)

// #@@range_begin(get_memory_type)
const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
  switch (type) {
    case EfiReservedMemoryType: return L"EfiReservedMemoryType";
    case EfiLoaderCode: return L"EfiLoaderCode";
    case EfiLoaderData: return L"EfiLoaderData";
    case EfiBootServicesCode: return L"EfiBootServicesCode";
    case EfiBootServicesData: return L"EfiBootServicesData";
    case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
    case EfiConventionalMemory: return L"EfiConventionalMemory";
    case EfiUnusableMemory: return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode: return L"EfiPalCode";
    case EfiPersistentMemory: return L"EfiPersistentMemory";
    case EfiMaxMemoryType: return L"EfiMaxMemoryType";
    default: return L"InvalidMemoryType";
  }
}
// #@@range_end(get_memory_type)

// #@@range_begin(save_memory_map)
EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
  CHAR8 buf[256];
  CHAR16 unicode_buf[256];
  UINTN len;

  CHAR8* header =
    "Index | PhysicalStart -> VirtualStart | Type, Type(name), NumberOfPages, Attribute\n";
  len = AsciiStrLen(header);
  file->Write(file, &len, header);

  Print(L"map->buffer = %08lx, map->map_size = %08lx\n",
      map->buffer, map->map_size);

  EFI_PHYSICAL_ADDRESS iter;
  int i;
  for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
       iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
       iter += map->descriptor_size, i++) {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
    len = AsciiSPrint(
        buf, sizeof(buf),
        "%4u | %08lx -> %08lx | %x, %-25ls, %lx, %lx\n",
        i, desc->PhysicalStart, desc->VirtualStart,
        desc->Type, GetMemoryTypeUnicode(desc->Type),
        desc->NumberOfPages,
        desc->Attribute & 0xffffflu);
    DEBUG((DEBUG_INFO, buf));

    AsciiStrToUnicodeStrS(buf, unicode_buf, sizeof(unicode_buf) / sizeof(CHAR16));
    //Print(L"%s", unicode_buf);
    file->Write(file, &len, buf);
  }

  return EFI_SUCCESS;
}
// #@@range_end(save_memory_map)

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
  EFI_STATUS status;

  status = gBS->OpenProtocol(
      image_handle,
      &gEfiLoadedImageProtocolGuid,
      (VOID**)&loaded_image,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (EFI_ERROR(status)) return status;

  status = gBS->OpenProtocol(
      loaded_image->DeviceHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&fs,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (EFI_ERROR(status)) return status;

  return fs->OpenVolume(fs, root);
}

EFI_STATUS OpenGOP(EFI_HANDLE image_handle,
                   EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
  UINTN num_gop_handles = 0;
  EFI_HANDLE* gop_handles = NULL;
  EFI_STATUS status;

  status = gBS->LocateHandleBuffer(
      ByProtocol,
      &gEfiGraphicsOutputProtocolGuid,
      NULL,
      &num_gop_handles,
      &gop_handles);
  if (EFI_ERROR(status)) return status;

  status = gBS->OpenProtocol(
      gop_handles[0],
      &gEfiGraphicsOutputProtocolGuid,
      (VOID**)gop,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (EFI_ERROR(status)) return status;

  FreePool(gop_handles);
  return EFI_SUCCESS;
}

const CHAR16* GetPixelFormatUnicode(EFI_GRAPHICS_PIXEL_FORMAT fmt) {
  switch (fmt) {
    case PixelRedGreenBlueReserved8BitPerColor:
      return L"PixelRedGreenBlueReserved8BitPerColor";
    case PixelBlueGreenRedReserved8BitPerColor:
      return L"PixelBlueGreenRedReserved8BitPerColor";
    case PixelBitMask:
      return L"PixelBitMask";
    case PixelBltOnly:
      return L"PixelBltOnly";
    case PixelFormatMax:
      return L"PixelFormatMax";
    default:
      return L"InvalidPixelFormat";
  }
}

VOID Halt(void) {
  while (1) __asm__("hlt");
}

VOID _IfErrorHalt(CHAR16* msg, EFI_STATUS status) {
  if (EFI_ERROR(status)) {
    Print(L"MSG: %s, Status: %r\n", msg, status);
    Halt();
  }
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  *first = MAX_UINT64;
  *last = 0;
  for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;
    *first = MIN(*first, phdr[i].p_vaddr);
    *last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
  }
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;
    UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
    CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

    UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
    SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
  }
}

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* file, VOID** buffer) {
  EFI_STATUS status;

  UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
  UINT8 file_info_buffer[file_info_size];
  status = file->GetInfo(
    file, &gEfiFileInfoGuid,
    &file_info_size, file_info_buffer);
  if (EFI_ERROR(status)) return status;

  EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
  UINTN file_size = file_info->FileSize;

  status = gBS->AllocatePool(EfiLoaderData, file_size, buffer);
  if (EFI_ERROR(status)) return status;

  return file->Read(file, &file_size, *buffer);
}

EFI_STATUS OpenBlockIoProtocolForLoadedImage(
    EFI_HANDLE image_handle, EFI_BLOCK_IO_PROTOCOL** block_io) {
  EFI_STATUS  status;
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image;

  status = gBS->OpenProtocol(
    image_handle,
    &gEfiLoadedImageProtocolGuid,
    (VOID**)&loaded_image,
    image_handle,
    NULL,
    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (EFI_ERROR(status)) return status;

  status = gBS->OpenProtocol(
    loaded_image->DeviceHandle,
    &gEfiBlockIoProtocolGuid,
    (VOID**)block_io,
    image_handle,
    NULL,
    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  return status;
}

EFI_STATUS ReadBlocks(
    EFI_BLOCK_IO_PROTOCOL* block_io, UINT32 media_id,
    UINTN read_bytes, VOID** buffer) {
  EFI_STATUS status;

  status = gBS->AllocatePool(EfiLoaderData, read_bytes, buffer);
  if (EFI_ERROR(status)) return status;

  status = block_io->ReadBlocks(
    block_io,
    media_id,
    0,
    read_bytes,
    *buffer);
  return status;
}

EFI_STATUS EFIAPI UefiMain(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE* system_table) {
  check_cpu_mode();
  Print(L"Hello, min-os World!\n");
  EFI_STATUS status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  status = gBS->OpenProtocol(
              image_handle,
              &gEfiLoadedImageProtocolGuid,
              (VOID **) &LoadedImage,
              image_handle,
              NULL,
              EFI_OPEN_PROTOCOL_GET_PROTOCOL
              );
  _IfErrorHalt(L"OpenProtocol (LoadedImage) Failed", status);
  Print(L"Base = %p\n", LoadedImage->ImageBase);

  // #@@range_begin(main)
  struct MemoryMap memmap = {0, NULL, 0, 0, 0, 0};
  status = GetMemoryMap(&memmap);
  _IfErrorHalt(L"get memory map Failed", status);

  EFI_FILE_PROTOCOL* root_dir;
  status = OpenRootDir(image_handle, &root_dir);
  _IfErrorHalt(L"OpenRootDir Failed", status);

  EFI_FILE_PROTOCOL* memmap_file;
  status = root_dir->Open(
      root_dir, &memmap_file, L"\\memmap",
      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(status)) {
    Print(L"failed to open file '\\memmap': %r\n", status);
    Print(L"Ignored.\n");
  } else {
    status = SaveMemoryMap(&memmap, memmap_file);
    _IfErrorHalt(L"SaveMemoryMap Failed", status);

    status = memmap_file->Close(memmap_file);
    _IfErrorHalt(L"close memory map Failed", status);
  }
  // #@@range_end(main)


  // #@@range_begin(draw)
  EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
  status = OpenGOP(image_handle, &gop);
  _IfErrorHalt(L"OpenGOP Failed", status);

  Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
    gop->Mode->Info->HorizontalResolution,
    gop->Mode->Info->VerticalResolution,
    GetPixelFormatUnicode(gop->Mode->Info->PixelFormat),
    gop->Mode->Info->PixelsPerScanLine);
  Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
    gop->Mode->FrameBufferBase,
    gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize,
    gop->Mode->FrameBufferSize);
  // #@@range_end(draw)


  // #@@range_begin(read_kernel)
  EFI_FILE_PROTOCOL* kernel_file;
  status = root_dir->Open(
      root_dir, &kernel_file, L"\\kernel.elf",
      EFI_FILE_MODE_READ, 0);
  _IfErrorHalt(L"open file '\\kernel.elf' Failed", status);

  VOID* kernel_buffer;
  status = ReadFile(kernel_file, &kernel_buffer);
  _IfErrorHalt(L"Read from kernel_file Failed..", status);

  Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
  UINT64 kernel_first_addr, kernel_last_addr;
  CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);
  Print(L"Kernel: 0x%0lx - 0x%0lx\n", kernel_first_addr, kernel_last_addr);
  

  UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
  status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                                num_pages, &kernel_first_addr);
  _IfErrorHalt(L"AllocatePages Failed!", status);

  CopyLoadSegments(kernel_ehdr);
  status = gBS->FreePool(kernel_buffer);
  _IfErrorHalt(L"FreePool Failed", status);
  // #@@range_end(read_kernel)

  // #@@range_begin(read_volume)
  VOID* volume_image;

  EFI_FILE_PROTOCOL* volume_file;
  status = root_dir->Open(
    root_dir, &volume_file, L"\\fat_disk",
    EFI_FILE_MODE_READ, 0);
  if (status == EFI_SUCCESS) {
    status = ReadFile(volume_file, &volume_image);
    _IfErrorHalt(L"failed to open Block I/O Protocol", status);
  } else {
    EFI_BLOCK_IO_PROTOCOL* block_io;
    status = OpenBlockIoProtocolForLoadedImage(image_handle, &block_io);
    _IfErrorHalt(L"failed to open Block I/O Protocol", status);

    EFI_BLOCK_IO_MEDIA* media = block_io->Media;
    UINTN volume_bytes = (UINTN)media->BlockSize * (media->LastBlock + 1);
    if (volume_bytes > 32 * 1024 * 1024) {
      volume_bytes = 32 * 1024 * 1024;
    }

    Print(L"Reading &lu bytes (Present %d, BlockSize %u, Lastblock %u)\n",
      volume_bytes, media->MediaPresent, media->BlockSize, media->LastBlock);

    status = ReadBlocks(block_io, media->MediaId, volume_bytes, &volume_image);
    _IfErrorHalt(L"failed to read blocks", status);
  }
  // #@@range_end(read_volume)

  // #@@range_begin(stop_bootservice)
  status = gBS->ExitBootServices(image_handle, memmap.map_key);
  if (EFI_ERROR(status)) {
    status = GetMemoryMap(&memmap);
    _IfErrorHalt(L"failed to get memory map", status);

    status = gBS->ExitBootServices(image_handle, memmap.map_key);
    _IfErrorHalt(L"Could not exit boot service", status);
  }
  // #@@range_end(stop_bootservice)

  // #@@range_begin(pass_frame_buffer_config)
  struct FrameBufferConfig config = {
    (UINT8*)gop->Mode->FrameBufferBase,
    gop->Mode->Info->PixelsPerScanLine,
    gop->Mode->Info->HorizontalResolution,
    gop->Mode->Info->VerticalResolution,
    0
  };

  switch (gop->Mode->Info->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
      config.pixel_format = kPixelRGBResv8BitPerColor;
      break;
    case PixelBlueGreenRedReserved8BitPerColor:
      config.pixel_format = kPixelBGRResv8BitPerColor;
      break;
    default:
      _IfErrorHalt(L"Unimplemented pixel format", gop->Mode->Info->PixelFormat);
  }
  // #@@range_end(pass_frame_buffer_config)

  // #@@range_begin(get_acpitable)
  VOID* acpi_table = NULL;
  for (UINTN i = 0; i < system_table->NumberOfTableEntries; ++i) {
    if (CompareGuid(&gEfiAcpiTableGuid,
                    &system_table->ConfigurationTable[i].VendorGuid)) {
      acpi_table = system_table->ConfigurationTable[i].VendorTable;
      break;
    }
  }
  // #@@range_end(get_acpitable)

  // #@@range_begin(run_kernel)
  UINT64 entry_addr = *(UINT64*)(kernel_first_addr + 24);

  typedef void EntryPointType(const struct FrameBufferConfig*,
                              const struct MemoryMap*,
                              const VOID*,
                              VOID*
                            );
  EntryPointType* entry_point = (EntryPointType*)entry_addr;
  entry_point(&config, &memmap, acpi_table, volume_image);
  // #@@range_end(run_kernel)

  Print(L"All done\n");

  while (1);
  return EFI_SUCCESS;
}
