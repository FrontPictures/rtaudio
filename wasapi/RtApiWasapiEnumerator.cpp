#include "RtApiWasapiEnumerator.h"
#include "WasapiCommon.h"

#ifndef INITGUID
#define INITGUID
#endif

#ifdef _MSC_VER
#pragma comment( lib, "ksuser" )
#pragma comment( lib, "mfplat.lib" )
#pragma comment( lib, "mfuuid.lib" )
#pragma comment( lib, "wmcodecdspuuid" )
#endif

RtApiWasapiEnumerator::RtApiWasapiEnumerator()
{
    HRESULT hr = CoInitialize(NULL);
    if (!FAILED(hr))
        coInitialized_ = true;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator_);
}

RtApiWasapiEnumerator::~RtApiWasapiEnumerator()
{
    deviceEnumerator_.Reset();
    if (coInitialized_)
        CoUninitialize();
}

std::vector<RtAudio::DeviceInfo> RtApiWasapiEnumerator::listDevices(void)
{
    if (!deviceEnumerator_) return {};
    std::vector<RtAudio::DeviceInfo> res;

    unsigned int nDevices = 0;
    Microsoft::WRL::ComPtr<IMMDeviceCollection> deviceCollection;
    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    Microsoft::WRL::ComPtr<IMMEndpoint> deviceEndpointPtr;

    LPWSTR deviceId = NULL;

    HRESULT hr = deviceEnumerator_->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr)) {
        error(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::probeDevices: Unable to retrieve render device collection.");
        return {};
    }

    hr = deviceCollection->GetCount(&nDevices);
    if (FAILED(hr)) {
        error(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::probeDevices: Unable to retrieve render device count.");
        return {};
    }

    if (nDevices == 0) {
        error(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::probeDevices: No devices found.");
        return {};
    }

    for (unsigned int n = 0; n < nDevices; n++) {
        hr = deviceCollection->Item(n, &devicePtr);
        if (FAILED(hr)) {
            error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to retrieve audio device handle.");
            continue;
        }

        hr = devicePtr->GetId(&deviceId);
        if (FAILED(hr)) {
            error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to get device Id.");
            continue;
        }
        auto id_str = convertCharPointerToStdString(deviceId);
        CoTaskMemFree(deviceId);
        RtAudio::DeviceInfo info;

        hr = devicePtr->QueryInterface(__uuidof(IMMEndpoint), (void**)deviceEndpointPtr.GetAddressOf());
        if (FAILED(hr)) {
            error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to retreive audio IMMEndpoint.");
            continue;
        }
        EDataFlow flow = eRender;
        hr = deviceEndpointPtr->GetDataFlow(&flow);
        if (FAILED(hr)) {
            error(RTAUDIO_WARNING, "RtApiWasapi::probeDevices: Unable to get data flow.");
            continue;
        }

        if (flow == eRender) {
            info.supportsOutput = true;
        }
        else if (flow == eCapture) {
            info.supportsInput = true;
        }
        info.busID = id_str;

        auto name_opt = probeDeviceName(devicePtr.Get());
        if (!name_opt) {
            continue;
        }
        info.name = *name_opt;
        res.push_back(info);
    }
    return res;
}

std::optional<std::string> RtApiWasapiEnumerator::probeDeviceName(IMMDevice* devicePtr)
{
    Microsoft::WRL::ComPtr<IPropertyStore> devicePropStore;
    PROPVARIANT_Raii deviceNameProp;
    HRESULT hr = devicePtr->OpenPropertyStore(STGM_READ, &devicePropStore);
    if (FAILED(hr)) {
        error(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::probeDeviceInfo: Unable to open device property store.");
        return {};
    }
    hr = devicePropStore->GetValue(PKEY_Device_FriendlyName, &deviceNameProp);
    if (FAILED(hr) || deviceNameProp.get().pwszVal == nullptr) {
        error(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::probeDeviceInfo: Unable to retrieve device property: PKEY_Device_FriendlyName.");
        return {};
    }
    return convertCharPointerToStdString(deviceNameProp.get().pwszVal);
}
