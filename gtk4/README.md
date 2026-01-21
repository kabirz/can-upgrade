# CAN 固件升级工具 (GTK4)

基于 GTK4 的 CAN 固件升级图形化工具，支持通过 SocketCAN 进行固件升级、版本查询和设备重启。

## 功能特性

- **CAN 设备管理**
  - 自动扫描系统中可用的 CAN 设备
  - 手动刷新设备列表
  - 连接/断开 CAN 设备

- **固件升级**
  - 支持 `.bin` 格式固件文件
  - 实时显示升级进度
  - 测试模式：第二次重启后固件恢复成之前的固件

- **设备控制**
  - 获取当前固件版本
  - 远程重启设备

- **日志显示**
  - 实时显示操作日志
  - 连接状态显示

## 依赖

- GTK4
- CMake 3.20+
- GCC (支持 C11)
- Linux SocketCAN (内核自带)

## 编译

```bash
mkdir build && cd build
cmake ..
make
```

## 安装

```bash
sudo make install
```

## 运行

```bash
./can-upgrade-gtk4
```

如果需要设置 CAN 接口波特率或其他操作，可能需要 root 权限：

```bash
sudo ./can-upgrade-gtk4
```

## 使用说明

### 1. 配置 CAN 设备

如果系统中没有 CAN 设备，可以创建虚拟 CAN 设备进行测试：

```bash
# 加载虚拟 CAN 模块
sudo modprobe vcan

# 创建虚拟 CAN 设备
sudo ip link add dev vcan0 type vcan

# 启动设备
sudo ip link set up vcan0
```

### 2. 连接设备

1. 在程序启动后会自动扫描可用的 CAN 设备
2. 如果没有找到设备，点击"刷新"按钮重新扫描
3. 从下拉列表中选择要连接的设备
4. 点击"连接"按钮
5. 连接成功后，按钮会变成"断开"，状态显示"已连接"

### 3. 固件升级

1. 点击"浏览..."按钮选择固件文件（.bin 格式）
2. 如需测试固件，勾选"测试模式"
3. 点击"开始升级"按钮
4. 等待升级完成，查看日志和进度条
5. 升级完成后，需要重启设备使固件生效

### 4. 获取版本

点击"获取版本"按钮，会在日志中显示当前固件版本号。

### 5. 重启设备

点击"重启板子"按钮，会发送重启命令给设备。

## CAN 协议

```
CAN ID 定义:
- PLATFORM_RX: 0x101
- PLATFORM_TX: 0x102
- FW_DATA_RX:  0x103

固件命令:
- BOARD_START_UPDATE: 0  (开始升级)
- BOARD_CONFIRM:       1  (确认升级)
- BOARD_VERSION:       2  (获取版本)
- BOARD_REBOOT:        3  (重启设备)

响应码:
- FW_CODE_OFFSET:           0  (偏移确认)
- FW_CODE_UPDATE_SUCCESS:   1  (更新成功)
- FW_CODE_VERSION:          2  (版本信息)
- FW_CODE_CONFIRM:          3  (确认完成)
- FW_CODE_FLASH_ERROR:      4  (Flash 错误)
- FW_CODE_TRANFER_ERROR:    5  (传输错误)
```

## 项目结构

```
/
├── CMakeLists.txt       # CMake 构建配置
├── README.md            # 本文件
├── can_upgrade.py       # Python 版本参考代码
└── src/
    ├── main.c           # 主程序入口
    ├── window.h         # 窗口头文件
    ├── window.c         # GTK4 窗口实现
    ├── can_socket.h     # CAN socket 封装头文件
    └── can_socket.c     # CAN socket 封装实现
```

## 注意事项

1. 需要 root 权限才能访问 CAN 设备（取决于设备配置）
2. 确保选择的 CAN 设备已正确配置并启动
3. 固件升级过程中请勿断开连接或关闭程序
4. 测试模式下的固件在第二次重启后会恢复

