#include "can_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// Internal structure definition
struct CanManager {
    pthread_mutex_t criticalSection;
    int sockFd;
    char ifName[MAX_IFNAME_LEN];
    msgCallback msgCallback;
    progressCallback progressCallback;
    gpointer msgUserData;
    gpointer progressUserData;
    int initialized;
    int connected;
};

// Get current time in milliseconds
static unsigned long GetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void Sleep_ms(unsigned long ms) {
    usleep(ms * 1000);
}

// Create CAN manager
CanManager* CanManager_Create(void) {
    CanManager* mgr = (CanManager*)malloc(sizeof(CanManager));
    if (!mgr) return NULL;

    pthread_mutex_init(&mgr->criticalSection, NULL);
    mgr->sockFd = -1;
    mgr->ifName[0] = '\0';
    mgr->msgCallback = NULL;
    mgr->progressCallback = NULL;
    mgr->msgUserData = NULL;
    mgr->progressUserData = NULL;
    mgr->initialized = 1;
    mgr->connected = 0;

    return mgr;
}

// Destroy CAN manager
void CanManager_Destroy(CanManager* mgr) {
    if (!mgr) return;
    if (mgr->initialized) {
        if (mgr->sockFd >= 0) {
            close(mgr->sockFd);
        }
        pthread_mutex_destroy(&mgr->criticalSection);
    }
    free(mgr);
}

// Set log callback function
void CanManager_SetCallback(CanManager* mgr, msgCallback msg_call, gpointer user_data) {
    if (mgr) {
        mgr->msgCallback = msg_call;
        mgr->msgUserData = user_data;
    }
}

// Set progress callback function
void CanManager_SetProgressCallback(CanManager* mgr, progressCallback progress_call, gpointer user_data) {
    if (mgr) {
        mgr->progressCallback = progress_call;
        mgr->progressUserData = user_data;
    }
}

// Append log
static void appendLog(CanManager* mgr, const char* msg) {
    if (mgr && mgr->msgCallback) {
        mgr->msgCallback(msg, mgr->msgUserData);
    }
}

// Connect CAN device
int CanManager_Connect(CanManager* mgr, const char* ifName, CanBaudRate baudrate) {
    if (!mgr || !mgr->initialized) return 0;

    pthread_mutex_lock(&mgr->criticalSection);

    if (mgr->connected) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN connection already exists, do not connect repeatedly");
        return 1;
    }

    // Create SocketCAN socket
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "Failed to create CAN socket");
        return 0;
    }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        close(sock);
        pthread_mutex_unlock(&mgr->criticalSection);
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "Failed to get CAN interface index for %s", ifName);
        appendLog(mgr, logMsg);
        return 0;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        pthread_mutex_unlock(&mgr->criticalSection);
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "Failed to bind CAN socket to %s", ifName);
        appendLog(mgr, logMsg);
        return 0;
    }

    mgr->sockFd = sock;
    strncpy(mgr->ifName, ifName, MAX_IFNAME_LEN - 1);
    mgr->ifName[MAX_IFNAME_LEN - 1] = '\0';
    mgr->connected = 1;
    pthread_mutex_unlock(&mgr->criticalSection);

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "CAN (%s) connected successfully at %lu baud",
             ifName, (unsigned long)baudrate);
    appendLog(mgr, logMsg);
    return 1;
}

// Disconnect CAN
void CanManager_Disconnect(CanManager* mgr) {
    if (!mgr || !mgr->initialized) return;

    pthread_mutex_lock(&mgr->criticalSection);

    char logMsg[128];
    if (mgr->sockFd >= 0) {
        snprintf(logMsg, sizeof(logMsg), "CAN (%s) disconnected", mgr->ifName);
        close(mgr->sockFd);
        mgr->sockFd = -1;
    } else {
        snprintf(logMsg, sizeof(logMsg), "CAN disconnected");
    }
    mgr->connected = 0;
    pthread_mutex_unlock(&mgr->criticalSection);
    appendLog(mgr, logMsg);
}

// Wait for CAN response
static int CAN_WaitForResponse(CanManager* mgr, uint32_t* code, uint32_t* param, int timeoutMs) {
    if (mgr->sockFd < 0) return 0;

    unsigned long startTime = GetTickCount();
    struct can_frame frame;
    fd_set readfds;
    struct timeval timeout;

    while ((int)(GetTickCount() - startTime) < timeoutMs) {
        FD_ZERO(&readfds);
        FD_SET(mgr->sockFd, &readfds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(mgr->sockFd + 1, &readfds, NULL, NULL, &timeout);
        if (activity > 0 && FD_ISSET(mgr->sockFd, &readfds)) {
            ssize_t nbytes = read(mgr->sockFd, &frame, sizeof(struct can_frame));
            if (nbytes > 0 && frame.can_id == PLATFORM_TX) {
                can_frame_t* cf = (can_frame_t*)frame.data;
                *code = cf->code;
                *param = cf->val;
                return 1;
            } else if (nbytes < 0) {
                break;
            }
        }
        usleep(1000);
    }
    return 0;
}

// Get firmware version
uint32_t CanManager_GetFirmwareVersion(CanManager* mgr) {
    if (!mgr || !mgr->initialized) return 0;

    pthread_mutex_lock(&mgr->criticalSection);

    if (!mgr->connected) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN disconnected, please reconnect");
        return 0;
    }

    if (mgr->sockFd < 0) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "Invalid socket");
        return 0;
    }

    struct can_frame frame;
    frame.can_id = PLATFORM_RX;
    frame.can_dlc = 8;
    memset(frame.data, 0, 8);

    can_frame_t* cf = (can_frame_t*)frame.data;
    cf->code = BOARD_VERSION;

    if (write(mgr->sockFd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN transmission failed");
        return 0;
    }

    uint32_t code, version;
    if (!CAN_WaitForResponse(mgr, &code, &version, 5000)) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN read failed, timeout!!");
        return 0;
    }

    if (code == FW_CODE_VERSION) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Firmware version: v%u.%u.%u",
                 (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        appendLog(mgr, buf);
        pthread_mutex_unlock(&mgr->criticalSection);
        return version;
    }

    pthread_mutex_unlock(&mgr->criticalSection);
    appendLog(mgr, "CAN read failed, data error!!");
    return 0;
}

// Reboot board
int CanManager_BoardReboot(CanManager* mgr) {
    if (!mgr || !mgr->initialized) return 0;

    pthread_mutex_lock(&mgr->criticalSection);

    if (!mgr->connected) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN disconnected, please reconnect");
        return 0;
    }

    if (mgr->sockFd < 0) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "Invalid socket");
        return 0;
    }

    struct can_frame frame;
    frame.can_id = PLATFORM_RX;
    frame.can_dlc = 8;
    memset(frame.data, 0, 8);

    can_frame_t* cf = (can_frame_t*)frame.data;
    cf->code = BOARD_REBOOT;

    int result = (write(mgr->sockFd, &frame, sizeof(struct can_frame)) == sizeof(struct can_frame));
    pthread_mutex_unlock(&mgr->criticalSection);

    if (!result) {
        appendLog(mgr, "CAN transmission failed");
        return 0;
    }

    appendLog(mgr, "Board reboot successfully");
    return 1;
}

// SocketCAN firmware upgrade
int CanManager_FirmwareUpgrade(CanManager* mgr, const char* fileName, int testMode) {
    if (!mgr || !mgr->initialized) return 0;

    pthread_mutex_lock(&mgr->criticalSection);

    if (!mgr->connected) {
        pthread_mutex_unlock(&mgr->criticalSection);
        appendLog(mgr, "CAN disconnected, please reconnect");
        return 0;
    }

    pthread_mutex_unlock(&mgr->criticalSection);


    char logMsg[256];

    FILE* file = fopen(fileName, "rb");
    if (!file) {
        snprintf(logMsg, sizeof(logMsg), "Cannot open file: %s", fileName);
        appendLog(mgr, logMsg);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    snprintf(logMsg, sizeof(logMsg), "Starting firmware upgrade, firmware size: %ld bytes", fileSize);
    appendLog(mgr, logMsg);

    if (mgr->sockFd < 0) {
        fclose(file);
        appendLog(mgr, "Invalid socket");
        return 0;
    }

    struct can_frame frame;
    frame.can_id = PLATFORM_RX;
    frame.can_dlc = 8;
    memset(frame.data, 0, 8);

    can_frame_t* cf = (can_frame_t*)frame.data;
    cf->code = BOARD_START_UPDATE;
    cf->val = fileSize;

    if (write(mgr->sockFd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        fclose(file);
        appendLog(mgr, "Failed to send firmware size");
        return 0;
    }

    uint32_t code, offset;
    if (!CAN_WaitForResponse(mgr, &code, &offset, 15000)) {
        fclose(file);
        appendLog(mgr, "Flash erase timeout");
        return 0;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        fclose(file);
        snprintf(logMsg, sizeof(logMsg), "Flash erase failed: code(%u), offset(%u)", code, offset);
        appendLog(mgr, logMsg);
        return 0;
    }

    long bytesSent = 0;
    long len;
    unsigned char buffer[8];

    frame.can_id = FW_DATA_RX;
    while ((len = fread(buffer, 1, 8, file)) > 0) {
        frame.can_dlc = len;
        memcpy(frame.data, buffer, len);
        bytesSent += len;

        if (write(mgr->sockFd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
            fclose(file);
            appendLog(mgr, "Failed to send file data");
            return 0;
        }

        if (bytesSent % 64 == 0 || ftell(file) == fileSize) {
            // Update progress
            if (mgr->progressCallback) {
                int percent = (int)(bytesSent * 100 / fileSize);
                mgr->progressCallback(percent, mgr->progressUserData);
            }

            if (!CAN_WaitForResponse(mgr, &code, &offset, 5000)) {
                fclose(file);
                appendLog(mgr, "Firmware update timeout!");
                return 0;
            }

            if (code == FW_CODE_UPDATE_SUCCESS && offset == fileSize) break;
            if (code != FW_CODE_OFFSET) {
                fclose(file);
                snprintf(logMsg, sizeof(logMsg), "Firmware upgrade failed: code(%u), offset(%u)", code, offset);
                appendLog(mgr, logMsg);
                return 0;
            }
        }
    }

    fclose(file);

    frame.can_id = PLATFORM_RX;
    frame.can_dlc = 8;
    memset(frame.data, 0, 8);
    cf = (can_frame_t*)frame.data;
    cf->code = BOARD_CONFIRM;
    cf->val = testMode ? 0 : 1;

    if (write(mgr->sockFd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        appendLog(mgr, "Firmware confirmation failed!");
        return 0;
    }

    if (!CAN_WaitForResponse(mgr, &code, &offset, 30000)) {
        appendLog(mgr, "Firmware confirmation timeout!");
        return 0;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        snprintf(logMsg, sizeof(logMsg),
                 "File %s upload completed. Click reboot, board will complete in 45-60 seconds", fileName);
        appendLog(mgr, logMsg);
        return 1;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        appendLog(mgr, "Firmware update failed");
    }

    return 0;
}

// Detect devices (check for available CAN interfaces)
int CanManager_DetectDevice(CanManager* mgr, char (*ifNames)[MAX_IFNAME_LEN], int maxCount) {
    int count = 0;
    const char* prefixes[] = {"can", "vcan"};

    // Check for can0-can15
    for (int p = 0; p < 2 && count < maxCount; p++) {
        for (int i = 0; i < 16 && count < maxCount; i++) {
            char ifName[16];
            snprintf(ifName, sizeof(ifName), "%s%d", prefixes[p], i);

            int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
            if (sock < 0) continue;

            struct ifreq ifr;
            strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';

            if (ioctl(sock, SIOCGIFINDEX, &ifr) >= 0) {
                strncpy(ifNames[count], ifName, MAX_IFNAME_LEN - 1);
                ifNames[count][MAX_IFNAME_LEN - 1] = '\0';
                count++;
            }
            close(sock);
        }
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Found %d CAN device(s)", count);
    appendLog(mgr, msg);
    return count;
}
