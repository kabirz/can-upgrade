# CAN 固件升级工具 - Qt6 版

基于 Qt6 的 GUI 应用程序，用于在 Windows 和 Linux 上通过 CAN 总线升级固件。

## 功能特性

- **自动检测 CAN 接口**：启动时自动检测可用的 CAN 接口和通道
- **支持多种 CAN 总线**：
  - PCAN（Windows）
  - SocketCAN（Linux）
  - Vector CAN
  - Peak CAN
  - Syntec CAN
  - Tiny CAN
  - Virtual CAN
  - Passthru
- **固件升级**：带有进度指示的固件烧录功能
- **测试模式**：支持临时升级（下次重启后恢复）
- **板卡命令**：获取固件版本和重启板卡
- **实时日志**：带时间戳的操作监控
- **国际化支持**：多语言支持（中文、英文）

## 系统要求

- Qt 6.10 或更高版本
- CMake 3.25 或更高版本（支持 workflow 功能）
- C++17 兼容的编译器（MSVC 2022+, GCC, Clang）
- Qt6 SerialBus 模块
- Qt6 LinguistTools（用于翻译）

## Windows 平台构建

### 前置条件

1. 安装 Qt 6.10.1（或更高版本）[Qt 下载](https://www.qt.io/download)
2. 安装 CMake 3.25+（或更高版本）[CMake 下载](https://cmake.org/download/)
3. 安装 Visual Studio 2022（或更高版本，需包含 C++ 支持）

### 构建步骤

```bash
# 打开 Qt 命令提示符或配置环境变量
# 将 Qt 添加到 PATH 或使用 Qt Creator

# 使用 CMake 配置
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64"

# 构建
cmake --build build --config Release

# 运行
.\build\bin\Release\can_upgrade.exe
```

### 使用 CMake Presets

```bash
# 使用 workflow preset 配置并构建（推荐）
cmake --workflow --preset build-release

# 或者一步配置并构建
cmake --preset build-release
```

### 使用 Qt Creator

1. 打开 Qt Creator
2. 文件 -> 打开文件或项目
3. 选择 `CMakeLists.txt`
4. 配置项目（选择适当的 Qt kit）
5. 构建并运行

## Linux 平台构建

### 前置条件

```bash
# 安装依赖（Ubuntu/Debian）
sudo apt-get install qt6-base-dev qt6-serialbus-dev qt6-tools-dev cmake g++

# 或在 Fedora/RHEL 上
sudo dnf install qt6-qtbase-devel qt6-serialbus-devel qt6-linguist-tools cmake gcc-c++
```

### 构建步骤

```bash
# 配置
cmake -B build -DCMAKE_PREFIX_PATH="/usr/lib/x86_64-linux-gnu/qt6"

# 构建
cmake --build build

# 运行
./build/can_upgrade
```

## 使用说明

### 连接步骤

1. 应用启动时自动检测可用的 CAN 接口
2. 从下拉菜单选择 CAN 接口（Windows 选 "pcan"，Linux 选 "socketcan"）
3. 所选接口的可用通道会自动填充
4. 点击"刷新"按钮可重新扫描可用通道
5. 点击"连接"建立连接

### 支持的 CAN 接口

| 接口 | 平台 | 通道 |
|-----------|----------|----------|
| pcan | Windows | PCAN_USBBUS1, PCAN_USBBUS2, PCAN_PCIBUS1 等 |
| socketcan | Linux | can0, can1, can2, vcan0, vcan1 等 |
| vector | Windows/Linux | CAN1, CAN2, CAN3, CAN4 |
| peakcan | Windows/Linux | usb0, usb1, usb2 |
| virtualcan | 所有平台 | can0 |
| systeccan | Windows/Linux | 自动检测 |
| tinycan | Windows/Linux | 自动检测 |
| passthru | Windows | 自动检测 |

### 固件升级流程

1. 点击"浏览"选择固件文件（.bin 格式）
2. 可选：启用"测试模式"进行临时升级（下次重启后恢复）
3. 点击"烧录固件"
4. 在进度条和日志窗口中监控进度
5. 上传完成后，重启板卡以应用升级

### 板卡命令

- **获取版本**：获取并显示当前固件版本
- **重启板卡**：向已连接的板卡发送重启命令

## CAN 协议

应用程序使用以下 CAN 协议：

- `0x101` (PLATFORM_RX)：发送到板卡的命令
- `0x102` (PLATFORM_TX)：来自板卡的响应
- `0x103` (FW_DATA_RX)：固件数据帧

### 命令

| 命令 | 代码 | 描述 |
|---------|------|-------------|
| BOARD_START_UPDATE | 0 | 初始化固件更新 |
| BOARD_CONFIRM | 1 | 确认并应用固件 |
| BOARD_VERSION | 2 | 请求固件版本 |
| BOARD_REBOOT | 3 | 重启板卡 |

### 响应代码

| 代码 | 描述 |
|------|-------------|
| FW_CODE_OFFSET (0) | 数据偏移确认 |
| FW_CODE_UPDATE_SUCCESS (1) | 更新成功 |
| FW_CODE_VERSION (2) | 版本响应 |
| FW_CODE_CONFIRM (3) | 确认响应 |
| FW_CODE_FLASH_ERROR (4) | Flash 操作错误 |
| FW_CODE_TRANFER_ERROR (5) | 数据传输错误 |

## 项目结构

```
can-upgrade-qt/
├── CMakeLists.txt              # CMake 构建配置
├── CMakePresets.json           # CMake preset 配置
├── src/
│   ├── main.cpp                # 应用程序入口
│   ├── mainwindow.h/.cpp       # 主窗口实现
│   ├── mainwindow.ui           # UI 设计器文件
│   └── canmanager.h/.cpp       # CAN 通信管理器
├── translations/
│   └── can_upgrade_zh_CN.ts    # 中文翻译文件
└── BUILD.md                     # 本文件
```

## 国际化

应用程序支持多种语言。翻译文件位于 `translations/` 目录。

### 添加或更新翻译

```bash
# 使用新的字符串更新翻译文件
cmake --build build --target lupdate

# 使用 Qt Linguist 编辑 .ts 文件
linguist translations/can_upgrade_zh_CN.ts

# 构建以编译翻译
cmake --build build
```

## 故障排除

### 未检测到 CAN 接口

- 确保 CAN 驱动程序已正确安装
- 检查 CAN 硬件是否已连接
- 验证应用程序目录中是否有 Qt SerialBus 插件可用

### 连接失败

- 确认所选接口和通道正确
- 检查是否有其他应用程序正在使用该 CAN 接口
- 确保 CAN 总线硬件已正确连接

### 翻译不工作

- 检查控制台输出的翻译加载调试信息
- 验证 .qm 文件是否正确嵌入到可执行文件中
- 确保系统语言与可用翻译匹配

## 许可证

本项目实现与原始 `can_upgrade_new.py` 工具相同的功能。
