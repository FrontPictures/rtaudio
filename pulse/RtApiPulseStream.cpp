#include "RtApiPulseStream.h"

RtApiPulseStream::RtApiPulseStream(RtApi::RtApiStream stream, pa_simple *handle)
    : mThread(std::bind(&RtApiPulseStream::processAudio, this),
              stream.callbackInfo.doRealtime,
              stream.callbackInfo.priority)
    , RtApiStreamClass(stream)
    , mHandle(handle)
{}

RtApiPulseStream::~RtApiPulseStream()
{
    stopStreamPriv();
    if (mHandle)
        pa_simple_free(mHandle);
}

RtAudioErrorType RtApiPulseStream::startStream()
{
    if (stream_.state != RtApi::STREAM_STOPPED) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    if (mThread.isValid() == false) {
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

RtAudioErrorType RtApiPulseStream::stopStreamPriv()
{
    if (stream_.state != RtApi::STREAM_RUNNING) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    if (mThread.isValid() == false) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    mThread.suspend();
    stream_.state = RtApi::STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}

bool RtApiPulseStream::processOutput()
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
                             stream_.bufferSize,
                             RtApi::OUTPUT);
        bytes = stream_.nDeviceChannels[RtApi::OUTPUT] * stream_.bufferSize
                * RtApi::formatBytes(stream_.deviceFormat[RtApi::OUTPUT]);
    } else
        bytes = stream_.nUserChannels[RtApi::OUTPUT] * stream_.bufferSize
                * RtApi::formatBytes(stream_.userFormat);

    auto res = pa_simple_write(mHandle, pulse_out, bytes, &pa_error);
    if (res < 0) {
        stream_.state = RtApi::STREAM_ERROR;
        return false;
    }
    return true;
}

bool RtApiPulseStream::processInput()
{
    int pa_error = 0;
    size_t bytes = 0;
    void *pulse_in = stream_.doConvertBuffer[RtApi::INPUT] ? stream_.deviceBuffer.get()
                                                           : stream_.userBuffer[RtApi::INPUT].get();

    if (stream_.doConvertBuffer[RtApi::INPUT])
        bytes = stream_.nDeviceChannels[RtApi::INPUT] * stream_.bufferSize
                * RtApi::formatBytes(stream_.deviceFormat[RtApi::INPUT]);
    else
        bytes = stream_.nUserChannels[RtApi::INPUT] * stream_.bufferSize
                * RtApi::formatBytes(stream_.userFormat);

    if (pa_simple_read(mHandle, pulse_in, bytes, &pa_error) < 0) {
        stream_.state = RtApi::STREAM_ERROR;
        return false;
    }
    if (stream_.doConvertBuffer[RtApi::INPUT]) {
        RtApi::convertBuffer(stream_,
                             stream_.userBuffer[RtApi::INPUT].get(),
                             stream_.deviceBuffer.get(),
                             stream_.convertInfo[RtApi::INPUT],
                             stream_.bufferSize,
                             RtApi::INPUT);
    }
    return true;
}

bool RtApiPulseStream::processAudio()
{
    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;

    if (stream_.mode == RtApi::INPUT) {
        if (!processInput())
            return false;
    }

    callback(stream_.userBuffer[RtApi::OUTPUT].get(),
             stream_.userBuffer[RtApi::INPUT].get(),
             stream_.bufferSize,
             streamTime,
             status,
             stream_.callbackInfo.userData);

    if (stream_.mode == RtApi::OUTPUT) {
        if (!processOutput())
            return false;
    }
    tickStreamTime();
    return true;
}
