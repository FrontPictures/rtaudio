#pragma once
#include "RtAudio.h"
#include "ThreadSuspendable.h"
#include "pulse/pulseaudio.h"
#include <pulse/simple.h>

class RtApiPulseStream : public RtApiStreamClass
{
public:
    RtApiPulseStream(RtApi::RtApiStream stream, pa_simple *handle);
    ~RtApiPulseStream();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;

private:
    RtAudioErrorType stopStreamPriv(void);
    bool processOutput();
    bool processInput();
    bool processAudio();
    ThreadSuspendable mThread;
    pa_simple *mHandle = nullptr;
};
