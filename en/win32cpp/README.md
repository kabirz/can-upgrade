# Firmware Upgrade Tool (C++ Version)

Pure C++20 implementation of a firmware upgrade tool supporting **CAN/UART dual-bus** communication for upgrading board firmware via PCAN interface or serial port.

## Features

- **Dual Transport Mode Support**
  - CAN Bus Mode: Firmware upgrade via PCAN-USB interface
  - UART Serial Mode: Firmware upgrade via serial port
  - Quick transport mode switching via menu bar

- **CAN Device Connection Management**
  - Auto-detect PCAN-USB devices (up to 16)
  - Support multiple baud rates (10K - 1M)
  - Connect/disconnect device management

- **UART Serial Connection Management**
  - Fast serial port enumeration (using SetupAPI)
  - Support multiple baud rates (9600 - 921600)
  - Auto-filter Bluetooth virtual serial ports
  - Non-blocking I/O for fast firmware transfer

- **Virtual CAN Support (Test Mode)**
  - Test GUI functionality without hardware
  - Simulate firmware upgrade process and save to file (`virtual_firmware.bin`)
  - Simulate version query and board reboot
  - Automatically displayed at end of device list (after all real devices)

- **Firmware Upgrade Features**
  - Read .bin firmware files
  - Real-time progress bar and percentage display (0% - 100%)
  - Support test mode (restores original firmware after second reboot)

- **Board Commands**
  - Get firmware version
  - Reboot board

- **GUI Interface**
  - Windows native dialog interface
  - Real-time log display
  - Progress bar showing upgrade progress
  - Percentage value display

## Directory Structure

```
win32cpp/
├── include/            # Header files
│   ├── can_manager.h   # CAN manager interface
│   ├── uart_manager.h  # UART manager interface
│   ├── resource.h      # Resource ID definitions
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # Source files
│   ├── main.cpp        # Main program and GUI logic
│   ├── can_manager.cpp # CAN communication management implementation
│   └── uart_manager.cpp # UART communication management implementation
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
- Visual Studio 2019 or higher (requires C++20 support, CMake auto-detects)
- CMake (>= 3.20)

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
- MinGW-w64 cross-compilation toolchain (with C++20 support)

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
| C++ Standard | C++20 | C++20 |
| Executable Size | ~34 KB | ~121 KB |
| Compiler | MSVC | GCC |
| Linking Method | Dynamically link system runtime | Statically link libgcc/libstdc++ |
| Debug Support | Excellent | Good |

**Note**: MinGW compiled files are larger mainly because libgcc and libstdc++ are statically linked, allowing the program to run directly on systems without MinGW runtime installed.

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
| setupapi | Serial port device enumeration (UART mode) |

### Compilation Requirements

- **C++ Standard**: C++20 (uses designated initializers and other features)
- **Character Set**: Unicode

## Binary File Size Analysis

### Size Optimization Results

| Compiler | Executable Size | Description |
|----------|----------------|-------------|
| MSVC (VS2022) | ~34 KB | Windows native build, minimum size |
| MinGW GCC | ~121 KB | Linux cross-compilation |
| C Version (win32c) | ~30 KB | C implementation with same functionality |

### MSVC Version Detailed Analysis

**File Size**: 33,792 bytes ≈ 34 KB

**Section Structure**:

| Section | Description | File Usage |
|---------|-------------|------------|
| .text | Code segment | ~12 KB |
| .rdata | Read-only data (constants, strings) | ~12 KB |
| .rsrc | Resources (icons, menus, dialogs) | ~12 KB |
| .data | Read/write data (global variables) | ~4 KB |
| .pdata | Exception handling table | ~4 KB |
| .reloc | Relocation table | ~4 KB |

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

## Communication Protocol

### CAN Protocol

#### CAN ID Definitions

| CAN ID | Direction | Description |
|--------|-----------|-------------|
| 0x101 | PC → Board | Platform command (PLATFORM_RX) |
| 0x102 | Board → PC | Platform response (PLATFORM_TX) |
| 0x103 | PC → Board | Firmware data (FW_DATA_RX) |

#### Frame Format

CAN data frame uses standard 8-byte data format:

```cpp
struct CanFrame {
    uint32_t code;   // Command code/response code (little-endian)
    uint32_t val;    // Parameter value (little-endian)
};
```

### UART Protocol

#### Frame Format

UART uses custom frame format with header, type, length, data, and checksum:

| Field | Bytes | Description |
|-------|-------|-------------|
| Header | 1 | Fixed 0xAA |
| Type | 1 | 0x01=Command, 0x02=Data |
| Length | 2 | Data length (big-endian) |
| Data | 0-8 | Valid data |
| CRC16 | 2 | CRC16-CCITT (big-endian) |
| Tail | 1 | Fixed 0x55 |

#### CRC16 Algorithm

Uses CRC16-CCITT algorithm (polynomial 0xA001, initial value 0xFFFF).

### Board Commands

Both transport modes use the same command codes:

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

| Feature | win32cpp (C++, MSVC) | win32cpp (C++, MinGW) | win32c (C, MSVC) | win32c (C, MinGW) |
|---------|-------------------|-------------------|-------------------|-------------------|
| Language | C++20 | C++20 | C | C |
| exe size | ~34 KB | ~121 KB | ~30 KB | ~121 KB |
| Compiler | MSVC | GCC | MSVC | GCC |
| Transport Mode | CAN/UART | CAN/UART | CAN/UART | CAN/UART |
| Synchronization | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION |
| Container | Fixed array | Fixed array | Fixed array | Fixed array |
| Memory management | new/delete | new/delete | malloc/free | malloc/free |
| Interface | Class member functions | Class member functions | Opaque handle + functions | Opaque handle + functions |
| Code style | Modern C++ | Modern C++ | Traditional C | Traditional C |

## License

This project is for learning and reference purposes only.
