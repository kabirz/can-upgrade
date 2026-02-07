#include "uart_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setupapi.h>
#include <devguid.h>
#include <initguid.h>
#include <devpkey.h>

// Need to link setupapi.lib
#pragma comment(lib, "setupapi.lib")

// CRC16-CCITT calculation function
static uint16_t CalcCRC16(const uint8_t* data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Internal structure definition
struct UartManager {
    CRITICAL_SECTION criticalSection;
    HANDLE hSerial;
    char portName[MAX_PORT_NAME_LEN];
    DWORD baudRate;
    UartMsgCallback msgCallback;
    UartProgressCallback progressCallback;
    int initialized;
};

// Create UART manager
UartManager* UartManager_Create(void) {
    UartManager* mgr = (UartManager*)malloc(sizeof(UartManager));
    if (!mgr) return NULL;

    InitializeCriticalSection(&mgr->criticalSection);
    mgr->hSerial = INVALID_HANDLE_VALUE;
    mgr->portName[0] = '\0';
    mgr->baudRate = 115200;
    mgr->msgCallback = NULL;
    mgr->progressCallback = NULL;
    mgr->initialized = 1;

    return mgr;
}

// Destroy UART manager
void UartManager_Destroy(UartManager* mgr) {
    if (!mgr) return;
    if (mgr->initialized) {
        DeleteCriticalSection(&mgr->criticalSection);
    }
    free(mgr);
}

// Set log callback function
void UartManager_SetCallback(UartManager* mgr, UartMsgCallback msg_call) {
    if (mgr) {
        mgr->msgCallback = msg_call;
    }
}

// Set progress callback function
void UartManager_SetProgressCallback(UartManager* mgr, UartProgressCallback progress_call) {
    if (mgr) {
        mgr->progressCallback = progress_call;
    }
}

// Append log
static void AppendLog(UartManager* mgr, const char* msg) {
    if (mgr && mgr->msgCallback) {
        mgr->msgCallback(msg);
    }
}

// Build UART frame
static int BuildUARTFrame(uint8_t type, const uint8_t* data, int dataLen, uint8_t* outFrame, int maxLen) {
    if (dataLen > 8) return 0;  // Data length limit

    int idx = 0;
    outFrame[idx++] = FRAME_HEAD;
    outFrame[idx++] = type;
    // Big endian length
    outFrame[idx++] = (dataLen >> 8) & 0xFF;
    outFrame[idx++] = dataLen & 0xFF;
    // Data
    if (dataLen > 0) {
        memcpy(&outFrame[idx], data, dataLen);
        idx += dataLen;
    }
    // CRC16 (big endian)
    uint16_t crc = CalcCRC16(data, dataLen);
    outFrame[idx++] = (crc >> 8) & 0xFF;
    outFrame[idx++] = crc & 0xFF;
    outFrame[idx++] = FRAME_TAIL;

    return idx;
}

// Parse UART frame
static int ParseUARTFrame(const uint8_t* buffer, int bufLen, uint8_t* outType, uint8_t* outData, int* outDataLen) {
    if (bufLen < 7) return 0;  // Minimum frame: HEAD(1) + TYPE(1) + LEN(2) + CRC(2) + TAIL(1)

    int idx = 0;

    // Find frame header
    while (idx < bufLen && buffer[idx] != FRAME_HEAD) {
        idx++;
    }
    if (idx >= bufLen - 6) return 0;  // Not enough bytes

    int frameStart = idx;
    idx++;  // Skip HEAD

    uint8_t frameType = buffer[idx++];
    uint16_t dataLen = ((uint16_t)buffer[idx] << 8) | buffer[idx + 1];
    idx += 2;

    if (dataLen > 8) {
        return -(frameStart + 1);  // Invalid data length, return discard position
    }

    // Check if there is enough data
    int totalLen = 1 + 1 + 2 + dataLen + 2 + 1;  // HEAD + TYPE + LEN + DATA + CRC + TAIL
    if (frameStart + totalLen > bufLen) {
        return 0;  // Incomplete frame
    }

    // Extract data
    if (dataLen > 0 && outData) {
        memcpy(outData, &buffer[idx], dataLen);
    }
    idx += dataLen;
    if (outDataLen) *outDataLen = dataLen;
    if (outType) *outType = frameType;

    // Verify CRC
    uint16_t recvCRC = ((uint16_t)buffer[idx] << 8) | buffer[idx + 1];
    idx += 2;
    uint16_t calcCRC = CalcCRC16(outData, dataLen);

    if (recvCRC != calcCRC) {
        return -(frameStart + 1);  // CRC error
    }

    // Check frame tail
    if (buffer[idx] != FRAME_TAIL) {
        return -(frameStart + 1);  // Frame tail error
    }

    return totalLen;  // Return frame length
}

// Serial port write
static int UartWrite(UartManager* mgr, const uint8_t* data, int len) {
    DWORD bytesWritten;
    if (!WriteFile(mgr->hSerial, data, len, &bytesWritten, NULL) || bytesWritten != (DWORD)len) {
        return 0;
    }
    return 1;
}

// Serial port read (with timeout)
static int UartRead(UartManager* mgr, uint8_t* buffer, int maxLen, int timeoutMs, int* outRead) {
    DWORD startTime = GetTickCount();
    int totalRead = 0;

    while (totalRead < maxLen) {
        DWORD elapsed = GetTickCount() - startTime;
        if (elapsed >= (DWORD)timeoutMs) {
            *outRead = totalRead;
            return 0;  // Timeout
        }

        DWORD bytesRead;
        if (ReadFile(mgr->hSerial, &buffer[totalRead], maxLen - totalRead, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                totalRead += bytesRead;
                // If minimum frame length read, try to parse
                if (totalRead >= 7) {
                    *outRead = totalRead;
                    return 1;
                }
            }
        } else {
            // Read error
            *outRead = totalRead;
            return 0;
        }

        Sleep(1);
    }

    *outRead = totalRead;
    return 1;
}

// Wait for UART response (with frame parsing)
static int UartWaitForResponse(UartManager* mgr, uint32_t* code, uint32_t* param, int timeoutMs) {
    uint8_t buffer[256];
    int bufPos = 0;
    DWORD startTime = GetTickCount();

    while ((int)(GetTickCount() - startTime) < timeoutMs) {
        DWORD bytesRead;
        if (ReadFile(mgr->hSerial, &buffer[bufPos], sizeof(buffer) - bufPos, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                bufPos += bytesRead;

                // Try to parse frame
                uint8_t frameType;
                uint8_t data[8];
                int dataLen;

                int result = ParseUARTFrame(buffer, bufPos, &frameType, data, &dataLen);
                if (result > 0) {
                    // Successfully parsed frame
                    if (dataLen == 8) {
                        // Parse parameters (little endian)
                        *code = ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) |
                                ((uint32_t)data[1] << 8) | data[0];
                        *param = ((uint32_t)data[7] << 24) | ((uint32_t)data[6] << 16) |
                                 ((uint32_t)data[5] << 8) | data[4];
                        return 1;
                    }
                    // Remove parsed frame
                    memmove(buffer, &buffer[result], bufPos - result);
                    bufPos -= result;
                } else if (result < 0) {
                    // Discard invalid data
                    int discard = -result;
                    memmove(buffer, &buffer[discard], bufPos - discard);
                    bufPos -= discard;
                }
            }
        }
    }

    return 0;  // Timeout
}

// Connect serial port device
int UartManager_Connect(UartManager* mgr, const char* portName, DWORD baudRate) {
    if (!mgr || !mgr->initialized) return 0;

    EnterCriticalSection(&mgr->criticalSection);

    if (mgr->hSerial != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Serial port already connected, please do not connect again");
        return 1;
    }

    // Build device name
    char deviceName[32];
    if (strncmp(portName, "COM", 3) == 0 && strlen(portName) < 8) {
        sprintf(deviceName, "\\\\.\\%s", portName);
    } else {
        strncpy(deviceName, portName, sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
    }

    // Open serial port
    mgr->hSerial = CreateFileA(deviceName, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);

    if (mgr->hSerial == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&mgr->criticalSection);
        char logMsg[64];
        sprintf(logMsg, "Cannot open serial port %s", portName);
        AppendLog(mgr, logMsg);
        return 0;
    }

    // Configure serial port parameters
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(mgr->hSerial, &dcb)) {
        CloseHandle(mgr->hSerial);
        mgr->hSerial = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to get serial port state");
        return 0;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(mgr->hSerial, &dcb)) {
        CloseHandle(mgr->hSerial);
        mgr->hSerial = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to configure serial port parameters");
        return 0;
    }

    // Set timeout - non-blocking mode
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;  // Disable interval timeout
    timeouts.ReadTotalTimeoutMultiplier = 0;  // Do not use multiplier timeout
    timeouts.ReadTotalTimeoutConstant = 0;    // Return immediately (non-blocking)
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 5000;

    if (!SetCommTimeouts(mgr->hSerial, &timeouts)) {
        CloseHandle(mgr->hSerial);
        mgr->hSerial = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to set serial port timeout");
        return 0;
    }

    // Clear buffers
    PurgeComm(mgr->hSerial, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

    strncpy(mgr->portName, portName, MAX_PORT_NAME_LEN - 1);
    mgr->portName[MAX_PORT_NAME_LEN - 1] = '\0';
    mgr->baudRate = baudRate;

    char logMsg[64];
    sprintf(logMsg, "Serial port %s connected successfully (%u bps)", portName, baudRate);
    AppendLog(mgr, logMsg);

    LeaveCriticalSection(&mgr->criticalSection);
    return 1;
}

// Disconnect serial port
void UartManager_Disconnect(UartManager* mgr) {
    if (!mgr || !mgr->initialized) return;

    EnterCriticalSection(&mgr->criticalSection);

    if (mgr->hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(mgr->hSerial);
        mgr->hSerial = INVALID_HANDLE_VALUE;

        char logMsg[64];
        sprintf(logMsg, "Serial port %s disconnected", mgr->portName);
        AppendLog(mgr, logMsg);
    }

    LeaveCriticalSection(&mgr->criticalSection);
}

// Get firmware version
uint32_t UartManager_GetFirmwareVersion(UartManager* mgr) {
    if (!mgr || !mgr->initialized) return 0;

    EnterCriticalSection(&mgr->criticalSection);

    if (mgr->hSerial == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Serial port not connected");
        return 0;
    }

    // Clear receive buffer
    PurgeComm(mgr->hSerial, PURGE_RXCLEAR);

    // Build command frame
    uint8_t cmdData[8];
    cmdData[0] = BOARD_VERSION & 0xFF;
    cmdData[1] = (BOARD_VERSION >> 8) & 0xFF;
    cmdData[2] = (BOARD_VERSION >> 16) & 0xFF;
    cmdData[3] = (BOARD_VERSION >> 24) & 0xFF;
    cmdData[4] = 0;
    cmdData[5] = 0;
    cmdData[6] = 0;
    cmdData[7] = 0;

    uint8_t frame[32];
    int frameLen = BuildUARTFrame(FRAME_TYPE_CMD, cmdData, 8, frame, sizeof(frame));

    FlushFileBuffers(mgr->hSerial);

    if (!UartWrite(mgr, frame, frameLen)) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to send version query command");
        return 0;
    }

    AppendLog(mgr, "Waiting for version response...");

    uint32_t code, version;
    if (!UartWaitForResponse(mgr, &code, &version, 5000)) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Version response timeout");
        return 0;
    }

    if (code == FW_CODE_VERSION) {
        char buf[64];
        sprintf(buf, "Version: v%u.%u.%u", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        AppendLog(mgr, buf);
        LeaveCriticalSection(&mgr->criticalSection);
        return version;
    }

    LeaveCriticalSection(&mgr->criticalSection);
    AppendLog(mgr, "Version response data error");
    return 0;
}

// Reboot board
int UartManager_BoardReboot(UartManager* mgr) {
    if (!mgr || !mgr->initialized) return 0;

    EnterCriticalSection(&mgr->criticalSection);

    if (mgr->hSerial == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Serial port not connected");
        return 0;
    }

    // Build command frame
    uint8_t cmdData[8];
    cmdData[0] = BOARD_REBOOT & 0xFF;
    cmdData[1] = (BOARD_REBOOT >> 8) & 0xFF;
    cmdData[2] = (BOARD_REBOOT >> 16) & 0xFF;
    cmdData[3] = (BOARD_REBOOT >> 24) & 0xFF;
    cmdData[4] = 0;
    cmdData[5] = 0;
    cmdData[6] = 0;
    cmdData[7] = 0;

    uint8_t frame[32];
    int frameLen = BuildUARTFrame(FRAME_TYPE_CMD, cmdData, 8, frame, sizeof(frame));

    int result = UartWrite(mgr, frame, frameLen);
    LeaveCriticalSection(&mgr->criticalSection);

    if (!result) {
        AppendLog(mgr, "Failed to send reboot command");
        return 0;
    }

    AppendLog(mgr, "Reboot command sent");
    return 1;
}

// Firmware upgrade
int UartManager_FirmwareUpgrade(UartManager* mgr, const wchar_t* fileName, int testMode) {
    if (!mgr || !mgr->initialized) return 0;

    EnterCriticalSection(&mgr->criticalSection);

    if (mgr->hSerial == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Serial port not connected");
        return 0;
    }

    char logMsg[256];

    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, logMsg + 20, 200, NULL, NULL);
        sprintf(logMsg, "Cannot open file: %s", logMsg + 20);
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, logMsg);
        return 0;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    sprintf(logMsg, "Starting firmware upgrade, firmware size: %u bytes", fileSize);
    AppendLog(mgr, logMsg);

    // Send start upgrade command
    uint8_t cmdData[8];
    cmdData[0] = BOARD_START_UPDATE & 0xFF;
    cmdData[1] = (BOARD_START_UPDATE >> 8) & 0xFF;
    cmdData[2] = (BOARD_START_UPDATE >> 16) & 0xFF;
    cmdData[3] = (BOARD_START_UPDATE >> 24) & 0xFF;
    cmdData[4] = fileSize & 0xFF;
    cmdData[5] = (fileSize >> 8) & 0xFF;
    cmdData[6] = (fileSize >> 16) & 0xFF;
    cmdData[7] = (fileSize >> 24) & 0xFF;

    uint8_t frame[32];
    int frameLen = BuildUARTFrame(FRAME_TYPE_CMD, cmdData, 8, frame, sizeof(frame));

    if (!UartWrite(mgr, frame, frameLen)) {
        CloseHandle(hFile);
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to send firmware size");
        return 0;
    }

    uint32_t code, offset;
    if (!UartWaitForResponse(mgr, &code, &offset, 15000)) {
        CloseHandle(hFile);
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Flash erase timeout");
        return 0;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        CloseHandle(hFile);
        sprintf(logMsg, "Flash erase failed: code(%u), offset(%u)", code, offset);
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, logMsg);
        return 0;
    }

    AppendLog(mgr, "Flash erase completed");

    DWORD bytesSent = 0;
    DWORD bytesRead;
    uint8_t dataBuffer[8];
    int transferComplete = 0;

    while (ReadFile(hFile, dataBuffer, 8, &bytesRead, NULL) && bytesRead > 0) {
        // Pad to 8 bytes
        while (bytesRead < 8) {
            dataBuffer[bytesRead++] = 0xFF;
        }

        // Build data frame
        frameLen = BuildUARTFrame(FRAME_TYPE_DATA, dataBuffer, 8, frame, sizeof(frame));

        if (!UartWrite(mgr, frame, frameLen)) {
            CloseHandle(hFile);
            LeaveCriticalSection(&mgr->criticalSection);
            AppendLog(mgr, "Failed to send file data");
            return 0;
        }

        bytesSent += 8;

        // Confirm every 64 bytes or after sending is complete
        if (bytesSent % 64 == 0 || bytesSent >= fileSize) {
            // Update progress
            if (mgr->progressCallback && bytesSent <= fileSize) {
                int percent = (int)(bytesSent * 100 / fileSize);
                mgr->progressCallback(percent);
            }

            if (!UartWaitForResponse(mgr, &code, &offset, 5000)) {
                CloseHandle(hFile);
                LeaveCriticalSection(&mgr->criticalSection);
                AppendLog(mgr, "Firmware update timeout");
                return 0;
            }

            if (code == FW_CODE_UPDATE_SUCCESS) {
                transferComplete = 1;
                break;
            }
            if (code != FW_CODE_OFFSET) {
                CloseHandle(hFile);
                sprintf(logMsg, "Firmware upgrade failed: code(%u), offset(%u)", code, offset);
                LeaveCriticalSection(&mgr->criticalSection);
                AppendLog(mgr, logMsg);
                return 0;
            }
        }
    }

    CloseHandle(hFile);

    // Set progress to 100%
    if (mgr->progressCallback) {
        mgr->progressCallback(100);
    }

    // Check if transfer is complete
    if (!transferComplete && bytesSent > 0) {
        // Last wait for completion response
        if (!UartWaitForResponse(mgr, &code, &offset, 5000)) {
            LeaveCriticalSection(&mgr->criticalSection);
            AppendLog(mgr, "Firmware transfer completion timeout");
            return 0;
        }
        if (code != FW_CODE_UPDATE_SUCCESS) {
            sprintf(logMsg, "Firmware transfer not completed successfully: code(%u), offset(%u)", code, offset);
            LeaveCriticalSection(&mgr->criticalSection);
            AppendLog(mgr, logMsg);
            return 0;
        }
    }

    // Send confirmation command
    cmdData[0] = BOARD_CONFIRM & 0xFF;
    cmdData[1] = (BOARD_CONFIRM >> 8) & 0xFF;
    cmdData[2] = (BOARD_CONFIRM >> 16) & 0xFF;
    cmdData[3] = (BOARD_CONFIRM >> 24) & 0xFF;
    uint32_t confirmParam = testMode ? 0 : 1;
    cmdData[4] = confirmParam & 0xFF;
    cmdData[5] = (confirmParam >> 8) & 0xFF;
    cmdData[6] = (confirmParam >> 16) & 0xFF;
    cmdData[7] = (confirmParam >> 24) & 0xFF;

    frameLen = BuildUARTFrame(FRAME_TYPE_CMD, cmdData, 8, frame, sizeof(frame));

    if (!UartWrite(mgr, frame, frameLen)) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Failed to send confirmation command");
        return 0;
    }

    if (!UartWaitForResponse(mgr, &code, &offset, 30000)) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Firmware confirmation timeout");
        return 0;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, logMsg + 20, 200, NULL, NULL);
        sprintf(logMsg, "File %s upload completed", logMsg + 20);
        AppendLog(mgr, logMsg);
        LeaveCriticalSection(&mgr->criticalSection);
        return 1;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        LeaveCriticalSection(&mgr->criticalSection);
        AppendLog(mgr, "Firmware update failed");
        return 0;
    }

    LeaveCriticalSection(&mgr->criticalSection);
    return 0;
}

// Enumerate available serial ports (using SetupAPI for fast enumeration)
int UartManager_EnumPorts(UartManager* mgr, SerialPortInfo* ports, int maxCount) {
    int count = 0;
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    // Get all serial port devices
    deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        AppendLog(mgr, "Failed to enumerate serial ports");
        return 0;
    }

    // Iterate through all serial port devices
    for (DWORD i = 0; i < 128 && count < maxCount; i++) {
        if (!SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData)) {
            break;  // No more devices
        }

        // Get device description (using Unicode version)
        WCHAR descW[256] = {0};
        DWORD dataSize = 0;
        DWORD dataType = 0;

        if (!SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                                SPDRP_FRIENDLYNAME, &dataType,
                                                (PBYTE)descW, sizeof(descW), &dataSize)) {
            // If no friendly name, try device description
            if (!SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                                    SPDRP_DEVICEDESC, &dataType,
                                                    (PBYTE)descW, sizeof(descW), &dataSize)) {
                continue;
            }
        }

        // Filter out Bluetooth serial ports (case insensitive)
        WCHAR descLower[256] = {0};
        for (int j = 0; descW[j] && j < 255; j++) {
            descLower[j] = (WCHAR)tolower(descW[j]);
        }

        if (wcsstr(descLower, L"bluetooth") || wcsstr(descLower, L"蓝牙")) {
            continue;  // Skip Bluetooth serial ports
        }

        // Extract COM port number from description (format like "USB Serial Port (COM3)")
        WCHAR* comStart = wcsstr(descW, L"(COM");
        if (comStart) {
            comStart += 4;  // Skip "(COM"
            int portNum = _wtoi(comStart);
            if (portNum > 0 && portNum <= 256) {
                // portName stored as "COM" + number, like "COM3"
                sprintf(ports[count].portName, "COM%d", portNum);
                // friendlyName also simplified to "COM3"
                sprintf(ports[count].friendlyName, "COM%d", portNum);
                count++;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    char msg[64];
    sprintf(msg, "Found %d available serial ports", count);
    AppendLog(mgr, msg);

    return count;
}
