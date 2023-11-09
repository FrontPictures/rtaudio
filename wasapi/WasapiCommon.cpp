#include "WasapiCommon.h"
#include "utils.h"
#include <functiondiscoverykeys_devpkey.h>
#include "windowscommon.h"

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
