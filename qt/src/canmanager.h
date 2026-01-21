#ifndef CANMANAGER_H
#define CANMANAGER_H

#include <QObject>
#include <QCanBus>
#include <QCanBusDevice>
#include <QFile>
#include <QByteArray>
#include <QTimer>

class CanManager : public QObject
{
    Q_OBJECT

public:
    enum FirmwareCode {
        FW_CODE_OFFSET = 0,
        FW_CODE_UPDATE_SUCCESS = 1,
        FW_CODE_VERSION = 2,
        FW_CODE_CONFIRM = 3,
        FW_CODE_FLASH_ERROR = 4,
        FW_CODE_TRANFER_ERROR = 5
    };

    enum BoardCommand {
        BOARD_START_UPDATE = 0,
        BOARD_CONFIRM = 1,
        BOARD_VERSION = 2,
        BOARD_REBOOT = 3
    };

    // CAN IDs
    static const quint32 PLATFORM_RX = 0x101;
    static const quint32 PLATFORM_TX = 0x102;
    static const quint32 FW_DATA_RX = 0x103;

    explicit CanManager(QObject *parent = nullptr);
    ~CanManager();

    bool connectToBus(const QString &interface = "pcan", const QString &channel = "usb0", int baudRate = 250000);
    void disconnectFromBus();
    bool isConnected() const;

    // 获取可用的CAN插件列表
    static QStringList getAvailablePlugins();

    // 获取指定插件的所有可用通道
    static QStringList getAvailableChannels(const QString &plugin);

    void setBitRate(int bitrate = 250000);

    // Firmware operations
    bool firmwareUpgrade(const QString &fileName, bool testMode = false);
    bool firmwareVersion();
    bool boardReboot();

signals:
    void connectedChanged(bool connected);
    void progressChanged(int percentage);
    void statusMessage(const QString &message);
    void errorMessage(const QString &message);
    void versionReceived(const QString &version);

private slots:
    void onFramesReceived();
    void onErrorOccurred(QCanBusDevice::CanBusError error);

private:
    bool sendCommand(quint32 canId, quint32 cmd, quint32 param = 0);
    bool sendData(const QByteArray &data);
    bool waitForResponse(quint32 &code, quint32 &param, int timeoutMs = 5000);
    QByteArray prepareCommand(quint32 cmd, quint32 param);

    QCanBusDevice *m_device;
    quint32 m_expectedCode;
    quint32 m_expectedParam;
    bool m_responseReceived;
    QTimer *m_timeoutTimer;
};

#endif // CANMANAGER_H
