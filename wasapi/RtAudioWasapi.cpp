#define NOMINMAX
#include <wrl.h>
#include "RtAudioWasapi.h"
#include "WasapiResampler.h"
#include "WasapiBuffer.h"
#include <memory>
#include <functional>
#include "OnExit.hpp"
#include <cassert>

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


#define SMART_PTR_WRAPPER(ptr_type, datatype, remove) ptr_type<datatype, remove>
#define UNIQUE_WRAPPER(type, remove) SMART_PTR_WRAPPER(std::unique_ptr, type, remove)

#define UNIQUE_FORMAT UNIQUE_WRAPPER(WAVEFORMATEX, decltype(&CoTaskMemFree))
#define MAKE_UNIQUE_FORMAT_EMPTY UNIQUE_FORMAT(nullptr, CoTaskMemFree);

#define UNIQUE_STRING UNIQUE_WRAPPER(WCHAR, decltype(&CoTaskMemFree))
#define MAKE_UNIQUE_STRING_EMPTY UNIQUE_STRING(nullptr, CoTaskMemFree);

#define UNIQUE_EVENT UNIQUE_WRAPPER(void, decltype(&CloseHandle))
#define MAKE_UNIQUE_EVENT_VALUE(v) UNIQUE_EVENT(v, CloseHandle);
#define MAKE_UNIQUE_EVENT_EMPTY MAKE_UNIQUE_EVENT_VALUE(nullptr);


#define CONSTRUCT_UNIQUE_FORMAT(create, out_res) makeUniqueContructed<HRESULT, WAVEFORMATEX, decltype(&CoTaskMemFree)>([&](WAVEFORMATEX** ptr) {return create(ptr);}, out_res, CoTaskMemFree)
#define CONSTRUCT_UNIQUE_STRING(create, out_res) makeUniqueContructed<HRESULT, WCHAR, decltype(&CoTaskMemFree)>([&](LPWSTR* ptr) {return create(ptr);}, out_res, CoTaskMemFree)

    template<class Result, class Type, class Remove>
    inline Result makeUniqueContructed(std::function<Result(Type**)> cc,
        std::unique_ptr<Type, Remove>& out_result, Remove remove_fun) {
        Type* temp = nullptr;
        Result res = cc(&temp);
        out_result = std::move(UNIQUE_WRAPPER(Type, Remove)(temp, remove_fun));
        return res;
    }

    bool NegotiateExclusiveFormat(IAudioClient* renderAudioClient, UNIQUE_FORMAT& format) {
        HRESULT hr = S_OK;
        hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, format.get(), nullptr);
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
            if ((format)->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                (format)->wFormatTag = WAVE_FORMAT_PCM;
                (format)->wBitsPerSample = 16;
                (format)->nBlockAlign = (format)->nChannels * (format)->wBitsPerSample / 8;
                (format)->nAvgBytesPerSec = (format)->nSamplesPerSec * (format)->nBlockAlign;
            }
            else if ((format)->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format.get())->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format.get());
                waveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                waveFormatExtensible->Format.wBitsPerSample = 16;
                waveFormatExtensible->Format.nBlockAlign = ((format)->wBitsPerSample / 8) * (format)->nChannels;
                waveFormatExtensible->Format.nAvgBytesPerSec = waveFormatExtensible->Format.nSamplesPerSec * waveFormatExtensible->Format.nBlockAlign;
                waveFormatExtensible->Samples.wValidBitsPerSample = 16;
            }
            else {
                return false;
            }
            hr = renderAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, format.get(), nullptr);
        }
        if (FAILED(hr)) {
            return false;
        }
        return true;
    }

    RtAudioFormat GetRtAudioTypeFromWasapi(const WAVEFORMATEX* format) {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
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
        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
        Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
        UNIQUE_EVENT streamEvent;
        AUDCLNT_SHAREMODE mode;
        UNIQUE_FORMAT renderFormat;
        REFERENCE_TIME bufferDuration;
        bool isInput;

        WasapiHandle() :
            streamEvent(0, CloseHandle),
            mode(AUDCLNT_SHAREMODE_SHARED),
            renderFormat(nullptr, CoTaskMemFree),
            bufferDuration(0),
            isInput(true) {}
    };

    class PROPVARIANT_Raii {
    public:
        PROPVARIANT_Raii() {
            PropVariantInit(&mPropVal);
        }
        ~PROPVARIANT_Raii() {
            PropVariantClear(&mPropVal);
        }
        PROPVARIANT* operator&() {
            return &mPropVal;
        }
        const PROPVARIANT get() const {
            return mPropVal;
        }
    private:
        PROPVARIANT mPropVal;
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

void RtApiWasapi::listDevices(void)
{
    deviceList_.clear();
    listAudioDevices(eRender);
    listAudioDevices(eCapture);
    updateDefaultDevices();
}

void RtApiWasapi::listAudioDevices(EDataFlow dataFlow)
{
    unsigned int nDevices = 0;

    Microsoft::WRL::ComPtr<IMMDeviceCollection> renderDevices;
    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;

    LPWSTR deviceId = NULL;

    if (!deviceEnumerator_) return;
    errorText_.clear();

    // Count render devices
    HRESULT hr = deviceEnumerator_->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &renderDevices);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve render device collection.";
        error(RTAUDIO_DRIVER_ERROR);
    }

    hr = renderDevices->GetCount(&nDevices);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve render device count.";
        error(RTAUDIO_DRIVER_ERROR);
    }

    if (nDevices == 0) {
        errorText_ = "RtApiWasapi::probeDevices: No devices found.";
        error(RTAUDIO_DRIVER_ERROR);
    }

    for (unsigned int n = 0; n < nDevices; n++) {
        hr = renderDevices->Item(n, &devicePtr);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to retrieve audio device handle.";
            error(RTAUDIO_WARNING);
            continue;
        }

        hr = devicePtr->GetId(&deviceId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get device Id.";
            error(RTAUDIO_WARNING);
            continue;
        }
        auto id_str = convertCharPointerToStdString(deviceId);
        CoTaskMemFree(deviceId);
        RtAudio::DeviceInfo info;
        if (dataFlow == eRender) {
            info.supportsOutput = true;
        }
        else if (dataFlow == eCapture) {
            info.supportsInput = true;
        }
        info.busID = id_str;
        if (probeDeviceName(info, devicePtr.Get()) == false) {
            continue;
        }
        info.ID = currentDeviceId_++;  // arbitrary internal device ID        
        deviceList_.push_back(info);
    }
}

void RtApiWasapi::updateDefaultDevices()
{
    UNIQUE_STRING defaultCaptureId = MAKE_UNIQUE_STRING_EMPTY;
    UNIQUE_STRING defaultRenderId = MAKE_UNIQUE_STRING_EMPTY;
    std::string defaultRenderString;
    std::string defaultCaptureString;

    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    HRESULT hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = CONSTRUCT_UNIQUE_STRING(devicePtr->GetId, defaultRenderId);;
        if (FAILED(hr) || !defaultRenderId) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default render device Id.";
            error(RTAUDIO_DRIVER_ERROR);
            return;
        }
        defaultRenderString = convertCharPointerToStdString(defaultRenderId.get());
    }
    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = CONSTRUCT_UNIQUE_STRING(devicePtr->GetId, defaultCaptureId);
        if (FAILED(hr) || !defaultCaptureId) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default capture device Id.";
            error(RTAUDIO_DRIVER_ERROR);
            return;
        }
        defaultCaptureString = convertCharPointerToStdString(defaultCaptureId.get());
    }

    for (auto& d : deviceList_) {
        d.isDefaultInput = false;
        d.isDefaultOutput = false;

        if (d.busID == defaultRenderString) {
            d.isDefaultOutput = true;
        }
        else if (d.busID == defaultCaptureString) {
            d.isDefaultInput = true;
        }
    }
}

bool RtApiWasapi::probeSingleDeviceInfo(RtAudio::DeviceInfo& info)
{
    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient;

    UNIQUE_FORMAT deviceFormat = MAKE_UNIQUE_FORMAT_EMPTY;
    HRESULT res = S_OK;

    errorText_.clear();
    RtAudioErrorType errorType = RTAUDIO_DRIVER_ERROR;

    // Get the device pointer from the device Id
    auto device_id = convertStdStringToWString(info.busID);

    HRESULT hr = deviceEnumerator_->GetDevice(device_id.c_str(), &devicePtr);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device handle.";
        error(errorType);
        return false;
    }

    // Get audio client
    hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device audio client.";
        error(errorType);
        return false;
    }

    res = CONSTRUCT_UNIQUE_FORMAT(audioClient->GetMixFormat, deviceFormat);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device mix format.";
        error(errorType);
        return false;
    }

    // Set channel count
    if (info.supportsInput) {
        info.inputChannels = deviceFormat->nChannels;
        info.outputChannels = 0;
        info.duplexChannels = 0;
    }
    else if (info.supportsOutput) {
        info.inputChannels = 0;
        info.outputChannels = deviceFormat->nChannels;
        info.duplexChannels = 0;
    }

    // Set sample rates
    info.sampleRates.clear();
    info.preferredSampleRate = deviceFormat->nSamplesPerSec;
    info.sampleRates.push_back(info.preferredSampleRate);

    // Set native formats
    info.nativeFormats = 0;

    if (deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            ((WAVEFORMATEXTENSIBLE*)deviceFormat.get())->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
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
            ((WAVEFORMATEXTENSIBLE*)deviceFormat.get())->SubFormat == KSDATAFORMAT_SUBTYPE_PCM))
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
    return true;
}

bool RtApiWasapi::probeDeviceName(RtAudio::DeviceInfo& info, IMMDevice* devicePtr)
{
    Microsoft::WRL::ComPtr<IPropertyStore> devicePropStore;
    PROPVARIANT_Raii deviceNameProp;
    HRESULT hr = devicePtr->OpenPropertyStore(STGM_READ, &devicePropStore);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to open device property store.";
        error(RTAUDIO_DRIVER_ERROR);
        return false;
    }
    hr = devicePropStore->GetValue(PKEY_Device_FriendlyName, &deviceNameProp);
    if (FAILED(hr) || deviceNameProp.get().pwszVal == nullptr) {
        errorText_ = "RtApiWasapi::probeDeviceInfo: Unable to retrieve device property: PKEY_Device_FriendlyName.";
        error(RTAUDIO_DRIVER_ERROR);
        return false;
    }
    info.name = convertCharPointerToStdString(deviceNameProp.get().pwszVal);
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

    Microsoft::WRL::ComPtr<IAudioClient> audioClient = ((WasapiHandle*)stream_.apiHandle)->audioClient;
    HRESULT hr = audioClient->Start();
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::wasapiThread: Unable to start stream.";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_THREAD_ERROR);
    }

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
    if (stream_.state != STREAM_RUNNING && stream_.state != STREAM_ERROR) {
        if (stream_.state == STREAM_STOPPED)
            errorText_ = "RtApiWasapi::stopStream(): the stream is already stopped!";
        else if (stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiWasapi::stopStream(): the stream is closed!";
        MUTEX_UNLOCK(&stream_.mutex);
        return error(RTAUDIO_WARNING);
    }

    Microsoft::WRL::ComPtr<IAudioClient> audioClient = ((WasapiHandle*)stream_.apiHandle)->audioClient;
    HRESULT hr = audioClient->Stop();

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
    return stopStream();
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
    RtAudioErrorType errorType = RTAUDIO_INVALID_USE;
    bool methodResult = FAILURE;

    OnExit onExit([&]() {
        if (methodResult == FAILURE)
        {
            MUTEX_UNLOCK(&stream_.mutex);
            closeStream();
            MUTEX_LOCK(&stream_.mutex);
        }
        if (!errorText_.empty())
            error(errorType);
        MUTEX_UNLOCK(&stream_.mutex);
        });

    MUTEX_LOCK(&stream_.mutex);
    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    UNIQUE_FORMAT deviceFormat = MAKE_UNIQUE_FORMAT_EMPTY;
    unsigned int bufferBytes;
    stream_.state = STREAM_STOPPED;
    bool isInput = false;
    std::string id;
    AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_SHARED;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
    UNIQUE_EVENT streamEvent = MAKE_UNIQUE_EVENT_EMPTY;

    unsigned int deviceIdx;
    for (deviceIdx = 0; deviceIdx < deviceList_.size(); deviceIdx++) {
        if (deviceList_[deviceIdx].ID == deviceId) {
            id = deviceList_[deviceIdx].busID;
            if (deviceList_[deviceIdx].supportsInput) isInput = true;
            break;
        }
    }

    errorText_.clear();
    if (id.empty()) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: the device ID was not found!";
        MUTEX_UNLOCK(&stream_.mutex);
        return FAILURE;
    }

    if (isInput && mode != StreamMode::INPUT || !isInput && mode != StreamMode::OUTPUT) {
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

    hr = devicePtr->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        NULL, (void**)&audioClient);
    if (FAILED(hr) || !audioClient) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device audio client.";
        return FAILURE;
    }

    hr = CONSTRUCT_UNIQUE_FORMAT(audioClient->GetMixFormat, deviceFormat);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device mix format.";
        return FAILURE;
    }
    audioClient->GetStreamLatency((long long*)&stream_.latency[mode]);

    REFERENCE_TIME userBufferSize = ((uint64_t)(*bufferSize) * 10000000 / deviceFormat->nSamplesPerSec);
    if (options && options->flags & RTAUDIO_HOG_DEVICE) {
        REFERENCE_TIME defaultBufferDuration = 0;
        REFERENCE_TIME minimumBufferDuration = 0;

        hr = audioClient->GetDevicePeriod(&defaultBufferDuration, &minimumBufferDuration);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device period.";
            return FAILURE;
        }

        bool res = NegotiateExclusiveFormat(audioClient.Get(), deviceFormat);
        if (res == false) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to negotiate format for exclusive device.";
            return FAILURE;
        }
        if (sampleRate != deviceFormat->nSamplesPerSec) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: samplerate exclusive mismatch.";
            return FAILURE;
        }
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
            deviceFormat.get(),
            NULL);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 nFrames = 0;
            hr = audioClient->GetBufferSize(&nFrames);
            if (FAILED(hr)) {
                errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.";
                return FAILURE;
            }
            constexpr int REFTIMES_PER_SEC = 10000000;
            userBufferSize = (REFERENCE_TIME)((double)REFTIMES_PER_SEC / deviceFormat->nSamplesPerSec * nFrames + 0.5);
            hr = audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                userBufferSize,
                userBufferSize,
                deviceFormat.get(),
                NULL);
        }
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to open device.";
            return FAILURE;
        }
        shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;
    }
    else {
        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            userBufferSize,
            0,
            deviceFormat.get(),
            NULL);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to open device.";
            return FAILURE;
        }
        shareMode = AUDCLNT_SHAREMODE_SHARED;

    }

    UINT32 nFrames = 0;
    hr = audioClient->GetBufferSize(&nFrames);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.";
        return FAILURE;
    }
    *bufferSize = nFrames;

    if (!isInput) {
        hr = audioClient->GetService(__uuidof(IAudioRenderClient),
            (void**)&renderClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::wasapiThread: Unable to retrieve render client handle.";
            return FAILURE;
        }
    }
    else {
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
            (void**)&captureClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::wasapiThread: Unable to retrieve capture client handle.";
            return FAILURE;
        }
    }

    streamEvent = MAKE_UNIQUE_EVENT_VALUE(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (!streamEvent) {
        errorType = RTAUDIO_SYSTEM_ERROR;
        errorText_ = "RtApiWasapi::wasapiThread: Unable to create render event.";
        return FAILURE;
    }

    hr = audioClient->SetEventHandle(streamEvent.get());
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::wasapiThread: Unable to set render event handle.";
        return FAILURE;
    }
    hr = audioClient->Reset();
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::wasapiThread: Unable to reset audio stream.";
        return FAILURE;
    }
    if (renderClient) {
        BYTE* pData = nullptr;
        hr = renderClient->GetBuffer(nFrames, &pData);
        hr = renderClient->ReleaseBuffer(nFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    // Fill stream data
    if ((stream_.mode == OUTPUT && mode == StreamMode::INPUT) ||
        (stream_.mode == StreamMode::INPUT && mode == OUTPUT)) {
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
    stream_.deviceFormat[mode] = GetRtAudioTypeFromWasapi(deviceFormat.get());
    if (stream_.deviceFormat[mode] == 0) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Hardware audio format not implemented.";
        return FAILURE;
    }
    stream_.nDeviceChannels[mode] = deviceFormat->nChannels;

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
        return FAILURE;
    }

    if (options && options->flags & RTAUDIO_SCHEDULE_REALTIME)
        stream_.callbackInfo.priority = 15;
    else
        stream_.callbackInfo.priority = 0;

    ((WasapiHandle*)stream_.apiHandle)->audioClient = audioClient;
    ((WasapiHandle*)stream_.apiHandle)->renderClient = renderClient;
    ((WasapiHandle*)stream_.apiHandle)->captureClient = captureClient;
    ((WasapiHandle*)stream_.apiHandle)->streamEvent = std::move(streamEvent);
    ((WasapiHandle*)stream_.apiHandle)->mode = shareMode;
    ((WasapiHandle*)stream_.apiHandle)->renderFormat = std::move(deviceFormat);
    ((WasapiHandle*)stream_.apiHandle)->bufferDuration = userBufferSize;///wrong, need?
    ((WasapiHandle*)stream_.apiHandle)->isInput = mode == INPUT ? true : false;

    methodResult = SUCCESS;
    return methodResult;
}

DWORD WINAPI RtApiWasapi::runWasapiThread(void* wasapiPtr)
{
    if (wasapiPtr)
        ((RtApiWasapi*)wasapiPtr)->wasapiThread();

    return 0;
}

static void markThreadAsProAudio() {
    // Attempt to assign "Pro Audio" characteristic to thread
    HMODULE AvrtDll = LoadLibraryW(L"AVRT.dll");
    if (AvrtDll) {
        DWORD taskIndex = 0;
        TAvSetMmThreadCharacteristicsPtr AvSetMmThreadCharacteristicsPtr =
            (TAvSetMmThreadCharacteristicsPtr)(void(*)()) GetProcAddress(AvrtDll, "AvSetMmThreadCharacteristicsW");
        if (AvSetMmThreadCharacteristicsPtr) {
            AvSetMmThreadCharacteristicsPtr(L"Pro Audio", &taskIndex);
        }
        FreeLibrary(AvrtDll);
    }
}

void RtApiWasapi::wasapiThread()
{
    // as this is a new thread, we must CoInitialize it
    COMLibrary_Raii comLibrary;
    OnExit onExit([&]() {
        stream_.state = STREAM_ERROR;
        });

    HRESULT hr = S_OK;

    Microsoft::WRL::ComPtr<IAudioClient> audioClient = ((WasapiHandle*)stream_.apiHandle)->audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient = ((WasapiHandle*)stream_.apiHandle)->captureClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient = ((WasapiHandle*)stream_.apiHandle)->renderClient;
    HANDLE streamEvent = ((WasapiHandle*)stream_.apiHandle)->streamEvent.get();
    AUDCLNT_SHAREMODE shareMode = ((WasapiHandle*)stream_.apiHandle)->mode;
    WAVEFORMATEX* exclusiveFormat = ((WasapiHandle*)stream_.apiHandle)->renderFormat.get();
    bool isInput = ((WasapiHandle*)stream_.apiHandle)->isInput;
    StreamMode modeDirection = isInput ? StreamMode::INPUT : StreamMode::OUTPUT;

    // declare local stream variables
    RtAudioCallback callback = (RtAudioCallback)stream_.callbackInfo.callback;
    BYTE* streamBuffer = NULL;
    DWORD captureFlags = 0;
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

    RtAudioErrorType errorType = RTAUDIO_DRIVER_ERROR;

    REFERENCE_TIME defaultBufferDuration = ((WasapiHandle*)stream_.apiHandle)->bufferDuration;

    markThreadAsProAudio();

    UINT32 bufferFrameCount = 0;
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr) || bufferFrameCount == 0) {
        errorText(errorType, "RtApiWasapi::wasapiThread: Unable to get buffer size.");
        return;
    }

    // stream process loop
    while (stream_.state != STREAM_STOPPING) {
        DWORD waitResult = WaitForSingleObject(streamEvent, 100);
        if (waitResult == WAIT_TIMEOUT)
            continue;
        if (waitResult != WAIT_OBJECT_0) {
            errorText(errorType, "RtApiWasapi::wasapiThread: Unable to wait event.");
            break;
        }

        void* userBufferInput = nullptr;
        void* userBufferOutput = nullptr;
        UINT32 bufferFrameAvailableCount = 0;

        if (!isInput) {
            unsigned int numFramesPadding = 0;
            if (shareMode == AUDCLNT_SHAREMODE_SHARED) {
                hr = audioClient->GetCurrentPadding(&numFramesPadding);
                if (FAILED(hr)) {
                    errorText(errorType, "RtApiWasapi::wasapiThread: Unable to retrieve render buffer padding.");
                    break;
                }
            }
            bufferFrameAvailableCount = bufferFrameCount - numFramesPadding;
            if (bufferFrameAvailableCount == 0) {
                continue;
            }

            hr = renderClient->GetBuffer(bufferFrameAvailableCount, &streamBuffer);
            if (FAILED(hr)) {
                errorText(errorType, "RtApiWasapi::wasapiThread: Unable to retrieve render buffer.");
                break;
            }
            if (stream_.doConvertBuffer[StreamMode::OUTPUT]) {
                userBufferOutput = stream_.userBuffer[OUTPUT];
            }
            else {
                userBufferOutput = streamBuffer;
            }
        }
        else {
            hr = captureClient->GetBuffer(&streamBuffer, &bufferFrameAvailableCount, &captureFlags, NULL, NULL);
            if (FAILED(hr)) {
                errorText(errorType, "RtApiWasapi::wasapiThread: Unable to get capture buffer.");
                break;
            }
            if (bufferFrameAvailableCount == 0 || hr == AUDCLNT_S_BUFFER_EMPTY || !streamBuffer) {
                continue;
            }
            if (stream_.doConvertBuffer[modeDirection]) {
                userBufferInput = stream_.userBuffer[INPUT];
                convertBuffer(stream_.userBuffer[modeDirection],
                    (char*)streamBuffer,
                    stream_.convertInfo[modeDirection], bufferFrameAvailableCount, StreamMode::INPUT);
            }
            else {
                userBufferInput = streamBuffer;
            }
        }


        callbackResult = callback(userBufferOutput,
            userBufferInput,
            bufferFrameAvailableCount,
            getStreamTime(),
            captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY ? RTAUDIO_INPUT_OVERFLOW : 0,
            stream_.callbackInfo.userData);
        RtApi::tickStreamTime();
        if (callbackResult == 1 || callbackResult == 2) {
            //stop it
        }


        if (!isInput) {
            if (stream_.doConvertBuffer[modeDirection])
            {
                // Convert callback buffer to stream format
                convertBuffer((char*)streamBuffer,
                    stream_.userBuffer[modeDirection],
                    stream_.convertInfo[modeDirection], bufferFrameAvailableCount, StreamMode::OUTPUT);
            }
            hr = renderClient->ReleaseBuffer(bufferFrameAvailableCount, 0);
            if (FAILED(hr)) {
                errorText(errorType, "RtApiWasapi::wasapiThread: Unable to release render buffer.");
                break;
            }
        }
        else {
            hr = captureClient->ReleaseBuffer(bufferFrameAvailableCount);
            if (FAILED(hr)) {
                errorText(errorType, "RtApiWasapi::wasapiThread: Unable to release render buffer.");
                break;
            }
        }

        streamBuffer = nullptr;
        captureFlags = 0;
    }
}
