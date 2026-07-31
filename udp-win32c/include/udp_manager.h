#ifndef UDP_MANAGER_H
#define UDP_MANAGER_H

/* winsock2 必须在 windows.h 之前 include, 否则 windows.h 会拉入 winsock1
 * 与 iphlpapi 的现代类型 (IP_ADAPTER_ADDRESSES 等) 冲突 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

/* UDP 配置命令格式: [cmd 1B][data...] (无魔数头, 走配置端口)
 * 与旧版不同: 不再有 0xAA 0x55 魔数头, 通道已分离 */
#define GATEWAY_DATA_PORT_DEFAULT   9090  /* 数据端口默认 (可配, 固件持久化) */
#define GATEWAY_CONFIG_PORT         9200  /* 配置端口 (固件固定, 不可改) */

/* UDP 命令码 (命令帧首字节, 走配置端口 9200) */
enum udp_cmd {
	UDP_CMD_SET_IP      = 0x01,
	UDP_CMD_SET_MASK    = 0x02,
	UDP_CMD_SET_GW      = 0x03,
	UDP_CMD_SET_PORT    = 0x04,
	UDP_CMD_GET_CONFIG  = 0x05,
	UDP_CMD_GET_VERSION = 0x06,
	UDP_CMD_SET_RF24_CH = 0x07,
	UDP_CMD_SET_RF24_ADDR = 0x08,
	UDP_CMD_REBOOT      = 0x09,
	UDP_CMD_SET_REMOTE_PORT = 0x0a,
	UDP_CMD_FW_START    = 0x10,
	UDP_CMD_FW_DATA     = 0x11,
	UDP_CMD_FW_END      = 0x12,
};

/* 通道类型: 一个 UdpManager 实例对应一个通道 (单 socket).
 * 配置通道收发命令 [cmd][data...], 数据通道收发数据帧 [frame_id 2B BE][payload].
 * 由调用方在 Bind 时指定, RX 线程据此决定走命令响应回调还是数据回调 */
typedef enum {
	UDP_CHAN_CONFIG,   /* 配置通道: 命令收发, msg_cb 上报响应, data_cb 不用 */
	UDP_CHAN_DATA,     /* 数据通道: 数据帧收发, data_cb 上报, msg_cb 不用 */
} UdpChannel;

/* 无线接收器配置 (UDP_CMD_GET_CONFIG 响应解析结果).
 * 响应格式按长度自动识别:
 *   22 字节 (新 gateway): [rf24_ch 1B][rf24_addr 5B][data_port 2B][config_port 2B][ip 4B][mask 4B][gw 4B]
 *   10 字节 (旧 gateway): [rf24_ch 1B][rf24_addr 5B][data_port 2B][config_port 2B]
 *    6 字节 (net_test):   [local_port 2B][remote_port 2B][config_port 2B] */
typedef struct {
	uint8_t rf24_channel;
	uint8_t rf24_addr[5];
	uint16_t data_port;        /* 本地数据端口 (固件 bind) */
	uint16_t remote_data_port; /* 远程数据端口 (固件发送目标, net_test 专属) */
	uint16_t config_port;      /* 配置端口 (恒为 9200) */
	char ip[16];               /* 固件 IP (点分十进制, 仅 22B 响应有效) */
	char netmask[16];          /* 固件掩码 */
	char gateway[16];          /* 固件网关 */
	uint8_t has_net;           /* 响应是否含 IP/掩码/网关 (22B 格式) */
} GatewayConfig;

/* 回调类型: 状态消息 / 透传数据 */
typedef void (*udp_msg_callback)(const char *msg, void *user_data);
typedef void (*udp_data_callback)(const uint8_t *data, size_t len, void *user_data);

/* UDP 管理器 (不透明指针, 单 socket, 单通道) */
typedef struct UdpManager UdpManager;

/* 生命周期 */
UdpManager *UdpManager_Create(void);
void UdpManager_Destroy(UdpManager *mgr);

/* 连接/断开: 创建 socket 绑定本机 0.0.0.0:local_port, 设广播 + REUSEADDR.
 * chan 决定本实例是配置还是数据通道 (仅影响 RX 分发逻辑).
 * 远程目标 = remote_ip:remote_port: remote_ip 为 NULL/空/0.0.0.0/非法 → 广播自动发现,
 * 否则单播到该 IP. (收到对端包后 RX 线程会自动学习并更新 remote_addr) */
bool UdpManager_Bind(UdpManager *mgr, UdpChannel chan,
                     uint16_t local_port, const char *remote_ip, uint16_t remote_port);
void UdpManager_Unbind(UdpManager *mgr);
bool UdpManager_IsBound(UdpManager *mgr);

/* 数据发送 (原始字节 / 协议命令帧).
 * SendData 发原始字节到 remote_addr (数据帧 [frame_id 2B BE][payload]).
 * SendCommand 发 [cmd][data...] (无魔数头). 两者走同一个 socket, 由调用方
 * 保证配置实例只发命令、数据实例只发数据. */
bool UdpManager_SendData(UdpManager *mgr, const uint8_t *data, size_t len);
bool UdpManager_SendCommand(UdpManager *mgr, uint8_t cmd, const uint8_t *data, uint8_t len);

/* 无线接收器配置 (封装为 UDP 命令发送, 走配置实例) */
bool UdpManager_SetIP(UdpManager *mgr, const char *ip);
bool UdpManager_SetMask(UdpManager *mgr, const char *mask);
bool UdpManager_SetGateway(UdpManager *mgr, const char *gw);
bool UdpManager_SetPort(UdpManager *mgr, uint16_t port);
bool UdpManager_SetRemotePort(UdpManager *mgr, uint16_t port);
bool UdpManager_SetRF24Channel(UdpManager *mgr, uint8_t channel);
bool UdpManager_SetRF24Addr(UdpManager *mgr, const uint8_t *addr);

/* 查询配置 (GET_CONFIG): 发请求并同步等待响应, 成功填充 *config.
 * 超时 ~500ms. 返回 true 表示收到并解析成功. */
bool UdpManager_GetConfig(UdpManager *mgr, GatewayConfig *config);

/* 查询版本 (GET_VERSION): 同步等待, 填入 version 字符串 (NUL 终止).
 * buf_len 为 buf 容量. 返回 true 表示成功. */
bool UdpManager_GetVersion(UdpManager *mgr, char *buf, size_t buf_len);

bool UdpManager_Reboot(UdpManager *mgr);

/* CRC16-CCITT (poly 0x1021, init 0x0000), 与固件 crc16_ccitt 对齐 */
uint16_t UdpManager_CRC16_CCITT(const uint8_t *data, size_t len);

/* 固件升级 (新协议, 配置端口 9200):
 *   Start: 发 [0x10][size 4B LE], 固件回 [0x10][1/0], 成功返回 true
 *   Data:  发 [0x11][data 256B], 固件回 [0x11][offset 4B LE], 校验 offset==*got_offset
 *   End:   发 [0x12][test_mode 1B][crc16 2B LE], 固件回 [0x12][1/0]
 * test_mode: 0=永久, 1=临时. 失败返回 false. */
bool UdpManager_FirmwareStart(UdpManager *mgr, uint32_t size);
bool UdpManager_FirmwareData(UdpManager *mgr, const uint8_t *data, size_t len,
			     uint32_t expected_offset, uint32_t *got_offset);
bool UdpManager_FirmwareEnd(UdpManager *mgr, uint8_t test_mode, uint16_t crc16);

/* 回调设置 */
void UdpManager_SetMsgCallback(UdpManager *mgr, udp_msg_callback cb, void *data);
void UdpManager_SetDataCallback(UdpManager *mgr, udp_data_callback cb, void *data);

/* 接收线程 (后台 recvfrom, 按 chan 分发命令响应/数据帧) */
void UdpManager_StartRxThread(UdpManager *mgr);
void UdpManager_StopRxThread(UdpManager *mgr);

#endif /* UDP_MANAGER_H */
