#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CAN ID definitions
#define PLATFORM_RX     0x101    // Command to board
#define PLATFORM_TX     0x102    // Response from board
#define FW_DATA_RX      0x103    // Firmware data

// Board commands
#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

// Firmware response codes
#define FW_CODE_OFFSET          0
#define FW_CODE_UPDATE_SUCCESS  1
#define FW_CODE_VERSION         2
#define FW_CODE_CONFIRM         3
#define FW_CODE_FLASH_ERROR     4
#define FW_CODE_TRANFER_ERROR   5

// CAN frame structure
typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} CAN_FRAME;

// CAN baud rate definitions
typedef enum {
    CAN_BAUD_10K   = 10000,
    CAN_BAUD_20K   = 20000,
    CAN_BAUD_50K   = 50000,
    CAN_BAUD_80K   = 80000,
    CAN_BAUD_125K  = 125000,
    CAN_BAUD_250K  = 250000,
    CAN_BAUD_500K  = 500000,
    CAN_BAUD_1000K = 1000000
} CAN_BAUD_RATE;

// CAN result codes
typedef enum {
    CAN_OK = 0,
    CAN_ERROR_NOT_INITIALIZED,
    CAN_ERROR_INIT_FAILED,
    CAN_ERROR_NOT_CONNECTED,
    CAN_ERROR_WRITE_FAILED,
    CAN_ERROR_READ_FAILED,
    CAN_ERROR_TIMEOUT,
    CAN_ERROR_INVALID_PARAM
} CAN_RESULT;

#ifdef __cplusplus
}
#endif

#endif // CAN_PROTOCOL_H
