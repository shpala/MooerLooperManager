#include "worker.h"
#include "audio_utils.h"
#include <portaudio.h>

Worker::Worker(USBDevice* dev, Op op, int slot, std::string filename,
               double trackDuration, std::atomic<int>* volumePtr, double startOffset)
    : device(dev), operation(op), slot(slot), filename(filename),
      trackDuration(trackDuration), volume(volumePtr), startOffset(startOffset), stopFlag(false)
{
}

void Worker::stop() { 
    stopFlag = true; 
}

Worker::Op Worker::getOperation() const { 
    return operation; 
}

void Worker::run() {
    try {
        if (operation == List) {
            auto tracks = device->listTracks();
            emit tracksLoaded(tracks);
        } else if (operation == Download) {
            auto callback = [](size_t c, size_t t, void* u) {
                static_cast<Worker*>(u)->emit progress(c, t);
            };
            auto data = device->downloadTrack(slot, callback, this);
            AudioUtils::saveWavFile(filename, data);
        } else if (operation == Upload) {
            auto audio = AudioUtils::loadWavFile(filename);
            auto callback = [](size_t c, size_t t, void* u) {
                static_cast<Worker*>(u)->emit progress(c, t);
            };
            device->uploadTrack(slot, audio, callback, this);
        } else if (operation == Delete) {
            device->deleteTrack(slot);
        } else if (operation == Play) {
             PaStream *stream;
             PaError err = Pa_OpenDefaultStream( &stream,
                                         0,          /* no input channels */
                                         2,          /* stereo output */
                                         paInt32,    /* 32 bit output */
                                         44100,
                                         256,
                                         NULL,
                                         NULL );

             if (err != paNoError) {
                 emit error(QString("PortAudio OpenStream error: %1").arg(Pa_GetErrorText(err)));
                 return;
             }

             err = Pa_StartStream( stream );
             if (err != paNoError) {
                 Pa_CloseStream( stream );
                 emit error(QString("PortAudio StartStream error: %1").arg(Pa_GetErrorText(err)));
                 return;
             }

             auto callback = [&](const std::vector<int32_t>& samples) {
                 if (stopFlag) return;
                 if (samples.empty()) return;
                 int vol = volume ? volume->load() : 100;
                 if (vol == 100) {
                     Pa_WriteStream( stream, samples.data(), samples.size() / 2 );
                 } else {
                     std::vector<int32_t> scaled(samples.size());
                     double scale = vol / 100.0;
                     for (size_t i = 0; i < samples.size(); i++) {
                         double s = static_cast<double>(samples[i]) * scale;
                         if (s > INT32_MAX) s = INT32_MAX;
                         if (s < INT32_MIN) s = INT32_MIN;
                         scaled[i] = static_cast<int32_t>(s);
                     }
                     Pa_WriteStream( stream, scaled.data(), scaled.size() / 2 );
                 }
             };

             auto progressCb = [](size_t c, size_t t, void* u) {
                 static_cast<Worker*>(u)->emit progress(static_cast<int>(c), static_cast<int>(t));
             };

             // Calculate start chunk
             // trackDuration is in seconds
             // Total bytes = trackDuration * 44100 * 6
             // Total chunks = Total bytes / 1024
             // startChunk = (startOffset / trackDuration) * Total chunks
             int startChunk = 1;
             if (trackDuration > 0 && startOffset > 0) {
                 // More precise:
                 // bytesOffset = startOffset * 44100 * 6
                 // chunkOffset = bytesOffset / 1024
                 double bytesOffset = startOffset * 44100.0 * 6.0;
                 startChunk = static_cast<int>(bytesOffset / 1024.0) + 1;
             }

             device->startStreaming(slot, callback, stopFlag, progressCb, this, startChunk);

             Pa_StopStream( stream );
             Pa_CloseStream( stream );
        }
        emit finished();
    } catch (const std::exception& e) {
        emit error(QString(e.what()));
    }
}
