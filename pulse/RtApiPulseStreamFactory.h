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

private:
    pa_simple *createPASimpleHandle(RtApi::StreamMode mode,
                                    unsigned int bufferBytes,
                                    unsigned int bufferNumbers,
                                    const char *streamName,
                                    const char *dev_name,
                                    pa_channel_map mapping,
                                    pa_sample_spec ss);

    pa_simple *createPAStream(RtApi::StreamMode mode,
                              unsigned int bufferBytes,
                              unsigned int bufferNumbers,
                              const char *streamName,
                              const char *dev_name,
                              pa_channel_map mapping,
                              pa_sample_spec ss,
                              pa_buffer_attr bufAttr);

    int createPAStreamHandles(pa_mainloop *ml,
                              pa_context *context,
                              const char *streamName,
                              const char *dev_name,
                              pa_channel_map mapping,
                              pa_sample_spec ss,
                              pa_buffer_attr bufAttr);
};
