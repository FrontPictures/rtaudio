#include "RtApiPulseStreamFactory.h"
#include "PaContextWithMainloop.h"
#include "PaMainloop.h"
#include "PaStream.h"
#include "PulseCommon.h"
#include "RtApiPulseStream.h"
#include <memory>
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

struct UserDataContext
{
public:
    void setState(pa_context_state_t _state) { mState = _state; }
    pa_context_state_t getState() const { return mState; }
    bool isError() const { return mState != PA_CONTEXT_UNCONNECTED && !PA_CONTEXT_IS_GOOD(mState); }
    bool isReady() const { return mState == PA_CONTEXT_READY; }
    bool isReadyOrError() const { return isError() || isReady(); }

private:
    pa_context_state_t mState = PA_CONTEXT_UNCONNECTED;
};

struct UserDataStream
{
public:
    void setState(pa_stream_state_t _state) { mState = _state; }
    pa_stream_state_t getState() const { return mState; }
    bool isError() const { return mState != PA_STREAM_UNCONNECTED && !PA_STREAM_IS_GOOD(mState); }
    bool isReady() const { return mState == PA_STREAM_READY; }

private:
    pa_stream_state_t mState = PA_STREAM_UNCONNECTED;
};

// This is the initial function that is called when the callback is
// set. This one then calls the functions above.
void rt_pa_context_state_cb(pa_context *context, void *userdata)
{
    UserDataContext *paProbeInfo = reinterpret_cast<UserDataContext *>(userdata);
    auto state = pa_context_get_state(context);
    paProbeInfo->setState(state);
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

    pa_buffer_attr buffer_attr{};
    buffer_attr.minreq = -1;
    buffer_attr.prebuf = -1;
    buffer_attr.tlength = -1;

    if (params.mode == RtApi::INPUT) {
        buffer_attr.fragsize = bufferBytes;
        buffer_attr.maxlength = bufferBytes * buffersCount;
    } else if (params.mode == RtApi::OUTPUT) {
        buffer_attr.maxlength = bufferBytes * buffersCount;
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

    auto contextWithLoop = PaContextWithMainloop::Create(nullptr);
    if (!contextWithLoop) {
        errorStream_ << "RtApiPulse::probeDevices: failed to create context with mainloop.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    const char *dev_name = params.busId.c_str();
    std::shared_ptr<PaStream> stream = std::make_shared<PaStream>(contextWithLoop->getContext(),
                                                                  "RtAudio",
                                                                  ss,
                                                                  mapping);
    if (stream->isValid() == false) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulse::probeDeviceOpen: error connecting output to PulseAudio server.");
        return {};
    }
    if (stream->connect(dev_name, buffer_attr, params.mode == RtApi::INPUT) == false) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiPulse::probeDeviceOpen: error connecting output to PulseAudio server.");
        return {};
    }
    return std::make_shared<RtApiPulseStream>(stream_,
                                              std::move(contextWithLoop),
                                              std::move(stream));
}
