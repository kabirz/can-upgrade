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
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_channel != PCAN_NONEBUS) {
        appendLog("CAN 连接已存在, 请勿重复连接");
        return true;
    }

    TPCANStatus status = CAN_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        appendLog("CAN 初始化失败");
        return false;
    } else {
        m_channel = channel;
        appendLog("CAN 连接成功");
        return true;
    }
}

void CanManager::disconnect(void) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CAN_Uninitialize(m_channel);
    m_channel = PCAN_NONEBUS;
    appendLog("CAN 连接已断开");
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
        } else {
            break;
        }
    }
    return false;
}


uint32_t CanManager::getFirmwareVersion(void) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_channel==PCAN_NONEBUS) {
        appendLog("CAN已断开连接, 请重新连接");
        return 0;
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
        appendLog("CAN 发送失败");
        return 0;
    }
    uint32_t code, version;
    if (!CAN_WaitForResponse(&code, &version, 5000)) {
        appendLog("CAN 读取失败，超时！！");
        return 0;
    }

    if (code == FW_CODE_VERSION) {
        char buf[64];
        sprintf(buf, "固件版本: v%u.%u.%u", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        appendLog(buf);
        return version;
    }

    appendLog("CAN 读取失败，数据错误！！");
    return 0;
}

bool CanManager::boardReboot(void) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_channel==PCAN_NONEBUS) {
        appendLog("CAN已断开连接, 请重新连接");
        return false;
    }

    TPCANMsg msg = {
        .ID = PLATFORM_RX,
        .MSGTYPE = PCAN_MODE_STANDARD,
        .LEN = 8,
    };
    can_frame_t *frame = reinterpret_cast<can_frame_t*>(msg.DATA);
    frame->code = BOARD_REBOOT;

    TPCANStatus status = CAN_Write(m_channel, &msg);
    if (status != PCAN_ERROR_OK) {
        appendLog("CAN 发送失败");
        return false;
    }

    return 0;
}


bool CanManager::firmwareUpgrade(const char* fileName, bool testMode)
{
    char logMsg[128];
    HANDLE hFile = CreateFileA(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        sprintf(logMsg, "无法打开文件: %s", fileName);
        appendLog(logMsg);
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
                return false;
            }
        }
    }

    CloseHandle(hFile);

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
        sprintf(logMsg, "文件 %s 上传完成. 点击重启，板卡将在45-60秒内完成重启", fileName);
        appendLog(logMsg);
        return true;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        appendLog("固件更新失败");
    }
    return false;
}

std::vector<TPCANHandle> CanManager::detectDevice(void)
{
    std::vector<TPCANHandle> v;
    char msg[64];
    for (int i = 0; i < 16; i++) {
        TPCANHandle channel = PCAN_NONEBUS;
        sprintf(msg, "devicetype=pcan_usb,controllernumber=%d", i);
        TPCANStatus result = CAN_LookUpChannel(msg, &channel);
        if (result == PCAN_ERROR_OK && channel != PCAN_NONEBUS) {
            v.push_back(channel);
        }
 
    }
    sprintf(msg, "查询到%u个设备", v.size());
    appendLog(msg);
    return v;
}
