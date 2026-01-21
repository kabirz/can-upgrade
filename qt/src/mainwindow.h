#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileDialog>
#include "canmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onBrowseClicked();
    void onFlashClicked();
    void onGetVersionClicked();
    void onRebootClicked();
    void onInterfaceChanged(int index);
    void onRefreshChannels();
    void onClearLog();
    void showLogContextMenu(const QPoint &pos);

    void onConnectedChanged(bool connected);
    void onProgressChanged(int percentage);
    void onStatusMessage(const QString &message);
    void onErrorMessage(const QString &message);
    void onVersionReceived(const QString &version);

private:
    Ui::MainWindow *ui;
    CanManager *m_canManager;

    void logMessage(const QString &message, const QString &type = "info");
    void updateConnectionState(bool connected);
    QString getSelectedInterface() const;
    QString getSelectedChannel() const;
    QString getSelectedBaudRate() const;
    void populateInterfaces();
    void populateChannels(const QString &plugin);
    void populateBaudRates();
};

#endif // MAINWINDOW_H
