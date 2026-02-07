#include <can_manager.h>
#include <cstdio>

CanManager& CanManager::getInstance() {
    static CanManager instance;
    return instance;
}

void CanManager::setCallback(msgCallback msg_call) {
    append_msg = msg_call;
}

void CanManager::setProgressCallback(progressCallback progress_call) {
    this->progress_call = progress_call;
}

void CanManager::appendLog(const char* msg)
{
    if (append_msg) {
        append_msg(msg);
    }
}

bool CanManager::connect(TPCANHandle channel, TPCANBaudrate baudrate) {
    EnterCriticalSection(&m_criticalSection);

    if (m_channel != PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN connection already exists, do not connect again");
        return true;
    }

    // Check if virtual CAN
    if (channel == VIRTUAL_CAN_CHANNEL) {
        m_channel = channel;
        LeaveCriticalSection(&m_criticalSection);
        appendLog("Virtual CAN connected successfully (Test Mode)");
        return true;
    }

    TPCANStatus status = CAN_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN initialization failed");
        return false;
    } else {
        m_channel = channel;
        LeaveCriticalSection(&m_criticalSection);
        char logMsg[32];
        sprintf(logMsg, "CAN(id=%xh) connected successfully", channel);
        appendLog(logMsg);
        CAN_FilterMessages(channel, PLATFORM_TX, PLATFORM_TX, PCAN_MODE_STANDARD);
        return true;
    }
}

void CanManager::disconnect(void) {
    EnterCriticalSection(&m_criticalSection);

    char logMsg[64];
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        sprintf(logMsg, "Virtual CAN disconnected");
    } else {
        sprintf(logMsg, "CAN(id=%xh) disconnected", m_channel);
        CAN_Uninitialize(m_channel);
    }
    m_channel = PCAN_NONEBUS;
    LeaveCriticalSection(&m_criticalSection);
    appendLog(logMsg);
}

bool CanManager::CAN_WaitForResponse(uint32_t* code, uint32_t* param, int timeoutMs) {

    TPCANMsg msg;
    TPCANTimestamp timestamp;
    DWORD startTime = GetTickCount();

    while ((int)(GetTickCount() - startTime) < timeoutMs) {
        TPCANStatus status = CAN_Read(m_channel, &msg, &timestamp);
        if (status == PCAN_ERROR_OK && msg.ID == PLATFORM_TX) {
            can_frame_t *frame = reinterpret_cast<can_frame_t*>(msg.DATA);
            *code = frame->code;
            *param = frame->val;
            return true;
        } else if (status == PCAN_ERROR_QRCVEMPTY) {
            Sleep(1);
        }
    }
    return false;
}


uint32_t CanManager::getFirmwareVersion(void) {
    EnterCriticalSection(&m_criticalSection);

    if (m_channel==PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN disconnected, please reconnect");
        return 0;
    }

    // Virtual CAN mode: return simulated version
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        appendLog("Firmware version: v1.0.0 (Virtual CAN)");
        LeaveCriticalSection(&m_criticalSection);
        return 0x01000000;  // v1.0.0
    }

    TPCANMsg msg = {
        .ID = PLATFORM_RX,
        .MSGTYPE = PCAN_MODE_STANDARD,
        .LEN = 8,
    };
    can_frame_t *frame = reinterpret_cast<can_frame_t*>(msg.DATA);
    frame->code = BOARD_VERSION;

    TPCANStatus status = CAN_Write(m_channel, &msg);
    if (status != PCAN_ERROR_OK) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN send failed");
        return 0;
    }
    uint32_t code, version;
    if (!CAN_WaitForResponse(&code, &version, 5000)) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN read failed, timeout!!");
        return 0;
    }

    if (code == FW_CODE_VERSION) {
        char buf[64];
        sprintf(buf, "Firmware version: v%u.%u.%u", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        appendLog(buf);
        LeaveCriticalSection(&m_criticalSection);
        return version;
    }

    LeaveCriticalSection(&m_criticalSection);
    appendLog("CAN read failed, data error!!");
    return 0;
}

bool CanManager::boardReboot(void) {
    EnterCriticalSection(&m_criticalSection);
    if (m_channel==PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN disconnected, please reconnect");
        return false;
    }

    // Virtual CAN mode: simulate reboot
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        appendLog("Virtual board reboot successful");
        LeaveCriticalSection(&m_criticalSection);
        return true;
    }

    TPCANMsg msg = {
        .ID = PLATFORM_RX,
        .MSGTYPE = PCAN_MODE_STANDARD,
        .LEN = 8,
    };
    can_frame_t *frame = reinterpret_cast<can_frame_t*>(msg.DATA);
    frame->code = BOARD_REBOOT;

    TPCANStatus status = CAN_Write(m_channel, &msg);
    LeaveCriticalSection(&m_criticalSection);
    if (status != PCAN_ERROR_OK) {
        appendLog("CAN send failed");
        return false;
    }

    return true;
}


// Virtual CAN firmware upgrade
bool CanManager::virtualCAN_FirmwareUpgrade(const wchar_t* fileName) {
    char logMsg[256];
    appendLog("Virtual CAN mode: Simulating firmware upgrade...");

    // Open source file
    HANDLE hSrcFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSrcFile == INVALID_HANDLE_VALUE) {
        appendLog("Cannot open source firmware file");
        return false;
    }

    // Create output file
    char outputFileName[256];
    GetModuleFileNameA(NULL, outputFileName, 256);
    char* lastSlash = strrchr(outputFileName, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    strcat(outputFileName, "virtual_firmware.bin");

    HANDLE hDstFile = CreateFileA(outputFileName, GENERIC_WRITE, 0, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDstFile == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrcFile);
        appendLog("Cannot create output file");
        return false;
    }

    DWORD fileSize = GetFileSize(hSrcFile, NULL);
    sprintf(logMsg, "Starting firmware upgrade, firmware size: %u bytes", fileSize);
    appendLog(logMsg);
    appendLog("Output file: virtual_firmware.bin");

    // Simulate Flash erase
    Sleep(500);
    appendLog("Flash erase completed");

    // Copy file and simulate progress
    BYTE buffer[4096];
    DWORD bytesRead, bytesWritten;
    DWORD totalBytes = 0;

    while (ReadFile(hSrcFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        WriteFile(hDstFile, buffer, bytesRead, &bytesWritten, NULL);
        totalBytes += bytesRead;

        // Update progress every 64 bytes
        if (totalBytes % 64 == 0 || totalBytes == fileSize) {
            if (progress_call) {
                int percent = (int)(totalBytes * 100 / fileSize);
                progress_call(percent);
            }
            // Simulate some delay
            if (totalBytes % 1024 == 0) {
                Sleep(10);
            }
        }
    }

    CloseHandle(hSrcFile);
    CloseHandle(hDstFile);

    Sleep(200);
    appendLog("Firmware send completed");
    Sleep(200);
    appendLog("Firmware confirmation completed");

    sprintf(logMsg, "Virtual firmware saved to: %s", outputFileName);
    appendLog(logMsg);

    return true;
}

// PCAN firmware upgrade
bool CanManager::pcan_FirmwareUpgrade(const wchar_t* fileName, bool testMode) {
    char logMsg[128];
    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        appendLog("Cannot open firmware file");
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    sprintf(logMsg, "Starting firmware upgrade, firmware size: %u bytes", fileSize);
    appendLog(logMsg);

    TPCANMsg msg = {
        .ID = PLATFORM_RX,
        .MSGTYPE = PCAN_MODE_STANDARD,
        .LEN = 8,
    };
    can_frame_t *frame = reinterpret_cast<can_frame_t*>(msg.DATA);
    frame->code = BOARD_START_UPDATE;
    frame->val = fileSize;
    TPCANStatus status = CAN_Write(m_channel, &msg);
    if (status != PCAN_ERROR_OK) {
        CloseHandle(hFile);
        appendLog("Failed to send firmware size");
        return false;
    }

    uint32_t code, offset;
    if (!CAN_WaitForResponse(&code, &offset, 15000)) {
        CloseHandle(hFile);
        appendLog("Flash erase timeout");
        return false;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        sprintf(logMsg, "Flash erase failed: code(%u), offset(%u)", code, offset);
        appendLog(logMsg);
        return false;
    }

    DWORD bytesSent = 0;
    DWORD bytesRead;

    msg.ID = FW_DATA_RX;
    while (ReadFile(hFile, msg.DATA, 8, &bytesRead, NULL) && bytesRead > 0) {
        msg.LEN = bytesRead;
        if (CAN_Write(m_channel, &msg) != PCAN_ERROR_OK) {
            CloseHandle(hFile);
            appendLog("Failed to send file data");
            return false;
        }

        bytesSent += bytesRead;

        if (bytesSent % 64 == 0 || bytesSent == fileSize) {
            // Update progress
            if (progress_call) {
                int percent = (int)(bytesSent * 100 / fileSize);
                progress_call(percent);
            }

            if (!CAN_WaitForResponse(&code, &offset, 5000)) {
                CloseHandle(hFile);
                appendLog("Firmware update timeout!");
                return false;
            }

            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) break;
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                sprintf(logMsg, "Firmware upgrade failed: code(%u), offset(%u)", code, offset);
                appendLog(logMsg);
                return false;
            }
        }
    }

    CloseHandle(hFile);

    msg.ID = PLATFORM_RX;
    frame->code = BOARD_CONFIRM;
    frame->val = testMode ? 0 : 1;
    if (CAN_Write(m_channel, &msg) != PCAN_ERROR_OK) {
        appendLog("Firmware confirmation send failed!");
        return false;
    }

    if (!CAN_WaitForResponse(&code, &offset, 30000)) {
        appendLog("Firmware confirmation timeout!");
        return false;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        appendLog("Firmware upload completed. Click reboot, board will complete in 45-60 seconds");
        return true;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        appendLog("Firmware update failed");
    }
    return false;
}

// Firmware upgrade (main dispatch function)
bool CanManager::firmwareUpgrade(const wchar_t* fileName, bool testMode)
{
    EnterCriticalSection(&m_criticalSection);

    if (m_channel == PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN disconnected, please reconnect");
        return false;
    }

    LeaveCriticalSection(&m_criticalSection);

    // Select upgrade method based on channel type
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        return virtualCAN_FirmwareUpgrade(fileName);
    } else {
        return pcan_FirmwareUpgrade(fileName, testMode);
    }
}

int CanManager::detectDevice(TPCANHandle* channels, int maxCount)
{
    int count = 0;
    char msg[64];
    for (int i = 0; i < 16 && count < maxCount; i++) {
        TPCANHandle channel = PCAN_NONEBUS;
        sprintf(msg, "devicetype=pcan_usb,controllernumber=%d", i);
        TPCANStatus result = CAN_LookUpChannel(msg, &channel);
        if (result == PCAN_ERROR_OK && channel != PCAN_NONEBUS) {
            channels[count++] = channel;
        }
    }
    sprintf(msg, "Found %d device(s)", count);
    appendLog(msg);
    return count;
}
