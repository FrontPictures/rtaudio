#pragma once
#include "RtAudio.h"
#include "pulse/pulseaudio.h"
struct pa_simple;

class RtApiPulseStreamFactory : public RtApiStreamClassFactory
{
public:
    RtApiPulseStreamFactory() = default;
    ~RtApiPulseStreamFactory() = default;
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;

private:
    pa_simple *createPASimpleHandle(RtApi::StreamMode mode,
                                    unsigned int bufferBytes,
                                    unsigned int bufferNumbers,
                                    const char *streamName,
                                    const char *dev_name,
                                    pa_channel_map mapping,
                                    pa_sample_spec ss);
};
