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
#include <QPainter>
#include <QFileInfo>
#include <QSettings>
#include <QShortcut>
#include <iostream>
#include <portaudio.h>

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

// FileDropTableWidget implementation
void FileDropTableWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void FileDropTableWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        QPoint pos = event->position().toPoint();
        int row = rowAt(pos.y());
        if (row >= 0) {
            selectRow(row);
        } else {
            clearSelection();
        }
    }
}

void FileDropTableWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    clearSelection();
}

void FileDropTableWidget::dropEvent(QDropEvent* event) {
    clearSelection();
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        if (!urlList.isEmpty()) {
            QString filePath = urlList.first().toLocalFile();
            QString ext = QFileInfo(filePath).suffix().toLower();
            static const QStringList supportedExts = {"wav", "mp3", "flac", "ogg", "m4a", "wma"};
            if (!supportedExts.contains(ext)) {
                QMessageBox::warning(nullptr, "Unsupported File",
                    "Unsupported audio format. Supported: WAV, MP3, FLAC, OGG, M4A, WMA.");
                return;
            }
            QPoint pos = event->position().toPoint();
            int row = rowAt(pos.y());
            if (row < 0) {
                // Find first empty slot (duration shows "—" for empty slots)
                for (int r = 0; r < rowCount(); r++) {
                    QTableWidgetItem* durItem = item(r, 0);
                    if (durItem && durItem->text() == QString::fromUtf8("\u2014")) {
                        row = r;
                        break;
                    }
                }
                if (row < 0) {
                    QMessageBox::warning(nullptr, "No Empty Slots",
                        "All slots are occupied. Delete a track first.");
                    return;
                }
            }
            emit fileDropped(row, filePath);
        }
    }
}

// MainWindow implementation
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), worker(nullptr), hotplugMonitor(nullptr),
      playbackVolume(100), currentPlayingSlot(-1), currentPlayingDuration(0.0), currentProgressTime(0.0), 
      isSeeking(false), isPaused(false),
      cancelBtn(nullptr) {
    Pa_Initialize();
    lastFileDialogDir = QSettings().value("lastFileDialogDir").toString();
    playbackVolume = QSettings().value("playbackVolume", 100).toInt();
    setupUi();

    hotplugMonitor = new HotplugMonitor(this);
    connect(hotplugMonitor, &HotplugMonitor::deviceChanged, this, [this]() {
        if (device.isConnected()) {
            // Check if our connected device was removed
            auto devices = USBDevice::enumerateDevices();
            bool found = false;
            for (const auto& d : devices) {
                if (d.bus == device.getBus() && d.address == device.getAddress()) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                stopExistingWorker();
                device.disconnect();
                connectBtn->setText("Connect");
                statusLabel->setText("Device disconnected");
                statusLabel->setStyleSheet("color: red; font-weight: bold;");
                refreshBtn->setEnabled(false);
                deviceCombo->setEnabled(true);
                trackTable->setRowCount(0);
                currentPlayingSlot = -1;
                setActionsEnabled(true);
                refreshDeviceList();
            }
        } else {
            refreshDeviceList();
        }
    });
    hotplugMonitor->start();

    // Auto-connect if exactly one device with permissions is available
    if (deviceList.size() == 1 && deviceList[0].hasPermission) {
        deviceCombo->setCurrentIndex(0);
        onConnectClicked();
    }
}

MainWindow::~MainWindow() {
    if (hotplugMonitor) {
        hotplugMonitor->stop();
    }
    stopExistingWorker();
    Pa_Terminate();
}

bool MainWindow::stopExistingWorker() {
    if (worker) {
        // Disconnect first to avoid any signals being processed while we are stopping/deleting
        disconnect(worker, nullptr, nullptr, nullptr);

        worker->stop();

        // If playing, send an explicit stop command to the device to abort internal state
        if (worker->getOperation() == Worker::Play && currentPlayingSlot != -1) {
            device.stopPlayback(currentPlayingSlot);
        }

        worker->wait();

        if (worker->getOperation() == Worker::Play) {
            playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPlay));
            playPauseBtn->setToolTip("Play Selected Track");
            stopBtn->setEnabled(false);
            currentPlayingSlot = -1;
            isPaused = false;
            currentProgressTime = 0.0;
            statusLabel->setText("Connected");
        }

        seekSlider->setVisible(false); cancelBtn->setVisible(false); timeLabel->setVisible(false);
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

    trackTable = new FileDropTableWidget();
    trackTable->setColumnCount(3);
    trackTable->setHorizontalHeaderLabels({"Duration", "Size", "Actions"});
    trackTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    trackTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    trackTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    trackTable->verticalHeader()->setVisible(true);
    trackTable->verticalHeader()->setDefaultSectionSize(40);
    trackTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    trackTable->setSelectionMode(QAbstractItemView::SingleSelection);
    trackTable->setContextMenuPolicy(Qt::CustomContextMenu);
    trackTable->setAcceptDrops(true);

    connect(trackTable, &FileDropTableWidget::customContextMenuRequested, this, &MainWindow::onCustomContextMenuRequested);
    connect(trackTable, &FileDropTableWidget::fileDropped, this, &MainWindow::onFileDropped);
    connect(trackTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < (int)cachedTracks.size() && cachedTracks[row].has_track) {
            onPlayClicked(row);
        }
    });
    // Update Play/Pause button state when selection changes
    connect(trackTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        int row = trackTable->currentRow();
        bool hasTrack = (row >= 0 && row < (int)cachedTracks.size() && cachedTracks[row].has_track);
        
        // If playing, button is Stop (enabled). If not playing, button is Play (enabled only if track exists)
        if (currentPlayingSlot != -1) {
            playPauseBtn->setEnabled(true); 
            // If the selected row is DIFFERENT from playing row, clicking Play will switch tracks.
            // If it is the SAME row, it's effectively a Stop button.
            // We'll handle this logic in the button click handler.
        } else {
             playPauseBtn->setEnabled(hasTrack);
        }
    });

    mainLayout->addWidget(trackTable);

    QHBoxLayout* progressLayout = new QHBoxLayout();
    
    // Play/Pause Button
    playPauseBtn = new QPushButton(styledIcon(QStyle::SP_MediaPlay), "");
    playPauseBtn->setToolTip("Play Selected Track");
    playPauseBtn->setEnabled(false);
    connect(playPauseBtn, &QPushButton::clicked, this, &MainWindow::onPlayPauseAction);
    progressLayout->addWidget(playPauseBtn);

    // Stop Button
    stopBtn = new QPushButton(styledIcon(QStyle::SP_MediaStop), "");
    stopBtn->setToolTip("Stop");
    stopBtn->setEnabled(false);
    connect(stopBtn, &QPushButton::clicked, this, [this]() {
        stopExistingWorker();
        setActionsEnabled(true);
        // Reset seek slider to 0
        if (seekSlider->isVisible()) seekSlider->setValue(0);
        timeLabel->setText("00:00 / " + QString::asprintf("%02d:%02d", (int)currentPlayingDuration / 60, (int)currentPlayingDuration % 60));
    });
    progressLayout->addWidget(stopBtn);

    progressBar = new QProgressBar();
    progressBar->setVisible(false);
    
    seekSlider = new QSlider(Qt::Horizontal);
    seekSlider->setVisible(false);
    seekSlider->setRange(0, 1000); // Higher resolution for smoother slider
    
    // Seek logic
    connect(seekSlider, &QSlider::sliderPressed, this, [this]() {
        isSeeking = true;
    });
    connect(seekSlider, &QSlider::sliderReleased, this, [this]() {
        isSeeking = false;
        // Calculate new position
        if (currentPlayingDuration > 0 && currentPlayingSlot != -1) {
             int val = seekSlider->value();
             double ratio = (double)val / seekSlider->maximum();
             double startTime = ratio * currentPlayingDuration;
             
             // Restart playback at new position
             int slot = currentPlayingSlot;
             stopExistingWorker();
             
             // Manually restart play with offset
             currentPlayingSlot = slot;
             worker = new Worker(&device, Worker::Play, slot, "", currentPlayingDuration, &playbackVolume, startTime);
             connect(worker, &Worker::progress, this, &MainWindow::onProgress);
             connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
             connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
             worker->start();
             
             seekSlider->setVisible(true); cancelBtn->setVisible(false);
             timeLabel->setVisible(true);
             setActionsEnabled(false);
             
             playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPause));
             playPauseBtn->setEnabled(true);
        }
    });

    timeLabel = new QLabel();
    timeLabel->setVisible(false);
    timeLabel->setMinimumWidth(80);
    cancelBtn = new QPushButton("Cancel");
    cancelBtn->setVisible(false);
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        stopExistingWorker();
        currentPlayingSlot = -1; // Explicitly reset since we cancelled
        setActionsEnabled(true);
    });
    progressLayout->addWidget(progressBar, 1);
    progressLayout->addWidget(seekSlider, 1);
    progressLayout->addWidget(timeLabel);
    progressLayout->addWidget(cancelBtn);

    progressLayout->addStretch();
    QLabel* volIcon = new QLabel("Vol:");
    volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(playbackVolume.load());
    volumeSlider->setMaximumWidth(120);
    volumeLabel = new QLabel(QString("%1%").arg(playbackVolume.load()));
    volumeLabel->setMinimumWidth(40);
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        playbackVolume = val;
        volumeLabel->setText(QString("%1%").arg(val));
        QSettings().setValue("playbackVolume", val);
    });
    progressLayout->addWidget(volIcon);
    progressLayout->addWidget(volumeSlider);
    progressLayout->addWidget(volumeLabel);

    mainLayout->addLayout(progressLayout);

    setWindowTitle("Mooer Looper Manager");
    setWindowIcon(QIcon(":/icon.png"));
    resize(900, 700);

    // Keyboard shortcuts
    auto* shortcutRefresh = new QShortcut(QKeySequence("Ctrl+R"), this);
    connect(shortcutRefresh, &QShortcut::activated, this, &MainWindow::onRefreshClicked);

    auto* shortcutDisconnect = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(shortcutDisconnect, &QShortcut::activated, this, &MainWindow::onConnectClicked);

    auto* shortcutDelete = new QShortcut(QKeySequence::Delete, this);
    connect(shortcutDelete, &QShortcut::activated, this, [this]() {
        int row = trackTable->currentRow();
        if (row >= 0) onDeleteClicked(row);
    });

    auto* shortcutPlay = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(shortcutPlay, &QShortcut::activated, this, &MainWindow::onPlayPauseAction);

    auto* shortcutUp = new QShortcut(QKeySequence(Qt::Key_Up), this);
    connect(shortcutUp, &QShortcut::activated, this, [this]() {
        int row = trackTable->currentRow();
        if (row > 0) trackTable->setCurrentCell(row - 1, 0);
    });

    auto* shortcutDown = new QShortcut(QKeySequence(Qt::Key_Down), this);
    connect(shortcutDown, &QShortcut::activated, this, [this]() {
        int row = trackTable->currentRow();
        if (row < trackTable->rowCount() - 1) trackTable->setCurrentCell(row + 1, 0);
    });
}

void MainWindow::onPlayPauseAction() {
    int row = trackTable->currentRow();
    if (row < 0 || row >= (int)cachedTracks.size() || !cachedTracks[row].has_track) {
        // If no valid track selected, but playing/paused, maybe Stop? 
        // For now, just return to keep it simple, or stop if user expects it.
        if (currentPlayingSlot != -1 || isPaused) stopExistingWorker();
        return;
    }

    // If we selected a DIFFERENT track than the one currently active (playing or paused)
    if (row != currentPlayingSlot) {
        onPlayClicked(row);
        return;
    }

    if (isPaused) {
        // Resume
        int slot = currentPlayingSlot;
        if (worker) {
             disconnect(worker, nullptr, nullptr, nullptr);
             worker->stop();
             worker->wait();
             delete worker;
             worker = nullptr;
        }
        isPaused = false;
        
        worker = new Worker(&device, Worker::Play, slot, "", currentPlayingDuration, &playbackVolume, currentProgressTime);
        connect(worker, &Worker::progress, this, &MainWindow::onProgress);
        connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
        connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
        worker->start();
        
        playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPause));
        playPauseBtn->setToolTip("Pause");
        stopBtn->setEnabled(true);
        
    } else if (currentPlayingSlot != -1) {
        // Pause
        isPaused = true;
        if (worker) {
             disconnect(worker, nullptr, nullptr, nullptr);
             worker->stop();
             worker->wait();
             delete worker;
             worker = nullptr;
        }
        // UI Update
        playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPlay));
        playPauseBtn->setToolTip("Resume");
        stopBtn->setEnabled(true);
    } else {
        // Play from start (row == currentPlayingSlot, which is -1)
        onPlayClicked(row);
    }
}


void MainWindow::onCustomContextMenuRequested(const QPoint& pos) {
    int row = trackTable->rowAt(pos.y());
    if (row < 0) return;

    bool hasTrack = (row >= 0 && row < (int)cachedTracks.size() && cachedTracks[row].has_track);

    QMenu menu(this);
    QAction* actPlay = menu.addAction(currentPlayingSlot == row ? "Stop" : "Play");
    actPlay->setEnabled(hasTrack);

    menu.addSeparator();

    QAction* actUpload = menu.addAction("Upload");
    QAction* actDownload = menu.addAction("Download");
    actDownload->setEnabled(hasTrack);

    QAction* actDelete = menu.addAction("Delete");
    actDelete->setEnabled(hasTrack);

    QAction* selectedAction = menu.exec(trackTable->viewport()->mapToGlobal(pos));
    if (!selectedAction) return;

    if (selectedAction == actPlay) onPlayClicked(row);
    else if (selectedAction == actUpload) onUploadClicked(row);
    else if (selectedAction == actDownload) onDownloadClicked(row);
    else if (selectedAction == actDelete) onDeleteClicked(row);
}

void MainWindow::onFileDropped(int row, QString filePath) {
    onUploadClicked(row, filePath);
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

        connectBtn->setEnabled(false);
        deviceCombo->setEnabled(false);
        statusLabel->setText("Connecting...");
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QApplication::processEvents();

        bool ok = device.connect(selectedDevice.bus, selectedDevice.address);

        while (QApplication::overrideCursor()) {
            QApplication::restoreOverrideCursor();
        }

        if (ok) {
            connectBtn->setEnabled(true);
            connectBtn->setText("Disconnect");
            statusLabel->setText("Connected");
            statusLabel->setStyleSheet("color: green; font-weight: bold;");
            refreshBtn->setEnabled(true);
            onRefreshClicked();
        } else {
            connectBtn->setEnabled(true);
            deviceCombo->setEnabled(true);
            statusLabel->setText("Not Connected");
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

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progressBar->setVisible(true); cancelBtn->setVisible(true);
    progressBar->setRange(0, 0);
    statusLabel->setText("Refreshing...");
    setActionsEnabled(false);
}

void MainWindow::onTracksLoaded(std::vector<TrackInfo> tracks) {
    cachedTracks = tracks;
    statusLabel->setText("Connected");
    trackTable->setRowCount(tracks.size());
    currentPlayingSlot = -1;

    for (const auto& t : tracks) {
        int r = t.slot;
        trackTable->setVerticalHeaderItem(r, new QTableWidgetItem(QString::number(r)));

        QTableWidgetItem* itemDuration = new QTableWidgetItem(
            t.has_track ? QString::asprintf("%02d:%02d", (int)t.duration/60, (int)t.duration%60)
                        : QString::fromUtf8("\u2014"));
        QTableWidgetItem* itemSize = new QTableWidgetItem(
            t.has_track ? QString::asprintf("%.2f MB", t.size / (1024.0*1024.0))
                        : QString::fromUtf8("\u2014"));

        if (t.has_track) {
            QColor green(0xcc, 0xff, 0xcc);
            itemDuration->setBackground(green);
            itemSize->setBackground(green);
        }

        trackTable->setItem(r, 0, itemDuration);
        trackTable->setItem(r, 1, itemSize);

        QWidget* pWidget = new QWidget();
        QHBoxLayout* pLayout = new QHBoxLayout(pWidget);
        pLayout->setContentsMargins(2, 2, 2, 2);
        pLayout->setSpacing(4);

        QPushButton* btnDown = new QPushButton(styledIcon(QStyle::SP_ArrowDown), "");
        btnDown->setToolTip("Download");
        connect(btnDown, &QPushButton::clicked, [this, r](){ onDownloadClicked(r); });
        pLayout->addWidget(btnDown);

        QPushButton* btnUp = new QPushButton(styledIcon(QStyle::SP_ArrowUp), "");
        btnUp->setToolTip("Upload");
        connect(btnUp, &QPushButton::clicked, [this, r](){ onUploadClicked(r); });
        pLayout->addWidget(btnUp);

        QPushButton* btnDel = new QPushButton(styledIcon(QStyle::SP_DialogCloseButton), "");
        btnDel->setToolTip("Delete");
        connect(btnDel, &QPushButton::clicked, [this, r](){ onDeleteClicked(r); });
        pLayout->addWidget(btnDel);

        if (!t.has_track) {
             btnDown->setEnabled(false);
             btnDel->setEnabled(false);
        }

        trackTable->setCellWidget(r, 2, pWidget);
    }
}

void MainWindow::onDownloadClicked(int slot) {
    stopExistingWorker();
    QString filename = QFileDialog::getSaveFileName(this, "Save Wav",
        lastFileDialogDir.isEmpty() ? QString("track_%1.wav").arg(slot)
                                    : lastFileDialogDir + QString("/track_%1.wav").arg(slot),
        "WAV Files (*.wav);;All Files (*)");
    if (filename.isEmpty()) return;
    lastFileDialogDir = QFileInfo(filename).absolutePath();
    QSettings().setValue("lastFileDialogDir", lastFileDialogDir);

    worker = new Worker(&device, Worker::Download, slot, filename.toStdString());
    connect(worker, &Worker::progress, this, &MainWindow::onProgress);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progressBar->setVisible(true); cancelBtn->setVisible(true);
    setActionsEnabled(false);
    statusLabel->setText(QString("Downloading Slot %1...").arg(slot));
}

void MainWindow::onUploadClicked(int slot, QString manualPath) {
    stopExistingWorker();

    // Check if slot already has a track and confirm overwrite
    if (slot >= 0 && slot < (int)cachedTracks.size() && cachedTracks[slot].has_track) {
        int ret = QMessageBox::question(this, "Confirm Overwrite",
            QString("Slot %1 already has a track. Overwrite it?").arg(slot));
        if (ret != QMessageBox::Yes) return;
    }

    QString filename = manualPath;
    if (filename.isEmpty()) {
        filename = QFileDialog::getOpenFileName(this, "Open Audio File",
            lastFileDialogDir, "Audio Files (*.wav *.mp3 *.flac *.ogg *.m4a *.wma);;WAV Files (*.wav);;All Files (*)");
    }

    if (filename.isEmpty()) return;
    lastFileDialogDir = QFileInfo(filename).absolutePath();
    QSettings().setValue("lastFileDialogDir", lastFileDialogDir);

    worker = new Worker(&device, Worker::Upload, slot, filename.toStdString());
    connect(worker, &Worker::progress, this, &MainWindow::onProgress);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progressBar->setVisible(true); cancelBtn->setVisible(true);
    setActionsEnabled(false);
    statusLabel->setText(QString("Uploading to Slot %1...").arg(slot));
}

void MainWindow::onDeleteClicked(int slot) {
    stopExistingWorker();
    int ret = QMessageBox::question(this, "Confirm Delete", QString("Are you sure you want to delete track %1?").arg(slot));
    if (ret != QMessageBox::Yes) return;

    worker = new Worker(&device, Worker::Delete, slot);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progressBar->setVisible(true); cancelBtn->setVisible(true);
    progressBar->setRange(0, 0);
    setActionsEnabled(false);
    statusLabel->setText(QString("Deleting Slot %1...").arg(slot));
}

void MainWindow::onPlayClicked(int slot) {
    if (currentPlayingSlot == slot) {
        stopExistingWorker();
        setActionsEnabled(true);
        return;
    }

    stopExistingWorker();

    double duration = 0.0;
    if (slot >= 0 && slot < (int)cachedTracks.size() && cachedTracks[slot].has_track) {
        duration = cachedTracks[slot].duration;
    }
    currentPlayingDuration = duration;

    worker = new Worker(&device, Worker::Play, slot, "", duration, &playbackVolume);
    connect(worker, &Worker::progress, this, &MainWindow::onProgress);
    connect(worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(worker, &Worker::error, this, &MainWindow::onWorkerError);
    worker->start();

    seekSlider->setVisible(true); progressBar->setVisible(false); cancelBtn->setVisible(false);
    timeLabel->setVisible(true);
    timeLabel->setText("00:00 / " + QString::asprintf("%02d:%02d", (int)duration / 60, (int)duration % 60));
    statusLabel->setText(QString("Playing Slot %1...").arg(slot));
    setActionsEnabled(false);

    currentPlayingSlot = slot;
    playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPause));
    playPauseBtn->setToolTip("Pause");
    playPauseBtn->setEnabled(true);
    
    stopBtn->setEnabled(true);
    isPaused = false;
    currentProgressTime = 0.0;
}

void MainWindow::onWorkerFinished() {
    Worker* finishedWorker = qobject_cast<Worker*>(sender());
    if (!finishedWorker) return;

    if (finishedWorker != worker) {
        finishedWorker->deleteLater();
        return;
    }

    while (QApplication::overrideCursor()) {
        QApplication::restoreOverrideCursor();
    }

    Worker::Op lastOp = worker->getOperation();
    worker->deleteLater();
    worker = nullptr;

    progressBar->setVisible(false); seekSlider->setVisible(false); cancelBtn->setVisible(false); timeLabel->setVisible(false);
    statusLabel->setText("Connected");
    setActionsEnabled(true);

    if (lastOp == Worker::Play) {
        playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPlay));
        playPauseBtn->setToolTip("Play Selected Track");
        stopBtn->setEnabled(false);
        currentPlayingSlot = -1;
        isPaused = false;
        currentProgressTime = 0.0;
    }

    if (lastOp == Worker::Upload || lastOp == Worker::Download || lastOp == Worker::Delete) {
        onRefreshClicked();
    }
}

void MainWindow::onWorkerError(QString msg) {
    Worker* finishedWorker = qobject_cast<Worker*>(sender());

    while (QApplication::overrideCursor()) {
        QApplication::restoreOverrideCursor();
    }

    if (finishedWorker && finishedWorker == worker) {
        disconnect(worker, nullptr, nullptr, nullptr);
        worker->deleteLater();
        worker = nullptr;
    } else if (finishedWorker) {
        finishedWorker->deleteLater();
        return;
    }

    progressBar->setVisible(false); seekSlider->setVisible(false); cancelBtn->setVisible(false); timeLabel->setVisible(false);
    statusLabel->setText("Connected");
    setActionsEnabled(true);

    if (currentPlayingSlot != -1) {
        playPauseBtn->setIcon(styledIcon(QStyle::SP_MediaPlay));
        playPauseBtn->setToolTip("Play Selected Track");
        stopBtn->setEnabled(false);
        currentPlayingSlot = -1;
        isPaused = false;
        currentProgressTime = 0.0;
    }

    QMessageBox::critical(this, "Error", msg);
}

void MainWindow::onProgress(int current, int total) {
    if (progressBar->isVisible()) {
        progressBar->setRange(0, total);
        progressBar->setValue(current);
    }

    if (currentPlayingSlot != -1 && total > 0 && currentPlayingDuration > 0) {
        if (seekSlider->isVisible() && !isSeeking) {
             // For streaming, 'current' is current chunk, 'total' is total chunks
             // We want slider to move linearly.
             int sliderMax = seekSlider->maximum();
             int val = static_cast<int>((static_cast<double>(current) / total) * sliderMax);
             seekSlider->setValue(val);
        }

        double elapsed = (static_cast<double>(current) / total) * currentPlayingDuration;
        currentProgressTime = elapsed;
        int elapsedSecs = static_cast<int>(elapsed);
        int totalSecs = static_cast<int>(currentPlayingDuration);
        timeLabel->setText(QString::asprintf("%02d:%02d / %02d:%02d",
            elapsedSecs / 60, elapsedSecs % 60, totalSecs / 60, totalSecs % 60));
    }
}

void MainWindow::setActionsEnabled(bool enabled) {
    bool isPlaying = (worker && worker->getOperation() == Worker::Play);
    bool isWorking = (worker != nullptr);

    refreshBtn->setEnabled(enabled && device.isConnected());
    connectBtn->setEnabled(enabled);
    
    // playPauseBtn logic
    if (isPlaying) {
        playPauseBtn->setEnabled(true); // Can always pause
        stopBtn->setEnabled(true);
    } else if (isWorking) {
        playPauseBtn->setEnabled(false);
        stopBtn->setEnabled(false);
    } else {
        int row = trackTable->currentRow();
        bool hasTrack = (row >= 0 && row < (int)cachedTracks.size() && cachedTracks[row].has_track);
        playPauseBtn->setEnabled(hasTrack);
        stopBtn->setEnabled(false);
    }

    for (int r = 0; r < trackTable->rowCount(); ++r) {
        QWidget* w = trackTable->cellWidget(r, 2);
        if (w) {
            auto buttons = w->findChildren<QPushButton*>();
            if (buttons.size() >= 3) {
                bool hasTrack = (r < (int)cachedTracks.size() && cachedTracks[r].has_track);

                if (isPlaying) {
                    buttons[0]->setEnabled(false); // Download
                    buttons[1]->setEnabled(false); // Upload
                    buttons[2]->setEnabled(false); // Delete
                } else if (isWorking) {
                    buttons[0]->setEnabled(false);
                    buttons[1]->setEnabled(false);
                    buttons[2]->setEnabled(false);
                } else {
                    buttons[0]->setEnabled(hasTrack);
                    buttons[1]->setEnabled(enabled);
                    buttons[2]->setEnabled(hasTrack);
                }
            }
        }
    }
}

QIcon MainWindow::styledIcon(QStyle::StandardPixmap sp) {
    QIcon base = style()->standardIcon(sp);
    QPixmap px = base.pixmap(16, 16);
    QPixmap black = px;
    QPainter p(&black);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(black.rect(), Qt::black);
    p.end();
    QIcon icon;
    icon.addPixmap(black, QIcon::Normal);
    icon.addPixmap(px, QIcon::Disabled);
    return icon;
}
