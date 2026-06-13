# can-win32c

Windows 原生 Win32 API + 纯 C 实现的 CAN 固件升级工具，通过 PEAK PCAN 接口与板卡通信。

本文档描述本工具使用的 **CAN 应用层协议**（帧定义、命令、交互流程），不涉及 CAN 总线本身的规范。

## 构建

```bash
cmake -B out
cmake --build out --config Release
```

运行 `out/Release/can-upgrade.exe`（需已安装 [PCAN-Basic](https://www.peak-system.com/PCAN-Basic.535.0.html) 驱动）。

## CAN 应用层协议

### 帧 ID

| CAN ID | 名称 | 方向 | 载荷 | 用途 |
|--------|------|------|------|------|
| 0x101 | PLATFORM_RX | PC → 板卡 | `code + val` | 下发命令（版本查询 / 重启 / 开始升级 / 确认） |
| 0x102 | PLATFORM_TX | 板卡 → PC | `code + val` | 返回命令应答 |
| 0x103 | FW_DATA_RX | PC → 板卡 | 原始固件字节 | 传输固件数据（无 `code + val` 封装） |

### 帧载荷结构

`0x101` / `0x102` 帧的 8 字节载荷为如下结构（小端序）：

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0–3 | `code` | uint32 | 命令码（发送方向）或响应码（接收方向） |
| 4–7 | `val`  | uint32 | 参数 / 返回值，含义随 `code` 变化 |

> `0x103` 数据帧的载荷是固件文件的原始字节，每帧 8 字节（最后一帧可不足 8 字节），不经过 `code + val` 封装。

### 命令码（`code` 字段，PC → 板卡）

| 码值 | 名称 | `val` 含义 | 说明 |
|------|------|-----------|------|
| 0 | BOARD_START_UPDATE | 固件大小（字节数） | 开始升级并擦除 Flash |
| 1 | BOARD_CONFIRM | 0 = 测试模式，1 = 正式烧录 | 传输完成后确认是否固化 |
| 2 | BOARD_VERSION | 0（保留） | 查询当前固件版本 |
| 3 | BOARD_REBOOT | 0（保留） | 远程重启板卡 |

### 响应码（`code` 字段，板卡 → PC）

| 码值 | 名称 | `val` 含义 | 说明 |
|------|------|-----------|------|
| 0 | FW_CODE_OFFSET | 已接收字节数 | 数据接收正常，返回期望的下一偏移 |
| 1 | FW_CODE_UPDATE_SUCCESS | 固件总字节数 | 全部数据接收完成 |
| 2 | FW_CODE_VERSION | 固件版本号 | 版本查询应答（编码见下） |
| 3 | FW_CODE_CONFIRM | 0x55AA55AA 表示成功 | 确认（固化）结果 |
| 4 | FW_CODE_FLASH_ERROR | — | Flash 操作错误 |
| 5 | FW_CODE_TRANFER_ERROR | — | 传输 / 校验错误 |

### 固件版本号编码

`FW_CODE_VERSION` 返回的 `val`（uint32）按字节拆分：

| 比特位 | 字节 | 含义 |
|-------|------|------|
| 31–24 | byte3 | 主版本 major |
| 23–16 | byte2 | 次版本 minor |
| 15–8  | byte1 | 修订号 patch |
| 7–0   | byte0 | 保留（未解析） |

显示格式 `v{major}.{minor}.{patch}`，例如 `0x01020300` → `v1.2.3`。

### 交互流程

#### 版本查询

| 步骤 | 方向 | 帧 ID | code | val |
|------|------|-------|------|-----|
| 1 | PC → 板卡 | 0x101 | BOARD_VERSION (2) | 0 |
| 2 | 板卡 → PC | 0x102 | FW_CODE_VERSION (2) | 版本号 |

#### 设备重启

| 步骤 | 方向 | 帧 ID | code | val |
|------|------|-------|------|-----|
| 1 | PC → 板卡 | 0x101 | BOARD_REBOOT (3) | 0 |

单向命令，不等待应答。

#### 固件升级

| 步骤 | 方向 | 帧 ID | code / 数据 | val | 超时 |
|------|------|-------|------------|-----|------|
| 1 | PC → 板卡 | 0x101 | BOARD_START_UPDATE (0) | 固件大小 | — |
| 2 | 板卡 → PC | 0x102 | FW_CODE_OFFSET (0) | 0（擦除完成） | 15 s |
| 3 | PC → 板卡 | 0x103 | 固件原始数据，8 B/帧 | — | — |
| 4 | 板卡 → PC | 0x102 | FW_CODE_OFFSET (0) 或 FW_CODE_UPDATE_SUCCESS (1) | 已接收字节数 | 5 s |
| 5 | PC → 板卡 | 0x101 | BOARD_CONFIRM (1) | 0 = 测试 / 1 = 正式 | — |
| 6 | 板卡 → PC | 0x102 | FW_CODE_CONFIRM (3) | 0x55AA55AA = 成功 | 30 s |

步骤 3–4 为流控循环：每发送 64 字节（8 帧）等待一次应答，板卡以 `FW_CODE_OFFSET` 返回已接收字节数；当收到 `FW_CODE_UPDATE_SUCCESS` 且 `val` 等于固件总大小时，接收完成，进入确认阶段。

任何一步超时，或收到 `FW_CODE_FLASH_ERROR` / `FW_CODE_TRANFER_ERROR`，本次升级即判定失败。
