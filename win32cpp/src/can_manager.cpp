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
        appendLog("CAN 连接已存在, 请勿重复连接");
        return true;
    }

    // 检查是否为虚拟 CAN
    if (channel == VIRTUAL_CAN_CHANNEL) {
        m_channel = channel;
        LeaveCriticalSection(&m_criticalSection);
        appendLog("虚拟 CAN 连接成功 (测试模式)");
        return true;
    }

    TPCANStatus status = CAN_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN 初始化失败");
        return false;
    } else {
        m_channel = channel;
        LeaveCriticalSection(&m_criticalSection);
        char logMsg[32];
        sprintf(logMsg, "CAN(id=%xh) 连接成功", channel);
        appendLog(logMsg);
        CAN_FilterMessages(channel, PLATFORM_TX, PLATFORM_TX, PCAN_MODE_STANDARD);
        return true;
    }
}

void CanManager::disconnect(void) {
    EnterCriticalSection(&m_criticalSection);

    char logMsg[64];
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        sprintf(logMsg, "虚拟 CAN 连接已断开");
    } else {
        sprintf(logMsg, "CAN(id=%xh) 连接已断开", m_channel);
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
        appendLog("CAN已断开连接, 请重新连接");
        return 0;
    }

    // 虚拟 CAN 模式：返回模拟版本
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        appendLog("固件版本: v1.0.0 (虚拟 CAN)");
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
        appendLog("CAN 发送失败");
        return 0;
    }
    uint32_t code, version;
    if (!CAN_WaitForResponse(&code, &version, 5000)) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN 读取失败，超时！！");
        return 0;
    }

    if (code == FW_CODE_VERSION) {
        char buf[64];
        sprintf(buf, "固件版本: v%u.%u.%u", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        appendLog(buf);
        LeaveCriticalSection(&m_criticalSection);
        return version;
    }

    LeaveCriticalSection(&m_criticalSection);
    appendLog("CAN 读取失败，数据错误！！");
    return 0;
}

bool CanManager::boardReboot(void) {
    EnterCriticalSection(&m_criticalSection);
    if (m_channel==PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN已断开连接, 请重新连接");
        return false;
    }

    // 虚拟 CAN 模式：模拟重启
    if (m_channel == VIRTUAL_CAN_CHANNEL) {
        appendLog("虚拟板卡重启成功");
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
        appendLog("CAN 发送失败");
        return false;
    }

    return true;
}


// 虚拟 CAN 固件升级
bool CanManager::virtualCAN_FirmwareUpgrade(const wchar_t* fileName) {
    char logMsg[256];
    appendLog("虚拟 CAN 模式：模拟固件升级...");

    // 打开源文件
    HANDLE hSrcFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSrcFile == INVALID_HANDLE_VALUE) {
        appendLog("无法打开源固件文件");
        return false;
    }

    // 创建输出文件
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
        appendLog("无法创建输出文件");
        return false;
    }

    DWORD fileSize = GetFileSize(hSrcFile, NULL);
    sprintf(logMsg, "开始固件升级, 固件大小: %u 字节", fileSize);
    appendLog(logMsg);
    appendLog("输出文件: virtual_firmware.bin");

    // 模拟 Flash 擦除
    Sleep(500);
    appendLog("Flash 擦除完成");

    // 复制文件并模拟进度
    BYTE buffer[4096];
    DWORD bytesRead, bytesWritten;
    DWORD totalBytes = 0;

    while (ReadFile(hSrcFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        WriteFile(hDstFile, buffer, bytesRead, &bytesWritten, NULL);
        totalBytes += bytesRead;

        // 每 64 字节更新一次进度
        if (totalBytes % 64 == 0 || totalBytes == fileSize) {
            if (progress_call) {
                int percent = (int)(totalBytes * 100 / fileSize);
                progress_call(percent);
            }
            // 模拟一些延迟
            if (totalBytes % 1024 == 0) {
                Sleep(10);
            }
        }
    }

    CloseHandle(hSrcFile);
    CloseHandle(hDstFile);

    Sleep(200);
    appendLog("固件发送完成");
    Sleep(200);
    appendLog("固件确认完成");

    sprintf(logMsg, "虚拟固件已保存到: %s", outputFileName);
    appendLog(logMsg);

    return true;
}

// PCAN 固件升级
bool CanManager::pcan_FirmwareUpgrade(const wchar_t* fileName, bool testMode) {
    char logMsg[128];
    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        appendLog("无法打开固件文件");
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    sprintf(logMsg, "开始固件升级, 固件大小: %u 字节", fileSize);
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
        appendLog("发送固件大小失败");
        return false;
    }

    uint32_t code, offset;
    if (!CAN_WaitForResponse(&code, &offset, 15000)) {
        CloseHandle(hFile);
        appendLog("Flash擦除超时");
        return false;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        sprintf(logMsg, "Flash擦除失败: code(%u), offset(%u)", code, offset);
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
            appendLog("发送文件数据失败");
            return false;
        }

        bytesSent += bytesRead;

        if (bytesSent % 64 == 0 || bytesSent == fileSize) {
            // 更新进度
            if (progress_call) {
                int percent = (int)(bytesSent * 100 / fileSize);
                progress_call(percent);
            }

            if (!CAN_WaitForResponse(&code, &offset, 5000)) {
                CloseHandle(hFile);
                appendLog("固件更新超时!");
                return false;
            }

            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) break;
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                sprintf(logMsg, "固件升级失败: code(%u), offset(%u)", code, offset);
                appendLog(logMsg);
                return false;
            }
        }
    }

    CloseHandle(hFile);

    // 设置进度为 100%
    if (progress_call) {
        progress_call(100);
    }

    msg.ID = PLATFORM_RX;
    frame->code = BOARD_CONFIRM;
    frame->val = testMode ? 0 : 1;
    if (CAN_Write(m_channel, &msg) != PCAN_ERROR_OK) {
        appendLog("固件发送确认失败!");
        return false;
    }

    if (!CAN_WaitForResponse(&code, &offset, 30000)) {
        appendLog("固件确认超时!");
        return false;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        appendLog("固件上传完成. 点击重启，板卡将在45-60秒内完成重启");
        return true;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        appendLog("固件更新失败");
    }
    return false;
}

// 固件升级（主调度函数）
bool CanManager::firmwareUpgrade(const wchar_t* fileName, bool testMode)
{
    EnterCriticalSection(&m_criticalSection);

    if (m_channel == PCAN_NONEBUS) {
        LeaveCriticalSection(&m_criticalSection);
        appendLog("CAN已断开连接, 请重新连接");
        return false;
    }

    LeaveCriticalSection(&m_criticalSection);

    // 根据通道类型选择升级方式
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
    sprintf(msg, "查询到 %d 个可用CAN设备", count);
    appendLog(msg);
    return count;
}
