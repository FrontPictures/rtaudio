#include "RtApiWasapiProber.h"
#include <audioclient.h>
#include "utils.h"
#include <cassert>

std::optional<RtAudio::DeviceInfo> RtApiWasapiProber::probeDevice(const std::string& busId)
{
    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient;
    Microsoft::WRL::ComPtr<IMMEndpoint> deviceEndpointPtr;
    RtAudio::DeviceInfo info{};

    UNIQUE_FORMAT deviceFormat = MAKE_UNIQUE_FORMAT_EMPTY;
    HRESULT res = S_OK;
    RtAudioErrorType errorType = RTAUDIO_DRIVER_ERROR;

    auto device_id = convertStdStringToWString(busId);

    HRESULT hr = deviceEnumerator_->GetDevice(device_id.c_str(), &devicePtr);
    if (FAILED(hr)) {
        error(errorType, "RtApiWasapi::probeDeviceInfo: Unable to retrieve device handle.");
        return {};
    }

    hr = devicePtr->QueryInterface(__uuidof(IMMEndpoint), (void**)deviceEndpointPtr.GetAddressOf());
    if (FAILED(hr)) {
        error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to retreive audio IMMEndpoint.");
        return {};
    }
    EDataFlow flow = eRender;
    hr = deviceEndpointPtr->GetDataFlow(&flow);
    if (FAILED(hr)) {
        error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to get data flow.");
        return {};
    }

    if (flow == eRender) {
        info.partial.supportsOutput = true;
    }
    else if (flow == eCapture) {
        info.partial.supportsInput = true;
    }

    hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
    if (FAILED(hr)) {
        error(errorType, "RtApiWasapi::probeDeviceInfo: Unable to retrieve device audio client.");
        return {};
    }

    res = CONSTRUCT_UNIQUE_FORMAT(audioClient->GetMixFormat, deviceFormat);
    if (FAILED(hr) || !deviceFormat) {
        error(errorType, "RtApiWasapi::probeDeviceInfo: Unable to retrieve device mix format.");
        return {};
    }

    auto name_opt = probeWasapiDeviceName(devicePtr.Get());
    if (!name_opt) {
        return {};
    }
    info.partial.name = *name_opt;
    info.partial.busID = busId;
    info.duplexChannels = 0;

    if (info.partial.supportsInput) {
        info.inputChannels = deviceFormat->nChannels;
        info.outputChannels = 0;
    }
    else if (info.partial.supportsOutput) {
        info.inputChannels = 0;
        info.outputChannels = deviceFormat->nChannels;
    }

    info.sampleRates.clear();
    info.preferredSampleRate = deviceFormat->nSamplesPerSec;
    info.sampleRates.push_back(info.preferredSampleRate);

    info.nativeFormats = 0;
    probeFormats(deviceFormat, info);
    return info;
}

void RtApiWasapiProber::probeFormats(const UNIQUE_FORMAT& deviceFormat, RtAudio::DeviceInfo& info)
{
    if (deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            ((const WAVEFORMATEXTENSIBLE*)deviceFormat.get())->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        if (deviceFormat->wBitsPerSample == 32) {
            info.nativeFormats |= RTAUDIO_FLOAT32;
        }
        else if (deviceFormat->wBitsPerSample == 64) {
            info.nativeFormats |= RTAUDIO_FLOAT64;
        }
    }
    else if (deviceFormat->wFormatTag == WAVE_FORMAT_PCM ||
        (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            ((const WAVEFORMATEXTENSIBLE*)deviceFormat.get())->SubFormat == KSDATAFORMAT_SUBTYPE_PCM))
    {
        if (deviceFormat->wBitsPerSample == 8) {
            info.nativeFormats |= RTAUDIO_SINT8;
        }
        else if (deviceFormat->wBitsPerSample == 16) {
            info.nativeFormats |= RTAUDIO_SINT16;
        }
        else if (deviceFormat->wBitsPerSample == 24) {
            info.nativeFormats |= RTAUDIO_SINT24;
        }
        else if (deviceFormat->wBitsPerSample == 32) {
            info.nativeFormats |= RTAUDIO_SINT32;
        }
    }
}
