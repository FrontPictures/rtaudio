#pragma once

#include "RtAudio.h"
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfplay.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include "utils.h"
#include "WasapiNotificationHandler.h"
#include <wrl.h>
#include <optional>

class RtApiWasapiEnumerator : public RtApiEnumerator {
public:
    RtApiWasapiEnumerator();
    ~RtApiWasapiEnumerator();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }

    virtual std::vector<RtAudio::DeviceInfo> listDevices(void) override;
private:
    std::optional<std::string> probeDeviceName(IMMDevice* devicePtr);

    bool coInitialized_ = false;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
};
