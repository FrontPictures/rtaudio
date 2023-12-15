#include "RtApiAlsaStreamFactory.h"

#include "RtApiAlsaStream.h"
#include <alsa/asoundlib.h>
#include <climits>

namespace{
constexpr snd_pcm_format_t getAlsaFormat(RtAudioFormat format){
    switch (format) {
    case RTAUDIO_SINT8:
        return SND_PCM_FORMAT_S8;
    case RTAUDIO_SINT16:
        return SND_PCM_FORMAT_S16;
    case RTAUDIO_SINT24:
        return SND_PCM_FORMAT_S24_3LE;
    case RTAUDIO_SINT32:
        return SND_PCM_FORMAT_S32;
    case RTAUDIO_FLOAT32:
        return SND_PCM_FORMAT_FLOAT;
    case RTAUDIO_FLOAT64:
        return SND_PCM_FORMAT_FLOAT64;
    default:
        return SND_PCM_FORMAT_UNKNOWN;
    }
}

constexpr std::optional<RtAudioFormat> getRtFormat(snd_pcm_format_t format){
    switch (format) {
    case SND_PCM_FORMAT_S8:
        return RTAUDIO_SINT8;
    case SND_PCM_FORMAT_S16:
        return RTAUDIO_SINT16;
    case SND_PCM_FORMAT_S24_3LE:
        return RTAUDIO_SINT24;
    case SND_PCM_FORMAT_S32:
        return RTAUDIO_SINT32;
    case SND_PCM_FORMAT_FLOAT:
        return RTAUDIO_FLOAT32;
    case SND_PCM_FORMAT_FLOAT64:
        return RTAUDIO_FLOAT64;
    default:
        return {};
    }
}

int setSwParams(snd_pcm_t * phandle, unsigned int bufferSize, snd_output_t* out)
{
    // Set the software configuration to fill buffers with zeros and prevent device stopping on xruns.
    snd_pcm_sw_params_t *sw_params = NULL;
    snd_pcm_sw_params_alloca( &sw_params );
    snd_pcm_sw_params_current( phandle, sw_params );
    snd_pcm_sw_params_set_start_threshold( phandle, sw_params, bufferSize );
    snd_pcm_sw_params_set_stop_threshold( phandle, sw_params, ULONG_MAX );
    snd_pcm_sw_params_set_silence_threshold( phandle, sw_params, 0 );

    // The following two settings were suggested by Theo Veenker
    //snd_pcm_sw_params_set_avail_min( phandle, sw_params, *bufferSize );
    //snd_pcm_sw_params_set_xfer_align( phandle, sw_params, 1 );

    // here are two options for a fix
    //snd_pcm_sw_params_set_silence_size( phandle, sw_params, ULONG_MAX );
    snd_pcm_uframes_t val;
    snd_pcm_sw_params_get_boundary( sw_params, &val );
    snd_pcm_sw_params_set_silence_size( phandle, sw_params, val );

    int result = snd_pcm_sw_params( phandle, sw_params );
#if defined(__RTAUDIO_DEBUG__)
    fprintf(stderr, "\nRtApiAlsa: dump software params after installation:\n\n");
    snd_pcm_sw_params_dump( sw_params, out );
#endif
    return result;
}

bool setupBufferPeriod(RtAudio::StreamOptions* options, unsigned int& bufferSize,
                       snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle,unsigned int& buffer_period_out)
{
    int dir = 0;
    int result = 0;
    snd_pcm_uframes_t periodSize = bufferSize;
    result = snd_pcm_hw_params_set_period_size_near( phandle, hw_params, &periodSize, &dir );
    if ( result < 0 ) {
        return false;
    }

    unsigned int periods = 0;
    if ( options && options->flags & RTAUDIO_MINIMIZE_LATENCY ) periods = 2;
    if ( options && options->numberOfBuffers > 0 ) periods = options->numberOfBuffers;
    if ( periods < 2 ) periods = 4; // a fairly safe default value
    result = snd_pcm_hw_params_set_periods_near( phandle, hw_params, &periods, &dir );
    if ( result < 0 ) {
        return false;
    }

    bufferSize = periodSize;
    buffer_period_out = periods;
    return true;
}

unsigned int getDeviceChannels(unsigned int channels, snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle)
{
    int result = 0;
    unsigned int value = 0;
    result = snd_pcm_hw_params_get_channels_max( hw_params, &value );

    if ( result < 0 || value < channels ) {
        return 0;
    }
    result = snd_pcm_hw_params_get_channels_min( hw_params, &value );
    if ( result < 0 ) {
        return 0;
    }
    if (value > channels){
        channels = value;
    }
    return channels;
}

bool getByteswap(snd_pcm_format_t deviceFormat)
{
    int result = 0;
    if ( deviceFormat != SND_PCM_FORMAT_S8 ) {
        result = snd_pcm_format_cpu_endian( deviceFormat );
        if ( result == 0 )
            return true;
    }
    return false;
}

std::optional<_snd_pcm_access> setAccessMode(const std::vector<_snd_pcm_access>& accessTries, snd_pcm_hw_params_t *hw_params, snd_pcm_t *phandle)
{
    int result = 0;
    for (auto& a : accessTries){
        result = snd_pcm_hw_params_set_access( phandle, hw_params, a );
        if ( result == 0 ) {
            return a;
        }
    }
    return {};
}

std::vector<_snd_pcm_access> getAccessTries(RtAudio::StreamOptions* options){
    if ( options && options->flags & RTAUDIO_NONINTERLEAVED ) {
        return {SND_PCM_ACCESS_RW_NONINTERLEAVED, SND_PCM_ACCESS_RW_INTERLEAVED};
    }
    else {
        return {SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_RW_NONINTERLEAVED};
    }
}

snd_pcm_format_t negotiateSupportedFormat(snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle, snd_pcm_format_t preferFormat)
{
    snd_pcm_format_t test_formats[] = {preferFormat, SND_PCM_FORMAT_FLOAT64, SND_PCM_FORMAT_FLOAT, SND_PCM_FORMAT_S32,
                                       SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S16, SND_PCM_FORMAT_S8};
    int res = 0;
    for (auto f : test_formats){
        res = snd_pcm_hw_params_test_format(phandle, hw_params, f);
        if (res == 0){
            auto f_o = getRtFormat(f);
            if (!f_o){
                continue;
            }
            return f;
        }
    }
    return SND_PCM_FORMAT_UNKNOWN;
}

bool isInterlievedAlsa(snd_pcm_access_t access)
{
    switch (access) {
    case SND_PCM_ACCESS_MMAP_INTERLEAVED:
    case SND_PCM_ACCESS_RW_INTERLEAVED:
        return true;
    case SND_PCM_ACCESS_MMAP_NONINTERLEAVED:
    case SND_PCM_ACCESS_RW_NONINTERLEAVED:
        return false;
    default:
        return true;
    }
}

bool fillRtApiStream(RtApi::StreamMode streamMode,
                     RtApi::RtApiStream &stream_,
                     const RtApiAlsaStreamFactory::streamOpenData &openData)
{
    auto rtFormat = getRtFormat(openData.deviceFormat);
    if (!rtFormat) {
        return false;
    }
    stream_.deviceFormat[streamMode] = rtFormat.value();
    stream_.doByteSwap[streamMode] = openData.doByteSwap;
    stream_.nDeviceChannels[streamMode] = openData.deviceChannels;
    stream_.deviceInterleaved[streamMode] = isInterlievedAlsa(openData.deviceAccessMode);
    stream_.latency[streamMode] = 0;
    return true;
}

bool isFormatsSuitable(const RtApiAlsaStreamFactory::streamOpenData &f1,
                       const RtApiAlsaStreamFactory::streamOpenData &f2)
{
    if (f1.bufferSize != f2.bufferSize)
        return false;
    return true;
}
}

std::shared_ptr<RtApiStreamClass> RtApiAlsaStreamFactory::createStream(CreateStreamParams params)
{
#if defined(__RTAUDIO_DEBUG__)
    struct SndOutputTdealloc
    {
        SndOutputTdealloc()
            : _out(NULL)
        {
            snd_output_stdio_attach(&_out, stderr, 0);
        }
        ~SndOutputTdealloc() { snd_output_close(_out); }
        operator snd_output_t *() { return _out; }
        snd_output_t *_out;
    } out;
#else
    snd_output_t *out = nullptr;
#endif

    int openMode = SND_PCM_ASYNC;

    if (params.options && params.options->flags & RTAUDIO_ALSA_NONBLOCK) {
        openMode = SND_PCM_NONBLOCK;
    }
    std::optional<streamOpenData> openDataPlayback;
    std::optional<streamOpenData> openDataCapture;

    snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
    if (params.mode == RtApi::StreamMode::OUTPUT || params.mode == RtApi::StreamMode::DUPLEX) {
        openDataPlayback = createStreamDirectionHandle(stream, params, openMode, out);
        if (!openDataPlayback) {
            return {};
        }
    }

    stream = SND_PCM_STREAM_CAPTURE;
    if (params.mode == RtApi::StreamMode::INPUT || params.mode == RtApi::StreamMode::DUPLEX) {
        openDataCapture = createStreamDirectionHandle(stream, params, openMode, out);
        if (!openDataCapture) {
            return {};
        }
    }

    if (params.mode == RtApi::DUPLEX && (openDataPlayback && openDataCapture)) {
        if (openDataPlayback->bufferSize != openDataCapture->bufferSize) {
            error(RTAUDIO_SYSTEM_ERROR, "Input and output buffer size mismatch");
            return {};
        }
        if (snd_pcm_link(openDataPlayback->han.handle(), openDataCapture->han.handle()) != 0) {
            error(RTAUDIO_WARNING,
                  "RtApiAlsa::probeDeviceOpen: unable to synchronize input and output devices.");
        }
    }
    if (openDataPlayback)
        params.bufferSize = openDataPlayback->bufferSize;
    if (openDataCapture)
        params.bufferSize = openDataCapture->bufferSize;

    RtApi::RtApiStream stream_{};
    if (openDataPlayback) {
        fillRtApiStream(RtApi::OUTPUT, stream_, openDataPlayback.value());
    }
    if (openDataCapture) {
        fillRtApiStream(RtApi::INPUT, stream_, openDataCapture.value());
    }
    stream_.nBuffers = 1;
    if (setupStreamWithParams(stream_, params) == false) {
        return {};
    }
    if (setupStreamCommon(stream_) == false) {
        return {};
    }
    return std::make_shared<RtApiAlsaStream>(std::move(stream_),
                                             openDataPlayback ? std::move(openDataPlayback->han)
                                                              : SndPcmHandle(),
                                             openDataCapture ? std::move(openDataCapture->han)
                                                             : SndPcmHandle());
}

std::optional<RtApiAlsaStreamFactory::streamOpenData>
RtApiAlsaStreamFactory::createStreamDirectionHandle(snd_pcm_stream_t stream,
                                                    CreateStreamParams params,
                                                    int openMode,
                                                    snd_output_t *out)
{
    streamOpenData data;
    data.han = SndPcmHandle(params.busId.c_str(), stream, openMode);
    if (data.han.isValid() == false)
        return {};
    snd_pcm_t *phandle = data.han.handle();

    snd_pcm_access_t deviceAccessMode = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    snd_pcm_format_t deviceFormat = SND_PCM_FORMAT_UNKNOWN;
    bool doByteSwap = false;
    unsigned int deviceChannels = 0;
    unsigned int periods = 1;
    unsigned int channels = 0;
    unsigned int bufferSize = 0;
    if (stream == SND_PCM_STREAM_PLAYBACK) {
        channels = params.channelsOutput;
    } else {
        channels = params.channelsInput;
    }

    int result = 0;
    snd_pcm_hw_params_t *hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);
    result = snd_pcm_hw_params_any(phandle, hw_params);
    if (result < 0) {
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error getting pcm device (" << params.busId
                     << ") parameters, " << snd_strerror(result) << ".";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

#if defined(__RTAUDIO_DEBUG__)
    fprintf(stderr, "\nRtApiAlsa: dump hardware params just after device open:\n\n");
    snd_pcm_hw_params_dump(hw_params, out);
#endif

    {
        auto accessTries = getAccessTries(params.options);
        auto actualAccessMode_opt = setAccessMode(accessTries, hw_params, phandle);
        if (!actualAccessMode_opt) {
            errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting pcm device (" << params.busId
                         << ") access, " << snd_strerror(result) << ".";
            error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
            return {};
        }
        deviceAccessMode = *actualAccessMode_opt;
    }

    {
        deviceFormat = negotiateSupportedFormat(hw_params, phandle, getAlsaFormat(params.format));
        if (deviceFormat == SND_PCM_FORMAT_UNKNOWN) {
            errorStream_ << "RtApiAlsa::probeDeviceOpen: pcm device (" << params.busId
                         << ") data format not supported by RtAudio.";
            error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
            return {};
        }
        result = snd_pcm_hw_params_set_format(phandle, hw_params, deviceFormat);
        if (result < 0) {
            errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting pcm device (" << params.busId
                         << ") data format, " << snd_strerror(result) << ".";
            error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
            return {};
        }
    }

    doByteSwap = getByteswap(deviceFormat);

    result = snd_pcm_hw_params_set_rate(phandle, hw_params, params.sampleRate, 0);
    if (result < 0) {
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting sample rate on device ("
                     << params.busId << "), " << snd_strerror(result) << ".";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    deviceChannels = getDeviceChannels(channels, hw_params, phandle);
    if (deviceChannels == 0) {
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error negotiate channels on device ("
                     << params.busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    result = snd_pcm_hw_params_set_channels(phandle, hw_params, deviceChannels);
    if (result < 0) {
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting channels for device "
                     << snd_strerror(result) << ".";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    if (setupBufferPeriod(params.options, params.bufferSize, hw_params, phandle, periods) == false) {
        errorStream_ << "RtApiAlsa::probeDeviceOpen: error setting buffer period on device ("
                     << params.busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    bufferSize = params.bufferSize;

    // Install the hardware configuration
    result = snd_pcm_hw_params(phandle, hw_params);
    if (result < 0) {
        errorStream_
            << "RtApiAlsa::probeDeviceOpen: error installing hardware configuration on device ("
            << params.busId << "), " << snd_strerror(result) << ".";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

#if defined(__RTAUDIO_DEBUG__)
    fprintf(stderr, "\nRtApiAlsa: dump hardware params after installation:\n\n");
    snd_pcm_hw_params_dump(hw_params, out);
#endif

#if defined(__RTAUDIO_DEBUG__)
    result = setSwParams(phandle, params.bufferSize, out);
#else
    result = setSwParams(phandle, params.bufferSize, nullptr);
#endif
    if (result < 0) {
        errorStream_
            << "RtApiAlsa::probeDeviceOpen: error installing software configuration on device ("
            << params.busId << "), " << snd_strerror(result) << ".";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    data.bufferSize = bufferSize;
    data.deviceAccessMode = deviceAccessMode;
    data.deviceChannels = deviceChannels;
    data.deviceFormat = deviceFormat;
    data.doByteSwap = doByteSwap;
    data.periods = periods;
    return data;
}
