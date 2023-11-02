#include "WasapiNotificationHandler.h"
#include "utils.h"
#include <assert.h>

NotificationHandler::NotificationHandler(RtAudioDeviceCallbackLambda callback) : mCallback(callback) {
    assert(callback);
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDefaultDeviceChanged(
    EDataFlow flow,
    ERole     role,
    LPCWSTR   pwstrDefaultDeviceId
) {
    if (mCallback && role == eConsole) {
        mCallback(convertCharPointerToStdString(pwstrDefaultDeviceId), RtAudioDeviceParam::DEFAULT_CHANGED);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceAdded(
    LPCWSTR pwstrDeviceId
) {
    if (mCallback) {
        mCallback(convertCharPointerToStdString(pwstrDeviceId), RtAudioDeviceParam::DEVICE_ADDED);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceRemoved(
    LPCWSTR pwstrDeviceId
) {
    if (mCallback) {
        mCallback(convertCharPointerToStdString(pwstrDeviceId), RtAudioDeviceParam::DEVICE_REMOVED);
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NotificationHandler::OnDeviceStateChanged(
    LPCWSTR pwstrDeviceId,
    DWORD   dwNewState
) {
    if (mCallback) {
        mCallback(convertCharPointerToStdString(pwstrDeviceId), RtAudioDeviceParam::DEVICE_STATE_CHANGED);
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NotificationHandler::OnPropertyValueChanged(
    LPCWSTR           pwstrDeviceId,
    const PROPERTYKEY key
) {
    if (mCallback) {
        mCallback(convertCharPointerToStdString(pwstrDeviceId), RtAudioDeviceParam::DEVICE_PROPERTY_CHANGED);
    }
    return S_OK;
}
