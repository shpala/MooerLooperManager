#ifndef WORKER_H
#define WORKER_H

#include <QThread>
#include <QString>
#include <string>
#include <vector>
#include <atomic>
#include "usb_device.h"

class Worker : public QThread {
    Q_OBJECT
public:
    enum Op { List, Download, Upload, Delete, Play };
    
    Worker(USBDevice* dev, Op op, int slot = -1, std::string filename = "");

    void stop();
    Op getOperation() const;
    void run() override;

signals:
    void progress(int current, int total);
    void finished();
    void error(QString msg);
    void tracksLoaded(std::vector<TrackInfo> tracks);

private:
    USBDevice* device;
    Op operation;
    int slot;
    std::string filename;
    std::atomic<bool> stopFlag;
};

#endif // WORKER_H
