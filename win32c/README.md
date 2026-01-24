# CAN 固件升级工具 (C 语言版本)

纯 C 语言实现的 CAN 固件升级工具，用于通过 PCAN 接口升级板卡固件。

## 功能特性

- **CAN 设备连接管理**
  - 自动检测 PCAN-USB 设备（最多 16 个）
  - 支持多种波特率（10K - 1M）
  - 连接/断开设备管理

- **固件升级功能**
  - 读取 .bin 固件文件
  - 通过 CAN 总线发送固件数据
  - 实时进度显示
  - 支持测试模式（第二次重启后恢复原固件）

- **板卡命令**
  - 获取固件版本
  - 重启板卡

- **GUI 界面**
  - Windows 原生对话框界面
  - 支持中文界面（微软雅黑字体）
  - 实时日志显示
  - 进度条显示升级进度

## 目录结构

```
win32c/
├── build/              # 构建输出目录
├── include/            # 头文件
│   ├── can_manager.h   # CAN 管理器接口
│   ├── resource.h      # 资源 ID 定义
│   └── PCANBasic.h     # PCAN-Basic API
├── src/                # 源文件
│   ├── main.c          # 主程序和 GUI 逻辑
│   └── can_manager.c   # CAN 通信管理实现
├── resources/          # 资源文件
│   ├── resource.rc     # Windows 资源脚本
│   └── icon.ico        # 应用图标
├── libs/               # 外部库目录
│   └── x64/            # 64位库
│       └── PCANBasic.lib  # PCAN-Basic 静态库
├── CMakeLists.txt      # CMake 构建配置
├── CMakePresets.json   # CMake 预设配置
└── README.md           # 项目文档
```

## 编译环境要求

### 交叉编译环境（Linux 编译 Windows 程序）

```bash
# Ubuntu/Debian - 编译 Win32 GUI 程序
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 cmake ninja-build

# Arch Linux
sudo pacman -S mingw-w64-gcc cmake ninja
```

### 工具链要求

- **CMake**: >= 3.25
- **C 编译器**: x86_64-w64-mingw32-gcc
- **资源编译器**: x86_64-w64-mingw32-windres
- **生成器**: Ninja

## 编译步骤

### 一键编译（推荐）

```bash
cmake --workflow --preset release
```

该命令会自动执行以下步骤：
1. 配置项目（使用 default 预设）
2. 构建 Release 版本

### 手动分步编译

```bash
# 1. 配置项目
cmake --preset default

# 2. 构建 Release 版本
cmake --build build --config Release

# 3. 构建 Debug 版本（如需调试）
cmake --build build --config Debug
```

### 构建产物

编译完成后，可执行文件位于：
```
build/build/Release/can-upgrade.exe
```

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

## 二进制文件大小分析

### 体积优化效果

| 编译配置 | 可执行文件大小 | 说明 |
|---------|---------------|------|
| 默认编译 | ~180 KB | 无优化，包含调试符号 |
| 优化编译 | ~120 KB | 启用大小优化和 strip |
| C++ 版本 (wincu) | ~124 KB | 相同优化级别的 C++ 实现 |

### 体积来源分析

```
可执行文件主要组成部分：
├── 代码段 (.text)        ~80 KB   - 主程序逻辑和 CAN 通信
├── 数据段 (.data)        ~8 KB    - 全局变量和静态数据
├── 资源段 (.rsrc)        ~20 KB   - 图标、对话框等资源
├── 导入表 (.idata)       ~4 KB    - Windows API 导入表
└── 其它段                ~8 KB    - 重定位、调试信息等
```

### 大小优化建议

如需进一步减小体积，可考虑：

1. **UPX 压缩**（可减小至 60-80 KB）：
   ```bash
   upx --best --lzma can-upgrade.exe
   ```

2. **移除未使用的资源**：
   - 删除不需要的图标或对话框
   - 使用更小的图标文件

3. **使用 LTO（链接时优化）**：
   ```cmake
   target_compile_options(can-upgrade PRIVATE -flto)
   target_link_options(can-upgrade PRIVATE -flto)
   ```

4. **合并字符串**：
   - 提取重复字符串为常量
   - 使用更简洁的错误提示

## CAN 通信协议

### CAN ID 定义

| CAN ID | 方向 | 说明 |
|--------|------|------|
| 0x101 | PC → 板卡 | 平台命令 (PLATFORM_RX) |
| 0x102 | 板卡 → PC | 平台响应 (PLATFORM_TX) |
| 0x103 | PC → 板卡 | 固件数据 (FW_DATA_RX) |

### 板卡命令

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

## 与 wincu 项目对比

| 特性 | wincu (C++) | win32c (C) |
|------|-------------|-------------|
| 语言 | C++ | C |
| exe 大小 | ~124KB | ~120KB |
| 同步机制 | CRITICAL_SECTION | CRITICAL_SECTION |
| 容器 | 固定数组 | 固定数组 |
| 内存管理 | new/delete | malloc/free |
| 接口 | class 成员函数 | 不透明句柄 + 函数 |

## 许可证

本项目仅供学习和参考使用。
