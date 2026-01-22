#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QThread>
#include <QMap>
#include <atomic>
#include <libusb-1.0/libusb.h>
#include "usb_device.h"
#include "worker.h"

class HotplugMonitor : public QThread {
    Q_OBJECT

public:
    HotplugMonitor(QObject* parent = nullptr);
    ~HotplugMonitor();

    void stop();

signals:
    void deviceChanged();

protected:
    void run() override;

private:
    libusb_context* ctx;
    libusb_hotplug_callback_handle callbackHandle;
    std::atomic<bool> stopFlag;

    static int hotplugCallback(libusb_context* ctx, libusb_device* dev,
                               libusb_hotplug_event event, void* userData);
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onRefreshDevicesClicked();
    void onConnectClicked();
    void onRefreshClicked();
    void onUploadClicked(int slot);
    void onDownloadClicked(int slot);
    void onDeleteClicked(int slot);
    void onPlayClicked(int slot);
    
    void onWorkerFinished();
    void onWorkerError(QString msg);
    void onTracksLoaded(std::vector<TrackInfo> tracks);
    void onProgress(int current, int total);

private:
    USBDevice device;
    Worker* worker;
    HotplugMonitor* hotplugMonitor;

    QComboBox* deviceCombo;
    std::vector<DeviceInfo> deviceList;

    QTableWidget* trackTable;
    QPushButton* connectBtn;
    QPushButton* refreshBtn;
    QLabel* statusLabel;
    QProgressBar* progressBar;

    int currentPlayingSlot;
    QMap<int, QPushButton*> playButtons;

    void setupUi();
    void refreshDeviceList();
    void updateTable();
    void updatePlayButtonState();
    bool stopExistingWorker();
};

#endif // MAINWINDOW_H
