#pragma once

/**
 * @file Interface between the app and driver/dirtyrammark.c. Plain C, included by both user mode and the kernel.
 */

#define DIRTYRAMMARK_DEVICE_PATH L"\\\\.\\DirtyRamMark"

#define DIRTYRAMMARK_IOCTL(function) CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + (function), METHOD_BUFFERED, FILE_READ_ACCESS)

/// Reads a dword of PCI configuration space. In: DirtyRamMarkPciRead, out: ULONG.
#define IOCTL_DIRTYRAMMARK_PCI_READ DIRTYRAMMARK_IOCTL(0)

/// Reads a register of the AMD System Management Network through the root complex (00:00.0, 0x60 index / 0x64 data).
/// In: ULONG (SMN address), out: ULONG.
#define IOCTL_DIRTYRAMMARK_SMN_READ DIRTYRAMMARK_IOCTL(1)

/// Reads dwords of physical memory-mapped I/O. In: DirtyRamMarkMmioRead, out: ULONG[count].
#define IOCTL_DIRTYRAMMARK_MMIO_READ DIRTYRAMMARK_IOCTL(2)

#define DIRTYRAMMARK_MMIO_MAX_DWORDS 4096

typedef struct DirtyRamMarkPciRead {
    unsigned long bus;
    unsigned long device;
    unsigned long function;
    unsigned long offset;   // dword aligned
} DirtyRamMarkPciRead;

typedef struct DirtyRamMarkMmioRead {
    unsigned long long physicalAddress;   // dword aligned
    unsigned long count;
} DirtyRamMarkMmioRead;
