#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <QMap>
#include <atomic>
#include "usb_device.h"
#include "worker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
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
    
    QTableWidget* trackTable;
    QPushButton* connectBtn;
    QPushButton* refreshBtn;
    QLabel* statusLabel;
    QProgressBar* progressBar;
    
    int currentPlayingSlot;
    QMap<int, QPushButton*> playButtons;

    void setupUi();
    void updateTable();
    void updatePlayButtonState();
    bool stopExistingWorker();
};

#endif // MAINWINDOW_H
