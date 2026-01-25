# CAN Firmware Upgrade Tool (GTK4/Linux Version)

Linux GTK4 implementation of a CAN firmware upgrade tool for upgrading board firmware via SocketCAN interface.

## Features

- **CAN Device Connection Management**
  - Auto-detect SocketCAN interfaces (can0-can15, vcan0-vcan15)
  - Support multiple baud rates (10K - 1M)
  - Connect/disconnect device management

- **Virtual CAN Support (Test Mode)**
  - Test GUI functionality without hardware
  - Simulate firmware upgrade process and save to file (`virtual_firmware.bin`)
  - Simulate version query and board reboot
  - Virtual CAN interfaces (vcan*) automatically detected

- **Firmware Upgrade Features**
  - Read .bin firmware files
  - Send firmware data via CAN bus
  - Real-time progress bar and percentage display
  - Support test mode (restores original firmware after second reboot)

- **Board Commands**
  - Get firmware version
  - Reboot board

- **GUI Interface**
  - GTK4 native interface
  - GtkBuilder with XML UI file
  - Real-time log display
  - Progress bar showing upgrade progress

## Directory Structure

```
can-gtk4/
├── include/
│   └── can_manager.h       # CAN manager interface
├── src/
│   ├── main.c              # Main program and GTK4 GUI logic
│   └── can_manager.c       # CAN communication (SocketCAN)
├── resources/
│   └── main.ui             # GTK4 UI definition file (XML)
├── build/                  # Build directory
├── CMakeLists.txt          # CMake build configuration
└── README.md               # Project documentation
```

## Build Environment Requirements

### Linux Build Requirements

**Environment Requirements**:
- Linux (Ubuntu/Debian/Arch/Fedora)
- GCC compiler
- CMake (>= 3.25)
- GTK4 development libraries
- pkg-config

**Install Dependencies**:

Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libgtk-4-dev
```

Arch Linux:
```bash
sudo pacman -S base-devel cmake gtk4
```

Fedora:
```bash
sudo dnf install gcc cmake gtk4-devel pkg-config
```

**Build**:
```bash
# 1. Configure project
cmake -B build -S .

# 2. Build
cmake --build build

# 3. Run from build directory
cd build/bin
./can-upgrade

# 4. Install (optional)
sudo cmake --install build --prefix /usr/local
```

## SocketCAN Setup

### Virtual CAN (vcan0) for Testing

```bash
# Load vcan module
sudo modprobe vcan

# Create virtual CAN interface
sudo ip link add dev vcan0 type vcan

# Bring up the interface
sudo ip link set up vcan0
```

Make vcan0 persistent across reboots (Ubuntu/Debian):
```bash
# Create config file
echo "vcan" | sudo tee -a /etc/modules

# Create network interface config
cat << EOF | sudo tee /etc/network/interfaces.d/vcan0
auto vcan0
iface vcan0 inet manual
    pre-up ip link add dev vcan0 type vcan || true
    up ip link set dev vcan0 up
    down ip link set dev vcan0 down
EOF
```

### Physical CAN Hardware

For USB-to-CAN adapters or built-in CAN hardware:

```bash
# Install can-utils for testing
sudo apt install can-utils

# Load CAN modules (if not loaded)
sudo modprobe can
sudo modprobe can_raw
sudo modprobe can_dev

# Bring up the interface with baud rate
sudo ip link set can0 up type can bitrate 500000

# Verify interface is up
ip link show can0
```

**Common CAN hardware supported by SocketCAN**:
- USB-to-CAN adapters (Peak PCAN, ESD CAN, etc.)
- Built-in CAN controllers (NXP SJA1000, Bosch C_CAN, etc.)
- BeagleBone Cape CAN
- Raspberry Pi HATs with CAN controllers

## Project Dependencies

### System Libraries (Linux)

| Library | Purpose |
|---------|---------|
| gtk-4 | GTK4 GUI toolkit |
| pthread | Thread synchronization |
| glib-2.0 | GTK4 dependency utilities |

### SocketCAN Kernel Modules

| Module | Purpose |
|--------|---------|
| can | Core CAN protocol support |
| can_raw | Raw CAN socket protocol |
| can_dev | CAN device support |
| vcan | Virtual CAN driver |

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

## Usage

### Running the Application

```bash
cd build/bin
./can-upgrade
```

### Connection Steps

1. **Select Device**: Choose a CAN interface from the dropdown (e.g., can0, vcan0)
2. **Select Baud Rate**: Choose appropriate baud rate (e.g., 500K)
3. **Click Connect**: Establish connection to the CAN interface
4. **Get Version**: Retrieve firmware version from the board
5. **Select Firmware**: Browse to select a .bin firmware file
6. **Upgrade**: Click Upgrade to flash the firmware

### Testing with Virtual CAN

1. Setup vcan0 interface (see SocketCAN Setup section)
2. Select "vcan0" from device dropdown
3. Any baud rate works (virtual mode ignores it)
4. Select a firmware file and click Upgrade
5. The simulated firmware will be saved to `virtual_firmware.bin`

### Using can-utils for Debugging

```bash
# Monitor CAN traffic
candump can0

# Send test CAN frame
cansend can0 101#0000000000000000

# View CAN interface statistics
ip -details -statistics link show can0
```

## Baud Rate Configuration

Baud rates are configured via `ip link` command:

```bash
# Set baud rate before bringing up interface
sudo ip link set can0 up type can bitrate 125000
sudo ip link set can0 up type can bitrate 250000
sudo ip link set can0 up type can bitrate 500000
sudo ip link set can0 up type can bitrate 1000000
```

Note: This tool's baud rate dropdown is for informational purposes only.
The actual baud rate is configured at the kernel level using `ip link`.

## Troubleshooting

### "Failed to load UI" Error

Ensure the `resources/main.ui` file is in the correct location:
- From build directory: `build/bin/resources/main.ui`
- From installation: `/usr/local/bin/can-upgrade` with UI in `/usr/local/bin/resources/`

### "Failed to create CAN socket"

- Check if CAN module is loaded: `lsmod | grep can`
- Load CAN modules: `sudo modprobe can can_raw`
- Check if interface exists: `ip link show | grep -E "can|vcan"`

### "Failed to bind CAN socket to can0"

- Bring up the interface first: `sudo ip link set can0 up type can bitrate 500000`
- Check interface status: `ip link show can0`
- Verify with `candump can0` (from can-utils)

### "Permission denied" on CAN socket

Add user to appropriate groups:
```bash
# For most distributions
sudo usermod -aG dialout $USER

# Or use sudo (not recommended for production)
sudo ./can-upgrade
```

### No CAN interfaces detected

- Check available interfaces: `ip link show | grep -E "can|vcan"`
- Create virtual CAN for testing: `sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up`
- For physical hardware: Ensure driver is loaded and device is connected

## Differences from Windows Version

| Feature | Windows (win32c) | Linux (can-gtk4) |
|---------|------------------|------------------|
| GUI Framework | Win32 API | GTK4 |
| UI Definition | resource.rc | main.ui (XML) |
| CAN Backend | PCAN-Basic API | SocketCAN |
| Thread Sync | CRITICAL_SECTION | pthread_mutex_t |
| File Paths | wchar_t (UTF-16) | char (UTF-8) |
| Build System | CMake + VS/MinGW | CMake + GCC |
| Baud Rate | Software controlled | Kernel controlled (ip link) |

## Technical Details

### SocketCAN Overview

SocketCAN is a standard Linux kernel subsystem that implements CAN controllers as network interfaces. This provides:

- Standard socket-based API (PF_CAN)
- Integration with Linux network tools (ip, ifconfig, netstat)
- Support for multiple CAN hardware drivers
- Virtual CAN interfaces for testing

### Thread Safety

The CAN manager uses `pthread_mutex_t` for thread synchronization:
- Protects socket access
- Ensures thread-safe connection state management
- Prevents concurrent read/write operations

### CAN Frame Format

```c
// Application protocol frame (8 bytes CAN data)
typedef struct {
    uint32_t code;  // Command code
    uint32_t val;   // Parameter/value
} can_frame_t;
```

## License

This project is for learning and reference purposes only.
