# Blackbird Kernel-Space Driver

This directory contains the kernel-space implementation of Blackbird, which runs as a Windows kernel driver instead of a user-space application.

## Overview

The kernel-space version intercepts keyboard input at the kernel level using a keyboard filter driver. This approach has several advantages over the user-space version:

- **Lower-level access**: Captures keystrokes before they reach user-space applications
- **Harder to detect**: Operates at kernel level, making it less visible to user-space monitoring tools
- **System-wide coverage**: Can capture input even in secure contexts where user-space applications cannot

## Architecture

The kernel driver (`driver.c`) implements:
- **Keyboard Filter Driver**: Intercepts keyboard input at the driver level
- **Buffered Logging**: Stores keystrokes in a kernel buffer before writing to disk
- **File I/O**: Writes captured data to `C:\blackbird_kernel.log`
- **Safe Operation**: Uses spinlocks for thread-safe buffer access

## Files

- `driver.c` - Main kernel driver source code
- `blackbird_driver.inf` - Driver installation file
- `CMakeLists.txt` - Build configuration (informational only)

## Requirements

### For Building
- **Windows Driver Kit (WDK)**: Required for compiling kernel drivers
- **Visual Studio**: 2019 or later with C/C++ and driver development workload
- **Windows SDK**: Compatible version with your WDK
- **64-bit Windows**: Kernel drivers must match the system architecture

### For Installation
- **Administrator privileges**: Required for driver installation and loading
- **Test signing enabled** OR **Driver signing certificate**: See below

## Building the Driver

### Option 1: Visual Studio with WDK
1. Install Visual Studio with C/C++ development tools
2. Install the Windows Driver Kit (WDK)
3. Open Visual Studio
4. Create a new "Kernel Mode Driver, Empty (KMDF)" project
5. Add `driver.c` to the project
6. Build the project for your target architecture (x64)

### Option 2: Command Line with WDK
1. Open a WDK command prompt (e.g., "x64 Free Build Environment")
2. Navigate to the driver directory
3. Use `msbuild` with appropriate parameters:
   ```
   msbuild /p:Configuration=Release /p:Platform=x64
   ```

## Installing and Loading the Driver

### Enable Test Signing (Development Only)
Since the driver is not signed by a trusted certificate authority, you must enable test signing:

```cmd
bcdedit /set testsigning on
```

Reboot your system after running this command.

### Install the Driver
1. Copy the built `blackbird_driver.sys` and `blackbird_driver.inf` to a directory
2. Right-click on `blackbird_driver.inf` and select "Install"
3. Or use the Device Manager to manually install the driver

### Load the Driver
Use the Service Control Manager to start the driver:

```cmd
sc create BlackbirdDriver type= kernel binPath= C:\Path\To\blackbird_driver.sys
sc start BlackbirdDriver
```

### Unload the Driver
```cmd
sc stop BlackbirdDriver
sc delete BlackbirdDriver
```

## Configuration

The kernel driver currently has the following hardcoded settings:
- **Log file location**: `C:\blackbird_kernel.log`
- **Buffer size**: 4096 characters
- **Flush threshold**: When buffer reaches 3996 characters

To modify these settings, edit the constants at the top of `driver.c` and rebuild.

## Logging

The driver logs keyboard input to `C:\blackbird_kernel.log`. The log file:
- Uses Unicode (UTF-16) encoding
- Appends data (does not overwrite on driver restart)
- Automatically flushes when the buffer is nearly full
- Flushes all remaining data on driver unload

## Debugging

For development and debugging:
- Use DebugView or similar tools to see `KdPrint` messages
- Enable kernel debugging in Windows
- Use WinDbg for advanced driver debugging

## Security Considerations

### Important Warnings
1. **Kernel-space code runs with highest privileges**: Bugs can crash the system
2. **No built-in security**: The log file is not encrypted
3. **Detection**: Anti-malware software may detect the driver
4. **Legal implications**: Unauthorized use may violate laws

### Recommendations
- Only use in controlled, authorized environments
- Test thoroughly in a virtual machine before deploying
- Implement proper error handling and validation
- Consider encrypting the log file
- Add proper driver signing for production use

## Comparison with User-Space Version

| Feature | User-Space | Kernel-Space |
|---------|------------|--------------|
| Privilege Level | User | Kernel |
| Installation Complexity | Simple | Complex (requires driver signing) |
| Detection Difficulty | Easier to detect | Harder to detect |
| System Impact | Minimal | Can crash system if buggy |
| Build Complexity | Standard C++ compiler | Requires WDK |
| Portability | Windows user-space | Windows kernel only |
| Debugging | Standard debugging | Kernel debugging required |

## Troubleshooting

### Driver fails to load
- Ensure test signing is enabled (`bcdedit /set testsigning on`)
- Check Windows Event Viewer for error messages
- Verify the driver is built for the correct architecture (x64)

### No log file created
- Check that the driver has write permissions to `C:\`
- Ensure the driver is actually loaded (`sc query BlackbirdDriver`)
- Check DebugView for error messages

### System crash (BSOD)
- This indicates a bug in the driver code
- Use WinDbg to analyze the crash dump
- Check for common issues: accessing invalid memory, incorrect IRQL, etc.

## Limitations

- **Simplified keyboard mapping**: Uses a basic US keyboard layout
- **No shift/capslock handling**: Does not track modifier keys properly
- **No locale support**: Unlike the user-space version, doesn't respect keyboard locale
- **No email support**: Only logs to a file
- **Fixed log location**: Cannot be configured without rebuilding

## Future Improvements

Potential enhancements for the kernel driver:
- Dynamic keyboard layout detection
- Proper modifier key handling
- Configurable log file location via registry
- Encrypted log storage
- Network transmission support
- Support for PS/2 and USB keyboards separately

## Ethics and Legal Notice

This kernel driver is provided for educational and authorized security testing purposes only. Unauthorized deployment or use of this software may violate:
- Computer Fraud and Abuse Act (CFAA)
- Electronic Communications Privacy Act (ECPA)
- State and local privacy laws
- Corporate policies and employment agreements

The authors do not endorse or support malicious use of this software.
