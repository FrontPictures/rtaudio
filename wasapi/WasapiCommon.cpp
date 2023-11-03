#include "WasapiCommon.h"
#include "utils.h"
#include <functiondiscoverykeys_devpkey.h>

RtApiWasapiCommon::RtApiWasapiCommon()
{
    HRESULT hr = CoInitialize(NULL);
    if (!FAILED(hr))
        coInitialized_ = true;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator_);
}

RtApiWasapiCommon::~RtApiWasapiCommon()
{
    deviceEnumerator_.Reset();
    if (coInitialized_)
        CoUninitialize();
}

PROPVARIANT_Raii::PROPVARIANT_Raii() {
    PropVariantInit(&mPropVal);
}

PROPVARIANT_Raii::~PROPVARIANT_Raii() {
    PropVariantClear(&mPropVal);
}

PROPVARIANT* PROPVARIANT_Raii::operator&() {
    return &mPropVal;
}

const PROPVARIANT PROPVARIANT_Raii::get() const {
    return mPropVal;
}

std::optional<std::string> probeWasapiDeviceName(IMMDevice* devicePtr)
{
    Microsoft::WRL::ComPtr<IPropertyStore> devicePropStore;
    PROPVARIANT_Raii deviceNameProp;
    HRESULT hr = devicePtr->OpenPropertyStore(STGM_READ, &devicePropStore);
    if (FAILED(hr)) {        
        return {};
    }
    hr = devicePropStore->GetValue(PKEY_Device_FriendlyName, &deviceNameProp);
    if (FAILED(hr) || deviceNameProp.get().pwszVal == nullptr) {        
        return {};
    }
    return convertCharPointerToStdString(deviceNameProp.get().pwszVal);
}
