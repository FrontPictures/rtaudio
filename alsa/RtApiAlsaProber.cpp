#include "RtApiAlsaProber.h"
#include "AlsaCommon.h"

namespace{
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

template<class T>
inline bool foundInVector(const std::vector<T>& vec, T value){
    for (auto& v: vec){
        if (v == value)
            return true;
    }
    return false;
}

template<class T>
inline std::vector<T> getSameValues(const std::vector<T>& v1, const std::vector<T>& v2){
    std::vector<T> res;
    for (auto& v: v1){
        if (foundInVector(v2, v))
            res.push_back(v);
    }
    return res;
}

inline RtAudioFormat getSameFormats(RtAudioFormat f1, RtAudioFormat f2){
    return f1 & f2;
}

constexpr int ALSA_PROBE_MODE = SND_PCM_ASYNC | SND_PCM_NONBLOCK;
}

std::optional<RtAudio::DeviceInfo> RtApiAlsaProber::probeDevice(const std::string & busId)
{
    snd_pcm_t *phandlePlayback = nullptr;
    snd_pcm_t *phandleCapture = nullptr;
    snd_pcm_open( &phandlePlayback, busId.c_str(), SND_PCM_STREAM_PLAYBACK, ALSA_PROBE_MODE);
    snd_pcm_open( &phandleCapture, busId.c_str(), SND_PCM_STREAM_CAPTURE, ALSA_PROBE_MODE);

    if (!phandlePlayback && !phandleCapture)
        return {};

    if (phandleCapture && phandlePlayback){
        phandleCapture =phandleCapture ;
    }
    auto info = probeDeviceHandles(phandlePlayback, phandleCapture, busId.c_str());
    if (phandlePlayback)
        snd_pcm_close(phandlePlayback);
    if (phandleCapture)
        snd_pcm_close(phandleCapture);
    return info;
}

std::optional<RtAudio::DeviceInfo> RtApiAlsaProber::probeDeviceHandles(snd_pcm_t * phandlePlayback, snd_pcm_t * phandleCapture, const char* busId)
{

    //snd_pcm_info_get_card();

    int result = 0;
    RtAudio::DeviceInfo info{};
    snd_pcm_hw_params_t *paramsPlayback = nullptr;
    snd_pcm_hw_params_t *paramsCapture = nullptr;
    snd_pcm_hw_params_alloca( &paramsPlayback );
    snd_pcm_hw_params_alloca( &paramsCapture );

    snd_pcm_info_t* pcmInfoPlayback = nullptr;
    snd_pcm_info_t* pcmInfoCapture = nullptr;

    snd_pcm_info_alloca(&pcmInfoPlayback);
    snd_pcm_info_alloca(&pcmInfoCapture);

    if (phandlePlayback){
        if (snd_pcm_hw_params_any( phandlePlayback, paramsPlayback )<0){
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << busId << "), " << snd_strerror( result ) << ".";
            error( RTAUDIO_WARNING, errorStream_.str());
        }
        if (snd_pcm_info(phandlePlayback, pcmInfoPlayback) < 0){
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_info error for device (" << busId << "), " << snd_strerror( result ) << ".";
            error( RTAUDIO_WARNING, errorStream_.str());
        }
    }

    if (phandleCapture){
        if (snd_pcm_hw_params_any( phandleCapture, paramsCapture ) < 0){
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_hw_params error for device (" << busId << "), " << snd_strerror( result ) << ".";
            error( RTAUDIO_WARNING, errorStream_.str());
        }
        if (snd_pcm_info(phandleCapture, pcmInfoCapture) < 0){
            errorStream_ << "RtApiAlsa::probeDeviceInfo: snd_pcm_info error for device (" << busId << "), " << snd_strerror( result ) << ".";
            error( RTAUDIO_WARNING, errorStream_.str());
        }
    }

    if (phandlePlayback){
        auto ch = probeSingleDeviceGetChannels(paramsPlayback, busId);
        if (ch)
            info.outputChannels = *ch;
    }
    if (phandleCapture){
        auto ch = probeSingleDeviceGetChannels(paramsCapture, busId);
        if (ch)
            info.inputChannels = *ch;
    }

    if ( info.outputChannels > 0 && info.inputChannels > 0 )
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;
    if (info.outputChannels == 0 && info.inputChannels == 0){
        errorStream_ << "RtApiAlsa::probeDevice: no channels for device (" << busId << ").";
        error( RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    std::vector<unsigned int> sampleRatesPlayback;
    std::vector<unsigned int> sampleRatesCapture;
    RtAudioFormat formatPlayback = 0;
    RtAudioFormat formatCapture = 0;

    if (phandlePlayback){
        sampleRatesPlayback = probeSingleDeviceSamplerates(phandlePlayback, paramsPlayback);
        formatPlayback = probeSingleDeviceFormats(phandlePlayback, paramsPlayback);
    }
    if (phandleCapture){
        sampleRatesCapture = probeSingleDeviceSamplerates(phandleCapture, paramsCapture);
        formatCapture = probeSingleDeviceFormats(phandleCapture, paramsCapture);
    }

    if (info.duplexChannels > 0){
        info.sampleRates = getSameValues(sampleRatesPlayback, sampleRatesCapture);
        info.nativeFormats = getSameFormats(formatPlayback, formatCapture);
        info.partial.supportsInput = true;
        info.partial.supportsOutput = true;
    }else if (info.outputChannels > 0){
        info.nativeFormats = formatPlayback;
        info.sampleRates = std::move(sampleRatesPlayback);
        info.partial.supportsOutput = true;
    }else if (info.inputChannels > 0){
        info.nativeFormats = formatCapture;
        info.sampleRates = std::move(sampleRatesCapture);
        info.partial.supportsInput = true;
    }

    snd_ctl_card_info_t *ctlinfo = nullptr;
    snd_ctl_card_info_alloca(&ctlinfo);
    result = getCardInfoById(getCardIdByPCMId(busId).c_str(), ctlinfo);
    if (result<0){
        errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting card info for (" << busId << ").";
        error( RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    if (phandlePlayback){
        info.partial.name = getAlsaPrettyName(ctlinfo, pcmInfoPlayback);
    }else if (phandleCapture){
        info.partial.name = getAlsaPrettyName(ctlinfo, pcmInfoCapture);
    }
    info.partial.busID = busId;
    return info;
}

std::optional<unsigned int> RtApiAlsaProber::probeSingleDeviceGetChannels(snd_pcm_hw_params_t *params, const char* busId)
{
    int result = 0;
    unsigned int value = 0;
    result = snd_pcm_hw_params_get_channels_max( params, &value );
    if ( result < 0 ) {
        errorStream_ << "RtApiAlsa::probeDeviceInfo: error getting device (" << busId << ") channels, " << snd_strerror( result ) << ".";
        error( RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    return value;
}

std::vector<unsigned int> RtApiAlsaProber::probeSingleDeviceSamplerates(snd_pcm_t *phandle, snd_pcm_hw_params_t *params)
{
    std::vector<unsigned int> sampleRates;
    for ( unsigned int i=0; i<RtAudio::MAX_SAMPLE_RATES; i++ ) {
        if ( snd_pcm_hw_params_test_rate( phandle, params, RtAudio::SAMPLE_RATES[i], 0 ) == 0 ) {
            sampleRates.push_back( RtAudio::SAMPLE_RATES[i] );
        }
    }
    return sampleRates;
}

RtAudioFormat RtApiAlsaProber::probeSingleDeviceFormats(snd_pcm_t * phandle, snd_pcm_hw_params_t * params)
{
    RtAudioFormat formats = 0;
    snd_pcm_format_t formats_to_test [] = {SND_PCM_FORMAT_S8, SND_PCM_FORMAT_S16, SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S32, SND_PCM_FORMAT_FLOAT};

    for (auto format : formats_to_test){
        if ( snd_pcm_hw_params_test_format(phandle, params, format) != 0 ){
            continue;
        }
        auto f_o = getRtFormat(format);
        if (!f_o){
            errorStream_ << "RtApiAlsa::probeDeviceInfo: format (" << format << ") not supported.";
            error( RTAUDIO_WARNING, errorStream_.str());
            continue;
        }
        formats |= *f_o;
    }
    return formats;
}
