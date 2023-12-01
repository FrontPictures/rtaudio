#include "RtApiPulseStream.h"

RtApiPulseStream::RtApiPulseStream(RtApi::RtApiStream stream, pa_simple *handle)
    : RtApiStreamClass(stream)
    , mHandle(handle)
{}

RtApiPulseStream::~RtApiPulseStream()
{
    stopStream();
    if (mHandle)
        pa_simple_free(mHandle);
}

RtAudioErrorType RtApiPulseStream::startStream() {}

RtAudioErrorType RtApiPulseStream::stopStream() {}
