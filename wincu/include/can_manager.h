#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <mutex>
#include <vector>
#include <windows.h>
#include <PCANBasic.h>

// CAN ID definitions
#define PLATFORM_RX     0x101    // Command to board
#define PLATFORM_TX     0x102    // Response from board
#define FW_DATA_RX      0x103    // Firmware data

// Board commands
#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

// Firmware response codes
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

typedef void (*msgCallback)(const char * msg);
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
    bool firmwareUpgrade(const char* fileName, bool testMode);
    std::vector<TPCANHandle> detectDevice(void);

private:
    CanManager(): m_channel(PCAN_NONEBUS) {}; // 私有构造
    ~CanManager() = default;
    void appendLog(const char *msg);
    bool CAN_WaitForResponse(uint32_t* code, uint32_t* param, int timeoutMs);

    mutable std::mutex m_mutex;
    TPCANHandle m_channel;
    msgCallback append_msg;
    progressCallback progress_call;
};

#endif
