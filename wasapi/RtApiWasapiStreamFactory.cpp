#include "RtApiWasapiStreamFactory.h"
#include <Audioclient.h>
#include "RtApiWasapiStream.h"

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
        unsigned int firstChannel = 0;
        if (mode == RtApi::INPUT) { // convert device to user buffer
            stream_.convertInfo[mode].inJump = stream_.nDeviceChannels[1];
            stream_.convertInfo[mode].outJump = stream_.nUserChannels[1];
            stream_.convertInfo[mode].inFormat = stream_.deviceFormat[1];
            stream_.convertInfo[mode].outFormat = stream_.userFormat;
        }
        else { // convert user to device buffer
            stream_.convertInfo[mode].inJump = stream_.nUserChannels[0];
            stream_.convertInfo[mode].outJump = stream_.nDeviceChannels[0];
            stream_.convertInfo[mode].inFormat = stream_.userFormat;
            stream_.convertInfo[mode].outFormat = stream_.deviceFormat[0];
        }

        if (stream_.convertInfo[mode].inJump < stream_.convertInfo[mode].outJump)
            stream_.convertInfo[mode].channels = stream_.convertInfo[mode].inJump;
        else
            stream_.convertInfo[mode].channels = stream_.convertInfo[mode].outJump;

        // Set up the interleave/deinterleave offsets.
        if (stream_.deviceInterleaved[mode] != stream_.userInterleaved) {
            if ((mode == RtApi::OUTPUT && stream_.deviceInterleaved[mode]) ||
                (mode == RtApi::INPUT && stream_.userInterleaved)) {
                for (int k = 0; k < stream_.convertInfo[mode].channels; k++) {
                    stream_.convertInfo[mode].inOffset.push_back(k * stream_.bufferSize);
                    stream_.convertInfo[mode].outOffset.push_back(k);
                    stream_.convertInfo[mode].inJump = 1;
                }
            }
            else {
                for (int k = 0; k < stream_.convertInfo[mode].channels; k++) {
                    stream_.convertInfo[mode].inOffset.push_back(k);
                    stream_.convertInfo[mode].outOffset.push_back(k * stream_.bufferSize);
                    stream_.convertInfo[mode].outJump = 1;
                }
            }
        }
        else { // no (de)interleaving
            if (stream_.userInterleaved) {
                for (int k = 0; k < stream_.convertInfo[mode].channels; k++) {
                    stream_.convertInfo[mode].inOffset.push_back(k);
                    stream_.convertInfo[mode].outOffset.push_back(k);
                }
            }
            else {
                for (int k = 0; k < stream_.convertInfo[mode].channels; k++) {
                    stream_.convertInfo[mode].inOffset.push_back(k * stream_.bufferSize);
                    stream_.convertInfo[mode].outOffset.push_back(k * stream_.bufferSize);
                    stream_.convertInfo[mode].inJump = 1;
                    stream_.convertInfo[mode].outJump = 1;
                }
            }
        }

        // Add channel offset.
        if (firstChannel > 0) {
            if (stream_.deviceInterleaved[mode]) {
                if (mode == RtApi::OUTPUT) {
                    for (int k = 0; k < stream_.convertInfo[mode].channels; k++)
                        stream_.convertInfo[mode].outOffset[k] += firstChannel;
                }
                else {
                    for (int k = 0; k < stream_.convertInfo[mode].channels; k++)
                        stream_.convertInfo[mode].inOffset[k] += firstChannel;
                }
            }
            else {
                if (mode == RtApi::OUTPUT) {
                    for (int k = 0; k < stream_.convertInfo[mode].channels; k++)
                        stream_.convertInfo[mode].outOffset[k] += (firstChannel * stream_.bufferSize);
                }
                else {
                    for (int k = 0; k < stream_.convertInfo[mode].channels; k++)
                        stream_.convertInfo[mode].inOffset[k] += (firstChannel * stream_.bufferSize);
                }
            }
        }
    }
}

std::shared_ptr<RtApiStreamClass> RtApiWasapiStreamFactory::createStream(RtAudio::DeviceInfo device, RtApi::StreamMode mode, unsigned int channels, unsigned int sampleRate, RtAudioFormat format, unsigned int bufferSize, RtAudioCallback callback,
    void* userData, RtAudio::StreamOptions* options)
{
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

    std::wstring temp = std::wstring(device.busID.begin(), device.busID.end());
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
    REFERENCE_TIME userBufferSize = (((uint64_t)(bufferSize)) * 10000000 / deviceFormat->nSamplesPerSec);
    if (options && options->flags & RTAUDIO_HOG_DEVICE) {
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
        if (sampleRate != deviceFormat->nSamplesPerSec) {
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
    bufferSize = nFrames;
    if (mode == RtApi::StreamMode::OUTPUT) {
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
    stream_.mode = mode;
    stream_.deviceId[mode] = device.busID;
    stream_.doByteSwap[mode] = false;
    stream_.sampleRate = sampleRate;
    stream_.bufferSize = bufferSize;
    stream_.nBuffers = 1;
    stream_.nUserChannels[mode] = channels;
    stream_.channelOffset[mode] = 0;
    stream_.userFormat = format;
    stream_.deviceFormat[mode] = GetRtAudioTypeFromWasapi(deviceFormat.get());
    stream_.callbackInfo.callback = callback;
    stream_.callbackInfo.userData = userData;

    if (stream_.deviceFormat[mode] == 0) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiStreamFactory: wasapi format not implemented");
        return {};
    }
    stream_.nDeviceChannels[mode] = deviceFormat->nChannels;
    if (options && options->flags & RTAUDIO_NONINTERLEAVED)
        stream_.userInterleaved = false;
    else
        stream_.userInterleaved = true;
    stream_.deviceInterleaved[mode] = true;
    stream_.doConvertBuffer[mode] = false;
    if (stream_.userFormat != stream_.deviceFormat[mode] ||
        stream_.nUserChannels[0] != stream_.nDeviceChannels[0] ||
        stream_.nUserChannels[1] != stream_.nDeviceChannels[1])
        stream_.doConvertBuffer[mode] = true;
    else if (stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
        stream_.nUserChannels[mode] > 1)
        stream_.doConvertBuffer[mode] = true;

    if (stream_.doConvertBuffer[mode])
        setConvertInfo(mode, stream_);

    unsigned int bufferBytes = stream_.nUserChannels[mode] * stream_.bufferSize * RtApi::formatBytes(stream_.userFormat);
    stream_.userBuffer[mode] = (char*)calloc(bufferBytes, 1);
    if (!stream_.userBuffer[mode]) {
        error(RTAUDIO_MEMORY_ERROR, "RtApiWasapi::probeDeviceOpen: Error allocating user buffer memory.");
        return {};
    }

    if (options && options->flags & RTAUDIO_SCHEDULE_REALTIME)
        stream_.callbackInfo.priority = 15;
    else
        stream_.callbackInfo.priority = 0;

    return std::shared_ptr<RtApiWasapiStream>(
        new RtApiWasapiStream(std::move(stream_), audioClient, renderClient, captureClient, std::move(deviceFormat),
            std::move(streamEvent), shareMode, mode));
}
