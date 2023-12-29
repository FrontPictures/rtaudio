#pragma once
#include "RtAudio.h"
#include "ThreadSuspendable.h"
#include <pulse/simple.h>

class PaContextWithMainloop;
class PaStream;

class RtApiPulseStream : public RtApiStreamClass
{
public:
    RtApiPulseStream(RtApi::RtApiStream apiStream,
                     std::shared_ptr<PaContextWithMainloop> contextMainloop,
                     std::shared_ptr<PaStream> stream);
    ~RtApiPulseStream();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;

private:
    bool threadMethod();

    RtAudioErrorType stopStreamPriv(void);
    bool processOutput(size_t nbytes);
    const void *processInput(size_t *nSamplesOut, size_t *nbytes);
    bool processAudio(size_t nbytes);
    std::shared_ptr<PaContextWithMainloop> mContextMainloop;
    std::shared_ptr<PaStream> mStream;
    ThreadSuspendable mThread;
};
