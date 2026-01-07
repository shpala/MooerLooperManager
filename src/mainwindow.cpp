#include "mainwindow.h"
#include "audio_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QColor>
#include <QIcon>
#include <iostream>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), worker(nullptr), currentPlayingSlot(-1) {
    setupUi();
}

MainWindow::~MainWindow() {
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

void MainWindow::onConnectClicked() {
    if (device.isConnected()) {
        device.disconnect();
        connectBtn->setText("Connect");
        statusLabel->setText("Not Connected");
        statusLabel->setStyleSheet("color: red; font-weight: bold;");
        refreshBtn->setEnabled(false);
        trackTable->setRowCount(0);
        playButtons.clear();
        currentPlayingSlot = -1;
    } else {
        if (device.connect()) {
            connectBtn->setText("Disconnect");
            statusLabel->setText("Connected");
            statusLabel->setStyleSheet("color: green; font-weight: bold;");
            refreshBtn->setEnabled(true);
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
