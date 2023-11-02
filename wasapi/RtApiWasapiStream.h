#pragma once

#include "RtAudio.h"
#include <memory>
#include <Audioclient.h>
#include "WasapiCommon.h"

class RtApiWasapiStream : public RtApiStreamClass, public RtApiWasapiCommon {
public:
    RtApiWasapiStream(RtApi::RtApiStream stream, Microsoft::WRL::ComPtr<IAudioClient> audioClient, Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient, Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient, UNIQUE_FORMAT deviceFormat, UNIQUE_EVENT streamEvent, AUDCLNT_SHAREMODE shareMode, RtApi::StreamMode mode);

    RtApiWasapiStream(const RtApiWasapiStream&) = delete;
    ~RtApiWasapiStream() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }

    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;    
private:
    static DWORD WINAPI runWasapiThread(void* wasapiPtr);
    void wasapiThread();

    Microsoft::WRL::ComPtr<IAudioClient> mAudioClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> mRenderClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> mCaptureClient;
    UNIQUE_FORMAT mDeviceFormat;
    UNIQUE_EVENT mStreamEvent;
    AUDCLNT_SHAREMODE mShareMode;
    RtApi::StreamMode mMode;
};
