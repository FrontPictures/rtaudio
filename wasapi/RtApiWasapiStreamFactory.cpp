#include "RtApiWasapiStreamFactory.h"
#include <Audioclient.h>
#include "RtApiWasapiStream.h"
#include "utils.h"

namespace {
    bool NegotiateExclusiveFormat(IAudioClient* renderAudioClient, UNIQUE_FORMAT& format) {
        HRESULT hr = S_OK;
        hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, format.get(), nullptr);
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
            if ((format)->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                (format)->wFormatTag = WAVE_FORMAT_PCM;
                (format)->wBitsPerSample = 16;
                (format)->nBlockAlign = (format)->nChannels * (format)->wBitsPerSample / 8;
                (format)->nAvgBytesPerSec = (format)->nSamplesPerSec * (format)->nBlockAlign;
            }
            else if ((format)->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format.get())->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format.get());
                waveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                waveFormatExtensible->Format.wBitsPerSample = 16;
                waveFormatExtensible->Format.nBlockAlign = ((format)->wBitsPerSample / 8) * (format)->nChannels;
                waveFormatExtensible->Format.nAvgBytesPerSec = waveFormatExtensible->Format.nSamplesPerSec * waveFormatExtensible->Format.nBlockAlign;
                waveFormatExtensible->Samples.wValidBitsPerSample = 16;
            }
            else {
                return false;
            }
            hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, format.get(), nullptr);
        }
        if (FAILED(hr)) {
            return false;
        }
        return true;
    }

    RtAudioFormat GetRtAudioTypeFromWasapi(const WAVEFORMATEX* format) {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                if (waveFormatExtensible->Format.wBitsPerSample == 32) {
                    return RTAUDIO_FLOAT32;
                }
            }
            else if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
                if (waveFormatExtensible->Format.wBitsPerSample == 16) {
                    return RTAUDIO_SINT16;
                }
            }
        }
        else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            if (format->wBitsPerSample == 32) {
                return RTAUDIO_FLOAT32;
            }
        }
        else if (format->wFormatTag == WAVE_FORMAT_PCM) {
            if (format->wBitsPerSample == 16) {
                return RTAUDIO_SINT16;
            }
        }
        return 0;
    }

    void setConvertInfo(RtApi::StreamMode mode, RtApi::RtApiStream& stream_)
    {

    }
}

std::shared_ptr<RtApiStreamClass> RtApiWasapiStreamFactory::createStream(CreateStreamParams params)
{
    if (params.mode != RtApi::StreamMode::INPUT && params.mode != RtApi::StreamMode::OUTPUT) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: WASAPI does not support DUPLEX streams.");
        return {};
    }

    if (!deviceEnumerator_)
        return {};

    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
    UNIQUE_FORMAT deviceFormat = MAKE_UNIQUE_FORMAT_EMPTY;
    UNIQUE_EVENT streamEvent = MAKE_UNIQUE_EVENT_EMPTY;
    long long streamLatency = 0;
    AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_SHARED;

    std::wstring temp = std::wstring(params.busId.begin(), params.busId.end());
    HRESULT hr = deviceEnumerator_->GetDevice((LPWSTR)temp.c_str(), &devicePtr);
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to retrieve device handle.");
        return {};
    }
    hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        NULL, (void**)&audioClient);
    if (FAILED(hr) || !audioClient) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to retrieve device audio client.");
        return {};
    }

    hr = CONSTRUCT_UNIQUE_FORMAT(audioClient->GetMixFormat, deviceFormat);
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to retrieve device mix format.");
        return {};
    }
    audioClient->GetStreamLatency(&streamLatency);
    REFERENCE_TIME userBufferSize = (((uint64_t)(params.bufferSize)) * 10000000 / deviceFormat->nSamplesPerSec);
    if (params.options && params.options->flags & RTAUDIO_HOG_DEVICE) {
        REFERENCE_TIME defaultBufferDuration = 0;
        REFERENCE_TIME minimumBufferDuration = 0;

        hr = audioClient->GetDevicePeriod(&defaultBufferDuration, &minimumBufferDuration);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to retrieve device period.");
            return {};
        }

        if (NegotiateExclusiveFormat(audioClient.Get(), deviceFormat) == false) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to negotiate format for exclusive device.");
            return {};
        }
        if (params.sampleRate != deviceFormat->nSamplesPerSec) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: samplerate exclusive mismatch.");
            return {};
        }
        if (userBufferSize == 0) {
            userBufferSize = defaultBufferDuration;
        }
        if (userBufferSize < minimumBufferDuration) {
            userBufferSize = minimumBufferDuration;
        }
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            userBufferSize,
            userBufferSize,
            deviceFormat.get(),
            NULL);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 nFrames = 0;
            hr = audioClient->GetBufferSize(&nFrames);
            if (FAILED(hr)) {
                error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.");
                return {};
            }
            constexpr int REFTIMES_PER_SEC = 10000000;
            userBufferSize = (REFERENCE_TIME)((double)REFTIMES_PER_SEC / deviceFormat->nSamplesPerSec * nFrames + 0.5);
            hr = audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                userBufferSize,
                userBufferSize,
                deviceFormat.get(),
                NULL);
        }
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to open device.");
            return {};
        }
        shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;
    }
    else {
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            userBufferSize,
            0,
            deviceFormat.get(),
            NULL);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to open device.");
            return {};
        }
        shareMode = AUDCLNT_SHAREMODE_SHARED;
    }

    UINT32 nFrames = 0;
    hr = audioClient->GetBufferSize(&nFrames);
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.");
        return {};
    }
    params.bufferSize = nFrames;
    if (params.mode == RtApi::StreamMode::OUTPUT) {
        hr = audioClient->GetService(__uuidof(IAudioRenderClient),
            (void**)&renderClient);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to retrieve render client handle.");
            return {};
        }
    }
    else {
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
            (void**)&captureClient);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to retrieve capture client handle.");
            return {};
        }
    }
    streamEvent = MAKE_UNIQUE_EVENT_VALUE(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (!streamEvent) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to create render event.");
        return {};
    }

    hr = audioClient->SetEventHandle(streamEvent.get());
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to set render event handle.");
        return {};
    }
    hr = audioClient->Reset();
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::wasapiThread: Unable to reset audio stream.");
        return {};
    }
    if (renderClient) {
        BYTE* pData = nullptr;
        hr = renderClient->GetBuffer(nFrames, &pData);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiStreamFactory: Unable to get buffer");
            return {};
        }
        hr = renderClient->ReleaseBuffer(nFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(hr)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiStreamFactory: Unable to release buffer");
            return {};
        }
    }

    RtApi::RtApiStream stream_{};
    stream_.nDeviceChannels[params.mode] = deviceFormat->nChannels;
    stream_.deviceInterleaved[params.mode] = true;
    stream_.nBuffers = 1;
    if (params.options && params.options->flags & RTAUDIO_SCHEDULE_REALTIME)
        stream_.callbackInfo.priority = 15;
    else
        stream_.callbackInfo.priority = 0;

    stream_.deviceFormat[params.mode] = GetRtAudioTypeFromWasapi(deviceFormat.get());
    if (stream_.deviceFormat[params.mode] == 0) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiStreamFactory: wasapi format not implemented");
        return {};
    }

    if (setupStreamWithParams(stream_, params) == false) {
        return {};
    }

    return std::shared_ptr<RtApiWasapiStream>(
        new RtApiWasapiStream(std::move(stream_), audioClient, renderClient, captureClient, std::move(deviceFormat),
            std::move(streamEvent), shareMode));
}
