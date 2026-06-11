#include "can_manager.h"
#include "pcan_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CanManager {
    CRITICAL_SECTION criticalSection;
    TPCANHandle channel;
    msgCallback msgCallback;
    progressCallback progressCallback;
};

CanManager* CanManager_Create(void) {
    CanManager* mgr = (CanManager*)malloc(sizeof(CanManager));
    if (!mgr) return NULL;
    InitializeCriticalSection(&mgr->criticalSection);
    mgr->channel = PCAN_NONEBUS;
    mgr->msgCallback = NULL;
    mgr->progressCallback = NULL;
    return mgr;
}

void CanManager_Destroy(CanManager* mgr) {
    if (!mgr) return;
    DeleteCriticalSection(&mgr->criticalSection);
    free(mgr);
}

void CanManager_SetCallback(CanManager* mgr, msgCallback cb) {
    if (mgr) mgr->msgCallback = cb;
}

void CanManager_SetProgressCallback(CanManager* mgr, progressCallback cb) {
    if (mgr) mgr->progressCallback = cb;
}

static void logMsg(CanManager* mgr, const char* msg) {
    if (mgr && mgr->msgCallback) mgr->msgCallback(msg);
}

static int ensureDriver(CanManager* mgr) {
    if (PcanLoader_IsLoaded()) return 1;
    if (PcanLoader_Load()) return 1;
    DWORD err = PcanLoader_GetLoadError();
    char buf[256];
    if (err == 0)
        logMsg(mgr, "PCANBasic.dll 加载失败，驱动函数不完整，请安装 PCAN 驱动");
    else {
        sprintf(buf, "PCANBasic.dll 加载失败 (错误码: %lu)，请安装 PCAN 驱动", err);
        logMsg(mgr, buf);
    }
    return 0;
}

static int checkChannel(CanManager* mgr) {
    if (mgr->channel != PCAN_NONEBUS) return 1;
    logMsg(mgr, "CAN 未连接");
    return 0;
}

int CanManager_Connect(CanManager* mgr, TPCANHandle channel, TPCANBaudrate baudrate) {
    if (!mgr || !ensureDriver(mgr)) return 0;
    EnterCriticalSection(&mgr->criticalSection);
    if (mgr->channel != PCAN_NONEBUS) {
        LeaveCriticalSection(&mgr->criticalSection);
        logMsg(mgr, "CAN 已连接，请勿重复连接");
        return 1;
    }
    TPCANStatus status = Pcan_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        LeaveCriticalSection(&mgr->criticalSection);
        logMsg(mgr, "CAN 初始化失败");
        return 0;
    }
    mgr->channel = channel;
    LeaveCriticalSection(&mgr->criticalSection);
    Pcan_FilterMessages(mgr->channel, PLATFORM_TX, PLATFORM_TX, PCAN_MODE_STANDARD);
    char buf[32];
    sprintf(buf, "CAN(id=%xh) 连接成功", channel);
    logMsg(mgr, buf);
    return 1;
}

void CanManager_Disconnect(CanManager* mgr) {
    if (!mgr) return;
    EnterCriticalSection(&mgr->criticalSection);
    char buf[64];
    sprintf(buf, "CAN(id=%xh) 连接已断开", mgr->channel);
    Pcan_Uninitialize(mgr->channel);
    mgr->channel = PCAN_NONEBUS;
    LeaveCriticalSection(&mgr->criticalSection);
    logMsg(mgr, buf);
}

static int waitResponse(CanManager* mgr, uint32_t* code, uint32_t* val, int timeoutMs) {
    TPCANMsg msg;
    TPCANTimestamp ts;
    DWORD start = GetTickCount();
    while ((int)(GetTickCount() - start) < timeoutMs) {
        TPCANStatus st = Pcan_Read(mgr->channel, &msg, &ts);
        if (st == PCAN_ERROR_OK && msg.ID == PLATFORM_TX) {
            can_frame_t* f = (can_frame_t*)msg.DATA;
            *code = f->code;
            *val = f->val;
            return 1;
        } else if (st == PCAN_ERROR_QRCVEMPTY) {
            Sleep(1);
        }
    }
    return 0;
}

uint32_t CanManager_GetFirmwareVersion(CanManager* mgr) {
    if (!mgr || !checkChannel(mgr)) return 0;
    EnterCriticalSection(&mgr->criticalSection);
    TPCANMsg msg = {.ID = PLATFORM_RX, .MSGTYPE = PCAN_MODE_STANDARD, .LEN = 8};
    ((can_frame_t*)msg.DATA)->code = BOARD_VERSION;
    if (Pcan_Write(mgr->channel, &msg) != PCAN_ERROR_OK) {
        LeaveCriticalSection(&mgr->criticalSection);
        logMsg(mgr, "CAN 发送失败");
        return 0;
    }
    uint32_t code, version;
    if (!waitResponse(mgr, &code, &version, 5000)) {
        LeaveCriticalSection(&mgr->criticalSection);
        logMsg(mgr, "CAN 读取超时");
        return 0;
    }
    if (code == FW_CODE_VERSION) {
        char buf[64];
        sprintf(buf, "固件版本: v%u.%u.%u", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        logMsg(mgr, buf);
        LeaveCriticalSection(&mgr->criticalSection);
        return version;
    }
    LeaveCriticalSection(&mgr->criticalSection);
    logMsg(mgr, "CAN 读取数据错误");
    return 0;
}

int CanManager_BoardReboot(CanManager* mgr) {
    if (!mgr || !checkChannel(mgr)) return 0;
    EnterCriticalSection(&mgr->criticalSection);
    TPCANMsg msg = {.ID = PLATFORM_RX, .MSGTYPE = PCAN_MODE_STANDARD, .LEN = 8};
    ((can_frame_t*)msg.DATA)->code = BOARD_REBOOT;
    TPCANStatus st = Pcan_Write(mgr->channel, &msg);
    LeaveCriticalSection(&mgr->criticalSection);
    if (st != PCAN_ERROR_OK) {
        logMsg(mgr, "CAN 发送失败");
        return 0;
    }
    return 1;
}

static int PCAN_FirmwareUpgrade(CanManager* mgr, const wchar_t* fileName, int testMode) {
    char log[256];
    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, log + 20, 200, NULL, NULL);
        sprintf(log, "无法打开文件: %s", log + 20);
        logMsg(mgr, log);
        return 0;
    }
    DWORD fileSize = GetFileSize(hFile, NULL);
    sprintf(log, "固件大小: %u 字节", fileSize);
    logMsg(mgr, log);

    TPCANMsg msg = {.ID = PLATFORM_RX, .MSGTYPE = PCAN_MODE_STANDARD, .LEN = 8};
    ((can_frame_t*)msg.DATA)->code = BOARD_START_UPDATE;
    ((can_frame_t*)msg.DATA)->val = fileSize;

    if (Pcan_Write(mgr->channel, &msg) != PCAN_ERROR_OK) {
        CloseHandle(hFile);
        logMsg(mgr, "发送固件大小失败");
        return 0;
    }
    uint32_t code, offset;
    if (!waitResponse(mgr, &code, &offset, 15000)) {
        CloseHandle(hFile);
        logMsg(mgr, "Flash 擦除超时");
        return 0;
    }
    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        sprintf(log, "Flash 擦除失败: code(%u), offset(%u)", code, offset);
        logMsg(mgr, log);
        return 0;
    }
    DWORD bytesSent = 0, bytesRead;
    msg.ID = FW_DATA_RX;
    while (ReadFile(hFile, msg.DATA, 8, &bytesRead, NULL) && bytesRead > 0) {
        msg.LEN = bytesRead;
        if (Pcan_Write(mgr->channel, &msg) != PCAN_ERROR_OK) {
            CloseHandle(hFile);
            logMsg(mgr, "发送数据失败");
            return 0;
        }
        bytesSent += bytesRead;
        if (bytesSent % 64 == 0 || bytesSent == fileSize) {
            if (mgr->progressCallback)
                mgr->progressCallback((int)(bytesSent * 100 / fileSize));
            if (!waitResponse(mgr, &code, &offset, 5000)) {
                CloseHandle(hFile);
                logMsg(mgr, "固件更新超时");
                return 0;
            }
            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) break;
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                sprintf(log, "固件升级失败: code(%u), offset(%u)", code, offset);
                logMsg(mgr, log);
                return 0;
            }
        }
    }
    CloseHandle(hFile);
    if (mgr->progressCallback) mgr->progressCallback(100);

    msg.ID = PLATFORM_RX;
    msg.LEN = 8;
    memset(msg.DATA, 0, 8);
    ((can_frame_t*)msg.DATA)->code = BOARD_CONFIRM;
    ((can_frame_t*)msg.DATA)->val = testMode ? 0 : 1;

    if (Pcan_Write(mgr->channel, &msg) != PCAN_ERROR_OK) {
        logMsg(mgr, "发送确认失败");
        return 0;
    }
    if (!waitResponse(mgr, &code, &offset, 30000)) {
        logMsg(mgr, "固件确认超时");
        return 0;
    }
    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, log + 20, 200, NULL, NULL);
        sprintf(log, "文件 %s 上传完成，请重启板卡", log + 20);
        logMsg(mgr, log);
        return 1;
    }
    if (code == FW_CODE_TRANFER_ERROR) logMsg(mgr, "固件更新失败");
    return 0;
}

int CanManager_FirmwareUpgrade(CanManager* mgr, const wchar_t* fileName, int testMode) {
    if (!mgr || !ensureDriver(mgr) || !checkChannel(mgr)) return 0;
    EnterCriticalSection(&mgr->criticalSection);
    LeaveCriticalSection(&mgr->criticalSection);
    return PCAN_FirmwareUpgrade(mgr, fileName, testMode);
}

int CanManager_DetectDevice(CanManager* mgr, TPCANHandle* channels, int maxCount) {
    if (!ensureDriver(mgr)) return 0;
    int count = 0;
    for (int i = 0; i < 16 && count < maxCount; i++) {
        TPCANHandle ch = PCAN_NONEBUS;
        char param[64];
        sprintf(param, "devicetype=pcan_usb,controllernumber=%d", i);
        if (Pcan_LookUpChannel(param, &ch) == PCAN_ERROR_OK && ch != PCAN_NONEBUS)
            channels[count++] = ch;
    }
    char buf[64];
    sprintf(buf, "查询到 %d 个可用 CAN 设备", count);
    logMsg(mgr, buf);
    return count;
}
