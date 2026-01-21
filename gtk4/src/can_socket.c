#include "can_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

/* 枚举系统中可用的 CAN 设备 */
char** can_enumerate_devices(int *count)
{
    DIR *dir;
    struct dirent *entry;
    char **devices = NULL;
    int device_count = 0;
    int capacity = 0;

    dir = opendir("/sys/class/net");
    if (!dir) {
        *count = 0;
        return NULL;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* 跳过 . 和 .. */
        if (entry->d_name[0] == '.') {
            continue;
        }

        /* 检查是否是 can 设备 (can0, can1, vcan0 等) */
        if (strstr(entry->d_name, "can")) {
            /* 扩展数组 */
            if (device_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                char **new_devices = realloc(devices, capacity * sizeof(char*));
                if (!new_devices) {
                    /* 内存不足，释放已分配的 */
                    for (int i = 0; i < device_count; i++) {
                        free(devices[i]);
                    }
                    free(devices);
                    closedir(dir);
                    *count = 0;
                    return NULL;
                }
                devices = new_devices;
            }

            /* 复制设备名 */
            devices[device_count] = strdup(entry->d_name);
            if (!devices[device_count]) {
                for (int i = 0; i < device_count; i++) {
                    free(devices[i]);
                }
                free(devices);
                closedir(dir);
                *count = 0;
                return NULL;
            }
            device_count++;
        }
    }

    closedir(dir);

    *count = device_count;
    return devices;
}

/* 释放设备列表 */
void can_free_device_list(char **devices, int count)
{
    if (devices) {
        for (int i = 0; i < count; i++) {
            free(devices[i]);
        }
        free(devices);
    }
}

can_socket_t* can_socket_create(const char *interface)
{
    struct ifreq ifr;
    can_socket_t *sock = calloc(1, sizeof(can_socket_t));
    if (!sock) {
        return NULL;
    }
    struct sockaddr_can addr;

    /* 创建 socket */
    sock->fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock->fd < 0) {
        free(sock);
        return NULL;
    }

    /* 设置非阻塞模式 */
    int flags = fcntl(sock->fd, F_GETFL, 0);
    fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK);

    /* 设置 CAN 接口 */
    strcpy(ifr.ifr_name, interface);
    if (ioctl(sock->fd, SIOCGIFINDEX, &ifr) < 0) {
        close(sock->fd);
        free(sock);
        return NULL;
    }

    /* 绑定 socket */
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock->fd);
        free(sock);
        return NULL;
    }

    /* 设置过滤器 */
    struct can_filter rfilter[1];
    rfilter[0].can_id   = PLATFORM_TX;
    rfilter[0].can_mask = 0x10f;
    setsockopt(sock->fd, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    strncpy(sock->interface, interface, sizeof(sock->interface) - 1);
    return sock;
}

void can_socket_destroy(can_socket_t *sock)
{
    if (sock) {
        if (sock->fd >= 0) {
            close(sock->fd);
        }
        free(sock);
    }
}

int can_socket_send(can_socket_t *sock, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame frame;
    frame.can_id = id;
    frame.can_dlc = dlc;
    if (data && dlc > 0) {
        memcpy(frame.data, data, dlc);
    }

    ssize_t nbytes = write(sock->fd, &frame, sizeof(struct can_frame));
    return (nbytes == sizeof(struct can_frame)) ? 0 : -1;
}

int can_socket_recv(can_socket_t *sock, can_frame_t *frame, int timeout_ms)
{
    fd_set readfds;
    struct timeval tv;
    struct can_frame raw_frame;
    ssize_t nbytes;

    FD_ZERO(&readfds);
    FD_SET(sock->fd, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sock->fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        return -1;  /* 超时或错误 */
    }

    nbytes = read(sock->fd, &raw_frame, sizeof(struct can_frame));
    if (nbytes < 0) {
        return -1;
    }

    if (raw_frame.can_id != PLATFORM_TX) {
        return -2;  /* 不匹配的 ID */
    }

    frame->id = raw_frame.can_id;
    frame->dlc = raw_frame.can_dlc;
    memcpy(frame->data, raw_frame.data, 8);

    return 0;
}

/* 打包两个 32 位整数到 CAN 数据 */
static void pack_u32_array(uint8_t *buf, uint32_t v1, uint32_t v2)
{
    buf[0] = v1 & 0xff;
    buf[1] = (v1 >> 8) & 0xff;
    buf[2] = (v1 >> 16) & 0xff;
    buf[3] = (v1 >> 24) & 0xff;
    buf[4] = v2 & 0xff;
    buf[5] = (v2 >> 8) & 0xff;
    buf[6] = (v2 >> 16) & 0xff;
    buf[7] = (v2 >> 24) & 0xff;
}

/* 解包 CAN 数据到两个 32 位整数 */
static void unpack_u32_array(const uint8_t *buf, uint32_t *v1, uint32_t *v2)
{
    *v1 = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    *v2 = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
}

static int can_recv_expect(can_socket_t *sock, uint32_t *code, uint32_t *offset, int timeout_ms)
{
    can_frame_t frame;
    int ret;

    while (1) {
        ret = can_socket_recv(sock, &frame, timeout_ms);
        if (ret < 0) {
            return ret;
        }
        unpack_u32_array(frame.data, code, offset);
        return 0;
    }
}

int can_firmware_upgrade(can_socket_t *sock, const char *file_path, int test,
                         void (*progress_cb)(size_t, size_t, void*),
                         void (*log_cb)(const char*, void*),
                         void *user_data)
{
    FILE *fp;
    long file_size;
    uint8_t data[8];
    uint32_t code, offset;
    size_t sent = 0;
    char log_buf[256];

    fp = fopen(file_path, "rb");
    if (!fp) {
        if (log_cb) {
            snprintf(log_buf, sizeof(log_buf), "无法打开文件: %s", file_path);
            log_cb(log_buf, user_data);
        }
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* 发送开始升级命令 */
    pack_u32_array(data, BOARD_START_UPDATE, file_size);
    can_socket_send(sock, PLATFORM_RX, data, 8);

    if (can_recv_expect(sock, &code, &offset, 5000) < 0) {
        fclose(fp);
        if (log_cb) log_cb("接收超时", user_data);
        return -1;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        fclose(fp);
        if (log_cb) {
            snprintf(log_buf, sizeof(log_buf), "Flash 擦除错误: code=%u, offset=%u", code, offset);
            log_cb(log_buf, user_data);
        }
        return -1;
    }

    /* 发送固件数据 */
    while (1) {
        size_t nread = fread(data, 1, 8, fp);
        if (nread == 0) {
            break;
        }

        can_socket_send(sock, FW_DATA_RX, data, nread);
        sent += nread;

        if (progress_cb) {
            progress_cb(sent, file_size, user_data);
        }

        /* 每 64 字节或结束时等待确认 */
        if (sent % 64 != 0 && sent < (size_t)file_size) {
            continue;
        }

        if (can_recv_expect(sock, &code, &offset, 5000) < 0) {
            fclose(fp);
            if (log_cb) log_cb("接收超时", user_data);
            return -1;
        }

        if (code == FW_CODE_UPDATE_SUCCESS && offset == sent) {
            break;
        }
        if (code != FW_CODE_OFFSET) {
            fclose(fp);
            if (log_cb) {
                snprintf(log_buf, sizeof(log_buf), "固件上传错误: code=%u, offset=%u", code, offset);
                log_cb(log_buf, user_data);
            }
            return -1;
        }
    }

    fclose(fp);

    /* 发送确认命令 */
    pack_u32_array(data, BOARD_CONFIRM, test ? 0 : 1);
    can_socket_send(sock, PLATFORM_RX, data, 8);

    if (can_recv_expect(sock, &code, &offset, 30000) < 0) {
        if (log_cb) log_cb("确认超时", user_data);
        return -1;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        if (log_cb) {
            log_cb("固件上传完成！请重启板子以完成升级，约需 45-90 秒", user_data);
        }
        return 0;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        if (log_cb) log_cb("下载失败", user_data);
        return -1;
    }

    return -1;
}

int can_firmware_get_version(can_socket_t *sock, char *version_str, size_t len)
{
    uint8_t data[8];
    uint32_t code, version;

    pack_u32_array(data, BOARD_VERSION, 0);
    can_socket_send(sock, PLATFORM_RX, data, 8);

    if (can_recv_expect(sock, &code, &version, 5000) < 0) {
        return -1;
    }

    if (code == FW_CODE_VERSION) {
        uint8_t v1 = (version >> 24) & 0xff;
        uint8_t v2 = (version >> 16) & 0xff;
        uint8_t v3 = (version >> 8) & 0xff;
        snprintf(version_str, len, "v%u.%u.%u", v1, v2, v3);
        return 0;
    }

    return -1;
}

int can_board_reboot(can_socket_t *sock)
{
    uint8_t data[8];
    pack_u32_array(data, BOARD_REBOOT, 0);
    return can_socket_send(sock, PLATFORM_RX, data, 8);
}
