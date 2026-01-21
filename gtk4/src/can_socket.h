#ifndef CAN_SOCKET_H
#define CAN_SOCKET_H

#include <stdint.h>
#include <stddef.h>

/* CAN 协议定义 */
#define PLATFORM_RX   0x101
#define PLATFORM_TX   0x102
#define FW_DATA_RX    0x103

/* 固件代码 */
#define FW_CODE_OFFSET           0
#define FW_CODE_UPDATE_SUCCESS   1
#define FW_CODE_VERSION          2
#define FW_CODE_CONFIRM          3
#define FW_CODE_FLASH_ERROR      4
#define FW_CODE_TRANFER_ERROR    5

/* 板子命令 */
#define BOARD_START_UPDATE  0
#define BOARD_CONFIRM       1
#define BOARD_VERSION       2
#define BOARD_REBOOT        3

/* CAN Socket 结构 */
typedef struct {
    int fd;
    char interface[32];
} can_socket_t;

/* CAN 消息结构 */
typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
} can_frame_t;

/* 函数声明 */
char** can_enumerate_devices(int *count);
void can_free_device_list(char **devices, int count);
can_socket_t* can_socket_create(const char *interface);
void can_socket_destroy(can_socket_t *sock);

int can_socket_send(can_socket_t *sock, uint32_t id, const uint8_t *data, uint8_t dlc);
int can_socket_recv(can_socket_t *sock, can_frame_t *frame, int timeout_ms);

/* 固件升级相关 */
int can_firmware_upgrade(can_socket_t *sock, const char *file_path, int test,
                         void (*progress_cb)(size_t current, size_t total, void *user_data),
                         void (*log_cb)(const char *msg, void *user_data),
                         void *user_data);

int can_firmware_get_version(can_socket_t *sock, char *version_str, size_t len);
int can_board_reboot(can_socket_t *sock);

#endif /* CAN_SOCKET_H */
