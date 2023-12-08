#include "RtApiPulseStreamFactory.h"
#include "PulseCommon.h"
#include "RtApiPulseStream.h"
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

namespace {
bool isSamplerateSupported(unsigned int sampleRate)
{
    for (const unsigned int sr : PULSE_SUPPORTED_SAMPLERATES) {
        if (sampleRate == sr) {
            return true;
        }
    }
    return false;
}

} // namespace
std::shared_ptr<RtApiStreamClass> RtApiPulseStreamFactory::createStream(CreateStreamParams params)
{
    if (params.mode == RtApi::DUPLEX) {
        /* Note: We could add DUPLEX by synchronizing multiple streams,
       but it would mean moving from Simple API to Asynchronous API:
       https://freedesktop.org/software/pulseaudio/doxygen/streams.html#sync_streams */
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: DUPLEX mode not supported by Pulse.");
        return {};
    }

    pa_sample_spec ss{};
    if (params.mode == RtApi::OUTPUT) {
        ss.channels = params.channelsOutput;
    } else {
        ss.channels = params.channelsInput;
    }
    if (ss.channels == 0) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiPulseStreamFactory::createStream: no channels.");
        return {};
    }
    if (isSamplerateSupported(params.sampleRate) == false) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: samplerate not supported.");
        return {};
    }
    ss.rate = params.sampleRate;
    ss.format = getPulseFormatByRt(params.format);
    if (ss.format == PA_SAMPLE_INVALID) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: sample format not supported.");
        return {};
    }

    int error_code = 0;
    pa_channel_map mapping{};
    if (pa_channel_map_init_extend(&mapping, ss.channels, PA_CHANNEL_MAP_WAVEEX) == NULL) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: channels map not initialized.");
        return {};
    }

    std::string streamName = "RtAudio";
    if (params.options && !params.options->streamName.empty())
        streamName = params.options->streamName;

    unsigned int bufferBytes = ss.channels * params.bufferSize * RtApi::formatBytes(params.format);
    unsigned int buffersCount = 4;

    if (params.options && params.options->numberOfBuffers > 0) {
        buffersCount = params.options->numberOfBuffers;
    }

    RtApi::RtApiStream stream_{};
    stream_.nDeviceChannels[params.mode] = ss.channels;
    stream_.deviceFormat[params.mode] = params.format;
    stream_.doByteSwap[params.mode] = false;
    stream_.deviceInterleaved[params.mode] = true;
    stream_.latency[params.mode] = 0;
    stream_.nBuffers = buffersCount;

    if (params.options && params.options->flags & RTAUDIO_SCHEDULE_REALTIME) {
        stream_.callbackInfo.priority = params.options->priority;
        stream_.callbackInfo.doRealtime = true;
    }

    if (setupStreamWithParams(stream_, params) == false) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: failed to setup stream.");
        return {};
    }
    if (setupStreamCommon(stream_) == false) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulseStreamFactory::createStream: failed to setup stream common.");
        return {};
    }

    const char *dev_name = params.busId.c_str();
    pa_simple *s_play_ptr = nullptr;

    s_play_ptr = createPASimpleHandle(params.mode,
                                      bufferBytes,
                                      buffersCount,
                                      streamName.c_str(),
                                      dev_name,
                                      mapping,
                                      ss);
    if (!s_play_ptr) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulse::probeDeviceOpen: error connecting output to PulseAudio server.");
        return {};
    }

    return std::make_shared<RtApiPulseStream>(stream_, s_play_ptr);
}

pa_simple *RtApiPulseStreamFactory::createPASimpleHandle(RtApi::StreamMode mode,
                                                         unsigned int bufferBytes,
                                                         unsigned int bufferNumbers,
                                                         const char *streamName,
                                                         const char *dev_name,
                                                         pa_channel_map mapping,
                                                         pa_sample_spec ss)
{
    int error_code = 0;
    pa_simple *s_play_ptr = nullptr;
    pa_buffer_attr buffer_attr{};
    switch (mode) {
    case RtApi::INPUT:
        buffer_attr.fragsize = bufferBytes;
        buffer_attr.maxlength = bufferBytes * bufferNumbers;

        s_play_ptr = pa_simple_new(NULL,
                                   streamName,
                                   PA_STREAM_RECORD,
                                   dev_name,
                                   "Record",
                                   &ss,
                                   &mapping,
                                   &buffer_attr,
                                   &error_code);
        break;
    case RtApi::OUTPUT: {
        // pa_buffer_attr::fragsize is recording-only.
        buffer_attr.maxlength = bufferBytes * bufferNumbers;
        buffer_attr.minreq = -1;
        buffer_attr.prebuf = -1;
        buffer_attr.tlength = -1;

        s_play_ptr = pa_simple_new(NULL,
                                   streamName,
                                   PA_STREAM_PLAYBACK,
                                   dev_name,
                                   "Playback",
                                   &ss,
                                   &mapping,
                                   &buffer_attr,
                                   &error_code);
        break;
    }
    default:
        return nullptr;
    }
    return s_play_ptr;
}
