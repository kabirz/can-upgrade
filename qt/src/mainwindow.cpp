#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_canManager(new CanManager(this))
{
    ui->setupUi(this);

    // Connect CAN manager signals
    connect(m_canManager, &CanManager::connectedChanged, this, &MainWindow::onConnectedChanged);
    connect(m_canManager, &CanManager::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_canManager, &CanManager::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_canManager, &CanManager::errorMessage, this, &MainWindow::onErrorMessage);
    connect(m_canManager, &CanManager::versionReceived, this, &MainWindow::onVersionReceived);

    // Connect UI signals
    connect(ui->comboBoxInterface, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInterfaceChanged);
    connect(ui->buttonConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->buttonBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->buttonFlash, &QPushButton::clicked, this, &MainWindow::onFlashClicked);
    connect(ui->buttonGetVersion, &QPushButton::clicked, this, &MainWindow::onGetVersionClicked);
    connect(ui->buttonReboot, &QPushButton::clicked, this, &MainWindow::onRebootClicked);
    connect(ui->buttonRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshChannels);

    // Setup log context menu
    ui->textEditLog->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->textEditLog, &QTextEdit::customContextMenuRequested,
            this, &MainWindow::showLogContextMenu);

    // Populate interfaces, channels and baud rates
    populateInterfaces();
    populateBaudRates();

    updateConnectionState(false);
    logMessage(tr("Application started. Ready to connect."));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onConnectClicked()
{
    if (m_canManager->isConnected()) {
        m_canManager->disconnectFromBus();
    } else {
        QString interface = getSelectedInterface();
        QString channel = getSelectedChannel();
        QString baudRateStr = getSelectedBaudRate();
        // 将 kbps 转换为 bps
        int baudRate = baudRateStr.toInt() * 1000;

        if (m_canManager->connectToBus(interface, channel, baudRate)) {
            logMessage(tr("Connected to %1 on %2 at %3 kbps").arg(interface, channel).arg(baudRateStr), "success");
        } else {
            logMessage(tr("Failed to connect to %1 on %2 at %3 kbps").arg(interface, channel).arg(baudRateStr), "error");
        }
    }
}

void MainWindow::onBrowseClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Select Firmware File"),
        "",
        tr("Binary Files (*.bin);;All Files (*)")
    );

    if (!fileName.isEmpty()) {
        ui->lineEditFirmwareFile->setText(fileName);
        logMessage(tr("Selected firmware file: %1").arg(fileName));
        ui->buttonFlash->setEnabled(m_canManager->isConnected());
    }
}

void MainWindow::onFlashClicked()
{
    QString fileName = ui->lineEditFirmwareFile->text();
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a firmware file first."));
        return;
    }

    bool testMode = ui->checkBoxTestMode->isChecked();

    logMessage(tr("Starting firmware upgrade: %1 (test mode: %2)")
                   .arg(fileName)
                   .arg(testMode ? tr("enabled") : tr("disabled")));

    ui->buttonFlash->setEnabled(false);
    ui->progressBar->setValue(0);

    // Run firmware upgrade in a separate thread or use QTimer to prevent UI freeze
    QTimer::singleShot(100, this, [this, fileName, testMode]() {
        if (m_canManager->firmwareUpgrade(fileName, testMode)) {
            logMessage(tr("Firmware upgrade completed successfully!"), "success");
            QMessageBox::information(this, tr("Success"),
                                     tr("Firmware upload completed. Please reboot the board to complete the upgrade."));
        } else {
            logMessage(tr("Firmware upgrade failed!"), "error");
            QMessageBox::critical(this, tr("Error"), tr("Firmware upgrade failed. Check log for details."));
        }
        ui->buttonFlash->setEnabled(true);
    });
}

void MainWindow::onGetVersionClicked()
{
    logMessage(tr("Requesting board version..."));
    if (m_canManager->firmwareVersion()) {
        // Version will be received via signal
    } else {
        logMessage(tr("Failed to get board version"));
        QMessageBox::warning(this, tr("Error"), tr("Failed to get board version"));
    }
}

void MainWindow::onRebootClicked()
{
    auto reply = QMessageBox::question(
        this,
        tr("Confirm Reboot"),
        tr("Are you sure you want to reboot the board?"),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        logMessage(tr("Sending reboot command..."));
        if (m_canManager->boardReboot()) {
            logMessage(tr("Reboot command sent successfully"));
            QMessageBox::information(this, tr("Info"), tr("Reboot command sent. The board will restart in a few seconds."));
        } else {
            logMessage(tr("Failed to send reboot command"));
            QMessageBox::warning(this, tr("Error"), tr("Failed to send reboot command"));
        }
    }
}

void MainWindow::onConnectedChanged(bool connected)
{
    updateConnectionState(connected);
}

void MainWindow::onProgressChanged(int percentage)
{
    ui->progressBar->setValue(percentage);
}

void MainWindow::onStatusMessage(const QString &message)
{
    logMessage(message);
}

void MainWindow::onErrorMessage(const QString &message)
{
    logMessage(tr("ERROR: %1").arg(message), "error");
}

void MainWindow::onVersionReceived(const QString &version)
{
    ui->labelVersion->setText(tr("Version: %1").arg(version));
    logMessage(tr("Board version: %1").arg(version));
}

void MainWindow::logMessage(const QString &message, const QString &type)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color;

    // 根据消息类型设置颜色
    if (type == "error") {
        color = "#d32f2f";  // 红色 - 错误
    } else if (type == "warning") {
        color = "#f57c00";  // 橙色 - 警告
    } else if (type == "success") {
        color = "#388e3c";  // 绿色 - 成功
    } else {
        color = "#212121";  // 黑色 - 信息
    }

    // 使用 HTML 格式设置颜色
    ui->textEditLog->append(QString("<span style=\"color: %1;\">[%2] %3</span>").arg(color, timestamp, message));
}

void MainWindow::onClearLog()
{
    ui->textEditLog->clear();
}

void MainWindow::showLogContextMenu(const QPoint &pos)
{
    QMenu *menu = ui->textEditLog->createStandardContextMenu();

    menu->addSeparator();

    QAction *clearAction = new QAction(tr("Clear Log"), this);
    connect(clearAction, &QAction::triggered, this, &MainWindow::onClearLog);
    menu->addAction(clearAction);

    menu->exec(ui->textEditLog->mapToGlobal(pos));
    delete menu;
}

void MainWindow::updateConnectionState(bool connected)
{
    if (connected) {
        ui->buttonConnect->setText(tr("Disconnect"));
        ui->labelConnectionStatus->setText(tr("Connected"));
        ui->labelConnectionStatus->setStyleSheet("QLabel { color: green; font-weight: bold; }");
        ui->buttonGetVersion->setEnabled(true);
        ui->buttonReboot->setEnabled(true);
        ui->buttonFlash->setEnabled(!ui->lineEditFirmwareFile->text().isEmpty());
        ui->comboBoxInterface->setEnabled(false);
        ui->comboBoxChannel->setEnabled(false);
    } else {
        ui->buttonConnect->setText(tr("Connect"));
        ui->labelConnectionStatus->setText(tr("Disconnected"));
        ui->labelConnectionStatus->setStyleSheet("QLabel { color: red; font-weight: bold; }");
        ui->buttonGetVersion->setEnabled(false);
        ui->buttonReboot->setEnabled(false);
        ui->buttonFlash->setEnabled(false);
        ui->comboBoxInterface->setEnabled(true);
        ui->comboBoxChannel->setEnabled(true);
    }
}

QString MainWindow::getSelectedInterface() const
{
    return ui->comboBoxInterface->currentText();
}

QString MainWindow::getSelectedChannel() const
{
    return ui->comboBoxChannel->currentText();
}

QString MainWindow::getSelectedBaudRate() const
{
    // 从显示文本中提取纯数字部分（去除 " (kbps)"）
    QString text = ui->comboBoxBaudRate->currentText();
    return text.split(' ').first();
}

void MainWindow::populateInterfaces()
{
    QStringList plugins = CanManager::getAvailablePlugins();

    ui->comboBoxInterface->clear();
    ui->comboBoxInterface->addItems(plugins);

    // Set default interface based on platform
#ifdef Q_OS_WIN
    if (plugins.contains("pcan")) {
        ui->comboBoxInterface->setCurrentText("pcan");
    } else if (!plugins.isEmpty()) {
        ui->comboBoxInterface->setCurrentIndex(0);
    }
#else
    if (plugins.contains("socketcan")) {
        ui->comboBoxInterface->setCurrentText("socketcan");
    } else if (!plugins.isEmpty()) {
        ui->comboBoxInterface->setCurrentIndex(0);
    }
#endif

    // Populate channels for the selected interface
    if (!plugins.isEmpty()) {
        populateChannels(getSelectedInterface());
    }

    logMessage(tr("Found %1 CAN plugin(s): %2").arg(plugins.size()).arg(plugins.join(", ")));
}

void MainWindow::populateChannels(const QString &plugin)
{
    QStringList channels = CanManager::getAvailableChannels(plugin);

    ui->comboBoxChannel->clear();
    ui->comboBoxChannel->addItems(channels);

    // Set default channel based on platform
#ifdef Q_OS_WIN
    if (channels.contains("PCAN_USBBUS1")) {
        ui->comboBoxChannel->setCurrentText("PCAN_USBBUS1");
    } else if (!channels.isEmpty()) {
        ui->comboBoxChannel->setCurrentIndex(0);
    }
#else
    if (channels.contains("can0")) {
        ui->comboBoxChannel->setCurrentText("can0");
    } else if (!channels.isEmpty()) {
        ui->comboBoxChannel->setCurrentIndex(0);
    }
#endif

    if (!channels.isEmpty()) {
        logMessage(tr("Found %1 channel(s) for %2").arg(channels.size()).arg(plugin));
    }
}

void MainWindow::onInterfaceChanged(int index)
{
    Q_UNUSED(index);
    QString plugin = getSelectedInterface();
    populateChannels(plugin);
}

void MainWindow::onRefreshChannels()
{
    QString plugin = getSelectedInterface();
    populateChannels(plugin);
}

void MainWindow::populateBaudRates()
{
    // 常用的 CAN 总线波特率列表（使用 kbps 作为单位）
    QStringList baudRates = {
        "10 (kbps)",        // 10 kbps
        "20 (kbps)",        // 20 kbps
        "50 (kbps)",        // 50 kbps
        "80 (kbps)",        // 80 kbps
        "125 (kbps)",       // 125 kbps
        "250 (kbps)",       // 250 kbps
        "500 (kbps)",       // 500 kbps
        "1000 (kbps)"       // 1000 kbps (1 Mbps)
    };

    ui->comboBoxBaudRate->clear();
    ui->comboBoxBaudRate->addItems(baudRates);

    // 设置默认值为 250 kbps
    ui->comboBoxBaudRate->setCurrentText("250 (kbps)");

    logMessage(tr("Baud rate options populated. Default: 250 kbps"));
}
