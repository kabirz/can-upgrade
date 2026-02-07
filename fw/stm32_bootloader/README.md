# STM32F103RCTx Bootloader

基于STM32F103RCTx的Bootloader，支持CAN总线固件升级，采用分层架构设计，便于扩展其他传输方式（如UART）。

## 功能特性

- **双入口升级模式**
  - 硬件：按键触发（Key1按下）
  - 软件：应用固件设置升级标志

- **CAN总线升级**
  - 波特率：250kbps
  - 支持固件分包传输
  - 自动Flash擦除优化（按实际大小擦除）
  - 升级完成自动跳转应用

- **安全机制**
  - 固件校验（栈指针和复位向量范围检查）
  - Flash写入错误检测
  - CAN环形缓冲区处理高速消息

## 硬件资源

| 外设 | 引脚 | 功能 |
|------|------|------|
| Key1 | PA13 | 进入升级模式按键（低电平有效） |
| LED1 | PC11 | 升级模式指示灯 |
| LED2 | PC12 | 错误指示灯 |
| CAN_RX | PA11 | CAN接收 |
| CAN_TX | PA12 | CAN发送 |
| UART1 | PA9/PA10 | 日志输出（115200bps） |

## Flash地址划分

| 地址范围 | 大小 | 用途 |
|----------|------|------|
| 0x08000000 - 0x0800FFFF | 64KB | Bootloader |
| 0x08010000 - 0x0803FFFF | 192KB | 应用固件 |
| 0x0803F800 | 2KB | 升级标志区域 |

## 项目架构

```
.
├── Core/
│   ├── Inc/
│   │   ├── fw_upgrade.h    # 固件升级框架头文件
│   │   ├── fw_can.h        # CAN传输层头文件
│   │   └── main.h          # 主程序头文件
│   └── Src/
│       ├── fw_upgrade.c    # 固件升级核心逻辑
│       ├── fw_can.c        # CAN传输层实现
│       ├── main.c          # 主程序
│       ├── can.c           # CAN外设配置
│       ├── usart.c         # UART日志输出
│       └── stm32f1xx_it.c  # 中断处理
├── build/                  # 编译输出
└── CMakeLists.txt          # 构建配置
```

## 架构设计

### 分层架构

```
┌─────────────────────────────────────────┐
│              main.c                     │
│  - 系统初始化                            │
│  - 检测升级入口                          │
│  - 主循环调度                            │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│           fw_upgrade.c                  │
│  - 传输层抽象接口                        │
│  - Flash操作（擦除/写入/读取）           │
│  - 固件验证、应用跳转                    │
│  - 命令处理                              │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│      fw_transport_t 传输层接口          │
│  - init()              初始化           │
│  - process_rx_data()   处理接收         │
│  - send_response()     发送响应         │
│  - wait_tx_complete()  等待发送完成     │
└─────────────────┬───────────────────────┘
                  │
         ┌────────┴────────┐
         ▼                 ▼
┌──────────────┐    ┌──────────────┐
│   fw_can.c   │    │  fw_uart.c   │
│  CAN传输层   │    │  UART传输层  │
│  (已实现)    │    │   (预留)     │
└──────────────┘    └──────────────┘
```

## CAN协议

### 消息格式

| CAN ID | 方向 | 说明 |
|--------|------|------|
| 0x101 | 上位机→Bootloader | 命令通道 |
| 0x102 | Bootloader→上位机 | 响应通道 |
| 0x103 | 上位机→Bootloader | 固件数据通道 |

### 命令定义

| 命令码 | 名称 | 参数 | 说明 |
|--------|------|------|------|
| 0 | START_UPDATE | 固件大小 | 开始升级，擦除Flash |
| 1 | CONFIRM | 1=启动应用 | 确认升级，验证并跳转 |
| 2 | VERSION | - | 获取Bootloader版本 |
| 3 | REBOOT | - | 重启系统 |

### 响应定义

| 响应码 | 名称 | 说明 |
|--------|------|------|
| 0 | OFFSET | 当前接收偏移量 |
| 1 | UPDATE_SUCCESS | 升级成功 |
| 2 | VERSION | 版本号响应 |
| 3 | CONFIRM | 确认响应 |
| 4 | FLASH_ERROR | Flash错误 |
| 5 | TRANSFER_ERROR | 传输错误 |

### 升级流程

```
上位机                      Bootloader
  │                             │
  ├──── 0x101: START_UPDATE ───→│  擦除Flash
  │←──── 0x102: OFFSET(0) ──────┤
  │                             │
  ├──── 0x103: 固件数据 ───────→│  写入Flash
  │←──── 0x102: OFFSET(64) ─────┤
  ├──── 0x103: 固件数据 ───────→│  写入Flash
  │←──── 0x102: OFFSET(128) ────┤
  │          ...                │
  │←──── 0x102: UPDATE_SUCCESS ─┤
  │                             │
  ├──── 0x101: CONFIRM(1) ────→│  验证固件
  │←──── 0x102: CONFIRM ────────┤  跳转应用
  │                             │
```

## 编译

```bash
# 配置项目（首次）
cmake --preset Release

# 编译
cmake --build --preset Release

# 输出文件
# build/Release/stm32_bootloader.elf
```

## 使用说明

### 1. 进入升级模式

**方式一：按键触发**
- 上电时按下Key1按键（PA13，低电平有效）
- LED1点亮表示进入升级模式

**方式二：应用固件触发**
- 应用固件向地址`0x0803F800`写入`0x55AADEAD`
- 重启后Bootloader检测到标志，清除标志并进入升级模式
- zephyr使用flash的shell命令行： `flash write <flash_dev> <offset> 0x55aadead`

### 2. CAN升级工具

使用本仓管编译出来的can工具


### 3. 串口日志

连接串口（115200, 8N1）查看升级日志：

```
========================================
  STM32 Bootloader v1.0.0
========================================
Flash: 08010000 - 08040000
App Start: 08010000
[BOOT] Key1 pressed, enter upgrade mode
[FW_CAN] CAN transport ready (LED1 ON)
[CMD] Processing: cmd=0, param=00004000
[FLASH] Erasing: 16384 bytes (16 pages)
[FLASH] Erase success
[FLASH] Progress: 4096/16384 bytes
[FLASH] Progress: 8192/16384 bytes
[FLASH] Progress: 16384/16384 bytes
[FLASH] Firmware write complete!
[CMD] Confirm upgrade: param=1
[CMD] Firmware verified OK, jumping to app...
```

## 应用固件要求

### 链接脚本修改

应用固件需要修改Flash起始地址：

```ld
/* STM32F103RCTx: 256KB Flash, 48KB RAM */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 192K
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 48K
}
```
在zephyr中直接修改对应board的设备树文件

在flash0下修改

`reg = < 0x8000000 0x40000 >;`

为

`reg = < 0x8010000 0x30000 >;`

bootloader的大小为64k，这里根据需求自行确定，本项目32k足够，有些flash最小的block为128k就只能设置为128k，然后删除输出文件目录重新编译即可

### 触发升级示例

```c
/* 应用固件中触发升级 */
void TriggerUpgrade(void)
{
    uint32_t flag = 0x55AADEAD;
    HAL_FLASH_Unlock();
    // 写入升级标志（注意：实际需要擦除页后写入）
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, 0x0803F800, flag);
    HAL_FLASH_Lock();
    NVIC_SystemReset();
}
```

## 扩展UART传输层

由于采用分层架构，添加UART支持只需实现传输层接口：

### 1. 创建 fw_uart.h

```c
typedef struct {
    uint8_t data[256];
    uint16_t head;
    uint16_t tail;
} fw_uart_ring_t;

void FW_UART_Init(void);
void FW_UART_ProcessRxData(void);
void FW_UART_SendResponse(uint32_t code, uint32_t value);
void FW_UART_WaitTxComplete(void);
```

### 2. 创建 fw_uart.c

实现上述函数，使用HAL_UART接收和发送。

### 3. 在main.c中添加

```c
static const fw_transport_t uart_transport = {
    .name = "UART",
    .init = FW_UART_Init,
    .process_rx_data = FW_UART_ProcessRxData,
    .send_response = FW_UART_SendResponse,
    .wait_tx_complete = FW_UART_WaitTxComplete
};

// 选择传输方式
FW_Upgrade_Init(&uart_transport);  // 或 &can_transport
```

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0.0 | 2026-02 | 初始版本，支持CAN升级 |

## 许可证

本项目基于STM32 HAL库开发，遵循相应的许可证条款。

## 作者

Bootloader Development Team
