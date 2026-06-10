#include "CanManager.h"
#include <cstdio>
#include <cstdarg>

static void htobe32_buf(uint8_t* buf, uint32_t v) {
    buf[0] = (v >> 24) & 0xff; buf[1] = (v >> 16) & 0xff;
    buf[2] = (v >> 8) & 0xff;  buf[3] = v & 0xff;
}

static uint32_t be32_to_h(const uint8_t* buf) {
    return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) |
           (uint32_t(buf[2]) << 8)  | uint32_t(buf[3]);
}

static void pack_frame(uint8_t* data, uint32_t code, uint32_t val) {
    htobe32_buf(data, code);
    htobe32_buf(data + 4, val);
}

static void unpack_frame(const uint8_t* data, uint32_t& code, uint32_t& val) {
    code = be32_to_h(data);
    val = be32_to_h(data + 4);
}

CanManager::CanManager() {
    InitializeCriticalSection(&m_cs);
}

CanManager::~CanManager() {
    if (m_channel != PCAN_NONEBUS) Disconnect();
    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }
    DeleteCriticalSection(&m_cs);
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

bool CanManager::EnsureDriver() {
    if (m_hModule) return true;
    m_hModule = LoadLibraryW(L"PCANBasic.dll");
    if (!m_hModule) {
        Log("PCANBasic.dll 不存在，请安装 PCAN 驱动");
        return false;
    }
    fn_Initialize       = reinterpret_cast<PFN_Initialize>(GetProcAddress(m_hModule, "CAN_Initialize"));
    fn_Uninitialize     = reinterpret_cast<PFN_Uninitialize>(GetProcAddress(m_hModule, "CAN_Uninitialize"));
    fn_Read             = reinterpret_cast<PFN_Read>(GetProcAddress(m_hModule, "CAN_Read"));
    fn_Write            = reinterpret_cast<PFN_Write>(GetProcAddress(m_hModule, "CAN_Write"));
    fn_FilterMessages   = reinterpret_cast<PFN_FilterMessages>(GetProcAddress(m_hModule, "CAN_FilterMessages"));
    fn_LookUpChannel    = reinterpret_cast<PFN_LookUpChannel>(GetProcAddress(m_hModule, "CAN_LookUpChannel"));
    fn_GetErrorText     = reinterpret_cast<PFN_GetErrorText>(GetProcAddress(m_hModule, "CAN_GetErrorText"));
    if (!fn_Initialize || !fn_Uninitialize || !fn_Read || !fn_Write ||
        !fn_FilterMessages || !fn_LookUpChannel || !fn_GetErrorText) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        Log("PCANBasic.dll 函数加载失败");
        return false;
    }
    return true;
}

bool CanManager::CheckChannel() {
    if (m_channel != PCAN_NONEBUS) return true;
    Log("CAN 未连接");
    return false;
}

std::vector<TPCANHandle> CanManager::DetectDevices() {
    std::vector<TPCANHandle> result;
    if (!EnsureDriver()) return result;
    for (int i = 0; i < MAX_DEVICES; i++) {
        TPCANHandle ch = PCAN_NONEBUS;
        char param[64];
        snprintf(param, sizeof(param), "devicetype=pcan_usb,controllernumber=%d", i);
        if (fn_LookUpChannel(param, &ch) == PCAN_ERROR_OK && ch != PCAN_NONEBUS)
            result.push_back(ch);
    }
    LogFmt("查询到 %zu 个可用 CAN 设备", result.size());
    return result;
}

bool CanManager::Connect(TPCANHandle channel, TPCANBaudrate baudrate) {
    if (!EnsureDriver()) return false;
    EnterCriticalSection(&m_cs);
    if (m_channel != PCAN_NONEBUS) {
        LeaveCriticalSection(&m_cs);
        Log("CAN 已连接，请勿重复连接");
        return true;
    }
    TPCANStatus status = fn_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        LeaveCriticalSection(&m_cs);
        Log("CAN 初始化失败");
        return false;
    }
    m_channel = channel;
    LeaveCriticalSection(&m_cs);
    fn_FilterMessages(m_channel, PLATFORM_TX, PLATFORM_TX, PCAN_MODE_STANDARD);
    LogFmt("CAN(id=%xh) 连接成功", static_cast<unsigned>(channel));
    return true;
}

void CanManager::Disconnect() {
    EnterCriticalSection(&m_cs);
    if (m_channel == PCAN_NONEBUS) {
        LeaveCriticalSection(&m_cs);
        return;
    }
    LogFmt("CAN(id=%xh) 连接已断开", static_cast<unsigned>(m_channel));
    fn_Uninitialize(m_channel);
    m_channel = PCAN_NONEBUS;
    LeaveCriticalSection(&m_cs);
}

bool CanManager::WaitResponse(uint32_t& code, uint32_t& val, int timeoutMs) {
    TPCANMsg msg = {};
    TPCANTimestamp ts = {};
    DWORD start = GetTickCount();
    while (static_cast<int>(GetTickCount() - start) < timeoutMs) {
        TPCANStatus st = fn_Read(m_channel, &msg, &ts);
        if (st == PCAN_ERROR_OK && msg.ID == PLATFORM_TX) {
            unpack_frame(msg.DATA, code, val);
            return true;
        }
        if (st == PCAN_ERROR_QRCVEMPTY) {
            Sleep(1);
        }
    }
    return false;
}

uint32_t CanManager::GetFirmwareVersion() {
    if (!EnsureDriver() || !CheckChannel()) return 0;
    EnterCriticalSection(&m_cs);
    TPCANMsg msg = {};
    msg.ID = PLATFORM_RX;
    msg.MSGTYPE = PCAN_MODE_STANDARD;
    msg.LEN = 8;
    pack_frame(msg.DATA, BOARD_VERSION, 0);
    if (fn_Write(m_channel, &msg) != PCAN_ERROR_OK) {
        LeaveCriticalSection(&m_cs);
        Log("CAN 发送失败");
        return 0;
    }
    uint32_t code = 0, version = 0;
    if (!WaitResponse(code, version, 5000)) {
        LeaveCriticalSection(&m_cs);
        Log("CAN 读取超时");
        return 0;
    }
    if (code == FW_CODE_VERSION) {
        LogFmt("固件版本: v%u.%u.%u",
               (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        LeaveCriticalSection(&m_cs);
        return version;
    }
    LeaveCriticalSection(&m_cs);
    Log("CAN 读取数据错误");
    return 0;
}

bool CanManager::BoardReboot() {
    if (!EnsureDriver() || !CheckChannel()) return false;
    EnterCriticalSection(&m_cs);
    TPCANMsg msg = {};
    msg.ID = PLATFORM_RX;
    msg.MSGTYPE = PCAN_MODE_STANDARD;
    msg.LEN = 8;
    pack_frame(msg.DATA, BOARD_REBOOT, 0);
    TPCANStatus st = fn_Write(m_channel, &msg);
    LeaveCriticalSection(&m_cs);
    if (st != PCAN_ERROR_OK) {
        Log("CAN 发送失败");
        return false;
    }
    return true;
}

bool CanManager::DoFirmwareUpgrade(const wchar_t* fileName, bool testMode) {
    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        char name[200] = {};
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, name, 200, nullptr, nullptr);
        LogFmt("无法打开文件: %s", name);
        return false;
    }
    DWORD fileSize = GetFileSize(hFile, nullptr);
    LogFmt("固件大小: %u 字节", fileSize);

    TPCANMsg msg = {};
    msg.ID = PLATFORM_RX;
    msg.MSGTYPE = PCAN_MODE_STANDARD;
    msg.LEN = 8;
    pack_frame(msg.DATA, BOARD_START_UPDATE, fileSize);

    if (fn_Write(m_channel, &msg) != PCAN_ERROR_OK) {
        CloseHandle(hFile);
        Log("发送固件大小失败");
        return false;
    }
    uint32_t code = 0, offset = 0;
    if (!WaitResponse(code, offset, 15000)) {
        CloseHandle(hFile);
        Log("Flash 擦除超时");
        return false;
    }
    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        LogFmt("Flash 擦除失败: code(%u), offset(%u)", code, offset);
        return false;
    }

    DWORD bytesSent = 0, bytesRead = 0;
    msg.ID = FW_DATA_RX;
    while (ReadFile(hFile, msg.DATA, 8, &bytesRead, nullptr) && bytesRead > 0) {
        msg.LEN = static_cast<BYTE>(bytesRead);
        if (fn_Write(m_channel, &msg) != PCAN_ERROR_OK) {
            CloseHandle(hFile);
            Log("发送数据失败");
            return false;
        }
        bytesSent += bytesRead;
        if (bytesSent % 64 == 0 || bytesSent == fileSize) {
            if (m_progressCb) m_progressCb(static_cast<int>(bytesSent * 100 / fileSize));
            if (!WaitResponse(code, offset, 5000)) {
                CloseHandle(hFile);
                Log("固件更新超时");
                return false;
            }
            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) break;
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                LogFmt("固件升级失败: code(%u), offset(%u)", code, offset);
                return false;
            }
        }
    }
    CloseHandle(hFile);
    if (m_progressCb) m_progressCb(100);

    msg.ID = PLATFORM_RX;
    msg.LEN = 8;
    memset(msg.DATA, 0, 8);
    pack_frame(msg.DATA, BOARD_CONFIRM, testMode ? 0u : 1u);

    if (fn_Write(m_channel, &msg) != PCAN_ERROR_OK) {
        Log("发送确认失败");
        return false;
    }
    if (!WaitResponse(code, offset, 30000)) {
        Log("固件确认超时");
        return false;
    }
    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        char name[200] = {};
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, name, 200, nullptr, nullptr);
        LogFmt("文件 %s 上传完成，请重启板卡", name);
        return true;
    }
    if (code == FW_CODE_TRANFER_ERROR) Log("固件更新失败");
    return false;
}

bool CanManager::FirmwareUpgrade(const wchar_t* fileName, bool testMode) {
    if (!EnsureDriver() || !CheckChannel()) return false;
    EnterCriticalSection(&m_cs);
    LeaveCriticalSection(&m_cs);
    return DoFirmwareUpgrade(fileName, testMode);
}
