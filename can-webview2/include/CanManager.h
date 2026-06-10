#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "PCANBasic.h"

#define MAX_DEVICES 16

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

struct CanFrame {
    uint32_t code;
    uint32_t val;
};

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

    std::vector<TPCANHandle> DetectDevices();
    bool Connect(TPCANHandle channel, TPCANBaudrate baudrate);
    void Disconnect();
    uint32_t GetFirmwareVersion();
    bool BoardReboot();
    bool FirmwareUpgrade(const wchar_t* fileName, bool testMode);

private:
    bool EnsureDriver();
    bool CheckChannel();
    void Log(const char* msg);
    void LogFmt(const char* fmt, ...);
    bool WaitResponse(uint32_t& code, uint32_t& val, int timeoutMs);
    bool DoFirmwareUpgrade(const wchar_t* fileName, bool testMode);

    CRITICAL_SECTION m_cs;
    TPCANHandle m_channel = PCAN_NONEBUS;
    LogCallback m_logCb;
    ProgressCallback m_progressCb;

    HMODULE m_hModule = nullptr;

    typedef TPCANStatus (__stdcall *PFN_Initialize)(TPCANHandle, TPCANBaudrate, TPCANType, DWORD, WORD);
    typedef TPCANStatus (__stdcall *PFN_Uninitialize)(TPCANHandle);
    typedef TPCANStatus (__stdcall *PFN_Read)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    typedef TPCANStatus (__stdcall *PFN_Write)(TPCANHandle, TPCANMsg*);
    typedef TPCANStatus (__stdcall *PFN_FilterMessages)(TPCANHandle, DWORD, DWORD, TPCANMode);
    typedef TPCANStatus (__stdcall *PFN_LookUpChannel)(LPSTR, TPCANHandle*);
    typedef TPCANStatus (__stdcall *PFN_GetErrorText)(TPCANStatus, WORD, LPSTR);

    PFN_Initialize  fn_Initialize  = nullptr;
    PFN_Uninitialize fn_Uninitialize = nullptr;
    PFN_Read        fn_Read        = nullptr;
    PFN_Write       fn_Write       = nullptr;
    PFN_FilterMessages fn_FilterMessages = nullptr;
    PFN_LookUpChannel  fn_LookUpChannel  = nullptr;
    PFN_GetErrorText   fn_GetErrorText   = nullptr;
};

#endif
