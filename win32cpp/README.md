# 固件升级工具 (C++ 版本)

纯 C++20 实现的固件升级工具，支持 **CAN/UART 双总线**通信，用于通过 PCAN 接口或串口升级板卡固件。

## 功能特性

- **双传输模式支持**
  - CAN 总线模式：通过 PCAN-USB 接口进行固件升级
  - UART 串口模式：通过串口进行固件升级
  - 菜单栏快速切换传输模式

- **CAN 设备连接管理**
  - 自动检测 PCAN-USB 设备（最多 16 个）
  - 支持多种波特率（10K - 1M）
  - 连接/断开设备管理

- **UART 串口连接管理**
  - 快速枚举可用串口（使用 SetupAPI）
  - 支持多种波特率（9600 - 921600）
  - 自动过滤蓝牙虚拟串口
  - 非阻塞 I/O，快速固件传输

- **虚拟 CAN 支持（测试模式）**
  - 无需硬件即可测试 GUI 功能
  - 模拟固件升级流程并保存到文件（`virtual_firmware.bin`）
  - 模拟版本查询和板卡重启
  - 自动显示在设备列表末尾（在所有真实设备之后）

- **固件升级功能**
  - 读取 .bin 固件文件
  - 实时进度条和百分比显示（0% - 100%）
  - 支持测试模式（第二次重启后恢复原固件）

- **板卡命令**
  - 获取固件版本
  - 重启板卡

- **GUI 界面**
  - Windows 原生对话框界面
  - 支持中文界面（微软雅黑字体）
  - 实时日志显示
  - 进度条显示升级进度
  - 百分比数值显示

## 目录结构

```
win32cpp/
├── include/            # 头文件
│   ├── can_manager.h   # CAN 管理器接口
│   ├── uart_manager.h  # UART 管理器接口
│   ├── resource.h      # 资源 ID 定义
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # 源文件
│   ├── main.cpp        # 主程序和 GUI 逻辑
│   ├── can_manager.cpp # CAN 通信管理实现
│   └── uart_manager.cpp # UART 通信管理实现
├── resources/          # 资源文件
│   ├── resource.rc     # Windows 资源脚本
│   └── icon.ico        # 应用图标
├── libs/               # 外部库目录
│   └── x64/            # 64位库
│       └── PCANBasic.lib  # PCAN-Basic 静态库(导入库)
├── CMakeLists.txt      # CMake 构建配置
├── CMakePresets.json   # CMake 预设配置
└── README.md           # 项目文档
```

## 编译环境要求

### 方式一：Windows 原生编译（Visual Studio）

**环境要求**：
- Windows 10/11
- Visual Studio 2019 或更高版本（需支持 C++20，CMake 会自动检测）
- CMake（>= 3.20）

**一键编译**：
```powershell
# 在 x64 Native Tools Command Prompt 或 PowerShell 中运行
cmake --workflow --preset vs-release
```

**手动编译**：
```powershell
# 1. 配置项目（自动检测 VS 版本）
cmake --preset vs

# 2. 编译 Release 版本
cmake --build out --config Release
```

**编译产物**：
```
out/Release/can-upgrade.exe
```

### 方式二：交叉编译（Linux 编译 Windows 程序）

**环境要求**：
- Ubuntu/Debian/Arch Linux
- MinGW-w64 交叉编译工具链（支持 C++20）

**安装依赖**：
```bash
# Ubuntu/Debian
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 cmake ninja-build

# Arch Linux
sudo pacman -S mingw-w64-gcc cmake ninja
```

**一键编译**：
```bash
cmake --workflow --preset release
```

**手动编译**：
```bash
# 1. 配置项目
cmake --preset default

# 2. 编译 Release 版本
cmake --build build --config Release
```

**编译产物**：
```
build/can-upgrade.exe
```

### 工具链对比

| 特性 | Visual Studio | MinGW 交叉编译 |
|------|--------------|----------------|
| 编译环境 | Windows | Linux |
| C++ 标准 | C++20 | C++20 |
| 可执行文件大小 | ~34 KB | ~121 KB |
| 编译器 | MSVC | GCC |
| 链接方式 | 动态链接系统运行时 | 静态链接 libgcc/libstdc++ |
| 调试支持 | 完美 | 较好 |

**说明**：MinGW 编译的文件较大主要是因为静态链接了 `libgcc` 和 `libstdc++`，这使程序可以在没有安装 MinGW 运行时的系统上直接运行。

## 项目依赖

### 外部依赖

| 依赖项 | 版本/类型 | 用途 |
|--------|----------|------|
| PCANBasic.lib | x64 静态库 | PEAK-System PCAN-Basic API，用于 CAN 总线通信 |

### 系统库（Windows）

| 库名 | 用途 |
|------|------|
| comctl32 | 通用控件库（进度条） |
| comdlg32 | 通用对话框库（文件选择） |
| gdi32 | GDI 图形设备接口 |
| setupapi | 串口设备枚举（UART 模式） |

### 编译要求

- **C++ 标准**: C++20（使用了指定初始值设定项等特性）
- **字符集**: Unicode

## 二进制文件大小分析

### 体积优化效果

| 编译器 | 可执行文件大小 | 说明 |
|--------|---------------|------|
| MSVC (VS2022) | ~34 KB | Windows 原生编译，体积最小 |
| MinGW GCC | ~121 KB | Linux 交叉编译 |
| C 版本 (win32c) | ~30 KB | 相同功能的 C 实现 |

### MSVC 版本详细分析

**文件大小**: 33,792 字节 ≈ 34 KB

**段结构组成**:

| 段名 | 描述 | 文件占用 |
|------|------|----------|
| .text | 代码段 | ~12 KB |
| .rdata | 只读数据（常量、字符串） | ~12 KB |
| .rsrc | 资源（图标、菜单、对话框） | ~12 KB |
| .data | 读写数据（全局变量） | ~4 KB |
| .pdata | 异常处理表 | ~4 KB |
| .reloc | 重定位表 | ~4 KB |

### 运行时依赖

**Windows 10/11**:
- 仅需安装 **PCAN-Driver** 即可运行

**Windows 7/8.1**:
- PCAN-Driver
- Microsoft Visual C++ 2015-2022 Redistributable (x64)

### 大小优化建议

如需进一步减小体积，可考虑：

1. **UPX 压缩**（可减小至 15-20 KB）：
   ```bash
   upx --best --lzma can-upgrade.exe
   ```

2. **移除未使用的资源**：
   - 删除不需要的图标或对话框
   - 使用更小的图标文件

3. **合并字符串**：
   - 提取重复字符串为常量
   - 使用更简洁的错误提示

## 通信协议

### CAN 协议

#### CAN ID 定义

| CAN ID | 方向 | 说明 |
|--------|------|------|
| 0x101 | PC → 板卡 | 平台命令 (PLATFORM_RX) |
| 0x102 | 板卡 → PC | 平台响应 (PLATFORM_TX) |
| 0x103 | PC → 板卡 | 固件数据 (FW_DATA_RX) |

#### 帧格式

CAN 数据帧使用标准 8 字节数据格式：

```cpp
struct CanFrame {
    uint32_t code;   // 命令码/响应码（小端序）
    uint32_t val;    // 参数值（小端序）
};
```

### UART 协议

#### 帧格式

UART 使用自定义帧格式，包含帧头、类型、长度、数据和校验：

| 字段 | 字节数 | 说明 |
|------|--------|------|
| 帧头 | 1 | 固定 0xAA |
| 类型 | 1 | 0x01=命令, 0x02=数据 |
| 长度 | 2 | 数据长度（大端序） |
| 数据 | 0-8 | 有效数据 |
| CRC16 | 2 | CRC16-CCITT（大端序） |
| 帧尾 | 1 | 固定 0x55 |

#### CRC16 算法

使用 CRC16-CCITT 算法（多项式 0xA001，初始值 0xFFFF）。

### 板卡命令

两种传输模式使用相同的命令码：

| 命令码 | 名称 | 说明 |
|--------|------|------|
| 0 | BOARD_START_UPDATE | 开始更新 |
| 1 | BOARD_CONFIRM | 确认 |
| 2 | BOARD_VERSION | 查询版本 |
| 3 | BOARD_REBOOT | 重启 |

### 响应码

| 响应码 | 名称 | 说明 |
|--------|------|------|
| 0 | FW_CODE_OFFSET | 偏移量响应 |
| 1 | FW_CODE_UPDATE_SUCCESS | 更新成功 |
| 2 | FW_CODE_VERSION | 版本信息 |
| 3 | FW_CODE_CONFIRM | 确认响应 |
| 4 | FW_CODE_FLASH_ERROR | Flash 错误 |
| 5 | FW_CODE_TRANFER_ERROR | 传输错误 |

## 与 win32c 项目对比

| 特性 | win32cpp (C++, MSVC) | win32cpp (C++, MinGW) | win32c (C, MSVC) | win32c (C, MinGW) |
|------|-------------------|-------------------|-------------------|-------------------|
| 语言 | C++20 | C++20 | C | C |
| exe 大小 | ~34 KB | ~121 KB | ~30 KB | ~121 KB |
| 编译器 | MSVC | GCC | MSVC | GCC |
| 传输模式 | CAN/UART | CAN/UART | CAN/UART | CAN/UART |
| 同步机制 | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION |
| 容器 | 固定数组 | 固定数组 | 固定数组 | 固定数组 |
| 内存管理 | new/delete | new/delete | malloc/free | malloc/free |
| 接口 | class 成员函数 | class 成员函数 | 不透明句柄 + 函数 | 不透明句柄 + 函数 |
| 代码风格 | 现代 C++ | 现代 C++ | 传统 C | 传统 C |

## 许可证

本项目仅供学习和参考使用。
