/*
 * DirtyRamMark helper driver.
 *
 * Gives the (administrator-only) app read access to what is reachable from ring 0 only: PCI configuration space, the
 * AMD SMN and memory-mapped I/O (Intel MCHBAR). Everything is read-only. The app deploys the driver on demand and
 * removes it when it exits - see src/platform/win32/driver.cpp.
 *
 * Build: driver/make.bat (needs the WDK headers, see the script).
 */
#include <ntddk.h>
#include <wdmsec.h>

#include "../src/platform/win32/driver_ioctl.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static const UNICODE_STRING kDeviceName = RTL_CONSTANT_STRING(L"\\Device\\DirtyRamMark");
static const UNICODE_STRING kSymlinkName = RTL_CONSTANT_STRING(L"\\DosDevices\\DirtyRamMark");
// SYSTEM and Administrators only.
static const UNICODE_STRING kSddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

static KSPIN_LOCK gPciLock;

static ULONG PciReadLocked(ULONG bus, ULONG device, ULONG function, ULONG offset) {
    KIRQL irql;
    KeAcquireSpinLock(&gPciLock, &irql);
    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS,
                     0x80000000u | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    ULONG value = READ_PORT_ULONG((PULONG)PCI_CONFIG_DATA);
    KeReleaseSpinLock(&gPciLock, irql);
    return value;
}

static ULONG SmnRead(ULONG address) {
    KIRQL irql;
    KeAcquireSpinLock(&gPciLock, &irql);
    // root complex is 00:00.0; the index/data pair must not be interleaved with other accessors, hence the lock held
    // for both accesses.
    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, 0x80000000u | 0x60);
    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_DATA, address);
    WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, 0x80000000u | 0x64);
    ULONG value = READ_PORT_ULONG((PULONG)PCI_CONFIG_DATA);
    KeReleaseSpinLock(&gPciLock, irql);
    return value;
}

static NTSTATUS Complete(PIRP irp, NTSTATUS status, ULONG_PTR information) {
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS DispatchCreateClose(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    return Complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    const ULONG inSize = stack->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG outSize = stack->Parameters.DeviceIoControl.OutputBufferLength;
    void* buffer = irp->AssociatedIrp.SystemBuffer;   // METHOD_BUFFERED: in and out share it

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_DIRTYRAMMARK_PCI_READ: {
            if (inSize < sizeof(DirtyRamMarkPciRead) || outSize < sizeof(ULONG)) {
                return Complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
            }
            const DirtyRamMarkPciRead request = *(DirtyRamMarkPciRead*)buffer;
            if (request.bus > 255 || request.device > 31 || request.function > 7 || request.offset > 0xFC ||
                (request.offset & 3)) {
                return Complete(irp, STATUS_INVALID_PARAMETER, 0);
            }
            *(ULONG*)buffer = PciReadLocked(request.bus, request.device, request.function, request.offset);
            return Complete(irp, STATUS_SUCCESS, sizeof(ULONG));
        }
        case IOCTL_DIRTYRAMMARK_SMN_READ: {
            if (inSize < sizeof(ULONG) || outSize < sizeof(ULONG)) {
                return Complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
            }
            *(ULONG*)buffer = SmnRead(*(ULONG*)buffer);
            return Complete(irp, STATUS_SUCCESS, sizeof(ULONG));
        }
        case IOCTL_DIRTYRAMMARK_MMIO_READ: {
            if (inSize < sizeof(DirtyRamMarkMmioRead)) {
                return Complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
            }
            const DirtyRamMarkMmioRead request = *(DirtyRamMarkMmioRead*)buffer;
            if (request.count == 0 || request.count > DIRTYRAMMARK_MMIO_MAX_DWORDS ||
                (request.physicalAddress & 3) != 0) {
                return Complete(irp, STATUS_INVALID_PARAMETER, 0);
            }
            const ULONG bytes = request.count * sizeof(ULONG);
            if (outSize < bytes) {
                return Complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
            }
            PHYSICAL_ADDRESS physical;
            physical.QuadPart = (LONGLONG)request.physicalAddress;
            volatile ULONG* mapped = (volatile ULONG*)MmMapIoSpace(physical, bytes, MmNonCached);
            if (!mapped) {
                return Complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            ULONG* out = (ULONG*)buffer;   // the request was copied above, so overwriting it is fine
            for (ULONG i = 0; i < request.count; ++i) {
                out[i] = READ_REGISTER_ULONG((PULONG)&mapped[i]);
            }
            MmUnmapIoSpace((PVOID)mapped, bytes);
            return Complete(irp, STATUS_SUCCESS, bytes);
        }
        default:
            return Complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static VOID DriverUnload(PDRIVER_OBJECT driver) {
    IoDeleteSymbolicLink((PUNICODE_STRING)&kSymlinkName);
    IoDeleteDevice(driver->DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(registryPath);
    KeInitializeSpinLock(&gPciLock);

    PDEVICE_OBJECT device = NULL;
    NTSTATUS status = IoCreateDeviceSecure(driver, 0, (PUNICODE_STRING)&kDeviceName, FILE_DEVICE_UNKNOWN,
                                           FILE_DEVICE_SECURE_OPEN, TRUE /* one client at a time */,
                                           (PUNICODE_STRING)&kSddl, NULL, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = IoCreateSymbolicLink((PUNICODE_STRING)&kSymlinkName, (PUNICODE_STRING)&kDeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }
    driver->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    driver->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    driver->DriverUnload = DriverUnload;
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
