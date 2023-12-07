#include "RtApiCoreStream.h"
#include "CoreCommon.h"

namespace {
static OSStatus callbackHandler(AudioDeviceID inDevice,
                                const AudioTimeStamp * /*inNow*/,
                                const AudioBufferList *inInputData,
                                const AudioTimeStamp * /*inInputTime*/,
                                AudioBufferList *outOutputData,
                                const AudioTimeStamp * /*inOutputTime*/,
                                void *infoPointer)
{
    RtApiCoreStream *object = reinterpret_cast<RtApiCoreStream *>(infoPointer);
    if (object == nullptr)
        return kAudioHardwareUnspecifiedError;
    if (object->callbackEvent(inDevice, inInputData, outOutputData) == false)
        return kAudioHardwareUnspecifiedError;
    else
        return kAudioHardwareNoError;
}
} // namespace

RtApiCoreStream::RtApiCoreStream(RtApi::RtApiStream stream, AudioDeviceID id)
    : RtApiStreamClass(std::move(stream))
    , mDeviceId(id)
{
    OSStatus result = 0;
#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    result = AudioDeviceCreateIOProcID(mDeviceId, callbackHandler, this, &mProcId);
#else
    // deprecated in favor of AudioDeviceCreateIOProcID()
    result = AudioDeviceAddIOProc(mDeviceId, callbackHandler, this);
#endif
    if (result != noErr) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error setting callback for device ("
                     << mDeviceId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return;
    }
}

RtApiCoreStream::~RtApiCoreStream()
{
    stopStreamPriv();
    if (mProcId == nullptr)
        return;

#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    AudioDeviceDestroyIOProcID(mDeviceId, mProcId);
#else // deprecated behaviour
    AudioDeviceRemoveIOProc(mDeviceId, callbackHandler);
#endif
}

RtAudioErrorType RtApiCoreStream::startStream()
{
    if (stream_.state != RtApi::STREAM_STOPPED) {
        if (stream_.state == RtApi::STREAM_RUNNING)
            return error(RTAUDIO_WARNING,
                         "RtApiCore::startStream(): the stream is already running!");
        else if (stream_.state == RtApi::STREAM_STOPPING || stream_.state == RtApi::STREAM_CLOSED)
            return error(RTAUDIO_WARNING,
                         "RtApiCore::startStream(): the stream is stopping or closed!");
        else if (stream_.state == RtApi::STREAM_ERROR)
            return error(RTAUDIO_WARNING, "RtApiCore::startStream(): the stream is in error state!");
        return error(RTAUDIO_WARNING, "RtApiCore::startStream(): the stream is not stopped");
    }

    OSStatus result = 0;

#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    result = AudioDeviceStart(mDeviceId, mProcId);
#else // deprecated behaviour
    result = AudioDeviceStart(mDeviceId, callbackHandler);
#endif
    if (result != noErr) {
        errorStream_ << "RtApiCore::startStream: system error (" << CoreCommon::getErrorCode(result)
                     << ") starting callback procedure on device (" << mDeviceId << ").";
        return error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
    }

    stream_.state = RtApi::STREAM_RUNNING;
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiCoreStream::stopStream()
{
    return stopStreamPriv();
}

bool RtApiCoreStream::callbackEvent(AudioDeviceID deviceId,
                                    const AudioBufferList *inBufferList,
                                    const AudioBufferList *outBufferList)
{
    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;
    unsigned int iStream = 0;

    if (stream_.mode == RtApi::INPUT || stream_.mode == RtApi::DUPLEX) {
        if (stream_.doConvertBuffer[RtApi::INPUT]) { // convert directly from CoreAudio stream buffer
            RtApi::convertBuffer(stream_,
                                 stream_.userBuffer[RtApi::INPUT].get(),
                                 (char *) inBufferList->mBuffers[iStream].mData,
                                 stream_.convertInfo[RtApi::INPUT],
                                 stream_.bufferSize,
                                 RtApi::INPUT);
        } else { // copy to user buffer
            memcpy(stream_.userBuffer[RtApi::INPUT].get(),
                   inBufferList->mBuffers[iStream].mData,
                   inBufferList->mBuffers[iStream].mDataByteSize);
        }
    }

    callback(stream_.userBuffer[RtApi::OUTPUT].get(),
             stream_.userBuffer[RtApi::INPUT].get(),
             stream_.bufferSize,
             streamTime,
             status,
             stream_.callbackInfo.userData);

    if (stream_.mode == RtApi::OUTPUT || stream_.mode == RtApi::DUPLEX) {
        if (stream_.doConvertBuffer[RtApi::OUTPUT]) { // convert directly to CoreAudio stream buffer
            //is it ok to make conversion directly to DMA buffer?
            RtApi::convertBuffer(stream_,
                                 reinterpret_cast<char *>(outBufferList->mBuffers[iStream].mData),
                                 stream_.userBuffer[RtApi::OUTPUT].get(),
                                 stream_.convertInfo[RtApi::OUTPUT],
                                 stream_.bufferSize,
                                 RtApi::OUTPUT);
        } else { // copy from user buffer
            memcpy(outBufferList->mBuffers[iStream].mData,
                   stream_.userBuffer[RtApi::OUTPUT].get(),
                   outBufferList->mBuffers[iStream].mDataByteSize);
        }
    }
    return true;
}

RtAudioErrorType RtApiCoreStream::stopStreamPriv()
{
    if (stream_.state != RtApi::STREAM_RUNNING && stream_.state != RtApi::STREAM_ERROR) {
        if (stream_.state == RtApi::STREAM_STOPPED)
            return error(RTAUDIO_WARNING, "RtApiCore::stopStream(): the stream is already stopped!");
        else if (stream_.state == RtApi::STREAM_CLOSED)
            return error(RTAUDIO_WARNING, "RtApiCore::stopStream(): the stream is closed!");
        return error(RTAUDIO_WARNING, "RtApiCore::stopStream(): the stream is not running");
    }

    OSStatus result = noErr;

#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    result = AudioDeviceStop(mDeviceId, mProcId);
#else // deprecated behaviour
    result = AudioDeviceStop(mDeviceId, callbackHandler);
#endif
    if (result != noErr) {
        errorStream_ << "RtApiCore::stopStream: system error (" << CoreCommon::getErrorCode(result)
                     << ") stopping callback procedure on device (" << stream_.deviceId[0] << ").";
        return error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
    }
    stream_.state = RtApi::STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}
