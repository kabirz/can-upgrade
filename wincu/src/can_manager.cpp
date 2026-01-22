#include <can_manager.h>

CanManager& CanManager::getInstance() {
    static CanManager instance;
    return instance;
}

void CanManager::setCallback(msgCallback msg_call) {
    appand_msg = msg_call;
}

bool CanManager::connect(TPCANHandle channel, TPCANBaudrate baudrate) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_connectedChannels[channel]) {
        return true;
    }

    TPCANStatus status = CAN_Initialize(channel, baudrate, 0, 0, 0);
    if (status != PCAN_ERROR_OK) {
        if (appand_msg) {
            appand_msg("CAN 初始化失败");
        }
        return false;
    }

    if (appand_msg) {
        appand_msg("CAN 连接成功");
    }
    m_connectedChannels[channel] = true;
    return true;
}

void CanManager::disconnect(TPCANHandle channel) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_connectedChannels.find(channel);
    if (it != m_connectedChannels.end() && it->second) {
        CAN_Reset(channel);
        m_connectedChannels.erase(it);
        if (appand_msg) {
            appand_msg("CAN 连接已断开");
        }
    }
}

bool CanManager::writeFrame(TPCANHandle channel, const TPCANMsg& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!isConnected(channel)) {
        if (appand_msg) {
            appand_msg("发送失败，CAN已断开连接");
        }
        return false;
    }

    TPCANStatus status = CAN_Write(channel, const_cast<TPCANMsg *>(&msg));
    if (status != PCAN_ERROR_OK) {
        if (appand_msg) {
            appand_msg("CAN 发送失败");
        }
        return false;
    }
    return true;
}

bool CanManager::readFrame(TPCANHandle channel, TPCANMsg& msg, TPCANTimestamp& timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!isConnected(channel)) {
        if (appand_msg) {
            appand_msg("发送失败，CAN已断开连接");
        }
        return false;
    }

    TPCANStatus status = CAN_Read(channel, &msg, &timestamp);
    if (status != PCAN_ERROR_OK) {
        if (status != PCAN_ERROR_QRCVEMPTY) {
            if (appand_msg) {
                appand_msg("CAN读取数据失败");
            }
        }
        return false;
    }
    return true;
}

bool CanManager::isConnected(TPCANHandle channel) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connectedChannels.find(channel);
    return (it != m_connectedChannels.end() && it->second);
}
