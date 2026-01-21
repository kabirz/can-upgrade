# CAN 固件升级工具

基于 Qt6 的 GUI 应用程序，用于通过 CAN 总线进行固件升级。

## 功能特性

- **CAN 连接**：支持多种 CAN 接口（PCAN、SocketCAN）
- **固件升级**：带进度显示的固件烧录
- **测试模式**：支持测试模式升级（重启后恢复）
- **板卡命令**：获取固件版本和重启板卡
- **实时日志**：带时间戳的操作日志

## 环境要求

- Qt 6.10.1 或更高版本
- CMake 3.25 或更高版本（用于 workflow presets）
- C++17 兼容编译器（MSVC 2022）
- Qt6 SerialBus 模块

## 构建

### 前置要求

1. 从 https://www.qt.io/download 安装 Qt 6.10.1
2. 从 https://cmake.org/download/ 安装 CMake 3.25+
3. 安装 Visual Studio 2022（包含 C++ 支持）

### 快速构建

使用 CMake presets（推荐）：

```bash
# 配置并构建 Release 版本
cmake --workflow --preset release

# 或配置并构建 Debug 版本
cmake --workflow --preset debug
```

### 手动构建

```bash
# 配置
cmake --preset default

# 构建 Release
cmake --build build --config Release

# 构建 Debug
cmake --build build --config Debug
```

### 使用 Qt Creator

1. 打开 Qt Creator
2. 文件 -> 打开文件或项目
3. 选择 `CMakeLists.txt`
4. 配置项目（选择合适的 Qt kit）
5. 构建并运行

## 使用说明

### 连接 CAN

1. 选择 CAN 接口（Windows 下选择 "pcan"）
2. 选择通道（如 "PCAN_USBBUS1"）
3. 点击 "连接"

### 固件升级

1. 点击 "浏览" 选择固件文件（.bin）
2. 可选：启用 "测试模式" 进行临时升级
3. 点击 "烧录固件"
4. 等待上传完成
5. 重启板卡以应用升级

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
/
├── CMakeLists.txt          # CMake 构建配置
├── CMakePresets.json       # CMake 预设，便于构建
├── README.md               # 本文件
├── BUILD.md                # 详细构建说明
├── can_upgrade_new.py      # 原始 Python 实现（参考）
└── src/                    # 源代码目录
    ├── main.cpp            # 应用程序入口
    ├── mainwindow.h/.cpp   # 主窗口实现
    ├── mainwindow.ui       # UI 设计文件
    └── canmanager.h/.cpp   # CAN 通信管理器
```

## 许可证

本项目实现了与原始 `can_upgrade_new.py` 工具相同的功能。

## 故障排除

### 找不到 windeployqt

如果在构建过程中看到 "windeployqt not found"，请确保：
1. Qt 安装在 CMakePresets.json 中指定的路径
2. 如果 Qt 安装在其他位置，请更新 `CMAKE_PREFIX_PATH`

### CAN 连接问题

1. 验证 CAN 接口驱动程序已安装（如 PCAN 驱动）
2. 检查 CAN 通道名称是否正确
3. 确保没有其他应用程序正在使用 CAN 接口
