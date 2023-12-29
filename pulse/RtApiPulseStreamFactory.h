#pragma once
#include "RtAudio.h"
#include <pulse/channelmap.h>
#include <pulse/def.h>
struct pa_simple;
struct pa_mainloop;
struct pa_context;

class RtApiPulseStreamFactory : public RtApiStreamClassFactory
{
public:
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;
};
