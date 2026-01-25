#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// GLib compatibility
#ifndef __GTK_TYPE_UTILS_H__
#define gpointer void*
#endif

// SocketCAN definitions
#define MAX_CAN_INTERFACES  16
#define MAX_IFNAME_LEN      16
#define VIRTUAL_CAN_CHANNEL "vcan0"

// CAN message frame structure (application protocol)
typedef struct {
    uint32_t code;
    uint32_t val;
} __attribute__((packed)) can_frame_t;

// CAN IDs
#define PLATFORM_RX    0x101
#define PLATFORM_TX    0x102
#define FW_DATA_RX     0x103

// Board command codes
#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

// Response codes
#define FW_CODE_OFFSET          0
#define FW_CODE_UPDATE_SUCCESS  1
#define FW_CODE_VERSION         2
#define FW_CODE_CONFIRM         3
#define FW_CODE_FLASH_ERROR     4
#define FW_CODE_TRANFER_ERROR   5

// Baud rate definitions (for SocketCAN via ip link)
typedef enum {
    CAN_BAUD_10K    = 10000,
    CAN_BAUD_20K    = 20000,
    CAN_BAUD_50K    = 50000,
    CAN_BAUD_100K   = 100000,
    CAN_BAUD_125K   = 125000,
    CAN_BAUD_250K   = 250000,
    CAN_BAUD_500K   = 500000,
    CAN_BAUD_1M     = 1000000
} CanBaudRate;

// Callback types
typedef void (*msgCallback)(const char* msg, gpointer user_data);
typedef void (*progressCallback)(int percent, gpointer user_data);

// Opaque CAN manager structure
typedef struct CanManager CanManager;

// API functions
CanManager* CanManager_Create(void);
void CanManager_Destroy(CanManager* mgr);
void CanManager_SetCallback(CanManager* mgr, msgCallback msg_call, gpointer user_data);
void CanManager_SetProgressCallback(CanManager* mgr, progressCallback progress_call, gpointer user_data);

int CanManager_Connect(CanManager* mgr, const char* ifName, CanBaudRate baudrate);
void CanManager_Disconnect(CanManager* mgr);
uint32_t CanManager_GetFirmwareVersion(CanManager* mgr);
int CanManager_BoardReboot(CanManager* mgr);
int CanManager_FirmwareUpgrade(CanManager* mgr, const char* fileName, int testMode);
int CanManager_DetectDevice(CanManager* mgr, char (*ifNames)[MAX_IFNAME_LEN], int maxCount);

#endif // CAN_MANAGER_H
