#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

#define PLATFORM_RX     0x101
#define PLATFORM_TX     0x102
#define FW_DATA_RX      0x103

#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

#define FW_CODE_OFFSET          0
#define FW_CODE_UPDATE_SUCCESS  1
#define FW_CODE_VERSION         2
#define FW_CODE_CONFIRM         3
#define FW_CODE_FLASH_ERROR     4
#define FW_CODE_TRANFER_ERROR   5

class CanManager {
public:
    CanManager();
    ~CanManager();

    CanManager(const CanManager&) = delete;
    CanManager& operator=(const CanManager&) = delete;

    using LogCallback = std::function<void(const char*)>;
    using ProgressCallback = std::function<void(int)>;

    void SetLogCallback(LogCallback cb);
    void SetProgressCallback(ProgressCallback cb);

    std::vector<std::string> DetectDevices();

    /** baudrateIndex: 0=10K,1=20K,2=50K,3=100K,4=125K,5=250K,6=500K,7=1M */
    bool Connect(const std::string& interface, int baudrateIndex);

    void Disconnect();
    uint32_t GetFirmwareVersion();
    bool BoardReboot();
    bool FirmwareUpgrade(const std::string& fileName, bool testMode);

private:
    bool CheckConnected();
    void Log(const char* msg);
    void LogFmt(const char* fmt, ...);
    bool WaitResponse(uint32_t& code, uint32_t& val, int timeoutMs);

    int m_fd = -1;
    std::string m_interface;
    std::mutex m_mutex;
    LogCallback m_logCb;
    ProgressCallback m_progressCb;
};
