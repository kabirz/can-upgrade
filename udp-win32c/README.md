# UDP 固件升级工具 (C 语言版本)

纯 C 语言实现的固件升级工具，通过 **UDP** 协议（配置端口 9200）升级板卡固件。

参考 `win32c`（CAN/UART 版本）的程序结构，通信层替换为 UDP。

## 功能特性

- **UDP 连接管理**
  - 目标 IP 留空 → 多网卡子网定向广播，自动发现固件
  - 指定目标 IP → 单播直连
  - 本地端口可配（默认 9201，监听固件回复）
  - 远程端口可配（默认 9200，固件配置端口）

- **固件升级**
  - 读取 .bin 固件文件
  - 实时进度条和百分比显示（0% - 100%）
  - 支持测试模式（重启后恢复原固件）
  - 分帧传输（每包 256B），offset 校验保证可靠性
  - CRC16-CCITT 端到端校验（与 Zephyr 固件对齐）

- **板卡命令**
  - 获取固件版本
  - 重启板卡（升级完成后触发新固件生效）

- **GUI 界面**
  - Windows 原生对话框界面
  - 支持中文界面（微软雅黑字体）
  - 实时日志显示（带时间戳）

## 通信协议

### 端口规则

| 端口 | 上位机 | 固件 |
|------|--------|------|
| 配置端口 (9200) | 发送命令 → 监听 9201 | 监听 9200 → 回复 9201 |

### 配置端口命令（配置端口 9200，帧 `[cmd 1B][data...]`）

**库内命令（0x01-0x05，由 udp_fw_upgrade 库处理）**：

| 命令 | 码 | 上位机发送 | 固件回复 | 说明 |
|------|----|-----------|---------|------|
| FW_START | 0x01 | `[0x01][size 4B LE]` | `[0x01][1/0]` | 保存 size + 擦 slot1 |
| FW_DATA | 0x02 | `[0x02][data 256B]` | `[0x02][offset 4B LE]` | 写 flash，回复累计字节数 |
| FW_END | 0x03 | `[0x03][test 1B][crc16 2B LE]` | `[0x03][1/0]` | flush + CRC 校验 + boot_request_upgrade |
| GET_VERSION | 0x04 | `[0x04]` | `[0x04][version string]` | 查询固件版本 |
| REBOOT | 0x05 | `[0x05]` | `[0x05]` | 重启设备 |

**应用业务命令（0x10+，由应用回调处理）**：

| 命令 | 码 | 说明 |
|------|----|------|
| SET_IP | 0x10 | 设置 IP |
| SET_MASK | 0x11 | 设置掩码 |
| SET_GW | 0x12 | 设置网关 |
| SET_PORT | 0x13 | 设置数据端口 |
| GET_CONFIG | 0x14 | 查询配置 |
| SET_RF24_CH | 0x15 | 设置 RF24 信道 |
| SET_RF24_ADDR | 0x16 | 设置 RF24 地址 |

- **test_mode**: 0=永久升级, 1=临时升级（重启后回滚）
- **CRC16**: Zephyr 特化的 bit-reflected CCITT 变体（非标准 MSB-first）
- 固件 FW_END 后不自动 reboot，由上位机重启按钮（REBOOT 0x05）触发

## 目录结构

```
udp-win32c/
├── include/            # 头文件
│   ├── resource.h      # 资源 ID 定义
│   └── udp_manager.h   # UDP 管理器接口 (移植自 gateway-tool)
├── src/                # 源文件
│   ├── main.c          # 主程序和 GUI 逻辑
│   └── udp_manager.c   # UDP 通信管理实现 (移植自 gateway-tool)
├── resources/          # 资源文件
│   ├── resource.rc     # Windows 资源脚本
│   └── icon.ico        # 应用图标
├── CMakeLists.txt      # CMake 构建配置
├── CMakePresets.json   # CMake 预设配置 (VS / MinGW)
└── README.md
```

## 编译

### 方式一：Windows 原生编译（Visual Studio）

**环境要求**：
- Windows 10/11
- Visual Studio 2019 或更高（支持 CMake 默认检测）
- CMake（>= 3.25）

**一键编译**：
```powershell
cmake --workflow --preset vs-release
```

**手动编译**：
```powershell
cmake --preset vs
cmake --build out --config Release
```

**编译产物**：
```
out/Release/udp-upgrade.exe
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
cmake --preset default
cmake --build build --config Release
```

**编译产物**：
```
build/udp-upgrade.exe
```

## 系统库依赖

| 库 | 用途 |
|----|------|
| comctl32 | 通用控件库（进度条） |
| comdlg32 | 通用对话框库（文件选择） |
| gdi32 | GDI 图形设备接口 |
| ws2_32 | Winsock2（UDP 通信） |
| iphlpapi | 网卡枚举（多网卡广播） |

## 使用方法

1. 启动程序
2. **目标 IP** 留空（广播自动发现）或填入固件具体 IP
3. **本地端口** 保持默认 9201
4. 点击「连接」
5. 「浏览...」选择固件 .bin 文件
6. 按需勾选「测试模式」
7. 点击「开始升级」，等待进度条到 100%
8. 升级成功后点击「重启板卡」使新固件生效

## 与 win32c 的区别

| 项 | win32c | udp-win32c |
|----|--------|------------|
| 通信层 | PCAN CAN + UART | UDP (Winsock2) |
| 连接设置 | 设备列表 + 波特率 | 目标 IP + 本地端口 |
| 外部库 | PCANBasic.lib + setupapi | ws2_32 + iphlpapi |
| 固件分帧 | 8B/帧 (CAN) | 256B/帧 (UDP) |
| 界面结构 | — | 照搬 |

## 许可证

本项目仅供学习和参考使用。
