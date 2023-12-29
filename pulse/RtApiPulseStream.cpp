#include "RtApiPulseStream.h"
#include "pulse/PaContext.h"
#include "pulse/PaContextWithMainloop.h"
#include "pulse/PaMainloop.h"
#include "pulse/PaStream.h"
#include <cassert>

RtApiPulseStream::RtApiPulseStream(RtApi::RtApiStream apiStream,
                                   std::shared_ptr<PaContextWithMainloop> contextMainloop,
                                   std::shared_ptr<PaStream> stream)
    : RtApiStreamClass(apiStream)
    , mContextMainloop(contextMainloop)
    , mStream(stream)
    , mThread([this]() { return threadMethod(); })
{
    mStream->setStreamRequest([this](size_t nbytes) { processAudio(nbytes); });
}

RtApiPulseStream::~RtApiPulseStream()
{
    stopStreamPriv();
}

RtAudioErrorType RtApiPulseStream::startStream()
{
    if (stream_.state != RtApi::STREAM_STOPPED) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    if (mStream->play() == false) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    mThread.resume();
    stream_.state = RtApi::STREAM_RUNNING;
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiPulseStream::stopStream()
{
    return stopStreamPriv();
}

bool RtApiPulseStream::threadMethod()
{
    auto loop = mContextMainloop->getContext()->getMainloop();
    if (loop->iterateBlocking() == false)
        return false;

    bool errorInAudiothread = false;
    errorInAudiothread = mContextMainloop->getContext()->hasError();
    errorInAudiothread = errorInAudiothread || mStream->hasError();
    if (errorInAudiothread) {
        stream_.errorState = true;
        return false;
    }
    return true;
}

RtAudioErrorType RtApiPulseStream::stopStreamPriv()
{
    if (stream_.state != RtApi::STREAM_RUNNING) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    mThread.suspend();
    if (mStream->pause() == false) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    stream_.state = RtApi::STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}

bool RtApiPulseStream::processOutput(size_t nsamples)
{
    int pa_error = 0;
    size_t bytes = 0;
    void *pulse_out = stream_.doConvertBuffer[RtApi::OUTPUT]
                          ? stream_.deviceBuffer.get()
                          : stream_.userBuffer[RtApi::OUTPUT].get();

    if (stream_.doConvertBuffer[RtApi::OUTPUT]) {
        RtApi::convertBuffer(stream_,
                             stream_.deviceBuffer.get(),
                             stream_.userBuffer[RtApi::OUTPUT].get(),
                             stream_.convertInfo[RtApi::OUTPUT],
                             nsamples,
                             RtApi::OUTPUT);
        bytes = stream_.nDeviceChannels[RtApi::OUTPUT] * nsamples
                * RtApi::formatBytes(stream_.deviceFormat[RtApi::OUTPUT]);
    } else
        bytes = stream_.nUserChannels[RtApi::OUTPUT] * nsamples
                * RtApi::formatBytes(stream_.userFormat);

    if (mStream->writeData(pulse_out, bytes) == false) {
        stream_.errorState = true;
        return false;
    }
    return true;
}

const void *RtApiPulseStream::processInput(size_t *nSamplesOut, size_t *nbytes)
{
    const void *data = nullptr;
    size_t readDataSize = mStream->peakData(&data);
    if (readDataSize == 0) {
        return nullptr;
    }

    size_t bufferSize = 0;
    bufferSize = readDataSize / stream_.nDeviceChannels[RtApi::INPUT]
                 / RtApi::formatBytes(stream_.deviceFormat[RtApi::INPUT]);

    (*nSamplesOut) = bufferSize;
    (*nbytes) = readDataSize;
    if (stream_.doConvertBuffer[RtApi::INPUT]) {
        RtApi::convertBuffer(stream_,
                             stream_.userBuffer[RtApi::INPUT].get(),
                             reinterpret_cast<const char *>(data),
                             stream_.convertInfo[RtApi::INPUT],
                             stream_.bufferSize,
                             RtApi::INPUT);
        return stream_.userBuffer[RtApi::INPUT].get();
    } else {
        return data;
    }
}

bool RtApiPulseStream::processAudio(size_t nbytes)
{
    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;

    if (stream_.mode == RtApi::INPUT) {
        size_t bufferSize = 0;
        size_t nbytesInput = 0;
        const void *dataIn = processInput(&bufferSize, &nbytesInput);
        assert(nbytesInput == nbytes);
        if (nbytesInput != nbytes)
            return false;

        if (!dataIn || bufferSize == 0)
            return false;
        size_t samplesProcessed = 0;
        while (samplesProcessed != bufferSize) {
            size_t samplesToProcess = std::min(bufferSize - samplesProcessed,
                                               (size_t) stream_.bufferSize);
            callback(nullptr,
                     reinterpret_cast<const char *>(dataIn) + samplesProcessed,
                     samplesToProcess,
                     streamTime,
                     status,
                     stream_.callbackInfo.userData);
            tickStreamTime();
            samplesProcessed += samplesToProcess;
        }
        mStream->dropData();
    } else {
        size_t bufferSize = 0;
        bufferSize = nbytes / stream_.nDeviceChannels[RtApi::OUTPUT]
                     / RtApi::formatBytes(stream_.deviceFormat[RtApi::OUTPUT]);

        size_t samplesProcessed = 0;
        while (samplesProcessed != bufferSize) {
            size_t samplesToProcess = std::min(bufferSize - samplesProcessed,
                                               (size_t) stream_.bufferSize);
            callback(stream_.userBuffer[RtApi::OUTPUT].get(),
                     nullptr,
                     samplesToProcess,
                     streamTime,
                     status,
                     stream_.callbackInfo.userData);
            if (!processOutput(samplesToProcess))
                return false;
            tickStreamTime();
            samplesProcessed += samplesToProcess;
        }
    }
    return true;
}
