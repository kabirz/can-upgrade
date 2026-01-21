#include "canmanager.h"
#include <QCanBusFrame>
#include <QCoreApplication>
#include <QVariant>
#include <QDebug>
#include <QRegularExpression>
#include <QDataStream>

CanManager::CanManager(QObject *parent)
    : QObject(parent)
    , m_device(nullptr)
    , m_expectedCode(0)
    , m_expectedParam(0)
    , m_responseReceived(false)
    , m_timeoutTimer(new QTimer(this))
{
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        m_responseReceived = true;
        emit errorMessage(tr("CAN receive timeout"));
    });
}

CanManager::~CanManager()
{
    disconnectFromBus();
}

bool CanManager::connectToBus(const QString &interface, const QString &channel, int baudRate)
{
    if (m_device) {
        disconnectFromBus();
    }

    QString errorString;
    m_device = QCanBus::instance()->createDevice(interface, channel, &errorString);

    if (!m_device) {
        emit errorMessage(tr("Failed to create CAN device: %1").arg(errorString));
        return false;
    }

    // Set configuration
    m_device->setConfigurationParameter(QCanBusDevice::BitRateKey, baudRate);
    m_device->setConfigurationParameter(QCanBusDevice::CanFdKey, false);

    // Connect signals
    connect(m_device, &QCanBusDevice::framesReceived, this, &CanManager::onFramesReceived);
    connect(m_device, &QCanBusDevice::errorOccurred, this, &CanManager::onErrorOccurred);

    if (!m_device->connectDevice()) {
        emit errorMessage(tr("Failed to connect CAN device"));
        delete m_device;
        m_device = nullptr;
        return false;
    }

    emit connectedChanged(true);
    return true;
}

void CanManager::disconnectFromBus()
{
    if (m_device) {
        m_device->disconnectDevice();
        delete m_device;
        m_device = nullptr;
        emit connectedChanged(false);
        emit statusMessage(tr("CAN disconnected"));
    }
}

bool CanManager::isConnected() const
{
    return m_device && m_device->state() == QCanBusDevice::ConnectedState;
}

QStringList CanManager::getAvailablePlugins()
{
    QStringList plugins = QCanBus::instance()->plugins();
    // 常见的CAN插件按优先级排序
    QStringList orderedPlugins;
    QStringList priorityOrder = {"socketcan", "vector", "peakcan", "systeccan", "tinycan", "virtualcan", "passthru"};

    // 按优先级添加已知插件
    for (const QString &priority : priorityOrder) {
        if (plugins.contains(priority)) {
            orderedPlugins.append(priority);
        }
    }

    // 添加其他插件
    for (const QString &plugin : plugins) {
        if (!priorityOrder.contains(plugin)) {
            orderedPlugins.append(plugin);
        }
    }

    return orderedPlugins;
}

QStringList CanManager::getAvailableChannels(const QString &plugin)
{
    QStringList channels;
    QList<QCanBusDeviceInfo> devices = QCanBus::instance()->availableDevices(plugin);

    for (const QCanBusDeviceInfo &info : devices) {
        channels.append(info.name());
    }

    // 如果没有找到设备，返回该插件的常见通道列表
    if (channels.isEmpty()) {
        if (plugin == "socketcan") {
            channels << "can0" << "can1" << "can2" << "can3" << "vcan0" << "vcan1";
        } else if (plugin == "vector") {
            channels << "CAN1" << "CAN2" << "CAN3" << "CAN4";
        } else if (plugin == "peakcan") {
            channels << "usb0" << "usb1" << "usb2";
        } else if (plugin == "virtualcan") {
            channels << "can0";
        }
    }

    return channels;
}

void CanManager::setBitRate(int bitrate)
{
    if (m_device) {
        m_device->setConfigurationParameter(QCanBusDevice::BitRateKey, bitrate);
    }
}

bool CanManager::sendCommand(quint32 canId, quint32 cmd, quint32 param)
{
    if (!isConnected()) {
        emit errorMessage(tr("CAN not connected"));
        return false;
    }

    QByteArray data = prepareCommand(cmd, param);
    QCanBusFrame frame(canId, data);

    return m_device->writeFrame(frame);
}

bool CanManager::sendData(const QByteArray &data)
{
    if (!isConnected()) {
        emit errorMessage(tr("CAN not connected"));
        return false;
    }

    QCanBusFrame frame(FW_DATA_RX, data);

    return m_device->writeFrame(frame);
}

bool CanManager::waitForResponse(quint32 &code, quint32 &param, int timeoutMs)
{
    m_responseReceived = false;
    m_expectedCode = 0;
    m_expectedParam = 0;

    m_timeoutTimer->start(timeoutMs);

    // Process events until response received or timeout
    QEventLoop loop;
    connect(this, &CanManager::connectedChanged, &loop, &QEventLoop::quit);

    while (!m_responseReceived && m_timeoutTimer->isActive()) {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 10);
    }

    m_timeoutTimer->stop();

    if (!m_responseReceived) {
        return false;
    }

    code = m_expectedCode;
    param = m_expectedParam;
    return true;
}

QByteArray CanManager::prepareCommand(quint32 cmd, quint32 param)
{
    QByteArray data(8, Qt::Uninitialized);
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream.setByteOrder(QDataStream::LittleEndian);
    stream << cmd << param;
    
    return data;
}

bool CanManager::firmwareUpgrade(const QString &fileName, bool testMode)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorMessage(tr("Failed to open file: %1").arg(fileName));
        return false;
    }

    qint64 totalSize = file.size();
    emit statusMessage(tr("Starting firmware upgrade, size: %1 bytes").arg(totalSize));

    // Send start update command
    if (!sendCommand(PLATFORM_RX, BOARD_START_UPDATE, static_cast<quint32>(totalSize))) {
        file.close();
        return false;
    }

    // Wait for response
    quint32 code, offset;
    if (!waitForResponse(code, offset)) {
        file.close();
        emit errorMessage(tr("Flash erase: timeout"));
        return false;
    }

    if (code != FW_CODE_OFFSET || offset != 0) {
        file.close();
        emit errorMessage(tr("Flash erase error: code(%1), offset(%2)").arg(code).arg(offset));
        return false;
    }

    // Send firmware data
    qint64 bytesSent = 0;
    char buffer[8];

    while (!file.atEnd()) {
        qint64 bytesRead = file.read(buffer, 8);
        if (bytesRead <= 0) break;

        QByteArray chunk(buffer, bytesRead);
        if (!sendData(chunk)) {
            file.close();
            emit errorMessage(tr("Failed to send data frame"));
            return false;
        }

        bytesSent += bytesRead;

        // Check progress every 64 bytes or at the end
        if (bytesSent % 64 == 0 || bytesSent == totalSize) {
            emit progressChanged(static_cast<int>((bytesSent * 100) / totalSize));

            if (!waitForResponse(code, offset)) {
                file.close();
                emit errorMessage(tr("Firmware upload: timeout"));
                return false;
            }

            if (code == FW_CODE_UPDATE_SUCCESS && offset == bytesSent) {
                break;
            }

            if (code != FW_CODE_OFFSET) {
                file.close();
                emit errorMessage(tr("Firmware upload error: code(%1), offset(%2)").arg(code).arg(offset));
                return false;
            }
        }
    }

    file.close();

    // Send confirm command
    quint32 confirmParam = testMode ? 0 : 1;
    if (!sendCommand(PLATFORM_RX, BOARD_CONFIRM, confirmParam)) {
        return false;
    }

    // Wait for confirm response
    if (!waitForResponse(code, offset, 30000)) {
        emit errorMessage(tr("Confirm: timeout"));
        return false;
    }

    if (code == FW_CODE_CONFIRM && offset == 0x55AA55AA) {
        emit statusMessage(tr("Image %1 upload finished. Please reboot board for upgrade, it will take about 45~90s").arg(fileName));
        return true;
    } else if (code == FW_CODE_TRANFER_ERROR) {
        emit errorMessage(tr("Download failed"));
        return false;
    }

    return false;
}

bool CanManager::firmwareVersion()
{
    if (!sendCommand(PLATFORM_RX, BOARD_VERSION, 0)) {
        return false;
    }

    quint32 code, version;
    if (!waitForResponse(code, version)) {
        return false;
    }

    if (code == FW_CODE_VERSION) {
        quint8 ver1 = (version >> 24) & 0xFF;
        quint8 ver2 = (version >> 16) & 0xFF;
        quint8 ver3 = (version >> 8) & 0xFF;
        emit versionReceived(tr("v%1.%2.%3").arg(ver1).arg(ver2).arg(ver3));
        return true;
    }

    return false;
}

bool CanManager::boardReboot()
{
    if (!sendCommand(PLATFORM_RX, BOARD_REBOOT, 0)) {
        return false;
    }

    emit statusMessage(tr("Reboot command sent"));
    return true;
}

void CanManager::onFramesReceived()
{
    while (m_device && m_device->framesAvailable()) {
        QCanBusFrame frame = m_device->readFrame();

        if (frame.frameId() == PLATFORM_TX) {
            QByteArray data = frame.payload();
            QDataStream stream(data);
            
            stream.setByteOrder(QDataStream::LittleEndian);
            stream >> m_expectedCode >> m_expectedParam;

            if (stream.status() != QDataStream::Ok) {
                qWarning() << "Failed to parse CAN frame payload";
                continue;
            }

            m_responseReceived = true;
        }
    }
}

void CanManager::onErrorOccurred(QCanBusDevice::CanBusError error)
{
    Q_UNUSED(error)
    if (m_device) {
        QString errorStr = m_device->errorString();

        // 清理乱码：只保留可打印的 ASCII 字符（空格到波浪号）
        QString cleanedStr;
        for (int i = 0; i < errorStr.length(); ++i) {
            QChar c = errorStr[i];
            // 保留可打印 ASCII 字符（32-126）和常见换行/制表符
            if ((c.unicode() >= 32 && c.unicode() <= 126) || c == '\n' || c == '\r' || c == '\t') {
                cleanedStr += c;
            } else if (!cleanedStr.isEmpty() && cleanedStr[cleanedStr.length()-1] != ' ') {
                // 用空格替换非 ASCII 字符
                cleanedStr += ' ';
            }
        }

        // 移除多余的空格
        cleanedStr = cleanedStr.simplified();

        // 如果清理后为空，使用原始字符串的开头部分
        if (cleanedStr.isEmpty()) {
            cleanedStr = errorStr.left(50);
        }

        emit errorMessage(tr("CAN error: %1").arg(cleanedStr));
    }
}
