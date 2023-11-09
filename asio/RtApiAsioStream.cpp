#include "AsioCommon.h"
#include "RtApiAsioStream.h"
#include "utils.h"
#include "windowscommon.h"

RtApiAsioStream* apiAsioStream = nullptr;

void asioBufferSwitch(long index, ASIOBool /*processNow*/)
{
    if (!apiAsioStream)
        return;
    apiAsioStream->callbackEvent(index);
    ASIOOutputReady();
}

void asioSampleRateChangedGlobal(ASIOSampleRate sRate)
{
    if (!apiAsioStream)
        return;
    apiAsioStream->sampleRateChanged(sRate);
}

long asioMessagesGlobal(long selector, long value, void* message, double* opt)
{
    long ret = 0;
    switch (selector) {
    case kAsioSelectorSupported:
        if (value == kAsioResetRequest
            || value == kAsioEngineVersion
            || value == kAsioResyncRequest
            || value == kAsioLatenciesChanged
            // The following three were added for ASIO 2.0, you don't
            // necessarily have to support them.
            || value == kAsioSupportsTimeInfo
            || value == kAsioSupportsTimeCode
            || value == kAsioSupportsInputMonitor)
            ret = 1L;
        break;
    case kAsioLatenciesChanged:
        // This will inform the host application that the drivers were
        // latencies changed.  Beware, it this does not mean that the
        // buffer sizes have changed!  You might need to update internal
        // delay data.
        // std::cerr << "\nRtApiAsio: driver latency may have changed!!!" << std::endl;
        ret = 1L;
        break;
    case kAsioEngineVersion:
        // Return the supported ASIO version of the host application.  If
        // a host application does not implement this selector, ASIO 1.0
        // is assumed by the driver.
        ret = 2L;
        break;
    case kAsioSupportsTimeInfo:
        // Informs the driver whether the
        // asioCallbacks.bufferSwitchTimeInfo() callback is supported.
        // For compatibility with ASIO 1.0 drivers the host application
        // should always support the "old" bufferSwitch method, too.
        ret = 0;
        break;
    case kAsioSupportsTimeCode:
        // Informs the driver whether application is interested in time
        // code info.  If an application does not need to know about time
        // code, the driver has less work to do.
        ret = 0;
        break;
    default:
        if (!apiAsioStream)
            return 0;
        return apiAsioStream->asioMessages(selector, value, message, opt);
    }
    return ret;
}

RtApiAsioStream::RtApiAsioStream(RtApi::RtApiStream stream, std::vector<ASIOBufferInfo> infos) :
    RtApiStreamClass(std::move(stream)),
    mBufferInfos(std::move(infos)),
    mWatchEvent(MAKE_UNIQUE_EVENT_EMPTY)
{

}

RtApiAsioStream::~RtApiAsioStream()
{
    stopStream();
    ASIODisposeBuffers();
    ASIOExit();
    apiAsioStream = nullptr;
    drivers.removeCurrentDriver();
}

RtAudioErrorType RtApiAsioStream::startStream(void)
{
    MutexRaii<StreamMutex> lock(stream_.mutex);
    if (stream_.state != RtApi::STREAM_STOPPED) {
        if (stream_.state == RtApi::STREAM_RUNNING)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is already running!");
        else if (stream_.state == RtApi::STREAM_STOPPING || stream_.state == RtApi::STREAM_CLOSED)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is stopping or closed!");
        else if (stream_.state == RtApi::STREAM_ERROR)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is in error state!");
        return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is not stopped!");
    }
    UNIQUE_EVENT evt = MAKE_UNIQUE_EVENT_EMPTY;
    evt = MAKE_UNIQUE_EVENT_VALUE(CreateEvent(NULL, TRUE, FALSE, NULL));
    if (!evt) {
        return error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to create event.");
    }

    ASIOError result = ASIOStart();
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::startStream: error (" << getAsioErrorString(result) << ") starting device.";
        return error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
    }

    mWatchEvent = std::move(evt);
    mDeviceWatcherThread = std::thread(&RtApiAsioStream::deviceWatcherThread, this);
    stream_.state = RtApi::STREAM_RUNNING;
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiAsioStream::stopStream(void)
{
    if (mDeviceWatcherThread.joinable()) {
        SetEvent(mWatchEvent.get());
        mDeviceWatcherThread.join();
        mWatchEvent.reset();
    }

    MutexRaii<StreamMutex> lock(stream_.mutex);
    if (stream_.state != RtApi::STREAM_RUNNING && stream_.state != RtApi::STREAM_ERROR) {
        if (stream_.state == RtApi::STREAM_STOPPED)
            return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): the stream is already stopped!");
        else if (stream_.state == RtApi::STREAM_CLOSED)
            return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): the stream is closed!");
        return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): stream is not running!");
    }

    ASIOError result = ASIOStop();
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::stopStream: error (" << getAsioErrorString(result) << ") stopping device.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
    }
    stream_.state = RtApi::STREAM_STOPPED;
    if (result == ASE_OK) return RTAUDIO_NO_ERROR;
    return error(RTAUDIO_SYSTEM_ERROR);
}

bool RtApiAsioStream::callbackEvent(long bufferIndex)
{
    mNoAudioCallbacks = 0;
    RtAudioCallback callback = (RtAudioCallback)stream_.callbackInfo.callback;
    RtAudioStreamStatus status = 0;
    if (stream_.mode != RtApi::INPUT && asioXRun == true) {
        status |= RTAUDIO_OUTPUT_UNDERFLOW;
        asioXRun = false;
    }
    if (stream_.mode != RtApi::OUTPUT && asioXRun == true) {
        status |= RTAUDIO_INPUT_OVERFLOW;
        asioXRun = false;
    }
    if (stream_.mode == RtApi::INPUT || stream_.mode == RtApi::DUPLEX) {
        unsigned int bufferBytes = stream_.bufferSize * RtApi::formatBytes(stream_.deviceFormat[1]);
        if (stream_.doConvertBuffer[1]) {
            // Always interleave ASIO input data.
            int j = 0;
            for (auto& b : mBufferInfos) {
                if (b.isInput != ASIOTrue)
                    continue;
                memcpy(stream_.deviceBuffer.get() + (j++ * bufferBytes),
                    b.buffers[bufferIndex],
                    bufferBytes);
            }
            if (stream_.doByteSwap[1]) {
                RtApi::byteSwapBuffer(stream_.deviceBuffer.get(),
                    stream_.bufferSize * stream_.nDeviceChannels[1],
                    stream_.deviceFormat[1]);
            }
            RtApi::convertBuffer(stream_, stream_.userBuffer[1], stream_.deviceBuffer.get(), stream_.convertInfo[1], stream_.bufferSize, RtApi::StreamMode::INPUT);
        }
        else {
            int j = 0;
            for (auto& b : mBufferInfos) {
                if (b.isInput != ASIOTrue)
                    continue;
                memcpy(stream_.userBuffer[1] + (j++ * bufferBytes),
                    b.buffers[bufferIndex],
                    bufferBytes);
            }

            if (stream_.doByteSwap[1]) {
                RtApi::byteSwapBuffer(stream_.userBuffer[1],
                    stream_.bufferSize * stream_.nUserChannels[1],
                    stream_.userFormat);
            }
        }
    }

    double streamTime = getStreamTime();
    callback(stream_.userBuffer[0], stream_.userBuffer[1],
        stream_.bufferSize, streamTime, status, stream_.callbackInfo.userData);

    if (stream_.mode == RtApi::OUTPUT || stream_.mode == RtApi::DUPLEX) {
        unsigned int bufferBytes = stream_.bufferSize * RtApi::formatBytes(stream_.deviceFormat[0]);
        if (stream_.doConvertBuffer[0]) {

            RtApi::convertBuffer(stream_, stream_.deviceBuffer.get(), stream_.userBuffer[0], stream_.convertInfo[0], stream_.bufferSize, RtApi::StreamMode::OUTPUT);
            if (stream_.doByteSwap[0])
                RtApi::byteSwapBuffer(stream_.deviceBuffer.get(),
                    stream_.bufferSize * stream_.nDeviceChannels[0],
                    stream_.deviceFormat[0]);

            int j = 0;
            for (auto& b : mBufferInfos) {
                if (b.isInput == ASIOTrue)
                    continue;
                memcpy(b.buffers[bufferIndex],
                    &stream_.deviceBuffer.get()[j++ * bufferBytes], bufferBytes);
            }
        }
        else {

            if (stream_.doByteSwap[0])
                RtApi::byteSwapBuffer(stream_.userBuffer[0],
                    stream_.bufferSize * stream_.nUserChannels[0],
                    stream_.userFormat);

            int j = 0;
            for (auto& b : mBufferInfos) {
                if (b.isInput == ASIOTrue)
                    continue;
                memcpy(b.buffers[bufferIndex],
                    &stream_.userBuffer[0][bufferBytes * j++], bufferBytes);
            }

        }
    }
    tickStreamTime();
    return true;
}

long RtApiAsioStream::asioMessages(long selector, long value, void* message, double* opt)
{
    long ret = 0;
    switch (selector) {
    case kAsioResetRequest:
        // This message is received when a device is disconnected (and
        // perhaps when the sample rate changes). It indicates that the
        // driver should be reset, which is accomplished by calling
        // ASIOStop(), ASIODisposeBuffers() and removing the driver. But
        // since this message comes from the driver, we need to let this
        // function return before attempting to close the stream and
        // remove the driver. Thus, we invoke a thread to initiate the
        // stream closing.        
        // std::cerr << "\nRtApiAsio: driver reset requested!!!" << std::endl;
        //asioCallbackInfo->deviceDisconnected = true; // flag for either rate change or disconnect
    {
        MutexRaii<StreamMutex> lock(stream_.mutex);
        stream_.state = RtApi::STREAM_ERROR;
    }
    ret = 1L;
    break;
    case kAsioResyncRequest:
        // This informs the application that the driver encountered some
        // non-fatal data loss.  It is used for synchronization purposes
        // of different media.  Added mainly to work around the Win16Mutex
        // problems in Windows 95/98 with the Windows Multimedia system,
        // which could lose data because the Mutex was held too long by
        // another thread.  However a driver can issue it in other
        // situations, too.
        asioXRun = true;
        ret = 1L;
        break;
    }
    return ret;

}

void RtApiAsioStream::sampleRateChanged(ASIOSampleRate sRate)
{
    // The ASIO documentation says that this usually only happens during
    // external sync.  Audio processing is not stopped by the driver,
    // actual sample rate might not have even changed, maybe only the
    // sample rate status of an AES/EBU or S/PDIF digital input at the
    // audio device.
}

void RtApiAsioStream::deviceWatcherThread()
{
    while (true) {
        auto res = WaitForSingleObject(mWatchEvent.get(), 100);
        if (res != WAIT_TIMEOUT)
            return;
        mNoAudioCallbacks++;
        if (mNoAudioCallbacks >= 10) {
            MutexRaii<StreamMutex> lock(stream_.mutex);
            stream_.state = RtApi::StreamState::STREAM_ERROR;
            return;
        }
    }
}
