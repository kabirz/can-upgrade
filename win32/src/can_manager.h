#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include "can_protocol.h"
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for opaque handle
typedef struct CANManager CANManager;

// Callback types for events
typedef void (*CAN_STATUS_CALLBACK)(const char* message, void* user_data);
typedef void (*CAN_ERROR_CALLBACK)(const char* message, void* user_data);
typedef void (*CAN_PROGRESS_CALLBACK)(int percentage, void* user_data);
typedef void (*CAN_VERSION_CALLBACK)(const char* version, void* user_data);
typedef void (*CAN_CONNECTED_CALLBACK)(bool connected, void* user_data);

// Create/destroy CAN manager
CANManager* CAN_Create(void);
void CAN_Destroy(CANManager* mgr);

// Set callbacks
void CAN_SetStatusCallback(CANManager* mgr, CAN_STATUS_CALLBACK cb, void* user_data);
void CAN_SetErrorCallback(CANManager* mgr, CAN_ERROR_CALLBACK cb, void* user_data);
void CAN_SetProgressCallback(CANManager* mgr, CAN_PROGRESS_CALLBACK cb, void* user_data);
void CAN_SetVersionCallback(CANManager* mgr, CAN_VERSION_CALLBACK cb, void* user_data);
void CAN_SetConnectedCallback(CANManager* mgr, CAN_CONNECTED_CALLBACK cb, void* user_data);

// Connection management
CAN_RESULT CAN_Connect(CANManager* mgr, const char* canInterface, const char* channel, int baudRate);
void CAN_Disconnect(CANManager* mgr);
bool CAN_IsConnected(const CANManager* mgr);

// Device detection
bool CAN_DetectDevice(const char* channel);

// Firmware operations
bool CAN_FirmwareUpgrade(CANManager* mgr, const char* fileName, bool testMode);
bool CAN_FirmwareVersion(CANManager* mgr);
bool CAN_BoardReboot(CANManager* mgr);

// Low level operations
CAN_RESULT CAN_SendCommand(CANManager* mgr, uint32_t canId, uint32_t cmd, uint32_t param);
CAN_RESULT CAN_SendData(CANManager* mgr, const uint8_t* data, uint8_t len);
CAN_RESULT CAN_WaitForResponse(CANManager* mgr, uint32_t* code, uint32_t* param, int timeoutMs);

// Utility functions
void CAN_PrepareCommand(uint8_t* buffer, uint32_t cmd, uint32_t param);
void CAN_ParseResponse(const uint8_t* data, uint32_t* code, uint32_t* param);

#ifdef __cplusplus
}
#endif

#endif // CAN_MANAGER_H
