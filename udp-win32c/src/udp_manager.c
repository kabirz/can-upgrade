#include "udp_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

/* 同步等待响应的超时 (GET_CONFIG / GET_VERSION) */
#define UDP_RESP_TIMEOUT_MS 500

/* ================================================================
 * 子网定向广播: Windows 发 255.255.255.255 (有限广播) 时, 多网卡主机路由表
 * 无法决定从哪个接口发出 → 包被丢弃. 改用各网卡的子网定向广播 (如
 * 192.168.1.255), 遍历所有非回环网卡逐个发出, 确保板子无论连哪个网卡都能收到.
 * ================================================================ */

/* 收集本机所有非回环网卡的子网定向广播地址. 返回填充数量. */
static int collect_broadcast_addrs(unsigned long *addrs, int max_cnt)
{
	/* 先用 GetIpAddrTable 取 IP; 掩码用 IP_ADAPTER_ADDRESSES 取.
	 * 这里用一次 GetAdaptersAddresses 同时拿 IP 和 IPv4Mask */
	ULONG bufLen = 15000;
	PIP_ADAPTER_ADDRESSES pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
	int cnt = 0;

	if (pAddrs == NULL) {
		return 0;
	}

	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		      GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_PREFIX;

	if (GetAdaptersAddresses(AF_INET, flags, NULL, pAddrs, &bufLen) != NO_ERROR) {
		free(pAddrs);
		return 0;
	}

	PIP_ADAPTER_ADDRESSES p = pAddrs;
	while (p && cnt < max_cnt) {
		/* 跳过未启用的适配器 */
		if (p->OperStatus != IfOperStatusUp) {
			p = p->Next;
			continue;
		}
		/* 跳过回环和虚拟隧道 (常见 VPN/虚拟网卡) */
		if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
		    p->IfType == IF_TYPE_TUNNEL) {
			p = p->Next;
			continue;
		}

		PIP_ADAPTER_UNICAST_ADDRESS ua = p->FirstUnicastAddress;
		while (ua && cnt < max_cnt) {
			struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
			unsigned long ip = sa->sin_addr.s_addr;
			ULONG plen;
			unsigned long mask, bcast;

			/* 跳过回环 (127.x), 未配置 (0.x), link-local (169.254.x) */
			if ((ip & htonl(0xFF000000)) == htonl(0x7F000000) ||
			    (ip & htonl(0xFFFF0000)) == htonl(0xA9FE0000) ||
			    ip == 0) {
				ua = ua->Next;
				continue;
			}

			/* OnLinkPrefixLength = IPv4 前缀长度 (Win Vista+).
			 * 定向广播 = (ip & mask) | ~mask */
			plen = ua->OnLinkPrefixLength;
			mask = (plen == 0) ? 0 : htonl(0xFFFFFFFF << (32 - plen));
			bcast = (ip & mask) | ~mask;

			addrs[cnt++] = bcast;
			ua = ua->Next;
		}
		p = p->Next;
	}

	free(pAddrs);
	return cnt;
}

/* 判断给定 IP (网络序 s_addr) 是否与本机某个已启用网卡在同一子网.
 * 用于 RX 学习对端地址前验证: 跨子网时不切单播 (单播发不过去, 保持广播). */
static bool is_local_subnet(unsigned long ip)
{
	ULONG bufLen = 15000;
	PIP_ADAPTER_ADDRESSES pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
	bool same = false;

	if (pAddrs == NULL) {
		return false;
	}

	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		      GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_PREFIX;

	if (GetAdaptersAddresses(AF_INET, flags, NULL, pAddrs, &bufLen) != NO_ERROR) {
		free(pAddrs);
		return false;
	}

	for (PIP_ADAPTER_ADDRESSES p = pAddrs; p && !same; p = p->Next) {
		if (p->OperStatus != IfOperStatusUp ||
		    p->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
		    p->IfType == IF_TYPE_TUNNEL) {
			continue;
		}
		for (PIP_ADAPTER_UNICAST_ADDRESS ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
			struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
			unsigned long localip = sa->sin_addr.s_addr;

			if ((localip & htonl(0xFF000000)) == htonl(0x7F000000) ||
			    (localip & htonl(0xFFFF0000)) == htonl(0xA9FE0000) ||
			    localip == 0) {
				continue;
			}
			ULONG plen = ua->OnLinkPrefixLength;
			if (plen == 0 || plen >= 32) {
				continue;
			}
			unsigned long mask = htonl(0xFFFFFFFF << (32 - plen));
			if ((ip & mask) == (localip & mask)) {
				same = true;
				break;
			}
		}
	}

	free(pAddrs);
	return same;
}


struct UdpManager {
	SOCKET sock;
	UdpChannel chan;                 /* 本实例通道类型 (RX 分发用) */
	struct sockaddr_in remote_addr;
	bool bound;
	bool broadcast_mode;             /* 目标 IP 空 = 广播: 发送时遍历所有网卡广播地址 */

	/* 广播地址列表 (broadcast_mode 下发送时遍历). 单播模式下为空, 用 remote_addr */
	unsigned long bcast_addrs[8];
	int bcast_cnt;

	/* 同步等待响应的状态 (仅配置通道使用) */
	volatile uint8_t pending_cmd;    /* 正在等待的命令码, 0 = 无 */
	uint8_t resp_buf[300];
	size_t resp_len;
	HANDLE resp_event;

	udp_msg_callback msg_cb;
	void *msg_data;
	udp_data_callback data_cb;
	void *data_data;

	HANDLE rx_thread;
	volatile bool rx_running;
};

UdpManager *UdpManager_Create(void)
{
	UdpManager *mgr = (UdpManager *)calloc(1, sizeof(UdpManager));
	if (mgr) {
		mgr->sock = INVALID_SOCKET;
		mgr->resp_event = CreateEvent(NULL, TRUE, FALSE, NULL);
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
	}
	return mgr;
}

void UdpManager_Destroy(UdpManager *mgr)
{
	if (!mgr) return;
	UdpManager_Unbind(mgr);
	if (mgr->resp_event) CloseHandle(mgr->resp_event);
	WSACleanup();
	free(mgr);
}

bool UdpManager_Bind(UdpManager *mgr, UdpChannel chan,
                     uint16_t local_port, const char *remote_ip, uint16_t remote_port)
{
	if (!mgr) return false;

	if (mgr->sock != INVALID_SOCKET) {
		closesocket(mgr->sock);
		mgr->sock = INVALID_SOCKET;
	}

	mgr->chan = chan;
	mgr->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mgr->sock == INVALID_SOCKET) return false;

	/* 强制端口独占 (SO_EXCLUSIVEADDRUSE 必须在 bind 前设). 端口被其他程序占用时
	 * bind 失败 → Bind 返回 false → 调用方弹窗报错, 避免静默"连接成功但收不到包".
	 * (Windows UDP 默认不强制独占; SO_REUSEADDR 更是允许抢占, 这里明确禁用) */
	BOOL exclusive = TRUE;
	setsockopt(mgr->sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		   (const char *)&exclusive, sizeof(exclusive));

	/* 允许广播收发 (Windows 发广播必须设 SO_BROADCAST, 须在 bind 前设) */
	BOOL broadcast = TRUE;
	setsockopt(mgr->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));

	/* 本地: 绑 0.0.0.0:local_port (可收广播, 多网卡由路由表自动选路) */
	struct sockaddr_in local_addr;
	memset(&local_addr, 0, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(local_port);
	local_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(mgr->sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
		closesocket(mgr->sock);
		mgr->sock = INVALID_SOCKET;
		return false;
	}

	/* 远程目标 = remote_ip:remote_port.
	 *   remote_ip = "255.255.255.255" → 有限广播 (255.255.255.255, 唯一能跨网段到达的方式)
	 *   remote_ip 留空/0.0.0.0/非法   → 子网定向广播自动发现 (收集各网卡广播地址)
	 *   其它                          → 单播到该 IP
	 * 收到对端包后 RX 线程会学习并覆盖 remote_addr (单播时与指定一致).
	 * 显式重置 broadcast_mode, 避免上次会话残留 (IP 学习会把广播切为单播). */
	bool unicast = false;
	bool limited_bcast = false;     /* 显式 255.255.255.255: 有限广播 (跨网段可达) */
	memset(&mgr->remote_addr, 0, sizeof(mgr->remote_addr));
	mgr->remote_addr.sin_family = AF_INET;
	mgr->remote_addr.sin_port = htons(remote_port);
	if (remote_ip && *remote_ip) {
		/* inet_addr("255.255.255.255") 在 Winsock 返回 INADDR_NONE (与 0xFFFFFFFF 同值),
		 * 故用字符串比较显式识别有限广播, 避免歧义. */
		if (strcmp(remote_ip, "255.255.255.255") == 0) {
			limited_bcast = true;
		} else {
			unsigned long a = inet_addr(remote_ip);
			if (a != INADDR_NONE && a != 0) {
				mgr->remote_addr.sin_addr.s_addr = a;
				unicast = true;
			}
		}
	}
	mgr->broadcast_mode = false;   /* 默认单播; 下方广播分支会覆盖 */
	if (!unicast) {
		mgr->broadcast_mode = true;
		if (limited_bcast) {
			/* 显式有限广播 255.255.255.255: 唯一能跨网段到达设备的方式 (路由器/驱动层转发).
			 * 子网定向广播 (x.x.x.255) 只在本地子网有效, 跨网段到不了设备. */
			mgr->bcast_addrs[0] = INADDR_BROADCAST;
			mgr->bcast_cnt = 1;
		} else {
			/* 自动发现: 收集所有非回环网卡的子网定向广播地址, 发送时逐个发出,
			 * 确保板子无论连哪个网卡都能收到 */
			mgr->bcast_cnt = collect_broadcast_addrs(mgr->bcast_addrs,
								 (int)(sizeof(mgr->bcast_addrs) / sizeof(mgr->bcast_addrs[0])));
			if (mgr->bcast_cnt == 0) {
				/* 兜底: 取不到网卡信息时退回有限广播 */
				mgr->bcast_addrs[0] = INADDR_BROADCAST;
				mgr->bcast_cnt = 1;
			}
		}
		mgr->remote_addr.sin_addr.s_addr = mgr->bcast_addrs[0];  /* 供提示消息显示 */
	}

	mgr->bound = true;

	if (mgr->msg_cb) {
		const char *chan_name = (chan == UDP_CHAN_CONFIG) ? "配置" : "数据";
		char buf[128];
		if (unicast) {
			sprintf(buf, "UDP %s通道已连接: 本地 %d → %s:%d", chan_name,
				local_port, remote_ip, remote_port);
		} else {
			struct in_addr b;
			b.s_addr = mgr->remote_addr.sin_addr.s_addr;
			sprintf(buf, "UDP %s通道已连接: 本地 %d → 广播 %s:%d", chan_name,
				local_port, inet_ntoa(b), remote_port);
		}
		mgr->msg_cb(buf, mgr->msg_data);
	}

	return true;
}

void UdpManager_Unbind(UdpManager *mgr)
{
	if (!mgr) return;
	UdpManager_StopRxThread(mgr);
	if (mgr->sock != INVALID_SOCKET) {
		closesocket(mgr->sock);
		mgr->sock = INVALID_SOCKET;
	}
	mgr->bound = false;
}

bool UdpManager_IsBound(UdpManager *mgr)
{
	return mgr && mgr->bound;
}

/* 内部发送: 广播模式遍历所有网卡广播地址逐个发, 单播发 remote_addr.
 * 只要至少一个发送成功即返回 true */
static bool udp_send_raw(UdpManager *mgr, const uint8_t *buf, int len)
{
	if (mgr->broadcast_mode) {
		bool any_ok = false;
		for (int i = 0; i < mgr->bcast_cnt; i++) {
			struct sockaddr_in dst;
			memset(&dst, 0, sizeof(dst));
			dst.sin_family = AF_INET;
			dst.sin_port = mgr->remote_addr.sin_port;
			dst.sin_addr.s_addr = mgr->bcast_addrs[i];
			if (sendto(mgr->sock, (const char *)buf, len, 0,
				   (struct sockaddr *)&dst, sizeof(dst)) == len) {
				any_ok = true;
			}
		}
		return any_ok;
	}

	int sent = sendto(mgr->sock, (const char *)buf, len, 0,
			  (struct sockaddr *)&mgr->remote_addr,
			  sizeof(mgr->remote_addr));
	return (sent == len);
}

bool UdpManager_SendData(UdpManager *mgr, const uint8_t *data, size_t len)
{
	if (!mgr || !mgr->bound || mgr->sock == INVALID_SOCKET) return false;

	return udp_send_raw(mgr, data, (int)len);
}

bool UdpManager_SendCommand(UdpManager *mgr, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	if (!mgr || !mgr->bound || mgr->sock == INVALID_SOCKET) return false;

	/* [cmd 1B][data...] 无魔数头 */
	uint8_t buf[260] = {0};
	buf[0] = cmd;
	if (data && len > 0 && len < sizeof(buf) - 1) {
		memcpy(buf + 1, data, len);
	}

	return udp_send_raw(mgr, buf, (int)(len + 1));
}

/* 设置一个 IPv4 字段 (IP/掩码/网关), 三个函数仅命令码不同 */
static bool send_ipv4_cmd(UdpManager *mgr, uint8_t cmd, const char *str)
{
	struct in_addr addr;
	addr.s_addr = inet_addr(str);
	if (addr.s_addr == INADDR_NONE) return false;
	return UdpManager_SendCommand(mgr, cmd, (uint8_t *)&addr, 4);
}

bool UdpManager_SetIP(UdpManager *mgr, const char *ip)
{
	return send_ipv4_cmd(mgr, UDP_CMD_SET_IP, ip);
}

bool UdpManager_SetMask(UdpManager *mgr, const char *mask)
{
	return send_ipv4_cmd(mgr, UDP_CMD_SET_MASK, mask);
}

bool UdpManager_SetGateway(UdpManager *mgr, const char *gw)
{
	return send_ipv4_cmd(mgr, UDP_CMD_SET_GW, gw);
}

bool UdpManager_SetPort(UdpManager *mgr, uint16_t port)
{
	uint8_t data[2];
	data[0] = (port >> 8) & 0xFF;
	data[1] = port & 0xFF;
	return UdpManager_SendCommand(mgr, UDP_CMD_SET_PORT, data, 2);
}

bool UdpManager_SetRemotePort(UdpManager *mgr, uint16_t port)
{
	uint8_t data[2];
	data[0] = (port >> 8) & 0xFF;
	data[1] = port & 0xFF;
	return UdpManager_SendCommand(mgr, UDP_CMD_SET_REMOTE_PORT, data, 2);
}

bool UdpManager_SetRF24Channel(UdpManager *mgr, uint8_t channel)
{
	return UdpManager_SendCommand(mgr, UDP_CMD_SET_RF24_CH, &channel, 1);
}

bool UdpManager_SetRF24Addr(UdpManager *mgr, const uint8_t *addr)
{
	return UdpManager_SendCommand(mgr, UDP_CMD_SET_RF24_ADDR, addr, 5);
}

/* 通用: 发送命令并同步等待同命令码的响应.
 * 成功时 resp_buf/resp_len 填入响应 [cmd][data...] 中 data 部分 (去掉首字节 cmd).
 * 返回 true = 收到响应. */
static bool send_and_wait(UdpManager *mgr, uint8_t cmd,
			  const uint8_t *data, uint8_t len)
{
	if (!mgr || !mgr->bound) return false;

	ResetEvent(mgr->resp_event);
	mgr->resp_len = 0;
	mgr->pending_cmd = cmd;

	bool ok = UdpManager_SendCommand(mgr, cmd, data, len);
	if (!ok) {
		mgr->pending_cmd = 0;
		return false;
	}

	DWORD wr = WaitForSingleObject(mgr->resp_event, UDP_RESP_TIMEOUT_MS);
	mgr->pending_cmd = 0;

	return (wr == WAIT_OBJECT_0) && (mgr->resp_len > 0);
}

bool UdpManager_GetConfig(UdpManager *mgr, GatewayConfig *config)
{
	if (!config) return false;
	memset(config, 0, sizeof(*config));

	if (!send_and_wait(mgr, UDP_CMD_GET_CONFIG, NULL, 0)) {
		return false;
	}

	const uint8_t *p = mgr->resp_buf;

	/* 把 4 字节网络序 IP 写入点分十进制字符串 buf (容量 >=16) */
	#define ipv4_to_str(b, off)  sprintf((b), "%u.%u.%u.%u", \
		p[(off)], p[(off)+1], p[(off)+2], p[(off)+3])

	if (mgr->resp_len >= 24) {
		/* 新 gateway 格式 (24B): [rf24_ch 1B][rf24_addr 5B][data_port 2B]
		 *   [remote_port 2B][config_port 2B][ip 4B][mask 4B][gw 4B]
		 * remote_port = data_port + 1 (gateway 广播目标端口规则) */
		config->rf24_channel = p[0];
		memcpy(config->rf24_addr, p + 1, 5);
		config->data_port = (p[6] << 8) | p[7];
		config->remote_data_port = (p[8] << 8) | p[9];
		config->config_port = (p[10] << 8) | p[11];
		ipv4_to_str(config->ip, 12);
		ipv4_to_str(config->netmask, 16);
		ipv4_to_str(config->gateway, 20);
		config->has_net = 1;
		return true;
	}

	if (mgr->resp_len >= 22) {
		/* 旧 22B gateway 格式 (无 remote_port): [rf24_ch 1B][rf24_addr 5B][data_port 2B]
		 *   [config_port 2B][ip 4B][mask 4B][gw 4B] */
		config->rf24_channel = p[0];
		memcpy(config->rf24_addr, p + 1, 5);
		config->data_port = (p[6] << 8) | p[7];
		config->config_port = (p[8] << 8) | p[9];
		config->remote_data_port = config->data_port;  /* 旧 22B 无远程端口, 默认同本地 */
		ipv4_to_str(config->ip, 10);
		ipv4_to_str(config->netmask, 14);
		ipv4_to_str(config->gateway, 18);
		config->has_net = 1;
		return true;
	}

	if (mgr->resp_len >= 18) {
		/* net_test 含网络配置 (18B): [local_port 2B][remote_port 2B][config_port 2B]
		 *   [ip 4B][mask 4B][gw 4B] */
		config->data_port = (p[0] << 8) | p[1];
		config->remote_data_port = (p[2] << 8) | p[3];
		config->config_port = (p[4] << 8) | p[5];
		ipv4_to_str(config->ip, 6);
		ipv4_to_str(config->netmask, 10);
		ipv4_to_str(config->gateway, 14);
		config->has_net = 1;
		return true;
	}

	if (mgr->resp_len >= 10) {
		/* 旧 gateway 格式: [rf24_ch 1B][rf24_addr 5B][data_port 2B][config_port 2B] */
		config->rf24_channel = p[0];
		memcpy(config->rf24_addr, p + 1, 5);
		config->data_port = (p[6] << 8) | p[7];
		config->config_port = (p[8] << 8) | p[9];
		config->remote_data_port = config->data_port;  /* 旧固件无远程端口, 默认同本地 */
		return true;
	}

	if (mgr->resp_len >= 6) {
		/* net_test 格式: [local_port 2B][remote_port 2B][config_port 2B] */
		config->data_port = (p[0] << 8) | p[1];
		config->remote_data_port = (p[2] << 8) | p[3];
		config->config_port = (p[4] << 8) | p[5];
		return true;
	}

	#undef ipv4_to_str

	return false;
}

bool UdpManager_GetVersion(UdpManager *mgr, char *buf, size_t buf_len)
{
	if (!buf || buf_len == 0) return false;

	if (!send_and_wait(mgr, UDP_CMD_GET_VERSION, NULL, 0)) {
		return false;
	}

	size_t n = mgr->resp_len;
	if (n >= buf_len) n = buf_len - 1;
	memcpy(buf, mgr->resp_buf, n);
	buf[n] = '\0';
	return true;
}

bool UdpManager_Reboot(UdpManager *mgr)
{
	return UdpManager_SendCommand(mgr, UDP_CMD_REBOOT, NULL, 0);
}

/* 通用: 发 [cmd][data...] 并等回复. 回复的 data 部分填入 out_buf (去掉首字节 cmd).
 * 返回回复 data 长度; -1 = 失败/超时. data_len 用 uint16_t 避免 256B 溢出 */
static int fw_exchange(UdpManager *mgr, uint8_t cmd,
		       const uint8_t *data, uint16_t data_len,
		       uint8_t *out_buf, uint8_t out_cap, DWORD timeout_ms)
{
	if (!mgr || !mgr->bound) return -1;
	if (data_len > 511) return -1;

	uint8_t buf[512];

	buf[0] = cmd;
	if (data && data_len > 0) {
		memcpy(buf + 1, data, data_len);
	}

	ResetEvent(mgr->resp_event);
	mgr->resp_len = 0;
	mgr->pending_cmd = cmd;

	bool ok = udp_send_raw(mgr, buf, (int)(data_len + 1));
	if (!ok) {
		mgr->pending_cmd = 0;
		return -1;
	}

	DWORD wr = WaitForSingleObject(mgr->resp_event, timeout_ms);
	mgr->pending_cmd = 0;

	if (wr != WAIT_OBJECT_0 || mgr->resp_len < 1) {
		return -1;
	}
	int n = (int)mgr->resp_len;

	if (n > out_cap) n = out_cap;
	if (out_buf && n > 0) {
		memcpy(out_buf, mgr->resp_buf, n);
	}
	return n;
}

uint16_t UdpManager_CRC16_CCITT(const uint8_t *data, size_t len)
{
	/* 与 Zephyr crc16_ccitt (subsys/crc/crc16_sw.c) 完全一致的实现.
	 * 注意: 这是 Zephyr 特化的 bit-reflected 变体, 非标准 MSB-first CCITT. */
	uint16_t seed = 0x0000;

	for (; len > 0; len--) {
		uint8_t e, f;

		e = (uint8_t)seed ^ *data;
		++data;
		f = (uint8_t)(e ^ (e << 4));
		seed = (uint16_t)((seed >> 8) ^ ((uint16_t)f << 8) ^ ((uint16_t)f << 3) ^
				  ((uint16_t)f >> 4));
	}
	return seed;
}

bool UdpManager_FirmwareStart(UdpManager *mgr, uint32_t size)
{
	uint8_t data[4];

	data[0] = size & 0xFF;
	data[1] = (size >> 8) & 0xFF;
	data[2] = (size >> 16) & 0xFF;
	data[3] = (size >> 24) & 0xFF;

	uint8_t resp;
	/* FW_START 擦除整个 slot1 分区, 耗时较长, 给 5s 超时 */
	int n = fw_exchange(mgr, UDP_CMD_FW_START, data, 4, &resp, 1, 5000);

	return (n == 1 && resp == 1);
}

bool UdpManager_FirmwareData(UdpManager *mgr, const uint8_t *data, size_t len,
			     uint32_t expected_offset, uint32_t *got_offset)
{
	uint8_t resp[4];
	int n = fw_exchange(mgr, UDP_CMD_FW_DATA, data, (uint16_t)len, resp, 4, 1000);

	if (n != 4) return false;
	uint32_t off = resp[0] | (resp[1] << 8) | (resp[2] << 16) | ((uint32_t)resp[3] << 24);

	if (got_offset) *got_offset = off;
	return (off == expected_offset);
}

bool UdpManager_FirmwareEnd(UdpManager *mgr, uint8_t test_mode, uint16_t crc16)
{
	uint8_t data[3];

	data[0] = test_mode;
	data[1] = crc16 & 0xFF;
	data[2] = (crc16 >> 8) & 0xFF;

	uint8_t resp;
	/* FW_END: flush + 读回 slot1 重算 CRC, 耗时较长, 给 10s 超时 */
	int n = fw_exchange(mgr, UDP_CMD_FW_END, data, 3, &resp, 1, 10000);

	return (n == 1 && resp == 1);
}

/* 处理配置通道收到的响应包 [cmd][data...].
 * 若是当前正在等待的命令, 唤醒同步调用; 否则上报 msg_cb */
static void handle_config_response(UdpManager *mgr, const uint8_t *buf, size_t len)
{
	if (len < 1) return;
	uint8_t cmd = buf[0];

	if (mgr->pending_cmd != 0 && cmd == mgr->pending_cmd) {
		size_t n = len - 1;
		if (n > sizeof(mgr->resp_buf)) n = sizeof(mgr->resp_buf);
		memcpy(mgr->resp_buf, buf + 1, n);
		mgr->resp_len = n;
		SetEvent(mgr->resp_event);
		return;
	}

	if (mgr->msg_cb) {
		char msg[128];
		sprintf(msg, "UDP response: cmd=0x%02x len=%d", cmd, (int)(len - 1));
		mgr->msg_cb(msg, mgr->msg_data);
	}
}

static DWORD WINAPI udp_rx_thread_proc(LPVOID param)
{
	UdpManager *mgr = (UdpManager *)param;
	uint8_t buf[600];

	while (mgr->rx_running) {
		if (!mgr->bound) {
			Sleep(10);
			continue;
		}

		struct sockaddr_in src;
		int alen = sizeof(src);
		int received = recvfrom(mgr->sock, (char *)buf, sizeof(buf), 0,
					(struct sockaddr *)&src, &alen);
		if (received <= 0) {
			continue;
		}

		/* 学习发送方地址 (后续发包到此).
		 * 仅当对端与本机同子网时才切单播; 跨子网 (如设备经路由回复) 单播发不过去,
		 * 此时保持广播模式, 确保后续通信可达. */
		if (is_local_subnet(src.sin_addr.s_addr)) {
			mgr->remote_addr = src;
			mgr->broadcast_mode = false;
		}

		/* 按 chan 分发 */
		if (mgr->chan == UDP_CHAN_CONFIG) {
			handle_config_response(mgr, buf, (size_t)received);
		} else if (mgr->data_cb) {
			mgr->data_cb(buf, (size_t)received, mgr->data_data);
		}
	}

	return 0;
}

void UdpManager_StartRxThread(UdpManager *mgr)
{
	if (!mgr || mgr->rx_running) return;
	mgr->rx_running = true;
	mgr->rx_thread = CreateThread(NULL, 0, udp_rx_thread_proc, mgr, 0, NULL);
}

void UdpManager_StopRxThread(UdpManager *mgr)
{
	if (!mgr || !mgr->rx_running) return;
	mgr->rx_running = false;

	/* 先关闭 socket 强制让阻塞在 recvfrom 的 RX 线程立即返回 (收到错误),
	 * 否则要等 WaitForSingleObject 超时 (2s) 才退出 → 断开按钮卡顿 */
	if (mgr->sock != INVALID_SOCKET) {
		closesocket(mgr->sock);
		mgr->sock = INVALID_SOCKET;
	}

	if (mgr->rx_thread) {
		WaitForSingleObject(mgr->rx_thread, 1000);
		CloseHandle(mgr->rx_thread);
		mgr->rx_thread = NULL;
	}
}

void UdpManager_SetMsgCallback(UdpManager *mgr, udp_msg_callback cb, void *data)
{
	if (mgr) { mgr->msg_cb = cb; mgr->msg_data = data; }
}

void UdpManager_SetDataCallback(UdpManager *mgr, udp_data_callback cb, void *data)
{
	if (mgr) { mgr->data_cb = cb; mgr->data_data = data; }
}
