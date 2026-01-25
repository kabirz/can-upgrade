# CAN Firmware Upgrade Tool (C++ Version)

A pure C++20 implementation of CAN firmware upgrade tool for upgrading board firmware via PCAN interface.

## Features

- **CAN Device Connection Management**
  - Auto-detect PCAN-USB devices (up to 16)
  - Support multiple baud rates (10K - 1M)
  - Connect/disconnect device management

- **Virtual CAN Support (Test Mode)**
  - Test GUI without hardware
  - Simulate firmware upgrade and save to file (`virtual_firmware.bin`)
  - Simulate version query and board reboot
  - Automatically shown at end of device list (after all real devices)

- **Firmware Upgrade Function**
  - Read .bin firmware files
  - Send firmware data via CAN bus
  - Real-time progress bar and percentage display
  - Support test mode (restore original firmware after second reboot)

- **Board Commands**
  - Get firmware version
  - Reboot board

- **GUI Interface**
  - Windows native dialog interface
  - Real-time log display
  - Progress bar for upgrade progress
  - Percentage value display (0% - 100%)

## Directory Structure

```
win32cpp/
├── include/            # Header files
│   ├── can_manager.h   # CAN manager interface
│   ├── resource.h      # Resource ID definitions
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # Source files
│   ├── main.cpp        # Main program and GUI logic
│   └── can_manager.cpp # CAN communication management implementation
├── resources/          # Resource files
│   ├── resource.rc     # Windows resource script
│   └── icon.ico        # Application icon
├── libs/               # External libraries directory
│   └── x64/            # 64-bit libraries
│       └── PCANBasic.lib  # PCAN-Basic static library (import library)
├── CMakeLists.txt      # CMake build configuration
├── CMakePresets.json   # CMake presets configuration
└── README.md           # Project documentation
```

## Build Environment Requirements

### Method 1: Windows Native Build (Visual Studio)

**Requirements:**
- Windows 10/11
- Visual Studio 2019 or later (C++20 support required, CMake auto-detects)
- CMake (>= 3.20)

**One-click build:**
```powershell
# Run in x64 Native Tools Command Prompt or PowerShell
cmake --workflow --preset vs-release
```

**Manual build:**
```powershell
# 1. Configure project (auto-detect VS version)
cmake --preset vs

# 2. Build Release version
cmake --build out --config Release
```

**Build output:**
```
out/Release/can-upgrade.exe
```

### Method 2: Cross-compilation (Linux builds Windows programs)

**Requirements:**
- Ubuntu/Debian/Arch Linux
- MinGW-w64 cross-compiler toolchain (C++20 support)

**Install dependencies:**
```bash
# Ubuntu/Debian
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 cmake ninja-build

# Arch Linux
sudo pacman -S mingw-w64-gcc cmake ninja
```

**One-click build:**
```bash
cmake --workflow --preset release
```

**Manual build:**
```bash
# 1. Configure project
cmake --preset default

# 2. Build Release version
cmake --build build --config Release
```

**Build output:**
```
build/can-upgrade.exe
```

### Toolchain Comparison

| Feature | Visual Studio | MinGW Cross-compilation |
|---------|--------------|-------------------------|
| Build environment | Windows | Linux |
| C++ standard | C++20 | C++20 |
| Executable size | ~34 KB | ~121 KB |
| Compiler | MSVC | GCC |
| Linking | Dynamic link system runtime | Static link libgcc/libstdc++ |
| Debug support | Excellent | Good |

**Note:** MinGW compiled files are larger mainly because static linking of `libgcc` and `libstdc++`, allowing the program to run directly on systems without MinGW runtime.

## Project Dependencies

### External Dependencies

| Dependency | Version/Type | Purpose |
|------------|-------------|---------|
| PCANBasic.lib | x64 static library | PEAK-System PCAN-Basic API for CAN bus communication |

### System Libraries (Windows)

| Library | Purpose |
|---------|---------|
| comctl32 | Common controls library (progress bar) |
| comdlg32 | Common dialog library (file selection) |
| gdi32 | GDI graphics device interface |

### Build Requirements

- **C++ Standard**: C++20 (uses designated initializers and other features)
- **Character Set**: Unicode

## Binary File Size Analysis

### Size Optimization Results

| Compiler | Executable Size | Description |
|----------|----------------|-------------|
| MSVC (VS2022) | ~34 KB | Windows native build, smallest size |
| MinGW GCC | ~121 KB | Linux cross-compilation |
| C version (win32c) | ~30 KB | Same functionality C implementation |

### MSVC Version Detailed Analysis

**File size**: 33,792 bytes ≈ 34 KB

**Section structure composition**:

| Section | Description | File占用 |
|---------|-------------|----------|
| .text | Code segment | ~12 KB |
| .rdata | Read-only data (constants, strings) | ~12 KB |
| .rsrc | Resources (icons, menus, dialogs) | ~12 KB |
| .data | Read-write data (global variables) | ~4 KB |
| .pdata | Exception handling table | ~4 KB |
| .reloc | Relocation table | ~4 KB |

### Runtime Dependencies

**Windows 10/11:**
- Only need to install **PCAN-Driver** to run

**Windows 7/8.1:**
- PCAN-Driver
- Microsoft Visual C++ 2015-2022 Redistributable (x64)

### Size Optimization Suggestions

To further reduce size, consider:

1. **UPX compression** (can reduce to 15-20 KB):
   ```bash
   upx --best --lzma can-upgrade.exe
   ```

2. **Remove unused resources:**
   - Delete unnecessary icons or dialogs
   - Use smaller icon files

3. **Merge strings:**
   - Extract duplicate strings as constants
   - Use more concise error messages

## CAN Communication Protocol

### CAN ID Definitions

| CAN ID | Direction | Description |
|--------|-----------|-------------|
| 0x101 | PC → Board | Platform command (PLATFORM_RX) |
| 0x102 | Board → PC | Platform response (PLATFORM_TX) |
| 0x103 | PC → Board | Firmware data (FW_DATA_RX) |

### Board Commands

| Command Code | Name | Description |
|--------------|------|-------------|
| 0 | BOARD_START_UPDATE | Start update |
| 1 | BOARD_CONFIRM | Confirm |
| 2 | BOARD_VERSION | Query version |
| 3 | BOARD_REBOOT | Reboot |

### Response Codes

| Response Code | Name | Description |
|---------------|------|-------------|
| 0 | FW_CODE_OFFSET | Offset response |
| 1 | FW_CODE_UPDATE_SUCCESS | Update successful |
| 2 | FW_CODE_VERSION | Version information |
| 3 | FW_CODE_CONFIRM | Confirm response |
| 4 | FW_CODE_FLASH_ERROR | Flash error |
| 5 | FW_CODE_TRANFER_ERROR | Transfer error |

## Comparison with win32c Project

| Feature | win32cpp (C++) | win32c (C, MSVC) |
|---------|---------------|-------------------|
| Language | C++20 | C |
| exe size | ~34 KB | ~30 KB |
| Compiler | MSVC | MSVC |
| Synchronization | CRITICAL_SECTION | CRITICAL_SECTION |
| Container | Fixed array | Fixed array |
| Memory management | new/delete | malloc/free |
| Interface | Class member functions | Opaque handle + functions |
| Code style | Modern C++ | Traditional C |

## License

This project is for learning and reference purposes only.
