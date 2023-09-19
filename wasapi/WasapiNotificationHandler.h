#pragma once
#include "RtAudio.h"
#include <mmdeviceapi.h>

class RtApiWasapi;

class NotificationHandler : public IMMNotificationClient {
private:
    RtAudioDeviceCallback callback_ = nullptr;
    void* userData_ = nullptr;
    RtApiWasapi* wasapi_;
public:
    NotificationHandler(RtApiWasapi* wasapi);
    void setCallback(RtAudioDeviceCallback callback, void* userData);
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole     role,
        LPCWSTR   pwstrDefaultDeviceId
    ) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(
        LPCWSTR pwstrDeviceId
    ) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(
        LPCWSTR pwstrDeviceId
    ) override;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
        LPCWSTR pwstrDeviceId,
        DWORD   dwNewState
    ) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR           pwstrDeviceId,
        const PROPERTYKEY key
    ) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override {
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef(void) override {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release(void) override {
        return 1;
    }
};
