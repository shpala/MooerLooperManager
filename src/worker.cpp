#include "worker.h"
#include "audio_utils.h"
#include <portaudio.h>

Worker::Worker(USBDevice* dev, Op op, int slot, std::string filename) 
    : device(dev), operation(op), slot(slot), filename(filename), stopFlag(false) 
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
             PaError err = Pa_Initialize();
             if (err != paNoError) {
                 emit error(QString("PortAudio error: %1").arg(Pa_GetErrorText(err)));
                 return;
             }

             PaStream *stream;
             err = Pa_OpenDefaultStream( &stream,
                                         0,          /* no input channels */
                                         2,          /* stereo output */
                                         paInt32,    /* 32 bit output */
                                         44100,
                                         256,
                                         NULL,
                                         NULL );

             if (err != paNoError) {
                 Pa_Terminate();
                 emit error(QString("PortAudio OpenStream error: %1").arg(Pa_GetErrorText(err)));
                 return;
             }

             err = Pa_StartStream( stream );
             if (err != paNoError) {
                 Pa_CloseStream( stream );
                 Pa_Terminate();
                 emit error(QString("PortAudio StartStream error: %1").arg(Pa_GetErrorText(err)));
                 return;
             }
             
             auto callback = [&](const std::vector<int32_t>& samples) {
                 if (stopFlag) return;
                 if (samples.empty()) return;
                 Pa_WriteStream( stream, samples.data(), samples.size() / 2 );
             };
             
             device->startStreaming(slot, callback, stopFlag);
             
             Pa_StopStream( stream );
             Pa_CloseStream( stream );
             Pa_Terminate();
        }
        emit finished();
    } catch (const std::exception& e) {
        emit error(QString(e.what()));
    }
}
