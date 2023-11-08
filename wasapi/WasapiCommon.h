#pragma once

#include <wrl.h>
#include <mmdeviceapi.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class RtApiWasapiCommon {
public:
    RtApiWasapiCommon();
    virtual ~RtApiWasapiCommon();
protected:
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
private:
    bool coInitialized_ = false;
};

class COMLibrary_Raii {
public:
    COMLibrary_Raii() {
        HRESULT hr = CoInitialize(NULL);
        if (!FAILED(hr))
            coInitialized_ = true;
    }
    COMLibrary_Raii(const COMLibrary_Raii&) = delete;
    ~COMLibrary_Raii() {
        if (coInitialized_)
            CoUninitialize();
    }
private:
    bool coInitialized_ = false;
};

std::optional<std::string> probeWasapiDeviceName(IMMDevice* devicePtr);