#pragma once
#include "RtAudio.h"
#include <alsa/asoundlib.h>

struct AlsaHandle;

class RtApiAlsa: public RtApi
{
public:
    RtApiAlsa();
    ~RtApiAlsa();
    RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_ALSA; }
    void closeStream( void ) override;
    RtAudioErrorType startStream( void ) override;
    RtAudioErrorType stopStream( void ) override;
    RtAudioErrorType abortStream( void ) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    void callbackEvent( void );

private:
    std::vector<std::pair<std::string, unsigned int>> deviceIdPairs_;

    bool probeAudioCardDevice(snd_ctl_t* handle, snd_ctl_card_info_t* ctlinfo, int device, int card);
    bool probeAudioCard(int card);
    void listDevices(void) override;
    bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) override;

    bool probeDeviceInfo( RtAudio::DeviceInfo &info, std::string name );
    bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                          unsigned int firstChannel, unsigned int sampleRate,
                          RtAudioFormat format, unsigned int *bufferSize,
                          RtAudio::StreamOptions *options ) override;

    bool setupThread(RtAudio::StreamOptions* options);
    bool allocateBuffers(StreamMode mode, unsigned int bufferSize);
    AlsaHandle* getAlsaHandle();
    void setupBufferConversion(StreamMode mode);
    int setSwParams(snd_pcm_t * phandle, unsigned int bufferSize, snd_output_t* out);
    bool setupBufferPeriod(RtAudio::StreamOptions* options, StreamMode mode, unsigned int *bufferSize, snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle, int& buffer_period_out);
    bool setupChannels(StreamMode mode, unsigned int channels, unsigned int firstChannel, snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle);
    int setupByteswap(StreamMode mode, snd_pcm_format_t deviceFormat);
    snd_pcm_format_t setFormat(snd_pcm_hw_params_t * hw_params, snd_pcm_t * phandle, StreamMode mode, RtAudioFormat format);
    int setAccessMode(RtAudio::StreamOptions* options, snd_pcm_hw_params_t *hw_params, snd_pcm_t *phandle, StreamMode mode);

    int processInput();
    bool processOutput(int samples);
};
