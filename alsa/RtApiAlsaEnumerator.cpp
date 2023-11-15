#include "RtApiAlsaEnumerator.h"
#include "AlsaCommon.h"

std::vector<RtAudio::DeviceInfoPartial> RtApiAlsaEnumerator::listDevices()
{
    int card = -1;
    std::vector<RtAudio::DeviceInfoPartial> devices;
    while (snd_card_next( &card ), card >= 0) {
        probeAudioCard(card, devices);
    }
    return devices;
}

bool RtApiAlsaEnumerator::probeAudioCard(int card, std::vector<RtAudio::DeviceInfoPartial>& devices)
{
    char name[128]{0};
    snd_ctl_t *handle = nullptr;

    int result = 0;
    sprintf( name, "hw:%d", card );

    result = snd_ctl_open( &handle, name, 0 );
    if ( result < 0 ) {
        errorStream_ << "RtApiAlsa::probeDevices: control open, card = " << card << ", " << snd_strerror( result ) << ".";
        error( RTAUDIO_WARNING, errorStream_.str());
        return RtApi::FAILURE;
    }
    return probeAudioCardHandle(handle, card, devices);
}

bool RtApiAlsaEnumerator::probeAudioCardHandle(snd_ctl_t * handle, int card, std::vector<RtAudio::DeviceInfoPartial> & devices)
{
    int result = 0;
    snd_ctl_card_info_t *ctlinfo = nullptr;
    snd_ctl_card_info_alloca(&ctlinfo);

    result = snd_ctl_card_info( handle, ctlinfo );
    if ( result < 0 ) {
        errorStream_ << "RtApiAlsa::probeDevices: control info, card = " << card << ", " << snd_strerror( result ) << ".";
        error( RTAUDIO_WARNING, errorStream_.str());
        return RtApi::FAILURE;
    }
    int device = -1;
    while( 1 ) {
        result = snd_ctl_pcm_next_device( handle, &device );
        if ( result < 0 ) {
            errorStream_ << "RtApiAlsa::probeDevices: control next device, card = " << card << ", " << snd_strerror( result ) << ".";
            error( RTAUDIO_WARNING, errorStream_.str());
            break;
        }
        if ( device < 0 )
            break;
        auto dev = probeAudioCardDevice(handle, ctlinfo, device, card);
        if (dev)
            devices.push_back(*dev);
    }
    return RtApi::SUCCESS;
}

std::optional<RtAudio::DeviceInfoPartial> RtApiAlsaEnumerator::probeAudioCardDevice(snd_ctl_t * handle, snd_ctl_card_info_t * ctlinfo, int device, int card)
{
    int result = 0;
    snd_pcm_info_t *pcminfo = nullptr;
    snd_pcm_info_alloca(&pcminfo);
    snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
    char name[128]{0};

    snd_pcm_info_set_device( pcminfo, device );
    snd_pcm_info_set_subdevice( pcminfo, 0 );

    bool supportsInput = false;
    bool supportsOutput = false;

    stream = SND_PCM_STREAM_PLAYBACK;
    snd_pcm_info_set_stream( pcminfo, stream );
    result = snd_ctl_pcm_info( handle, pcminfo );
    if (result==0){
        supportsOutput = true;
    }else if (result != -ENOENT){
        return {};
    }

    stream = SND_PCM_STREAM_CAPTURE;
    snd_pcm_info_set_stream( pcminfo, stream );
    result = snd_ctl_pcm_info( handle, pcminfo );
    if (result==0){
        supportsInput = true;
    }else if (result != -ENOENT){
        return {};
    }

    if (!supportsInput && !supportsOutput){
        errorStream_ << "RtApiAlsa::probeDevices: control pcm info, card = " << card << ", device = " << device << ", " << snd_strerror( result ) << ".";
        error( RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    sprintf( name, "hw:%s,%d", snd_ctl_card_info_get_id(ctlinfo), device );
    std::string id(name);
    std::string prettyName = getAlsaPrettyName(ctlinfo, pcminfo);

    RtAudio::DeviceInfoPartial info;
    info.name = prettyName;
    info.busID = id;
    info.supportsInput = supportsInput;
    info.supportsOutput = supportsOutput;
    return info;
}
