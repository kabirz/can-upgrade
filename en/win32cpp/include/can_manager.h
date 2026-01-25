#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <windows.h>
#include <stdint.h>
#include <PCANBasic.h>

#define MAX_DEVICES 16
#define VIRTUAL_CAN_CHANNEL  ((TPCANHandle)0xFFFF)

#ifdef __cplusplus
extern "C" {
#endif

// CAN ID 定义
#define PLATFORM_RX     0x101    // 发送到板卡的命令
#define PLATFORM_TX     0x102    // 板卡响应
#define FW_DATA_RX      0x103    // 固件数据

// 板卡命令
#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

// 固件响应码
#define FW_CODE_OFFSET          0
#define FW_CODE_UPDATE_SUCCESS  1
#define FW_CODE_VERSION         2
#define FW_CODE_CONFIRM         3
#define FW_CODE_FLASH_ERROR     4
#define FW_CODE_TRANFER_ERROR   5

typedef struct {
    uint32_t code;
    uint32_t val;
} can_frame_t;

// 日志回调函数类型
typedef void (*msgCallback)(const char * msg);
// 进度回调函数类型
typedef void (*progressCallback)(int percent);

class CanManager {
public:
    static CanManager& getInstance();

    CanManager(const CanManager&) = delete;
    CanManager& operator=(const CanManager&) = delete;

    bool connect(TPCANHandle channel, TPCANBaudrate baudrate = PCAN_BAUD_500K);
    void disconnect(void);
    void setCallback(msgCallback msg_call);
    void setProgressCallback(progressCallback progress_call);
    uint32_t getFirmwareVersion(void);
    bool boardReboot(void);
    bool firmwareUpgrade(const wchar_t* fileName, bool testMode);
    int detectDevice(TPCANHandle* channels, int maxCount);

private:
    CanManager(): m_channel(PCAN_NONEBUS) {
        InitializeCriticalSection(&m_criticalSection);
    }
    ~CanManager() {
        DeleteCriticalSection(&m_criticalSection);
    }
    void appendLog(const char *msg);
    bool CAN_WaitForResponse(uint32_t* code, uint32_t* param, int timeoutMs);
    bool virtualCAN_FirmwareUpgrade(const wchar_t* fileName);
    bool pcan_FirmwareUpgrade(const wchar_t* fileName, bool testMode);

    CRITICAL_SECTION m_criticalSection;
    TPCANHandle m_channel;
    msgCallback append_msg;
    progressCallback progress_call;
};
#ifdef __cplusplus
}
#endif

#endif
