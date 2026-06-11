#include "CanManager.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

static const int BAUD_RATES[] = {
    10000, 20000, 50000, 100000,
    125000, 250000, 500000, 1000000
};

CanManager::CanManager() = default;

CanManager::~CanManager() {
    Disconnect();
}

void CanManager::SetLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
void CanManager::SetProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }

void CanManager::Log(const char* msg) {
    if (m_logCb) m_logCb(msg);
}

void CanManager::LogFmt(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Log(buf);
}

bool CanManager::CheckConnected() {
    if (m_fd >= 0) return true;
    Log("CAN 未连接");
    return false;
}

std::vector<std::string> CanManager::DetectDevices() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/class/net");
    if (!dir) {
        Log("无法扫描网络设备");
        return result;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (strstr(entry->d_name, "can") != nullptr) {
            result.emplace_back(entry->d_name);
        }
    }
    closedir(dir);
    LogFmt("查询到 %zu 个可用 CAN 设备", result.size());
    return result;
}

bool CanManager::Connect(const std::string& interface, int baudrateIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fd >= 0) {
        Log("已连接，请先断开");
        return false;
    }
    if (baudrateIndex < 0 || baudrateIndex >= 8) {
        Log("无效波特率索引");
        return false;
    }

    int bitrate = BAUD_RATES[baudrateIndex];
    std::string cmd = "ip link set " + interface + " type can bitrate " + std::to_string(bitrate) + " 2>/dev/null";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        LogFmt("设置波特率 %d 失败 (可能需要 root)", bitrate);
    }

    cmd = "ip link set " + interface + " up 2>/dev/null";
    ret = system(cmd.c_str());
    if (ret != 0) {
        LogFmt("启动接口 %s 失败", interface.c_str());
    }

    struct ifreq ifr {};
    struct sockaddr_can addr {};

    m_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_fd < 0) {
        Log("创建 CAN socket 失败");
        m_fd = -1;
        return false;
    }

    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
    if (ioctl(m_fd, SIOCGIFINDEX, &ifr) < 0) {
        Log("获取接口索引失败");
        close(m_fd);
        m_fd = -1;
        return false;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(m_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        Log("绑定 CAN socket 失败");
        close(m_fd);
        m_fd = -1;
        return false;
    }

    struct can_filter rfilter[1];
    rfilter[0].can_id = PLATFORM_TX;
    rfilter[0].can_mask = 0x7FF;
    setsockopt(m_fd, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    m_interface = interface;
    LogFmt("已连接 %s, 波特率 %d", interface.c_str(), bitrate);
    return true;
}

void CanManager::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
        if (!m_interface.empty()) {
            std::string cmd = "ip link set " + m_interface + " down 2>/dev/null";
            system(cmd.c_str());
        }
        m_interface.clear();
        Log("已断开连接");
    }
}

static void pack_u32(uint8_t* buf, uint32_t v1, uint32_t v2) {
    buf[0] = (v1 >> 24) & 0xff; buf[1] = (v1 >> 16) & 0xff;
    buf[2] = (v1 >> 8) & 0xff; buf[3] = v1 & 0xff;
    buf[4] = (v2 >> 24) & 0xff; buf[5] = (v2 >> 16) & 0xff;
    buf[6] = (v2 >> 8) & 0xff; buf[7] = v2 & 0xff;
}

static void unpack_u32(const uint8_t* buf, uint32_t* v1, uint32_t* v2) {
    *v1 = (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);
    *v2 = (uint32_t(buf[4]) << 24) | (uint32_t(buf[5]) << 16) | (uint32_t(buf[6]) << 8) | uint32_t(buf[7]);
}

static int can_send(int fd, uint32_t id, const uint8_t* data, uint8_t dlc) {
    struct can_frame frame {};
    frame.can_id = id;
    frame.can_dlc = dlc;
    if (data && dlc > 0) memcpy(frame.data, data, dlc);
    ssize_t nbytes = write(fd, &frame, sizeof(frame));
    return (nbytes == sizeof(frame)) ? 0 : -1;
}

static int can_recv(int fd, uint32_t* code, uint32_t* val, int timeoutMs) {
    fd_set readfds;
    struct timeval tv {};
    struct can_frame frame {};

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ret <= 0) return -1;

    ssize_t nbytes = read(fd, &frame, sizeof(frame));
    if (nbytes < 0) return -1;

    unpack_u32(frame.data, code, val);
    return 0;
}

bool CanManager::WaitResponse(uint32_t& code, uint32_t& val, int timeoutMs) {
    return can_recv(m_fd, &code, &val, timeoutMs) == 0;
}

uint32_t CanManager::GetFirmwareVersion() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!CheckConnected()) return 0;

    uint8_t data[8] = {};
    pack_u32(data, BOARD_VERSION, 0);
    can_send(m_fd, PLATFORM_RX, data, 8);

    uint32_t code = 0, version = 0;
    if (!WaitResponse(code, version, 5000)) {
        Log("获取版本超时");
        return 0;
    }
    if (code == FW_CODE_VERSION) {
        LogFmt("固件版本: v%u.%u.%u", (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        return version;
    }
    Log("获取版本失败");
    return 0;
}

bool CanManager::BoardReboot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!CheckConnected()) return false;

    uint8_t data[8] = {};
    pack_u32(data, BOARD_REBOOT, 0);
    if (can_send(m_fd, PLATFORM_RX, data, 8) == 0) {
        Log("重启命令已发送");
        return true;
    }
    return false;
}

bool CanManager::FirmwareUpgrade(const std::string& fileName, bool testMode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!CheckConnected()) return false;

    FILE* fp = fopen(fileName.c_str(), "rb");
    if (!fp) {
        LogFmt("无法打开文件: %s", fileName.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    LogFmt("固件大小: %ld 字节", fileSize);

    uint8_t data[8] = {};
    uint32_t code = 0, val = 0;

    pack_u32(data, BOARD_START_UPDATE, static_cast<uint32_t>(fileSize));
    can_send(m_fd, PLATFORM_RX, data, 8);

    if (!WaitResponse(code, val, 5000)) {
        fclose(fp);
        Log("等待板卡响应超时");
        return false;
    }
    if (code != FW_CODE_OFFSET || val != 0) {
        fclose(fp);
        LogFmt("Flash 擦除错误: code=%u, offset=%u", code, val);
        return false;
    }

    Log("开始传输固件数据...");
    size_t sent = 0;
    while (true) {
        size_t nread = fread(data, 1, 8, fp);
        if (nread == 0) break;

        can_send(m_fd, FW_DATA_RX, data, static_cast<uint8_t>(nread));
        sent += nread;

        if (m_progressCb) {
            int percent = static_cast<int>(sent * 100 / fileSize);
            m_progressCb(percent);
        }

        if (sent % 64 != 0 && static_cast<long>(sent) < fileSize) continue;

        if (!WaitResponse(code, val, 5000)) {
            fclose(fp);
            Log("传输超时");
            return false;
        }

        if (code == FW_CODE_UPDATE_SUCCESS && val == sent) break;
        if (code != FW_CODE_OFFSET) {
            fclose(fp);
            LogFmt("固件上传错误: code=%u, offset=%u", code, val);
            return false;
        }
    }

    fclose(fp);

    pack_u32(data, BOARD_CONFIRM, testMode ? 0u : 1u);
    can_send(m_fd, PLATFORM_RX, data, 8);

    if (!WaitResponse(code, val, 30000)) {
        Log("确认超时");
        return false;
    }

    if (code == FW_CODE_CONFIRM && val == 0x55AA55AA) {
        Log("固件上传完成！请重启板子以完成升级，约需 45-90 秒");
        return true;
    }
    if (code == FW_CODE_TRANFER_ERROR) {
        Log("下载失败");
    }
    return false;
}
