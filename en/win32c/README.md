# Firmware Upgrade Tool (C Language Version)

Pure C language implementation of a firmware upgrade tool supporting **CAN/UART dual-bus** communication for upgrading board firmware via PCAN interface or serial port.

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
win32c/
├── include/            # Header files
│   ├── can_manager.h   # CAN manager interface
│   ├── uart_manager.h  # UART manager interface
│   ├── resource.h      # Resource ID definitions
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # Source files
│   ├── main.c          # Main program and GUI logic
│   ├── can_manager.c   # CAN communication management implementation
│   └── uart_manager.c  # UART communication management implementation
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

**Note**: MinGW compiled files are larger mainly because libgcc is statically linked, allowing the program to run directly on systems without MinGW runtime installed.

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

```c
typedef struct {
    uint32_t code;   // Command code/response code (little-endian)
    uint32_t val;    // Parameter value (little-endian)
} can_frame_t;
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

## Comparison with win32cpp Project

| Feature | win32cpp (C++, MSVC) | win32cpp (C++, MinGW) | win32c (C, MinGW) | win32c (C, MSVC) |
|---------|-------------------|-------------------|-------------------|-------------------|
| Language | C++ | C++ | C | C |
| exe size | ~34KB | ~121KB | ~121KB | ~30KB |
| Compiler | MSVC | GCC | GCC | MSVC |
| Transport Mode | CAN/UART | CAN/UART | CAN/UART | CAN/UART |
| Synchronization | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION |
| Container | Fixed array | Fixed array | Fixed array | Fixed array |
| Memory management | new/delete | new/delete | malloc/free | malloc/free |
| Interface | Class member functions | Class member functions | Opaque handle + functions | Opaque handle + functions |

## License

This project is for learning and reference purposes only.
