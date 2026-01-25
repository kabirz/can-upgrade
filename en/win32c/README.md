# CAN Firmware Upgrade Tool (C Language Version)

Pure C language implementation of a CAN firmware upgrade tool for upgrading board firmware via PCAN interface.

## Features

- **CAN Device Connection Management**
  - Auto-detect PCAN-USB devices (up to 16)
  - Support multiple baud rates (10K - 1M)
  - Connect/disconnect device management

- **Virtual CAN Support (Test Mode)**
  - Test GUI functionality without hardware
  - Simulate firmware upgrade process and save to file (`virtual_firmware.bin`)
  - Simulate version query and board reboot
  - Automatically displayed at end of device list (after all real devices)

- **Firmware Upgrade Features**
  - Read .bin firmware files
  - Send firmware data via CAN bus
  - Real-time progress bar and percentage display
  - Support test mode (restores original firmware after second reboot)

- **Board Commands**
  - Get firmware version
  - Reboot board

- **GUI Interface**
  - Windows native dialog interface
  - Real-time log display
  - Progress bar showing upgrade progress
  - Percentage value display (0% - 100%)

## Directory Structure

```
win32c/
├── include/            # Header files
│   ├── can_manager.h   # CAN manager interface
│   ├── resource.h      # Resource ID definitions
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # Source files
│   ├── main.c          # Main program and GUI logic
│   └── can_manager.c   # CAN communication management implementation
├── resources/          # Resource files
│   ├── resource.rc     # Windows resource script
│   └── icon.ico        # Application icon
├── libs/               # External library directory
│   └── x64/            # 64-bit libraries
│       └── PCANBasic.lib  # PCAN-Basic static library (import library)
├── CMakeLists.txt      # CMake build configuration
├── CMakePresets.json   # CMake preset configuration
└── README.md           # Project documentation
```

## Build Environment Requirements

### Method 1: Windows Native Build (Visual Studio)

**Environment Requirements**:
- Windows 10/11
- Visual Studio 2019 or higher (CMake auto-detects by default)
- CMake (>= 3.25)

**One-click Build**:
```powershell
# Run in x64 Native Tools Command Prompt or PowerShell
cmake --workflow --preset vs-release
```

**Manual Build**:
```powershell
# 1. Configure project (auto-detect VS version)
cmake --preset vs

# 2. Build Release version
cmake --build out --config Release
```

**Build Output**:
```
out/Release/can-upgrade.exe
```

### Method 2: Cross-compilation (Linux Compiling Windows Programs)

**Environment Requirements**:
- Ubuntu/Debian/Arch Linux
- MinGW-w64 cross-compilation toolchain

**Install Dependencies**:
```bash
# Ubuntu/Debian
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 cmake ninja-build

# Arch Linux
sudo pacman -S mingw-w64-gcc cmake ninja
```

**One-click Build**:
```bash
cmake --workflow --preset release
```

**Manual Build**:
```bash
# 1. Configure project
cmake --preset default

# 2. Build Release version
cmake --build build --config Release
```

**Build Output**:
```
build/can-upgrade.exe
```

### Toolchain Comparison

| Feature | Visual Studio | MinGW Cross-compilation |
|---------|--------------|-------------------------|
| Build Environment | Windows | Linux |
| Executable Size | ~30 KB | ~121 KB |
| Compiler | MSVC | GCC |
| Linking Method | Dynamically link system runtime | Statically link libgcc |
| Debug Support | Excellent | Good |

**Note**: MinGW compiled files are larger mainly because libgcc and C runtime library are statically linked, allowing the program to run directly on systems without MinGW runtime installed.

## Project Dependencies

### External Dependencies

| Dependency | Version/Type | Purpose |
|-----------|-------------|---------|
| PCANBasic.lib | x64 Static Library | PEAK-System PCAN-Basic API for CAN bus communication |

### System Libraries (Windows)

| Library | Purpose |
|---------|---------|
| comctl32 | Common controls library (progress bar) |
| comdlg32 | Common dialog library (file selection) |
| gdi32 | GDI Graphics Device Interface |

### Compiler Optimization Options

| Option | Description |
|--------|-------------|
| `-Os` | Optimize code size |
| `-ffunction-sections` | Place functions in independent sections for link-time unused code removal |
| `-fdata-sections` | Place data in independent sections for link-time unused data removal |
| `-mwindows` | Windows GUI application (no console window) |
| `-static-libgcc` | Statically link libgcc, reduce runtime dependencies |
| `-s` / `--strip-all` | Remove symbol table, reduce file size |
| `--gc-sections` | Remove unused sections |

## Binary File Size Analysis

### Size Optimization Results

| Compiler | Executable Size | Description |
|----------|----------------|-------------|
| MSVC (VS2022) | ~30 KB | Windows native build, minimum size |
| MinGW GCC | ~121 KB | Linux cross-compilation |
| C++ Version (win32cpp) | MSVC: ~34 KB / MinGW: ~121 KB | C++ implementation with same functionality |

### MSVC Version Detailed Analysis

**File Size**: 30,720 bytes ≈ 30 KB

**Section Structure**:

| Section | Description | File Usage | Memory Usage |
|---------|-------------|------------|--------------|
| .text | Code segment | 9,728 B | 9,708 B |
| .rdata | Read-only data (constants, strings) | 7,168 B | 6,812 B |
| .rsrc | Resources (icons, menus, dialogs) | 9,728 B | 9,352 B |
| .pdata | Exception handling table | 1,024 B | 624 B |
| .data | Read/write data (global variables) | 512 B | 400 B |
| .reloc | Relocation table | 512 B | 68 B |

**Memory Usage**:
- **Runtime Image**: 48 KB
- **Stack Space**: Reserved 1 MB / Actual 4 KB
- **Heap Space**: Reserved 1 MB / Actual 4 KB

### Runtime Dependencies

**Windows 10/11**:
- Only **PCAN-Driver** needs to be installed

**Windows 7/8.1**:
- PCAN-Driver
- Microsoft Visual C++ 2015-2022 Redistributable (x64)

### Size Optimization Recommendations

To further reduce size, consider:

1. **UPX Compression** (can reduce to 15-20 KB):
   ```bash
   upx --best --lzma can-upgrade.exe
   ```

2. **Remove Unused Resources**:
   - Delete unneeded icons or dialogs
   - Use smaller icon files

3. **Merge Strings**:
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

## Comparison with win32cpp Project

| Feature | win32cpp (C++, MSVC) | win32c (C, MSVC) |
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
