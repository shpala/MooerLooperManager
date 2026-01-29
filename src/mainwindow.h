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
#include <QSettings>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMenu>
#include <QSlider>
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

class FileDropTableWidget : public QTableWidget {
    Q_OBJECT
public:
    using QTableWidget::QTableWidget;
signals:
    void fileDropped(int row, QString filePath);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
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
    void onUploadClicked(int slot, QString manualPath = QString());
    void onDownloadClicked(int slot);
    void onDeleteClicked(int slot);
    void onPlayClicked(int slot);
    void onPlayPauseAction();
    void onCustomContextMenuRequested(const QPoint& pos);
    void onFileDropped(int row, QString filePath);

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

    FileDropTableWidget* trackTable;
    QPushButton* connectBtn;
    QPushButton* refreshBtn;
    QLabel* statusLabel;
    QProgressBar* progressBar;
    QSlider* seekSlider; // Replaces progressBar
    QLabel* timeLabel;
    QSlider* volumeSlider;
    QLabel* volumeLabel;
    std::atomic<int> playbackVolume;

    int currentPlayingSlot;
    double currentPlayingDuration;
    double currentProgressTime;
    bool isSeeking; // Flag to track if user is interacting with seek slider
    bool isPaused;
    QPushButton* playPauseBtn; // Global play/pause button
    QPushButton* stopBtn;      // Stop button
    QPushButton* cancelBtn;
    QString lastFileDialogDir;
    std::vector<TrackInfo> cachedTracks;

    void setupUi();
    void refreshDeviceList();
    void updateTable();
    void updatePlayButtonState();
    bool stopExistingWorker();
    void setActionsEnabled(bool enabled);
    QIcon styledIcon(QStyle::StandardPixmap sp);
};

#endif // MAINWINDOW_H
