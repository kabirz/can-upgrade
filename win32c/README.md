# 固件升级工具 (C 语言版本)

纯 C 语言实现的固件升级工具，支持 **CAN/UART 双总线**通信，用于通过 PCAN 接口或串口升级板卡固件。

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
win32c/
├── include/            # 头文件
│   ├── can_manager.h   # CAN 管理器接口
│   ├── uart_manager.h  # UART 管理器接口
│   ├── resource.h      # 资源 ID 定义
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # 源文件
│   ├── main.c          # 主程序和 GUI 逻辑
│   ├── can_manager.c   # CAN 通信管理实现
│   └── uart_manager.c  # UART 通信管理实现
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
- Visual Studio 2019 或更高版本（支持 CMake 默认检测）
- CMake（>= 3.25）

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
- MinGW-w64 交叉编译工具链

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
| 可执行文件大小 | ~30 KB | ~121 KB |
| 编译器 | MSVC | GCC |
| 链接方式 | 动态链接系统运行时 | 静态链接 libgcc |
| 调试支持 | 完美 | 较好 |

**说明**：MinGW 编译的文件较大主要是因为静态链接了 `libgcc`，这使程序可以在没有安装 MinGW 运行时的系统上直接运行。

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

### 编译优化选项

| 选项 | 说明 |
|------|------|
| `-Os` | 优化代码大小 |
| `-ffunction-sections` | 将函数放入独立段，便于链接时删除未使用代码 |
| `-fdata-sections` | 将数据放入独立段，便于链接时删除未使用数据 |
| `-mwindows` | Windows GUI 应用程序（无控制台窗口） |
| `-static-libgcc` | 静态链接 libgcc，减少运行时依赖 |
| `-s` / `--strip-all` | 去除符号表，减小文件体积 |
| `--gc-sections` | 删除未使用的段 |

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

```c
typedef struct {
    uint32_t code;   // 命令码/响应码（小端序）
    uint32_t val;    // 参数值（小端序）
} can_frame_t;
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

## 与 win32cpp 项目对比

| 特性 | win32cpp (C++, MSVC) | win32cpp (C++, MinGW) | win32c (C, MinGW) | win32c (C, MSVC) |
|------|-------------------|-------------------|-------------------|-------------------|
| 语言 | C++ | C++ | C | C |
| exe 大小 | ~34KB | ~121KB | ~121KB | ~30KB |
| 编译器 | MSVC | GCC | GCC | MSVC |
| 传输模式 | CAN/UART | CAN/UART | CAN/UART | CAN/UART |
| 同步机制 | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION | CRITICAL_SECTION |
| 容器 | 固定数组 | 固定数组 | 固定数组 | 固定数组 |
| 内存管理 | new/delete | new/delete | malloc/free | malloc/free |
| 接口 | class 成员函数 | class 成员函数 | 不透明句柄 + 函数 | 不透明句柄 + 函数 |

## 许可证

本项目仅供学习和参考使用。
