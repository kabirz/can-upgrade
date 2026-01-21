#include "can_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

// Include PCAN-Basic SDK header
#include <PCANBasic.h>

// CAN Manager implementation structure
struct CANManager {
    bool connected;
    char iface[32];
    char channel[32];
    int baudRate;
    TPCANHandle pcanChannel;
    CAN_STATUS_CALLBACK statusCallback;
    void* statusUserData;
    CAN_ERROR_CALLBACK errorCallback;
    void* errorUserData;
    CAN_PROGRESS_CALLBACK progressCallback;
    void* progressUserData;
    CAN_VERSION_CALLBACK versionCallback;
    void* versionUserData;
    CAN_CONNECTED_CALLBACK connectedCallback;
    void* connectedUserData;
    CRITICAL_SECTION lock;
};

// Helper: Convert channel string to PCAN handle
static TPCANHandle GetPCANHandle(const char* channel) {
    if (strcmp(channel, "usb1") == 0) return PCAN_USBBUS2;
    if (strcmp(channel, "usb2") == 0) return PCAN_USBBUS3;
    if (strcmp(channel, "usb3") == 0) return PCAN_USBBUS4;
    return PCAN_USBBUS1;  // default usb0
}

// Helper: Convert baud rate to PCAN constant
static TPCANBaudrate GetPCANBaudrate(int baudRate) {
    switch (baudRate) {
        case 10000:   return PCAN_BAUD_10K;
        case 20000:   return PCAN_BAUD_20K;
        case 50000:   return PCAN_BAUD_50K;
        case 80000:   return PCAN_BAUD_83K;
        case 125000:  return PCAN_BAUD_125K;
        case 500000:  return PCAN_BAUD_500K;
        case 1000000: return PCAN_BAUD_1M;
        default:      return PCAN_BAUD_250K;
    }
}

CANManager* CAN_Create(void) {
    CANManager* mgr = (CANManager*)malloc(sizeof(CANManager));
    if (!mgr) return NULL;

    memset(mgr, 0, sizeof(CANManager));
    InitializeCriticalSection(&mgr->lock);
    CreateEvent(NULL, FALSE, FALSE, NULL);

    strcpy(mgr->iface, "pcan");
    strcpy(mgr->channel, "usb0");
    mgr->baudRate = CAN_BAUD_250K;

    return mgr;
}

void CAN_Destroy(CANManager* mgr) {
    if (!mgr) return;
    CAN_Disconnect(mgr);
    DeleteCriticalSection(&mgr->lock);
    free(mgr);
}

void CAN_SetStatusCallback(CANManager* mgr, CAN_STATUS_CALLBACK cb, void* user_data) {
    if (mgr) {
        mgr->statusCallback = cb;
        mgr->statusUserData = user_data;
    }
}

void CAN_SetErrorCallback(CANManager* mgr, CAN_ERROR_CALLBACK cb, void* user_data) {
    if (mgr) {
        mgr->errorCallback = cb;
        mgr->errorUserData = user_data;
    }
}

void CAN_SetProgressCallback(CANManager* mgr, CAN_PROGRESS_CALLBACK cb, void* user_data) {
    if (mgr) {
        mgr->progressCallback = cb;
        mgr->progressUserData = user_data;
    }
}

void CAN_SetVersionCallback(CANManager* mgr, CAN_VERSION_CALLBACK cb, void* user_data) {
    if (mgr) {
        mgr->versionCallback = cb;
        mgr->versionUserData = user_data;
    }
}

void CAN_SetConnectedCallback(CANManager* mgr, CAN_CONNECTED_CALLBACK cb, void* user_data) {
    if (mgr) {
        mgr->connectedCallback = cb;
        mgr->connectedUserData = user_data;
    }
}

CAN_RESULT CAN_Connect(CANManager* mgr, const char* canInterface, const char* channel, int baudRate) {
    if (!mgr) return CAN_ERROR_INVALID_PARAM;

    EnterCriticalSection(&mgr->lock);

    if (mgr->connected) {
        CAN_Disconnect(mgr);
    }

    if (canInterface) strncpy(mgr->iface, canInterface, sizeof(mgr->iface) - 1);
    if (channel) strncpy(mgr->channel, channel, sizeof(mgr->channel) - 1);
    mgr->baudRate = baudRate;
    mgr->pcanChannel = GetPCANHandle(mgr->channel);

    TPCANStatus status = CAN_Initialize(mgr->pcanChannel, GetPCANBaudrate(mgr->baudRate), PCAN_TYPE_ISA, 0, 0);
    if (status != PCAN_ERROR_OK && status != PCAN_ERROR_CAUTION) {
        if (mgr->errorCallback) {
            char errMsg[64];
            sprintf(errMsg, "PCAN_Initialize failed: 0x%08X", status);
            mgr->errorCallback(errMsg, mgr->errorUserData);
        }
        LeaveCriticalSection(&mgr->lock);
        return CAN_ERROR_INIT_FAILED;
    }

    mgr->connected = true;

    if (mgr->statusCallback) {
        char msg[64];
        sprintf(msg, "Connected to %s:%s, baudrate %d", mgr->iface, mgr->channel, mgr->baudRate);
        mgr->statusCallback(msg, mgr->statusUserData);
    }

    if (mgr->connectedCallback) {
        mgr->connectedCallback(true, mgr->connectedUserData);
    }

    LeaveCriticalSection(&mgr->lock);
    return CAN_OK;
}

void CAN_Disconnect(CANManager* mgr) {
    if (!mgr) return;

    EnterCriticalSection(&mgr->lock);

    if (mgr->connected) {
        CAN_Uninitialize(mgr->pcanChannel);
        mgr->connected = false;

        if (mgr->connectedCallback) {
            mgr->connectedCallback(false, mgr->connectedUserData);
        }

        if (mgr->statusCallback) {
            mgr->statusCallback("Disconnected from CAN bus", mgr->statusUserData);
        }
    }

    LeaveCriticalSection(&mgr->lock);
}

bool CAN_IsConnected(const CANManager* mgr) {
    return mgr ? mgr->connected : false;
}

CAN_RESULT CAN_SendCommand(CANManager* mgr, uint32_t canId, uint32_t cmd, uint32_t param) {
    if (!mgr || !mgr->connected) return CAN_ERROR_NOT_CONNECTED;

    TPCANMsg msg;
    msg.ID = canId;
    msg.MSGTYPE = PCAN_MESSAGE_STANDARD;
    msg.LEN = 8;
    msg.DATA[0] = cmd & 0xFF;
    msg.DATA[1] = (cmd >> 8) & 0xFF;
    msg.DATA[2] = (cmd >> 16) & 0xFF;
    msg.DATA[3] = (cmd >> 24) & 0xFF;
    msg.DATA[4] = param & 0xFF;
    msg.DATA[5] = (param >> 8) & 0xFF;
    msg.DATA[6] = (param >> 16) & 0xFF;
    msg.DATA[7] = (param >> 24) & 0xFF;

    return (CAN_Write(mgr->pcanChannel, &msg) == PCAN_ERROR_OK) ? CAN_OK : CAN_ERROR_WRITE_FAILED;
}

CAN_RESULT CAN_SendData(CANManager* mgr, const uint8_t* data, uint8_t len) {
    if (!mgr || !mgr->connected) return CAN_ERROR_NOT_CONNECTED;
    if (len > 8) len = 8;

    TPCANMsg msg;
    msg.ID = FW_DATA_RX;
    msg.MSGTYPE = PCAN_MESSAGE_STANDARD;
    msg.LEN = len;
    memcpy(msg.DATA, data, len);

    return (CAN_Write(mgr->pcanChannel, &msg) == PCAN_ERROR_OK) ? CAN_OK : CAN_ERROR_WRITE_FAILED;
}

CAN_RESULT CAN_WaitForResponse(CANManager* mgr, uint32_t* code, uint32_t* param, int timeoutMs) {
    if (!mgr || !mgr->connected) return CAN_ERROR_NOT_CONNECTED;

    TPCANMsg msg;
    TPCANTimestamp timestamp;
    DWORD startTime = GetTickCount();

    while ((int)(GetTickCount() - startTime) < timeoutMs) {
        TPCANStatus status = CAN_Read(mgr->pcanChannel, &msg, &timestamp);
        if (status == PCAN_ERROR_OK && msg.ID == PLATFORM_TX) {
            *code = msg.DATA[0] | (msg.DATA[1] << 8) | (msg.DATA[2] << 16) | (msg.DATA[3] << 24);
            *param = msg.DATA[4] | (msg.DATA[5] << 8) | (msg.DATA[6] << 16) | (msg.DATA[7] << 24);
            return CAN_OK;
        } else if (status == PCAN_ERROR_QRCVEMPTY) {
            Sleep(1);
        } else {
            break;
        }
    }

    if (mgr->errorCallback) {
        mgr->errorCallback("CAN response timeout", mgr->errorUserData);
    }
    return CAN_ERROR_TIMEOUT;
}

bool CAN_FirmwareUpgrade(CANManager* mgr, const char* fileName, bool testMode) {
    if (!mgr || !mgr->connected) {
        if (mgr->errorCallback) mgr->errorCallback("CAN not connected", mgr->errorUserData);
        return false;
    }

    HANDLE hFile = CreateFileA(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (mgr->errorCallback) {
            char errMsg[128];
            sprintf(errMsg, "Cannot open file: %s", fileName);
            mgr->errorCallback(errMsg, mgr->errorUserData);
        }
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (mgr->statusCallback) {
        char msg[64];
        sprintf(msg, "Starting firmware upgrade, size: %u bytes", fileSize);
        mgr->statusCallback(msg, mgr->statusUserData);
    }

    if (CAN_SendCommand(mgr, PLATFORM_RX, BOARD_START_UPDATE, fileSize) != CAN_OK) {
        CloseHandle(hFile);
        return false;
    }

    uint32_t code, offset;
    if (CAN_WaitForResponse(mgr, &code, &offset, 5000) != CAN_OK) {
        CloseHandle(hFile);
        if (mgr->errorCallback) mgr->errorCallback("Flash erase: timeout", mgr->errorUserData);
        return false;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        if (mgr->errorCallback) {
            char errMsg[64];
            sprintf(errMsg, "Flash erase failed: code(%u), offset(%u)", code, offset);
            mgr->errorCallback(errMsg, mgr->errorUserData);
        }
        return false;
    }

    DWORD bytesSent = 0;
    uint8_t buffer[8];
    DWORD bytesRead;

    while (ReadFile(hFile, buffer, 8, &bytesRead, NULL) && bytesRead > 0) {
        if (CAN_SendData(mgr, buffer, (uint8_t)bytesRead) != CAN_OK) {
            CloseHandle(hFile);
            if (mgr->errorCallback) mgr->errorCallback("Failed to send data frame", mgr->errorUserData);
            return false;
        }

        bytesSent += bytesRead;

        if (bytesSent % 64 == 0 || bytesSent == fileSize) {
            if (mgr->progressCallback) {
                int percentage = (int)((bytesSent * 100) / fileSize);
                mgr->progressCallback(percentage, mgr->progressUserData);
            }

            if (CAN_WaitForResponse(mgr, &code, &offset, 5000) != CAN_OK) {
                CloseHandle(hFile);
                if (mgr->errorCallback) mgr->errorCallback("Firmware upload: timeout", mgr->errorUserData);
                return false;
            }

            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) break;
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                if (mgr->errorCallback) {
                    char errMsg[64];
                    sprintf(errMsg, "Firmware upload failed: code(%u), offset(%u)", code, offset);
                    mgr->errorCallback(errMsg, mgr->errorUserData);
                }
                return false;
            }
        }
    }

    CloseHandle(hFile);

    uint32_t confirmParam = testMode ? 0 : 1;
    if (CAN_SendCommand(mgr, PLATFORM_RX, BOARD_CONFIRM, confirmParam) != CAN_OK) {
        return false;
    }

    if (CAN_WaitForResponse(mgr, &code, &offset, 30000) != CAN_OK) {
        if (mgr->errorCallback) mgr->errorCallback("Confirm: timeout", mgr->errorUserData);
        return false;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        if (mgr->statusCallback) {
            char msg[128];
            sprintf(msg, "File %s upload complete. Board will reboot in about 45-90 seconds", fileName);
            mgr->statusCallback(msg, mgr->statusUserData);
        }
        return true;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        if (mgr->errorCallback) mgr->errorCallback("Upload failed", mgr->errorUserData);
        return false;
    }

    return false;
}

bool CAN_FirmwareVersion(CANManager* mgr) {
    if (!mgr || !mgr->connected) return false;

    if (CAN_SendCommand(mgr, PLATFORM_RX, BOARD_VERSION, 0) != CAN_OK) return false;

    uint32_t code, version;
    if (CAN_WaitForResponse(mgr, &code, &version, 5000) != CAN_OK) return false;

    if (code == FW_CODE_VERSION) {
        if (mgr->versionCallback) {
            char verStr[32];
            sprintf(verStr, "v%u.%u.%u", (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
            mgr->versionCallback(verStr, mgr->versionUserData);
        }
        return true;
    }
    return false;
}

bool CAN_BoardReboot(CANManager* mgr) {
    if (!mgr || !mgr->connected) return false;

    if (CAN_SendCommand(mgr, PLATFORM_RX, BOARD_REBOOT, 0) != CAN_OK) return false;

    if (mgr->statusCallback) {
        mgr->statusCallback("Board reboot command sent", mgr->statusUserData);
    }
    return true;
}

// Device detection using CAN_GetValue with PCAN_CHANNEL_CONDITION
bool CAN_DetectDevice(const char* channel) {
    TPCANHandle h = GetPCANHandle(channel);
    DWORD condition = 0;
    TPCANStatus status = CAN_GetValue(h, PCAN_CHANNEL_CONDITION, &condition, sizeof(condition));
    return (status == PCAN_ERROR_OK && (condition & 0x02));  // 0x02 = PCAN_CHANNEL_AVAILABLE
}
