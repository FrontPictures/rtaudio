#include "WasapiNotificationHandler.h"
#include "RtAudioWasapi.h"
#include "utils.h"

NotificationHandler::NotificationHandler(RtApiWasapi* wasapi) : wasapi_(wasapi) {

}

void NotificationHandler::setCallback(RtAudioDeviceCallback callback, void* userData) {
    callback_ = callback;
    userData_ = userData;
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDefaultDeviceChanged(
    EDataFlow flow,
    ERole     role,
    LPCWSTR   pwstrDefaultDeviceId
) {
    if (callback_ && role == eConsole && wasapi_) {
        auto busId = convertCharPointerToStdString(pwstrDefaultDeviceId);
        //auto device = wasapi_->getDeviceInfoByBusID(busId);
        callback_(0, RtAudioDeviceParam::DEFAULT_CHANGED, userData_);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceAdded(
    LPCWSTR pwstrDeviceId
) {
    if (callback_ && wasapi_ && pwstrDeviceId) {        
        auto busId = convertCharPointerToStdString(pwstrDeviceId);
        //auto device = wasapi_->getDeviceInfoByBusID(busId);
        callback_(0, RtAudioDeviceParam::DEVICE_ADDED, userData_);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceRemoved(
    LPCWSTR pwstrDeviceId
) {
    if (callback_ && wasapi_ && pwstrDeviceId) {
        auto busId = convertCharPointerToStdString(pwstrDeviceId);
        //auto device = wasapi_->getDeviceInfoByBusID(busId);
        callback_(0, RtAudioDeviceParam::DEVICE_REMOVED, userData_);
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceStateChanged(
    LPCWSTR pwstrDeviceId,
    DWORD   dwNewState
) {
    if (callback_ && wasapi_ && pwstrDeviceId) {
        auto busId = convertCharPointerToStdString(pwstrDeviceId);
        //auto device = wasapi_->getDeviceInfoByBusID(busId);
        callback_(0, RtAudioDeviceParam::DEVICE_STATE_CHANGED, userData_);
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NotificationHandler::OnPropertyValueChanged(
    LPCWSTR           pwstrDeviceId,
    const PROPERTYKEY key
) {
    if (callback_ && wasapi_ && pwstrDeviceId) {
        auto busId = convertCharPointerToStdString(pwstrDeviceId);
        //Need mutex here 
        //auto device = wasapi_->getDeviceInfoByBusID(busId);
        callback_(0, RtAudioDeviceParam::DEVICE_PROPERTY_CHANGED, userData_);
    }
    return S_OK;
}
