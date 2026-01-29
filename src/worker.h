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

    Worker(USBDevice* dev, Op op, int slot = -1, std::string filename = "",
           double trackDuration = 0.0, std::atomic<int>* volumePtr = nullptr, double startOffset = 0.0);

    ~Worker() { stop(); wait(); }

    void stop();
    Op getOperation() const;

signals:
    void finished();
    void error(QString msg);
    void tracksLoaded(std::vector<TrackInfo> tracks);
    void progress(int current, int total);

protected:
    void run() override;

private:
    USBDevice* device;
    Op operation;
    int slot;
    std::string filename;
    double trackDuration;
    std::atomic<int>* volume;
    double startOffset;
    std::atomic<bool> stopFlag;
};

#endif // WORKER_H
