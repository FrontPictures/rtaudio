// Authored by Marcus Tomlinson <themarcustomlinson@gmail.com>, April 2014
// Updates for new device selection scheme by Gary Scavone, January 2022
// - Introduces support for the Windows WASAPI API
// - Aims to deliver bit streams to and from hardware at the lowest possible latency, via the absolute minimum buffer sizes required
// - Provides flexible stream configuration to an otherwise strict and inflexible WASAPI interface
// - Includes automatic internal conversion of sample rate and buffer size between hardware and the user

#include "RtAudioWasapi.h"
#include "WasapiResampler.h"
#include "WasapiBuffer.h"

namespace {
#ifndef INITGUID
#define INITGUID
#endif

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT _HRESULT_TYPEDEF_(0xc00d6d72)
#endif

#ifndef MFSTARTUP_NOSOCKET
#define MFSTARTUP_NOSOCKET 0x1
#endif

#ifdef _MSC_VER
#pragma comment( lib, "ksuser" )
#pragma comment( lib, "mfplat.lib" )
#pragma comment( lib, "mfuuid.lib" )
#pragma comment( lib, "wmcodecdspuuid" )
#endif

#ifndef __IAudioClient3_INTERFACE_DEFINED__
    MIDL_INTERFACE("00000000-0000-0000-0000-000000000000") IAudioClient3
    {
        virtual HRESULT GetSharedModeEnginePeriod(WAVEFORMATEX*, UINT32*, UINT32*, UINT32*, UINT32*) = 0;
        virtual HRESULT InitializeSharedAudioStream(DWORD, UINT32, WAVEFORMATEX*, LPCGUID) = 0;
        virtual HRESULT Release() = 0;
    };
#ifdef __CRT_UUID_DECL
    __CRT_UUID_DECL(IAudioClient3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#endif
#endif
        ;
    bool NegotiateExclusiveFormat(IAudioClient* renderAudioClient, WAVEFORMATEX** format) {
        HRESULT hr = S_OK;
        hr = renderAudioClient->GetMixFormat(format);
        if (FAILED(hr)) {
            goto negotiate_error;
        }
        hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, *format, nullptr);
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
            if ((*format)->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                (*format)->wFormatTag = WAVE_FORMAT_PCM;
                (*format)->wBitsPerSample = 16;
                (*format)->nBlockAlign = (*format)->nChannels * (*format)->wBitsPerSample / 8;
                (*format)->nAvgBytesPerSec = (*format)->nSamplesPerSec * (*format)->nBlockAlign;
            }
            else if ((*format)->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>((*format))->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>((*format));
                waveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                waveFormatExtensible->Format.wBitsPerSample = 16;
                waveFormatExtensible->Format.nBlockAlign = ((*format)->wBitsPerSample / 8) * (*format)->nChannels;
                waveFormatExtensible->Format.nAvgBytesPerSec = waveFormatExtensible->Format.nSamplesPerSec * waveFormatExtensible->Format.nBlockAlign;
                waveFormatExtensible->Samples.wValidBitsPerSample = 16;
            }
            else {
                goto negotiate_error;
            }
            hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, *format, nullptr);
        }
        if (FAILED(hr)) {
            goto negotiate_error;
        }
        return true;
    negotiate_error:
        if (*format) {
            CoTaskMemFree(*format);
            (*format) = nullptr;
        }
        return false;
    }

    RtAudioFormat GetRtAudioTypeFromWasapi(WAVEFORMATEX* format) {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
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

    typedef HANDLE(__stdcall* TAvSetMmThreadCharacteristicsPtr)(LPCWSTR TaskName, LPDWORD TaskIndex);

    // A structure to hold various information related to the WASAPI implementation.
    struct WasapiHandle
    {
        IAudioClient* captureAudioClient;
        IAudioClient* renderAudioClient;
        IAudioCaptureClient* captureClient;
        IAudioRenderClient* renderClient;
        HANDLE captureEvent;
        HANDLE renderEvent;
        AUDCLNT_SHAREMODE mode;
        WAVEFORMATEX* renderFormat;
        REFERENCE_TIME bufferDuration;

        WasapiHandle()
            : captureAudioClient(NULL),
            renderAudioClient(NULL),
            captureClient(NULL),
            renderClient(NULL),
            captureEvent(NULL),
            renderEvent(NULL),
            mode(AUDCLNT_SHAREMODE_SHARED),
            renderFormat(NULL),
            bufferDuration(0) {}
    };
}

RtApiWasapi::RtApiWasapi()
    : coInitialized_(false), deviceEnumerator_(NULL), wasapiNotificationHandler_(this)
{
    // WASAPI can run either apartment or multi-threaded
    HRESULT hr = CoInitialize(NULL);
    if (!FAILED(hr))
        coInitialized_ = true;

    // Instantiate device enumerator
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator_);

    // If this runs on an old Windows, it will fail. Ignore and proceed.
    if (FAILED(hr))
        deviceEnumerator_ = NULL;
}

RtApiWasapi::~RtApiWasapi()
{
    MUTEX_LOCK(&stream_.mutex);
    if (stream_.state != STREAM_CLOSED)
    {
        MUTEX_UNLOCK(&stream_.mutex);
        closeStream();
        MUTEX_LOCK(&stream_.mutex);
    }

    if (callbackExtra_)
        unregisterExtraCallback();
    SAFE_RELEASE(deviceEnumerator_);

    // If this object previously called CoInitialize()
    if (coInitialized_)
        CoUninitialize();
    MUTEX_UNLOCK(&stream_.mutex);
}

unsigned int RtApiWasapi::getDefaultInputDevice(void)
{
    IMMDevice* devicePtr = NULL;
    LPWSTR defaultId = NULL;
    std::string id;

    if (!deviceEnumerator_) return 0; // invalid ID
    errorText_.clear();

    // Get the default capture device Id.
    HRESULT hr = deviceEnumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &devicePtr);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::getDefaultInputDevice: Unable to retrieve default capture device handle.";
        goto Release;
    }

    hr = devicePtr->GetId(&defaultId);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::getDefaultInputDevice: Unable to get default capture device Id.";
        goto Release;
    }
    id = convertCharPointerToStdString(defaultId);

Release:
    SAFE_RELEASE(devicePtr);
    CoTaskMemFree(defaultId);

    if (!errorText_.empty()) {
        error(RTAUDIO_DRIVER_ERROR);
        return 0;
    }

    for (unsigned int m = 0; m < deviceIds_.size(); m++) {
        if (deviceIds_[m].first == id) {
            if (deviceList_[m].isDefaultInput == false) {
                deviceList_[m].isDefaultInput = true;
                for (unsigned int j = m + 1; j < deviceIds_.size(); j++) {
                    // make sure any remaining devices are not listed as the default
                    deviceList_[j].isDefaultInput = false;
                }
            }
            return deviceList_[m].ID;
        }
        deviceList_[m].isDefaultInput = false;
    }

    // If not found above, then do system probe of devices and try again.
    probeDevices();
    for (unsigned int m = 0; m < deviceIds_.size(); m++) {
        if (deviceIds_[m].first == id) return deviceList_[m].ID;
    }

    return 0;
}

unsigned int RtApiWasapi::getDefaultOutputDevice(void)
{
    IMMDevice* devicePtr = NULL;
    LPWSTR defaultId = NULL;
    std::string id;

    if (!deviceEnumerator_) return 0; // invalid ID
    errorText_.clear();

    // Get the default render device Id.
    HRESULT hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &devicePtr);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::getDefaultOutputDevice: Unable to retrieve default render device handle.";
        goto Release;
    }

    hr = devicePtr->GetId(&defaultId);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::getDefaultOutputDevice: Unable to get default render device Id.";
        goto Release;
    }
    id = convertCharPointerToStdString(defaultId);

Release:
    SAFE_RELEASE(devicePtr);
    CoTaskMemFree(defaultId);

    if (!errorText_.empty()) {
        error(RTAUDIO_DRIVER_ERROR);
        return 0;
    }

    for (unsigned int m = 0; m < deviceIds_.size(); m++) {
        if (deviceIds_[m].first == id) {
            if (deviceList_[m].isDefaultOutput == false) {
                deviceList_[m].isDefaultOutput = true;
                for (unsigned int j = m + 1; j < deviceIds_.size(); j++) {
                    // make sure any remaining devices are not listed as the default
                    deviceList_[j].isDefaultOutput = false;
                }
            }
            return deviceList_[m].ID;
        }
        deviceList_[m].isDefaultOutput = false;
    }

    // If not found above, then do system probe of devices and try again.
    probeDevices();
    for (unsigned int m = 0; m < deviceIds_.size(); m++) {
        if (deviceIds_[m].first == id) return deviceList_[m].ID;
    }

    return 0;
}

void RtApiWasapi::probeDevices(void)
{
    unsigned int captureDeviceCount = 0;
    unsigned int renderDeviceCount = 0;

    IMMDeviceCollection* captureDevices = NULL;
    IMMDeviceCollection* renderDevices = NULL;
    IMMDevice* devicePtr = NULL;

    LPWSTR defaultCaptureId = NULL;
    LPWSTR defaultRenderId = NULL;
    std::string defaultCaptureString;
    std::string defaultRenderString;

    unsigned int nDevices;
    bool isCaptureDevice = false;
    std::vector< std::pair< std::string, bool> > ids;
    LPWSTR deviceId = NULL;

    if (!deviceEnumerator_) return;
    errorText_.clear();

    // Count capture devices
    HRESULT hr = deviceEnumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &captureDevices);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve capture device collection.";
        goto Exit;
    }

    hr = captureDevices->GetCount(&captureDeviceCount);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve capture device count.";
        goto Exit;
    }

    // Count render devices
    hr = deviceEnumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &renderDevices);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve render device collection.";
        goto Exit;
    }

    hr = renderDevices->GetCount(&renderDeviceCount);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve render device count.";
        goto Exit;
    }

    nDevices = captureDeviceCount + renderDeviceCount;
    if (nDevices == 0) {
        errorText_ = "RtApiWasapi::probeDevices: No devices found.";
        goto Exit;
    }

    // Get the default capture device Id.
    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = devicePtr->GetId(&defaultCaptureId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default capture device Id.";
            goto Exit;
        }
        defaultCaptureString = convertCharPointerToStdString(defaultCaptureId);
    }

    // Get the default render device Id.
    SAFE_RELEASE(devicePtr);
    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = devicePtr->GetId(&defaultRenderId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default render device Id.";
            goto Exit;
        }
        defaultRenderString = convertCharPointerToStdString(defaultRenderId);
    }

    // Collect device IDs with mode.
    for (unsigned int n = 0; n < nDevices; n++) {
        SAFE_RELEASE(devicePtr);
        if (n < renderDeviceCount) {
            hr = renderDevices->Item(n, &devicePtr);
            if (FAILED(hr)) {
                errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve render device handle.";
                error(RTAUDIO_WARNING);
                continue;
            }
        }
        else {
            hr = captureDevices->Item(n - renderDeviceCount, &devicePtr);
            if (FAILED(hr)) {
                errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve capture device handle.";
                error(RTAUDIO_WARNING);
                continue;
            }
            isCaptureDevice = true;
        }

        hr = devicePtr->GetId(&deviceId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get device Id.";
            error(RTAUDIO_WARNING);
            continue;
        }

        ids.push_back(std::pair< std::string, bool>(convertCharPointerToStdString(deviceId), isCaptureDevice));
        CoTaskMemFree(deviceId);
    }

    // Fill or update the deviceList_ and also save a corresponding list of Ids.
    for (unsigned int n = 0; n < ids.size(); n++) {
        if (std::find(deviceIds_.begin(), deviceIds_.end(), ids[n]) != deviceIds_.end()) {
            continue; // We already have this device.
        }
        else { // There is a new device to probe.
            RtAudio::DeviceInfo info;
            std::wstring temp = std::wstring(ids[n].first.begin(), ids[n].first.end());
            if (probeDeviceInfo(info, (LPWSTR)temp.c_str(), ids[n].second) == false) continue; // ignore if probe fails
            deviceIds_.push_back(ids[n]);
            info.ID = currentDeviceId_++;  // arbitrary internal device ID
            deviceList_.push_back(info);
        }
    }

    // Remove any devices left in the list that are no longer available.
    unsigned int m;
    for (std::vector< std::pair< std::string, bool> >::iterator it = deviceIds_.begin(); it != deviceIds_.end(); ) {
        for (m = 0; m < ids.size(); m++) {
            if (ids[m] == *it) {
                ++it;
                break;
            }
        }
        if (m == ids.size()) { // not found so remove it from our two lists
            it = deviceIds_.erase(it);
            deviceList_.erase(deviceList_.begin() + distance(deviceIds_.begin(), it));
        }
    }

    // Set the default device flags in deviceList_.
    for (m = 0; m < deviceList_.size(); m++) {
        if (deviceIds_[m].first == defaultRenderString)
            deviceList_[m].isDefaultOutput = true;
        else
            deviceList_[m].isDefaultOutput = false;
        if (deviceIds_[m].first == defaultCaptureString)
            deviceList_[m].isDefaultInput = true;
        else
            deviceList_[m].isDefaultInput = false;
    }

Exit:
    // Release all references
    SAFE_RELEASE(captureDevices);
    SAFE_RELEASE(renderDevices);
    SAFE_RELEASE(devicePtr);

    CoTaskMemFree(defaultCaptureId);
    CoTaskMemFree(defaultRenderId);

    if (!errorText_.empty()) {
        deviceList_.clear();
        deviceIds_.clear();
        error(RTAUDIO_DRIVER_ERROR);
    }
    return;
}

bool RtApiWasapi::probeDeviceInfo(RtAudio::DeviceInfo& info, LPWSTR deviceId, bool isCaptureDevice)
{
    PROPVARIANT deviceNameProp;
    IMMDevice* devicePtr = NULL;
    IAudioClient* audioClient = NULL;
    IPropertyStore* devicePropStore = NULL;

    WAVEFORMATEX* deviceFormat = NULL;
    WAVEFORMATEX* closestMatchFormat = NULL;

    errorText_.clear();
    RtAudioErrorType errorType = RTAUDIO_DRIVER_ERROR;

    // Get the device pointer from the device Id
    HRESULT hr = deviceEnumerator_->GetDevice(deviceId, &devicePtr);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device handle.";
        goto Exit;
    }

    // Get device name
    hr = devicePtr->OpenPropertyStore(STGM_READ, &devicePropStore);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to open device property store.";
        goto Exit;
    }

    PropVariantInit(&deviceNameProp);

    hr = devicePropStore->GetValue(PKEY_Device_FriendlyName, &deviceNameProp);
    if (FAILED(hr) || deviceNameProp.pwszVal == nullptr) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device property: PKEY_Device_FriendlyName.";
        goto Exit;
    }

    info.name = convertCharPointerToStdString(deviceNameProp.pwszVal);
    info.busID = convertCharPointerToStdString(deviceId);

    // Get audio client
    hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device audio client.";
        goto Exit;
    }

    hr = audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device mix format.";
        goto Exit;
    }

    // Set channel count
    if (isCaptureDevice) {
        info.inputChannels = deviceFormat->nChannels;
        info.outputChannels = 0;
        info.duplexChannels = 0;
    }
    else {
        info.inputChannels = 0;
        info.outputChannels = deviceFormat->nChannels;
        info.duplexChannels = 0;
    }

    // Set sample rates
    info.sampleRates.clear();

    // Allow support for all sample rates as we have a built-in sample rate converter.
    for (unsigned int i = 0; i < MAX_SAMPLE_RATES; i++) {
        info.sampleRates.push_back(SAMPLE_RATES[i]);
    }
    info.preferredSampleRate = deviceFormat->nSamplesPerSec;

    // Set native formats
    info.nativeFormats = 0;

    if (deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
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
            ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM))
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

Exit:
    // Release all references
    PropVariantClear(&deviceNameProp);

    SAFE_RELEASE(devicePtr);
    SAFE_RELEASE(audioClient);
    SAFE_RELEASE(devicePropStore);

    CoTaskMemFree(deviceFormat);
    CoTaskMemFree(closestMatchFormat);

    if (!errorText_.empty()) {
        error(errorType);
        return false;
    }
    return true;
}

void RtApiWasapi::closeStream(void)
{
    MUTEX_LOCK(&stream_.mutex);
    if (stream_.state == STREAM_CLOSED) {
        errorText_ = "RtApiWasapi::closeStream: No open stream to close.";
        error(RTAUDIO_WARNING);
        MUTEX_UNLOCK(&stream_.mutex);
        return;
    }

    if (stream_.state != STREAM_STOPPED)
    {
        MUTEX_UNLOCK(&stream_.mutex);
        stopStream();
        MUTEX_LOCK(&stream_.mutex);
    }

    // clean up stream memory
    if (stream_.apiHandle) {
        SAFE_RELEASE(((WasapiHandle*)stream_.apiHandle)->captureClient);
        SAFE_RELEASE(((WasapiHandle*)stream_.apiHandle)->renderClient);
        SAFE_RELEASE(((WasapiHandle*)stream_.apiHandle)->captureAudioClient)
            SAFE_RELEASE(((WasapiHandle*)stream_.apiHandle)->renderAudioClient);
        if (((WasapiHandle*)stream_.apiHandle)->renderFormat) {
            CoTaskMemFree(((WasapiHandle*)stream_.apiHandle)->renderFormat);
            ((WasapiHandle*)stream_.apiHandle)->renderFormat = NULL;
        }

        if (((WasapiHandle*)stream_.apiHandle)->captureEvent)
            CloseHandle(((WasapiHandle*)stream_.apiHandle)->captureEvent);

        if (((WasapiHandle*)stream_.apiHandle)->renderEvent)
            CloseHandle(((WasapiHandle*)stream_.apiHandle)->renderEvent);

        delete (WasapiHandle*)stream_.apiHandle;
        stream_.apiHandle = NULL;
    }

    for (int i = 0; i < 2; i++) {
        if (stream_.userBuffer[i]) {
            free(stream_.userBuffer[i]);
            stream_.userBuffer[i] = 0;
        }
    }

    if (stream_.deviceBuffer) {
        free(stream_.deviceBuffer);
        stream_.deviceBuffer = 0;
    }

    clearStreamInfo();
    MUTEX_UNLOCK(&stream_.mutex);
}

RtAudioErrorType RtApiWasapi::startStream(void)
{
    MUTEX_LOCK(&stream_.mutex);
    if (stream_.state != STREAM_STOPPED) {
        if (stream_.state == STREAM_RUNNING)
            errorText_ = "RtApiWasapi::startStream(): the stream is already running!";
        else if (stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiWasapi::startStream(): the stream is stopping or closed!";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_WARNING);
    }

    /*
    #if defined( HAVE_GETTIMEOFDAY )
    gettimeofday( &stream_.lastTickTimestamp, NULL );
    #endif
    */

    // update stream state
    stream_.state = STREAM_RUNNING;

    // create WASAPI stream thread
    stream_.callbackInfo.thread = (ThreadHandle)CreateThread(NULL, 0, runWasapiThread, this, CREATE_SUSPENDED, NULL);

    if (!stream_.callbackInfo.thread) {
        errorText_ = "RtApiWasapi::startStream: Unable to instantiate callback thread.";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_THREAD_ERROR);
    }
    else {
        SetThreadPriority((void*)stream_.callbackInfo.thread, stream_.callbackInfo.priority);
        ResumeThread((void*)stream_.callbackInfo.thread);
    }
    MUTEX_UNLOCK(&stream_.mutex);
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiWasapi::stopStream(void)
{
    MUTEX_LOCK(&stream_.mutex);
    if (stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING) {
        if (stream_.state == STREAM_STOPPED)
            errorText_ = "RtApiWasapi::stopStream(): the stream is already stopped!";
        else if (stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiWasapi::stopStream(): the stream is closed!";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_WARNING);
    }

    // inform stream thread by setting stream state to STREAM_STOPPING
    stream_.state = STREAM_STOPPING;

    WaitForSingleObject((void*)stream_.callbackInfo.thread, INFINITE);

    // close thread handle
    if (stream_.callbackInfo.thread && !CloseHandle((void*)stream_.callbackInfo.thread)) {
        errorText_ = "RtApiWasapi::stopStream: Unable to close callback thread.";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_THREAD_ERROR);
    }

    stream_.callbackInfo.thread = (ThreadHandle)NULL;
    MUTEX_UNLOCK(&stream_.mutex);
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiWasapi::abortStream(void)
{
    MUTEX_LOCK(&stream_.mutex);
    if (stream_.state != STREAM_RUNNING) {
        if (stream_.state == STREAM_STOPPED)
            errorText_ = "RtApiWasapi::abortStream(): the stream is already stopped!";
        else if (stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiWasapi::abortStream(): the stream is stopping or closed!";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_WARNING);
    }

    // inform stream thread by setting stream state to STREAM_STOPPING
    stream_.state = STREAM_STOPPING;

    WaitForSingleObject((void*)stream_.callbackInfo.thread, INFINITE);

    // close thread handle
    if (stream_.callbackInfo.thread && !CloseHandle((void*)stream_.callbackInfo.thread)) {
        errorText_ = "RtApiWasapi::abortStream: Unable to close callback thread.";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_THREAD_ERROR);
    }

    stream_.callbackInfo.thread = (ThreadHandle)NULL;
    MUTEX_UNLOCK(&stream_.mutex);
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiWasapi::registerExtraCallback(RtAudioDeviceCallback callback, void* userData)
{
    if (callbackExtra_) {
        return RTAUDIO_INVALID_USE;
    }
    callbackExtra_ = callback;
    wasapiNotificationHandler_.setCallback(callbackExtra_, userData);
    HRESULT hr = deviceEnumerator_->RegisterEndpointNotificationCallback(&wasapiNotificationHandler_);
    if (FAILED(hr)) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiWasapi::unregisterExtraCallback()
{
    if (!callbackExtra_) {
        return RTAUDIO_INVALID_USE;
    }
    callbackExtra_ = nullptr;
    HRESULT hr = deviceEnumerator_->UnregisterEndpointNotificationCallback(&wasapiNotificationHandler_);
    if (FAILED(hr)) {
        return RTAUDIO_SYSTEM_ERROR;
    }
    return RTAUDIO_NO_ERROR;
}

bool RtApiWasapi::probeDeviceOpen(unsigned int deviceId, StreamMode mode, unsigned int channels,
    unsigned int firstChannel, unsigned int sampleRate,
    RtAudioFormat format, unsigned int* bufferSize,
    RtAudio::StreamOptions* options)
{
    MUTEX_LOCK(&stream_.mutex);
    bool methodResult = FAILURE;
    IMMDevice* devicePtr = NULL;
    WAVEFORMATEX* deviceFormat = NULL;
    unsigned int bufferBytes;
    stream_.state = STREAM_STOPPED;
    bool isInput = false;
    std::string id;

    unsigned int deviceIdx;
    for (deviceIdx = 0; deviceIdx < deviceList_.size(); deviceIdx++) {
        if (deviceList_[deviceIdx].ID == deviceId) {
            id = deviceIds_[deviceIdx].first;
            if (deviceIds_[deviceIdx].second) isInput = true;
            break;
        }
    }

    errorText_.clear();
    RtAudioErrorType errorType = RTAUDIO_INVALID_USE;
    if (id.empty()) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: the device ID was not found!";
        MUTEX_UNLOCK(&stream_.mutex);
        return FAILURE;
    }

    if (isInput && mode != INPUT) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: deviceId specified does not support output mode.";
        MUTEX_UNLOCK(&stream_.mutex);
        return FAILURE;
    }

    // Get the device pointer from the device Id
    errorType = RTAUDIO_DRIVER_ERROR;
    std::wstring temp = std::wstring(id.begin(), id.end());
    HRESULT hr = deviceEnumerator_->GetDevice((LPWSTR)temp.c_str(), &devicePtr);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device handle.";
        MUTEX_UNLOCK(&stream_.mutex);
        return FAILURE;
    }

    // Create API handle if not already created.
    if (!stream_.apiHandle)
        stream_.apiHandle = (void*) new WasapiHandle();

    if (isInput) {
        IAudioClient*& captureAudioClient = ((WasapiHandle*)stream_.apiHandle)->captureAudioClient;

        hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            NULL, (void**)&captureAudioClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve capture device audio client.";
            goto Exit;
        }

        hr = captureAudioClient->GetMixFormat(&deviceFormat);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve capture device mix format.";
            goto Exit;
        }

        stream_.nDeviceChannels[mode] = deviceFormat->nChannels;
        captureAudioClient->GetStreamLatency((long long*)&stream_.latency[mode]);
    }

    // If an output device and is configured for loopback (input mode)
    if (isInput == false && mode == INPUT) {
        // If renderAudioClient is not initialised, initialise it now
        IAudioClient*& renderAudioClient = ((WasapiHandle*)stream_.apiHandle)->renderAudioClient;
        if (!renderAudioClient) {
            MUTEX_UNLOCK(&stream_.mutex);
            probeDeviceOpen(deviceId, OUTPUT, channels, firstChannel, sampleRate, format, bufferSize, options);
            MUTEX_LOCK(&stream_.mutex);
        }

        // Retrieve captureAudioClient from our stream handle.
        IAudioClient*& captureAudioClient = ((WasapiHandle*)stream_.apiHandle)->captureAudioClient;

        hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            NULL, (void**)&captureAudioClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve render device audio client.";
            goto Exit;
        }

        hr = captureAudioClient->GetMixFormat(&deviceFormat);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve render device mix format.";
            goto Exit;
        }

        stream_.nDeviceChannels[mode] = deviceFormat->nChannels;
        captureAudioClient->GetStreamLatency((long long*)&stream_.latency[mode]);
    }

    // If output device and is configured for output.
    if (isInput == false && mode == OUTPUT) {
        // If renderAudioClient is already initialised, don't initialise it again
        IAudioClient*& renderAudioClient = ((WasapiHandle*)stream_.apiHandle)->renderAudioClient;
        if (renderAudioClient) {
            methodResult = SUCCESS;
            goto Exit;
        }

        hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            NULL, (void**)&renderAudioClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve render device audio client.";
            goto Exit;
        }

        hr = renderAudioClient->GetMixFormat(&deviceFormat);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve render device mix format.";
            goto Exit;
        }

        stream_.nDeviceChannels[mode] = deviceFormat->nChannels;
        renderAudioClient->GetStreamLatency((long long*)&stream_.latency[mode]);
    }

    if (options && options->flags & RTAUDIO_HOG_DEVICE) {
        if (isInput == false && mode == INPUT) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Exclusive device not supports loopback.";
            goto Exit;
        }
        REFERENCE_TIME defaultBufferDuration = 0;
        REFERENCE_TIME minimumBufferDuration = 0;
        IAudioClient* audioClient = NULL;

        if (isInput) {
            audioClient = ((WasapiHandle*)stream_.apiHandle)->captureAudioClient;
        }
        else {
            audioClient = ((WasapiHandle*)stream_.apiHandle)->renderAudioClient;
        }
        if (!audioClient) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Audio client is null";
            goto Exit;
        }

        hr = audioClient->GetDevicePeriod(&defaultBufferDuration, &minimumBufferDuration);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device period.";
            goto Exit;
        }

        bool res = NegotiateExclusiveFormat(audioClient, &deviceFormat);
        if (res == false) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to negotiate format for exclusive device.";
            goto Exit;
        }
        if (sampleRate != deviceFormat->nSamplesPerSec) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: samplerate exclusive mismatch.";
            goto Exit;
        }
        REFERENCE_TIME userBufferSize = ((uint64_t)(*bufferSize) * 10000000 / deviceFormat->nSamplesPerSec);
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
            deviceFormat,
            NULL);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 nFrames = 0;
            hr = audioClient->GetBufferSize(&nFrames);
            if (FAILED(hr)) {
                errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.";
                goto Exit;
            }
            constexpr int REFTIMES_PER_SEC = 10000000;
            userBufferSize = (REFERENCE_TIME)((double)REFTIMES_PER_SEC / deviceFormat->nSamplesPerSec * nFrames + 0.5);
            hr = audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                userBufferSize,
                userBufferSize,
                deviceFormat,
                NULL);
        }
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to open device.";
            goto Exit;
        }

        UINT32 nFrames = 0;
        hr = audioClient->GetBufferSize(&nFrames);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.";
            goto Exit;
        }
        *bufferSize = nFrames;
        ((WasapiHandle*)stream_.apiHandle)->mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        if (((WasapiHandle*)stream_.apiHandle)->renderFormat) {
            CoTaskMemFree(((WasapiHandle*)stream_.apiHandle)->renderFormat);
            ((WasapiHandle*)stream_.apiHandle)->renderFormat = NULL;
        }
        ((WasapiHandle*)stream_.apiHandle)->renderFormat = deviceFormat;
        ((WasapiHandle*)stream_.apiHandle)->bufferDuration = userBufferSize;
    }

    // Fill stream data
    if ((stream_.mode == OUTPUT && mode == INPUT) ||
        (stream_.mode == INPUT && mode == OUTPUT)) {
        stream_.mode = DUPLEX;
    }
    else {
        stream_.mode = mode;
    }

    stream_.deviceId[mode] = deviceId;
    stream_.doByteSwap[mode] = false;
    stream_.sampleRate = sampleRate;
    stream_.bufferSize = *bufferSize;
    stream_.nBuffers = 1;
    stream_.nUserChannels[mode] = channels;
    stream_.channelOffset[mode] = firstChannel;
    stream_.userFormat = format;
    if (((WasapiHandle*)stream_.apiHandle)->mode == AUDCLNT_SHAREMODE_SHARED) {
        stream_.deviceFormat[mode] = deviceList_[deviceIdx].nativeFormats;
    }
    else {
        stream_.deviceFormat[mode] = GetRtAudioTypeFromWasapi(((WasapiHandle*)stream_.apiHandle)->renderFormat);
        if (stream_.deviceFormat[mode] == 0) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Hardware audio format not implemented.";
            goto Exit;
        }
    }

    if (options && options->flags & RTAUDIO_NONINTERLEAVED)
        stream_.userInterleaved = false;
    else
        stream_.userInterleaved = true;
    stream_.deviceInterleaved[mode] = true;

    // Set flags for buffer conversion.
    stream_.doConvertBuffer[mode] = false;
    if (stream_.userFormat != stream_.deviceFormat[mode] ||
        stream_.nUserChannels[0] != stream_.nDeviceChannels[0] ||
        stream_.nUserChannels[1] != stream_.nDeviceChannels[1])
        stream_.doConvertBuffer[mode] = true;
    else if (stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
        stream_.nUserChannels[mode] > 1)
        stream_.doConvertBuffer[mode] = true;

    if (stream_.doConvertBuffer[mode])
        setConvertInfo(mode, firstChannel);

    // Allocate necessary internal buffers
    bufferBytes = stream_.nUserChannels[mode] * stream_.bufferSize * formatBytes(stream_.userFormat);

    stream_.userBuffer[mode] = (char*)calloc(bufferBytes, 1);
    if (!stream_.userBuffer[mode]) {
        errorType = RTAUDIO_MEMORY_ERROR;
        errorText_ = "RtApiWasapi::probeDeviceOpen: Error allocating user buffer memory.";
        goto Exit;
    }

    if (options && options->flags & RTAUDIO_SCHEDULE_REALTIME)
        stream_.callbackInfo.priority = 15;
    else
        stream_.callbackInfo.priority = 0;

    ///! TODO: RTAUDIO_MINIMIZE_LATENCY // Provide stream buffers directly to callback
    ///! TODO: RTAUDIO_HOG_DEVICE       // Exclusive mode  

    methodResult = SUCCESS;

Exit:
    //clean up
    SAFE_RELEASE(devicePtr);

    // if method failed, close the stream
    if (methodResult == FAILURE)
    {
        MUTEX_UNLOCK(&stream_.mutex);
        closeStream();
        MUTEX_LOCK(&stream_.mutex);
    }

    if (!errorText_.empty())
        error(errorType);

    MUTEX_UNLOCK(&stream_.mutex);
    return methodResult;
}

DWORD WINAPI RtApiWasapi::runWasapiThread(void* wasapiPtr)
{
    if (wasapiPtr)
        ((RtApiWasapi*)wasapiPtr)->wasapiThread();

    return 0;
}

DWORD WINAPI RtApiWasapi::stopWasapiThread(void* wasapiPtr)
{
    if (wasapiPtr)
        ((RtApiWasapi*)wasapiPtr)->stopStream();

    return 0;
}

DWORD WINAPI RtApiWasapi::abortWasapiThread(void* wasapiPtr)
{
    if (wasapiPtr)
        ((RtApiWasapi*)wasapiPtr)->abortStream();

    return 0;
}

void RtApiWasapi::wasapiThread()
{
    // as this is a new thread, we must CoInitialize it
    CoInitialize(NULL);

    HRESULT hr;

    IAudioClient* captureAudioClient = ((WasapiHandle*)stream_.apiHandle)->captureAudioClient;
    IAudioClient* renderAudioClient = ((WasapiHandle*)stream_.apiHandle)->renderAudioClient;
    IAudioClient3* renderAudioClient3 = nullptr;
    IAudioCaptureClient* captureClient = ((WasapiHandle*)stream_.apiHandle)->captureClient;
    IAudioClient3* captureAudioClient3 = nullptr;
    IAudioRenderClient* renderClient = ((WasapiHandle*)stream_.apiHandle)->renderClient;
    HANDLE captureEvent = ((WasapiHandle*)stream_.apiHandle)->captureEvent;
    HANDLE renderEvent = ((WasapiHandle*)stream_.apiHandle)->renderEvent;
    AUDCLNT_SHAREMODE shareMode = ((WasapiHandle*)stream_.apiHandle)->mode;
    WAVEFORMATEX* exclusiveFormat = ((WasapiHandle*)stream_.apiHandle)->renderFormat;

    WAVEFORMATEX* captureFormat = NULL;
    WAVEFORMATEX* renderFormat = NULL;
    float captureSrRatio = 0.0f;
    float renderSrRatio = 0.0f;
    WasapiBuffer captureBuffer;
    WasapiBuffer renderBuffer;
    WasapiResampler* captureResampler = NULL;
    WasapiResampler* renderResampler = NULL;

    // declare local stream variables
    RtAudioCallback callback = (RtAudioCallback)stream_.callbackInfo.callback;
    BYTE* streamBuffer = NULL;
    DWORD captureFlags = 0;
    unsigned int bufferFrameCount = 0;
    unsigned int numFramesPadding = 0;
    unsigned int convBufferSize = 0;
    bool loopbackEnabled = stream_.deviceId[INPUT] == stream_.deviceId[OUTPUT];
    bool callbackPushed = true;
    bool callbackPulled = false;
    bool callbackStopped = false;
    int callbackResult = 0;

    // convBuffer is used to store converted buffers between WASAPI and the user
    char* convBuffer = NULL;
    unsigned int convBuffSize = 0;
    unsigned int deviceBuffSize = 0;

    std::string errorText;
    RtAudioErrorType errorType = RTAUDIO_DRIVER_ERROR;

    REFERENCE_TIME defaultBufferDuration = ((WasapiHandle*)stream_.apiHandle)->bufferDuration;

    // Attempt to assign "Pro Audio" characteristic to thread
    HMODULE AvrtDll = LoadLibraryW(L"AVRT.dll");
    if (AvrtDll) {
        DWORD taskIndex = 0;
        TAvSetMmThreadCharacteristicsPtr AvSetMmThreadCharacteristicsPtr =
            (TAvSetMmThreadCharacteristicsPtr)(void(*)()) GetProcAddress(AvrtDll, "AvSetMmThreadCharacteristicsW");
        AvSetMmThreadCharacteristicsPtr(L"Pro Audio", &taskIndex);
        FreeLibrary(AvrtDll);
    }

    // start capture stream if applicable
    if (captureAudioClient) {
        hr = captureAudioClient->GetMixFormat(&captureFormat);
        if (FAILED(hr)) {
            errorText = "RtApiWasapi::wasapiThread: Unable to retrieve device mix format.";
            goto Exit;
        }

        // init captureResampler
        captureResampler = new WasapiResampler(stream_.deviceFormat[INPUT] == RTAUDIO_FLOAT32 || stream_.deviceFormat[INPUT] == RTAUDIO_FLOAT64,
            formatBytes(stream_.deviceFormat[INPUT]) * 8, stream_.nDeviceChannels[INPUT],
            captureFormat->nSamplesPerSec, stream_.sampleRate);

        captureSrRatio = ((float)captureFormat->nSamplesPerSec / stream_.sampleRate);

        if (!captureClient) {
            captureAudioClient->QueryInterface(__uuidof(IAudioClient3), (void**)&captureAudioClient3);
            if (captureAudioClient3 && !loopbackEnabled && shareMode == AUDCLNT_SHAREMODE_SHARED)
            {
                UINT32 Ignore;
                UINT32 MinPeriodInFrames;
                hr = captureAudioClient3->GetSharedModeEnginePeriod(captureFormat,
                    &Ignore,
                    &Ignore,
                    &MinPeriodInFrames,
                    &Ignore);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to initialize capture audio client.";
                    goto Exit;
                }

                hr = captureAudioClient3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    MinPeriodInFrames,
                    captureFormat,
                    NULL);
                SAFE_RELEASE(captureAudioClient3);
            }
            else if (shareMode == AUDCLNT_SHAREMODE_SHARED)
            {
                hr = captureAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                    loopbackEnabled ? AUDCLNT_STREAMFLAGS_LOOPBACK : AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    0,
                    0,
                    captureFormat,
                    NULL);
            }

            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to initialize capture audio client.";
                goto Exit;
            }

            hr = captureAudioClient->GetService(__uuidof(IAudioCaptureClient),
                (void**)&captureClient);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve capture client handle.";
                goto Exit;
            }

            // don't configure captureEvent if in loopback mode
            if (!loopbackEnabled)
            {
                // configure captureEvent to trigger on every available capture buffer
                captureEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
                if (!captureEvent) {
                    errorType = RTAUDIO_SYSTEM_ERROR;
                    errorText = "RtApiWasapi::wasapiThread: Unable to create capture event.";
                    goto Exit;
                }

                hr = captureAudioClient->SetEventHandle(captureEvent);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to set capture event handle.";
                    goto Exit;
                }

                ((WasapiHandle*)stream_.apiHandle)->captureEvent = captureEvent;
            }

            ((WasapiHandle*)stream_.apiHandle)->captureClient = captureClient;

            // reset the capture stream
            hr = captureAudioClient->Reset();
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to reset capture stream.";
                goto Exit;
            }

            // start the capture stream
            hr = captureAudioClient->Start();
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to start capture stream.";
                goto Exit;
            }
        }

        unsigned int inBufferSize = 0;
        hr = captureAudioClient->GetBufferSize(&inBufferSize);
        if (FAILED(hr)) {
            errorText = "RtApiWasapi::wasapiThread: Unable to get capture buffer size.";
            goto Exit;
        }

        // scale outBufferSize according to stream->user sample rate ratio
        unsigned int outBufferSize = (unsigned int)ceilf(stream_.bufferSize * captureSrRatio) * stream_.nDeviceChannels[INPUT];
        inBufferSize *= stream_.nDeviceChannels[INPUT];

        // set captureBuffer size
        captureBuffer.setBufferSize(inBufferSize + outBufferSize, formatBytes(stream_.deviceFormat[INPUT]));
    }

    // start render stream if applicable
    if (renderAudioClient) {
        hr = renderAudioClient->GetMixFormat(&renderFormat);
        if (FAILED(hr)) {
            errorText = "RtApiWasapi::wasapiThread: Unable to retrieve device mix format.";
            goto Exit;
        }

        // init renderResampler
        renderResampler = new WasapiResampler(stream_.deviceFormat[OUTPUT] == RTAUDIO_FLOAT32 || stream_.deviceFormat[OUTPUT] == RTAUDIO_FLOAT64,
            formatBytes(stream_.deviceFormat[OUTPUT]) * 8, stream_.nDeviceChannels[OUTPUT],
            stream_.sampleRate, renderFormat->nSamplesPerSec);

        renderSrRatio = ((float)renderFormat->nSamplesPerSec / stream_.sampleRate);

        if (!renderClient) {

            renderAudioClient->QueryInterface(__uuidof(IAudioClient3), (void**)&renderAudioClient3);
            if (renderAudioClient3 && shareMode == AUDCLNT_SHAREMODE_SHARED)
            {
                UINT32 Ignore;
                UINT32 MinPeriodInFrames;
                hr = renderAudioClient3->GetSharedModeEnginePeriod(renderFormat,
                    &Ignore,
                    &Ignore,
                    &MinPeriodInFrames,
                    &Ignore);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to initialize render audio client.";
                    goto Exit;
                }

                hr = renderAudioClient3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    MinPeriodInFrames,
                    renderFormat,
                    NULL);
                SAFE_RELEASE(renderAudioClient3);
            }
            else if (shareMode == AUDCLNT_SHAREMODE_SHARED)
            {
                hr = renderAudioClient->Initialize(shareMode,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE ? defaultBufferDuration : 0,
                    shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE ? defaultBufferDuration : 0,
                    exclusiveFormat,
                    NULL);
            }
            if (FAILED(hr) && (hr != AUDCLNT_E_ALREADY_INITIALIZED && shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to initialize render audio client.";
                goto Exit;
            }

            hr = renderAudioClient->GetService(__uuidof(IAudioRenderClient),
                (void**)&renderClient);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render client handle.";
                goto Exit;
            }

            // configure renderEvent to trigger on every available render buffer
            renderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            if (!renderEvent) {
                errorType = RTAUDIO_SYSTEM_ERROR;
                errorText = "RtApiWasapi::wasapiThread: Unable to create render event.";
                goto Exit;
            }

            hr = renderAudioClient->SetEventHandle(renderEvent);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to set render event handle.";
                goto Exit;
            }

            ((WasapiHandle*)stream_.apiHandle)->renderClient = renderClient;
            ((WasapiHandle*)stream_.apiHandle)->renderEvent = renderEvent;

            // reset the render stream
            hr = renderAudioClient->Reset();
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to reset render stream.";
                goto Exit;
            }

            // start the render stream
            {
                BYTE* pData = nullptr;
                UINT32 bufferSize = 0;
                renderAudioClient->GetBufferSize(&bufferSize);
                hr = renderClient->GetBuffer(bufferSize, &pData);
                hr = renderClient->ReleaseBuffer(bufferSize, 0);
            }
            hr = renderAudioClient->Start();
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to start render stream.";
                goto Exit;
            }
        }

        unsigned int outBufferSize = 0;
        hr = renderAudioClient->GetBufferSize(&outBufferSize);
        if (FAILED(hr)) {
            errorText = "RtApiWasapi::wasapiThread: Unable to get render buffer size.";
            goto Exit;
        }

        // scale inBufferSize according to user->stream sample rate ratio
        unsigned int inBufferSize = (unsigned int)ceilf(stream_.bufferSize * renderSrRatio) * stream_.nDeviceChannels[OUTPUT];
        outBufferSize *= stream_.nDeviceChannels[OUTPUT];

        // set renderBuffer size
        renderBuffer.setBufferSize(inBufferSize + outBufferSize, formatBytes(stream_.deviceFormat[OUTPUT]));
    }

    // malloc buffer memory
    if (stream_.mode == INPUT)
    {
        using namespace std; // for ceilf
        convBuffSize = (unsigned int)(ceilf(stream_.bufferSize * captureSrRatio)) * stream_.nDeviceChannels[INPUT] * formatBytes(stream_.deviceFormat[INPUT]);
        deviceBuffSize = stream_.bufferSize * stream_.nDeviceChannels[INPUT] * formatBytes(stream_.deviceFormat[INPUT]);
    }
    else if (stream_.mode == OUTPUT)
    {
        convBuffSize = (unsigned int)(ceilf(stream_.bufferSize * renderSrRatio)) * stream_.nDeviceChannels[OUTPUT] * formatBytes(stream_.deviceFormat[OUTPUT]);
        deviceBuffSize = stream_.bufferSize * stream_.nDeviceChannels[OUTPUT] * formatBytes(stream_.deviceFormat[OUTPUT]);
    }
    else if (stream_.mode == DUPLEX)
    {
        convBuffSize = std::max((unsigned int)(ceilf(stream_.bufferSize * captureSrRatio)) * stream_.nDeviceChannels[INPUT] * formatBytes(stream_.deviceFormat[INPUT]),
            (unsigned int)(ceilf(stream_.bufferSize * renderSrRatio)) * stream_.nDeviceChannels[OUTPUT] * formatBytes(stream_.deviceFormat[OUTPUT]));
        deviceBuffSize = std::max(stream_.bufferSize * stream_.nDeviceChannels[INPUT] * formatBytes(stream_.deviceFormat[INPUT]),
            stream_.bufferSize * stream_.nDeviceChannels[OUTPUT] * formatBytes(stream_.deviceFormat[OUTPUT]));
    }

    convBuffSize *= 2; // allow overflow for *SrRatio remainders
    convBuffer = (char*)calloc(convBuffSize, 1);
    stream_.deviceBuffer = (char*)calloc(deviceBuffSize, 1);
    if (!convBuffer || !stream_.deviceBuffer) {
        errorType = RTAUDIO_MEMORY_ERROR;
        errorText = "RtApiWasapi::wasapiThread: Error allocating device buffer memory.";
        goto Exit;
    }

    // stream process loop
    while (stream_.state != STREAM_STOPPING) {
        if (!callbackPulled) {
            // Callback Input
            // ==============
            // 1. Pull callback buffer from inputBuffer
            // 2. If 1. was successful: Convert callback buffer to user sample rate and channel count
            //                          Convert callback buffer to user format

            if (captureAudioClient)
            {
                int samplesToPull = (unsigned int)floorf(stream_.bufferSize * captureSrRatio);

                convBufferSize = 0;
                while (convBufferSize < stream_.bufferSize)
                {
                    // Pull callback buffer from inputBuffer
                    callbackPulled = captureBuffer.pullBuffer(convBuffer,
                        samplesToPull * stream_.nDeviceChannels[INPUT],
                        stream_.deviceFormat[INPUT]);

                    if (!callbackPulled)
                    {
                        break;
                    }

                    // Convert callback buffer to user sample rate
                    unsigned int deviceBufferOffset = convBufferSize * stream_.nDeviceChannels[INPUT] * formatBytes(stream_.deviceFormat[INPUT]);
                    unsigned int convSamples = 0;

                    captureResampler->Convert(stream_.deviceBuffer + deviceBufferOffset,
                        convBuffer,
                        samplesToPull,
                        convSamples,
                        convBufferSize == 0 ? -1 : stream_.bufferSize - convBufferSize);

                    convBufferSize += convSamples;
                    samplesToPull = 1; // now pull one sample at a time until we have stream_.bufferSize samples
                }

                if (callbackPulled)
                {
                    if (stream_.doConvertBuffer[INPUT]) {
                        // Convert callback buffer to user format
                        convertBuffer(stream_.userBuffer[INPUT],
                            stream_.deviceBuffer,
                            stream_.convertInfo[INPUT]);
                    }
                    else {
                        // no further conversion, simple copy deviceBuffer to userBuffer
                        memcpy(stream_.userBuffer[INPUT],
                            stream_.deviceBuffer,
                            stream_.bufferSize * stream_.nUserChannels[INPUT] * formatBytes(stream_.userFormat));
                    }
                }
            }
            else {
                // if there is no capture stream, set callbackPulled flag
                callbackPulled = true;
            }

            // Execute Callback
            // ================
            // 1. Execute user callback method
            // 2. Handle return value from callback

            // if callback has not requested the stream to stop
            if (callbackPulled && !callbackStopped) {
                // Execute user callback method
                callbackResult = callback(stream_.userBuffer[OUTPUT],
                    stream_.userBuffer[INPUT],
                    stream_.bufferSize,
                    getStreamTime(),
                    captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY ? RTAUDIO_INPUT_OVERFLOW : 0,
                    stream_.callbackInfo.userData);

                // tick stream time
                RtApi::tickStreamTime();

                // Handle return value from callback
                if (callbackResult == 1) {
                    // instantiate a thread to stop this thread
                    HANDLE threadHandle = CreateThread(NULL, 0, stopWasapiThread, this, 0, NULL);
                    if (!threadHandle) {
                        errorType = RTAUDIO_THREAD_ERROR;
                        errorText = "RtApiWasapi::wasapiThread: Unable to instantiate stream stop thread.";
                        goto Exit;
                    }
                    else if (!CloseHandle(threadHandle)) {
                        errorType = RTAUDIO_THREAD_ERROR;
                        errorText = "RtApiWasapi::wasapiThread: Unable to close stream stop thread handle.";
                        goto Exit;
                    }

                    callbackStopped = true;
                }
                else if (callbackResult == 2) {
                    // instantiate a thread to stop this thread
                    HANDLE threadHandle = CreateThread(NULL, 0, abortWasapiThread, this, 0, NULL);
                    if (!threadHandle) {
                        errorType = RTAUDIO_THREAD_ERROR;
                        errorText = "RtApiWasapi::wasapiThread: Unable to instantiate stream abort thread.";
                        goto Exit;
                    }
                    else if (!CloseHandle(threadHandle)) {
                        errorType = RTAUDIO_THREAD_ERROR;
                        errorText = "RtApiWasapi::wasapiThread: Unable to close stream abort thread handle.";
                        goto Exit;
                    }

                    callbackStopped = true;
                }
            }
        }

        // Callback Output
        // ===============
        // 1. Convert callback buffer to stream format
        // 2. Convert callback buffer to stream sample rate and channel count
        // 3. Push callback buffer into outputBuffer

        if (renderAudioClient && callbackPulled)
        {
            // if the last call to renderBuffer.PushBuffer() was successful
            if (callbackPushed || convBufferSize == 0)
            {
                if (stream_.doConvertBuffer[OUTPUT])
                {
                    // Convert callback buffer to stream format
                    convertBuffer(stream_.deviceBuffer,
                        stream_.userBuffer[OUTPUT],
                        stream_.convertInfo[OUTPUT]);

                }
                else {
                    // no further conversion, simple copy userBuffer to deviceBuffer
                    memcpy(stream_.deviceBuffer,
                        stream_.userBuffer[OUTPUT],
                        stream_.bufferSize * stream_.nUserChannels[OUTPUT] * formatBytes(stream_.userFormat));
                }

                // Convert callback buffer to stream sample rate
                renderResampler->Convert(convBuffer,
                    stream_.deviceBuffer,
                    stream_.bufferSize,
                    convBufferSize);
            }

            // Push callback buffer into outputBuffer
            callbackPushed = renderBuffer.pushBuffer(convBuffer,
                convBufferSize * stream_.nDeviceChannels[OUTPUT],
                stream_.deviceFormat[OUTPUT]);
        }
        else {
            // if there is no render stream, set callbackPushed flag
            callbackPushed = true;
        }

        // Stream Capture
        // ==============
        // 1. Get capture buffer from stream
        // 2. Push capture buffer into inputBuffer
        // 3. If 2. was successful: Release capture buffer

        if (captureAudioClient) {
            // if the callback input buffer was not pulled from captureBuffer, wait for next capture event
            if (!callbackPulled || shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
                WaitForSingleObject(loopbackEnabled ? renderEvent : captureEvent, INFINITE);
            }

            // Get capture buffer from stream
            hr = captureClient->GetBuffer(&streamBuffer,
                &bufferFrameCount,
                &captureFlags, NULL, NULL);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve capture buffer.";
                goto Exit;
            }

            if (bufferFrameCount != 0) {
                // Push capture buffer into inputBuffer
                if (captureBuffer.pushBuffer((char*)streamBuffer,
                    bufferFrameCount * stream_.nDeviceChannels[INPUT],
                    stream_.deviceFormat[INPUT]))
                {
                    // Release capture buffer
                    hr = captureClient->ReleaseBuffer(bufferFrameCount);
                    if (FAILED(hr)) {
                        errorText = "RtApiWasapi::wasapiThread: Unable to release capture buffer.";
                        goto Exit;
                    }
                }
                else
                {
                    // Inform WASAPI that capture was unsuccessful
                    hr = captureClient->ReleaseBuffer(0);
                    if (FAILED(hr)) {
                        errorText = "RtApiWasapi::wasapiThread: Unable to release capture buffer.";
                        goto Exit;
                    }
                }
            }
            else
            {
                // Inform WASAPI that capture was unsuccessful
                hr = captureClient->ReleaseBuffer(0);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to release capture buffer.";
                    goto Exit;
                }
            }
        }

        // Stream Render
        // =============
        // 1. Get render buffer from stream
        // 2. Pull next buffer from outputBuffer
        // 3. If 2. was successful: Fill render buffer with next buffer
        //                          Release render buffer

        if (renderAudioClient) {
            // if the callback output buffer was not pushed to renderBuffer, wait for next render event
            if ((callbackPulled && !callbackPushed) || shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
                WaitForSingleObject(renderEvent, INFINITE);
            }

            // Get render buffer from stream
            hr = renderAudioClient->GetBufferSize(&bufferFrameCount);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render buffer size.";
                goto Exit;
            }

            hr = renderAudioClient->GetCurrentPadding(&numFramesPadding);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render buffer padding.";
                goto Exit;
            }

            if (shareMode == AUDCLNT_SHAREMODE_SHARED) {
                bufferFrameCount -= numFramesPadding;
            }

            if (bufferFrameCount != 0) {
                hr = renderClient->GetBuffer(bufferFrameCount, &streamBuffer);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render buffer.";
                    goto Exit;
                }

                // Pull next buffer from outputBuffer
                // Fill render buffer with next buffer
                if (renderBuffer.pullBuffer((char*)streamBuffer,
                    bufferFrameCount * stream_.nDeviceChannels[OUTPUT],
                    stream_.deviceFormat[OUTPUT]))
                {
                    // Release render buffer
                    hr = renderClient->ReleaseBuffer(bufferFrameCount, 0);
                    if (FAILED(hr)) {
                        errorText = "RtApiWasapi::wasapiThread: Unable to release render buffer.";
                        goto Exit;
                    }
                }
                else
                {
                    // Inform WASAPI that render was unsuccessful
                    hr = renderClient->ReleaseBuffer(0, 0);
                    if (FAILED(hr)) {
                        errorText = "RtApiWasapi::wasapiThread: Unable to release render buffer.";
                        goto Exit;
                    }
                }
            }
            else
            {
                // Inform WASAPI that render was unsuccessful
                hr = renderClient->ReleaseBuffer(0, 0);
                if (FAILED(hr)) {
                    errorText = "RtApiWasapi::wasapiThread: Unable to release render buffer.";
                    goto Exit;
                }
            }
        }

        // if the callback buffer was pushed renderBuffer reset callbackPulled flag
        if (callbackPushed) {
            // unsetting the callbackPulled flag lets the stream know that
            // the audio device is ready for another callback output buffer.
            callbackPulled = false;
        }

    }

Exit:
    // clean up
    SAFE_RELEASE(renderAudioClient3);
    SAFE_RELEASE(captureAudioClient3);
    CoTaskMemFree(captureFormat);
    CoTaskMemFree(renderFormat);

    free(convBuffer);
    delete renderResampler;
    delete captureResampler;

    CoUninitialize();

    if (!errorText.empty())
    {
        errorText_ = errorText;
        error(errorType);
    }

    // update stream state
    stream_.state = STREAM_STOPPED;
}
