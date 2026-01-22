#include "mainwindow.h"
#include "audio_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <iostream>

// HotplugMonitor implementation
HotplugMonitor::HotplugMonitor(QObject* parent)
    : QThread(parent), ctx(nullptr), callbackHandle(0), stopFlag(false) {
    libusb_init(&ctx);
}

HotplugMonitor::~HotplugMonitor() {
    stop();
    if (ctx) {
        libusb_exit(ctx);
    }
}

void HotplugMonitor::stop() {
    stopFlag = true;
    if (ctx && callbackHandle) {
        libusb_hotplug_deregister_callback(ctx, callbackHandle);
        callbackHandle = 0;
    }
    if (isRunning()) {
        // Interrupt the blocking libusb_handle_events call
        libusb_interrupt_event_handler(ctx);
        wait();
    }
}

int HotplugMonitor::hotplugCallback(libusb_context* ctx, libusb_device* dev,
                                     libusb_hotplug_event event, void* userData) {
    Q_UNUSED(ctx);
    Q_UNUSED(dev);
    Q_UNUSED(event);
    HotplugMonitor* monitor = static_cast<HotplugMonitor*>(userData);
    emit monitor->deviceChanged();
    return 0;  // Keep the callback registered
}

void HotplugMonitor::run() {
    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        std::cerr << "Hotplug not supported on this platform" << std::endl;
        return;
    }

    int rc = libusb_hotplug_register_callback(
        ctx,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_NO_FLAGS,
        Protocol::VENDOR_ID,
        LIBUSB_HOTPLUG_MATCH_ANY,  // Any product ID
        LIBUSB_HOTPLUG_MATCH_ANY,  // Any device class
        hotplugCallback,
        this,
        &callbackHandle
    );

    if (rc != LIBUSB_SUCCESS) {
        std::cerr << "Failed to register hotplug callback: " << libusb_error_name(rc) << std::endl;
        return;
    }

    while (!stopFlag) {
        int rc = libusb_handle_events(ctx);
        if (rc < 0 && rc != LIBUSB_ERROR_INTERRUPTED) {
            std::cerr << "libusb_handle_events error: " << libusb_error_name(rc) << std::endl;
            break;
        }
    }
}

// MainWindow implementation
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), worker(nullptr), hotplugMonitor(nullptr), currentPlayingSlot(-1) {
    setupUi();

    hotplugMonitor = new HotplugMonitor(this);
    connect(hotplugMonitor, &HotplugMonitor::deviceChanged, this, [this]() {
        if (!device.isConnected()) {
            refreshDeviceList();
        }
    });
    hotplugMonitor->start();
}

MainWindow::~MainWindow() {
    if (hotplugMonitor) {
        hotplugMonitor->stop();
    }
    stopExistingWorker();
}

bool MainWindow::stopExistingWorker() {
    if (worker) {
        worker->stop();
        worker->wait();
        disconnect(worker, nullptr, nullptr, nullptr);
        
        if (worker->getOperation() == Worker::Play) {
            if (currentPlayingSlot != -1 && playButtons.contains(currentPlayingSlot)) {
                playButtons[currentPlayingSlot]->setText("Play");
            }
            currentPlayingSlot = -1;
            statusLabel->setText("Connected");
        }
        
        delete worker;
        worker = nullptr;
    }
    return true;
}

void MainWindow::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout* deviceLayout = new QHBoxLayout();
    QLabel* deviceLabel = new QLabel("Device:");
    deviceCombo = new QComboBox();
    deviceCombo->setMinimumWidth(300);
    QPushButton* refreshDevicesBtn = new QPushButton("Refresh");
    connect(refreshDevicesBtn, &QPushButton::clicked, this, &MainWindow::onRefreshDevicesClicked);

    deviceLayout->addWidget(deviceLabel);
    deviceLayout->addWidget(deviceCombo, 1);
    deviceLayout->addWidget(refreshDevicesBtn);
    mainLayout->addLayout(deviceLayout);

    QHBoxLayout* topLayout = new QHBoxLayout();
    connectBtn = new QPushButton("Connect");
    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);

    statusLabel = new QLabel("Not Connected");
    statusLabel->setStyleSheet("color: red; font-weight: bold;");

    refreshBtn = new QPushButton("Refresh Track List");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    refreshBtn->setEnabled(false);

    topLayout->addWidget(connectBtn);
    topLayout->addWidget(statusLabel);
    topLayout->addStretch();
    topLayout->addWidget(refreshBtn);
    mainLayout->addLayout(topLayout);

    refreshDeviceList();

    trackTable = new QTableWidget();
    trackTable->setColumnCount(4);
    trackTable->setHorizontalHeaderLabels({"Status", "Duration", "Size", "Actions"});
    trackTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    trackTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    trackTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    trackTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    
    trackTable->verticalHeader()->setVisible(true);
    trackTable->verticalHeader()->setDefaultSectionSize(40);
    mainLayout->addWidget(trackTable);

    progressBar = new QProgressBar();
    progressBar->setVisible(false);
    mainLayout->addWidget(progressBar);

    setWindowTitle("Mooer Looper Manager");
    setWindowIcon(QIcon(":/icon.png"));
    resize(900, 700);
}

void MainWindow::refreshDeviceList() {
    deviceCombo->clear();
    deviceList = USBDevice::enumerateDevices();

    if (deviceList.empty()) {
        deviceCombo->addItem("No devices found");
        connectBtn->setEnabled(false);
    } else {
        connectBtn->setEnabled(true);
        for (const auto& dev : deviceList) {
            QString displayName = QString::fromStdString(dev.name);
            if (!dev.serial.empty()) {
                displayName += QString(" [%1]").arg(QString::fromStdString(dev.serial));
            }
            displayName += QString(" (USB VID: %1, PID: %2)")
                .arg(dev.vid, 4, 16, QChar('0')).arg(dev.pid, 4, 16, QChar('0'));
            if (!dev.hasPermission) {
                displayName += " - No permission";
            }
            deviceCombo->addItem(displayName);
        }
    }
}

void MainWindow::onRefreshDevicesClicked() {
    if (device.isConnected()) {
        QMessageBox::warning(this, "Warning", "Disconnect before refreshing device list");
        return;
    }
    refreshDeviceList();
}

void MainWindow::onConnectClicked() {
    if (device.isConnected()) {
        device.disconnect();
        connectBtn->setText("Connect");
        statusLabel->setText("Not Connected");
        statusLabel->setStyleSheet("color: red; font-weight: bold;");
        refreshBtn->setEnabled(false);
        deviceCombo->setEnabled(true);
        trackTable->setRowCount(0);
        playButtons.clear();
        currentPlayingSlot = -1;
    } else {
        int idx = deviceCombo->currentIndex();
        if (idx < 0 || idx >= (int)deviceList.size()) {
            QMessageBox::critical(this, "Error", "No device selected");
            return;
        }

        const DeviceInfo& selectedDevice = deviceList[idx];

        if (!selectedDevice.hasPermission) {
#ifdef __linux__
            int ret = QMessageBox::question(this, "Permission Required",
                "Cannot access this USB device due to insufficient permissions.\n\n"
                "Would you like to install the udev rule to fix this?\n"
                "(This will require administrator privileges)",
                QMessageBox::Yes | QMessageBox::No);

            if (ret == QMessageBox::Yes) {
                statusLabel->setText("Installing udev rule...");
                if (USBDevice::installUdevRule()) {
                    // Wait for udev to apply the new rules
                    statusLabel->setText("Waiting for udev...");
                    QApplication::processEvents();
                    QThread::sleep(1);

                    refreshDeviceList();
                    // Retry connection with updated device list
                    if (idx < (int)deviceList.size() && deviceList[idx].hasPermission) {
                        statusLabel->setText("Connecting...");
                        onConnectClicked();
                        return;
                    }
                } else {
                    QMessageBox::critical(this, "Error", "Failed to install udev rule");
                }
                statusLabel->setText("Not Connected");
            }
            return;
#else
            QMessageBox::critical(this, "Error", "Cannot access device - permission denied");
            return;
#endif
        }

        if (device.connect(selectedDevice.bus, selectedDevice.address)) {
            connectBtn->setText("Disconnect");
            statusLabel->setText("Connected");
            statusLabel->setStyleSheet("color: green; font-weight: bold;");
            refreshBtn->setEnabled(true);
            deviceCombo->setEnabled(false);
            onRefreshClicked();
        } else {
            QMessageBox::critical(this, "Error", "Failed to connect");
        }
    }
}

void MainWindow::onRefreshClicked() {
    stopExistingWorker();
    worker = new Worker(&device, Worker::List);
    connect(worker, &Worker::tracksLoaded, this, &MainWindow::onTracksLoaded);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);
    statusLabel->setText("Refreshing...");
}

void MainWindow::onTracksLoaded(std::vector<TrackInfo> tracks) {
    statusLabel->setText("Connected");
    trackTable->setRowCount(tracks.size());
    playButtons.clear();
    currentPlayingSlot = -1;

    for (const auto& t : tracks) {
        int r = t.slot;
        trackTable->setVerticalHeaderItem(r, new QTableWidgetItem(QString::number(r)));

        QTableWidgetItem* itemStatus = new QTableWidgetItem(t.has_track ? "Has Track" : "Empty");
        QTableWidgetItem* itemDuration = new QTableWidgetItem(t.has_track ? QString::asprintf("%02d:%02d", (int)t.duration/60, (int)t.duration%60) : "-");
        QTableWidgetItem* itemSize = new QTableWidgetItem(t.has_track ? QString::asprintf("%.2f MB", t.size / (1024.0*1024.0)) : "-");
        
        if (t.has_track) {
            QColor green(0xcc, 0xff, 0xcc);
            itemStatus->setBackground(green);
            itemDuration->setBackground(green);
            itemSize->setBackground(green);
        }

        trackTable->setItem(r, 0, itemStatus);
        trackTable->setItem(r, 1, itemDuration);
        trackTable->setItem(r, 2, itemSize);
        
        QWidget* pWidget = new QWidget();
        QHBoxLayout* pLayout = new QHBoxLayout(pWidget);
        pLayout->setContentsMargins(2, 2, 2, 2);
        pLayout->setSpacing(4);
        
        QPushButton* btnDown = new QPushButton("Download");
        connect(btnDown, &QPushButton::clicked, [this, r](){ onDownloadClicked(r); });
        pLayout->addWidget(btnDown);

        QPushButton* btnUp = new QPushButton("Upload");
        connect(btnUp, &QPushButton::clicked, [this, r](){ onUploadClicked(r); });
        pLayout->addWidget(btnUp);

        QPushButton* btnDel = new QPushButton("Delete");
        connect(btnDel, &QPushButton::clicked, [this, r](){ onDeleteClicked(r); });
        pLayout->addWidget(btnDel);
        
        QPushButton* btnPlay = new QPushButton("Play");
        connect(btnPlay, &QPushButton::clicked, [this, r](){ onPlayClicked(r); });
        pLayout->addWidget(btnPlay);
        playButtons[r] = btnPlay;

        if (!t.has_track) {
             btnDown->setEnabled(false);
             btnDel->setEnabled(false);
             btnPlay->setEnabled(false);
        }

        trackTable->setCellWidget(r, 3, pWidget);
    }
}

void MainWindow::onDownloadClicked(int slot) {
    stopExistingWorker();
    QString filename = QFileDialog::getSaveFileName(this, "Save Wav", QString("track_%1.wav").arg(slot));
    if (filename.isEmpty()) return;

    worker = new Worker(&device, Worker::Download, slot, filename.toStdString());
    connect(worker, &Worker::progress, this, &MainWindow::onProgress);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();
    progressBar->setVisible(true);
}

void MainWindow::onUploadClicked(int slot) {
    stopExistingWorker();
    QString filename = QFileDialog::getOpenFileName(this, "Open Wav");
    if (filename.isEmpty()) return;

    worker = new Worker(&device, Worker::Upload, slot, filename.toStdString());
    connect(worker, &Worker::progress, this, &MainWindow::onProgress);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();
    progressBar->setVisible(true);
}

void MainWindow::onDeleteClicked(int slot) {
    stopExistingWorker();
    int ret = QMessageBox::question(this, "Confirm Delete", QString("Are you sure you want to delete track %1?").arg(slot));
    if (ret != QMessageBox::Yes) return;

    worker = new Worker(&device, Worker::Delete, slot);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);
}

void MainWindow::onPlayClicked(int slot) {
    if (currentPlayingSlot == slot) {
        stopExistingWorker();
        return;
    }

    stopExistingWorker();
    
    worker = new Worker(&device, Worker::Play, slot);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();
    
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);
    statusLabel->setText(QString("Playing Slot %1...").arg(slot));
    
    currentPlayingSlot = slot;
    if (playButtons.contains(slot)) {
        playButtons[slot]->setText("Stop");
    }
}

void MainWindow::onWorkerFinished() {
    if (!worker) return;
    if (sender() != worker) return;

    Worker::Op lastOp = worker->getOperation();
    worker->deleteLater();
    worker = nullptr;
    
    progressBar->setVisible(false);
    statusLabel->setText("Connected");

    if (lastOp == Worker::Play) {
        if (currentPlayingSlot != -1 && playButtons.contains(currentPlayingSlot)) {
            playButtons[currentPlayingSlot]->setText("Play");
        }
        currentPlayingSlot = -1;
    }
    
    if (lastOp == Worker::Upload || lastOp == Worker::Download || lastOp == Worker::Delete) {
        onRefreshClicked();
    }
}

void MainWindow::onWorkerError(QString msg) {
    if (worker) {
        disconnect(worker, nullptr, nullptr, nullptr);
        worker->deleteLater();
        worker = nullptr;
    }
    progressBar->setVisible(false);
    statusLabel->setText("Connected");
    
    if (currentPlayingSlot != -1) {
        if (playButtons.contains(currentPlayingSlot)) {
            playButtons[currentPlayingSlot]->setText("Play");
        }
        currentPlayingSlot = -1;
    }

    QMessageBox::critical(this, "Error", msg);
}

void MainWindow::onProgress(int current, int total) {
    progressBar->setRange(0, total);
    progressBar->setValue(current);
}
