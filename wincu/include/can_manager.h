#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <windows.h>
#include <PCANBasic.h>

typedef void (*msgCallback)(const char * msg);

class CanManager {
public:
    static CanManager& getInstance();

    CanManager(const CanManager&) = delete;
    CanManager& operator=(const CanManager&) = delete;

    bool connect(TPCANHandle channel, TPCANBaudrate baudrate = PCAN_BAUD_500K);
    void disconnect(TPCANHandle channel);
    void setCallback(msgCallback msg_call);
    bool isConnected(TPCANHandle channel) const;

private:
    CanManager() = default; // 私有构造
    ~CanManager() = default;
    bool writeFrame(TPCANHandle channel, const TPCANMsg& msg);
    bool readFrame(TPCANHandle channel, TPCANMsg& msg, TPCANTimestamp& timestamp);

    mutable std::mutex m_mutex;
    std::unordered_map<TPCANHandle, bool> m_connectedChannels;
    msgCallback appand_msg;
};

#endif
