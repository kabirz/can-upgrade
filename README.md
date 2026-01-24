# CAN 固件升级工具

多语言实现的 CAN 总线固件升级工具集。

## 概述

本项目提供了使用不同编程语言和 GUI 框架实现的 CAN 固件升级工具，所有子项目实现相同的核心功能：通过 CAN 总线升级设备固件、查询版本、重启设备等。

## 项目结构

```
/
├── gtk4/       # GTK4/C 实现 (Linux)
├── python/     # Python 命令行工具 (跨平台)
├── qt/         # Qt6/C++ 实现 (跨平台)
├── rust/       # Rust + Slint 实现 (跨平台)
├── win32c/     # Windows 原生 C 实现 (Windows)
└── win32cpp/   # Windows 原生 C++ 实现 (Windows)
```

## 子项目

| 项目 | 语言 | GUI 框架 | 平台 | CAN 接口 | 说明 |
|------|------|----------|------|----------|------|
| [gtk4](./gtk4/) | C | GTK4 | Linux | SocketCAN | 轻量级 Linux 桌面应用 |
| [python](./python/) | Python | CLI | 跨平台 | PCAN/SocketCAN | 命令行工具 |
| [qt](./qt/) | C++ | Qt6 | 跨平台 | PCAN/SocketCAN | 功能完善的跨平台 GUI |
| [rust](./rust/) | Rust | Slint | 跨平台 | PCAN/SocketCAN | 现代化实现 |
| [win32c](./win32c/) | C | Win32 API | Windows | PCAN | 纯 C 原生 Windows GUI |
| [win32cpp](./win32cpp/) | C++ | Win32 API | Windows | PCAN | C++ 原生 Windows GUI |

## 功能特性

- **固件升级**: 通过 CAN 总线升级设备固件
- **版本查询**: 获取设备当前固件版本
- **设备重启**: 远程重启 CAN 设备
- **测试模式**: 支持临时固件升级

## CAN 协议

所有项目使用统一的 CAN 协议：

| CAN ID | 名称 | 方向 |
|--------|------|------|
| 0x101 | PLATFORM_RX | PC → 板卡 |
| 0x102 | PLATFORM_TX | 板卡 → PC |
| 0x103 | FW_DATA_RX | 固件数据帧 |

### 命令码

| 码 | 命令 | 说明 |
|----|------|------|
| 0 | BOARD_START_UPDATE | 开始固件更新 |
| 1 | BOARD_CONFIRM | 确认命令 |
| 2 | BOARD_VERSION | 查询版本 |
| 3 | BOARD_REBOOT | 重启设备 |

## 硬件支持

- **Linux**: SocketCAN 接口
- **Windows**: PEAK PCAN 接口 (需要安装 [PCAN-Basic](https://www.peak-system.com/PCAN-Basic.535.0.html))

## 快速开始

### Python (命令行)

```bash
cd python
pip install -r requirements.txt
python can_upgrade_new.py --help
```

### GTK4 (Linux)

```bash
cd gtk4
cmake -B build
cmake --build build
./build/can-upgrade
```

### Qt6 (跨平台)

```bash
cd qt
cmake --preset default
cmake --build build
./build/can-upgrade
```

### Rust

```bash
cd rust
cargo build --release
./target/release/can-upgrade
```

### Windows (C/C++)

```bash
cd win32c  # 或 win32cpp
cmake --preset default
cmake --build build
./build/can-upgrade.exe
```

## 许可证

请参阅各子项目目录中的 LICENSE 文件。

## 链接

- [gtk4 README](./gtk4/README.md)
- [python README](./python/README.md)
- [qt README](./qt/README.md) / [BUILD.md](./qt/BUILD.md)
- [rust README](./rust/README.md)
- [win32c README](./win32c/README.md)
- [win32cpp README](./win32cpp/README.md)
