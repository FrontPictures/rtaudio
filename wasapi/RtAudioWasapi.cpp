#define NOMINMAX
#include <wrl.h>
#include "RtAudioWasapi.h"
#include "WasapiResampler.h"
#include "WasapiBuffer.h"
#include <memory>
#include <functional>

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

#define UNIQUE_EVENT UNIQUE_WRAPPER(void, decltype(&CloseHandle))
#define MAKE_UNIQUE_EVENT_VALUE(v) UNIQUE_EVENT(v, CloseHandle);
#define MAKE_UNIQUE_EVENT_EMPTY MAKE_UNIQUE_EVENT_VALUE(nullptr);


#define CONSTRUCT_UNIQUE_FORMAT(create, out_res) makeUniqueContructed<HRESULT, WAVEFORMATEX, decltype(&CoTaskMemFree)>([&](WAVEFORMATEX** ptr) {return create(ptr);}, out_res, CoTaskMemFree)

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
        std::unique_ptr<WasapiResampler> resampler;

        WasapiHandle() :
            streamEvent(0, CloseHandle),
            mode(AUDCLNT_SHAREMODE_SHARED),
            renderFormat(nullptr, CoTaskMemFree),
            bufferDuration(0) {}
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
    LPWSTR defaultCaptureId = NULL;
    LPWSTR defaultRenderId = NULL;
    std::string defaultRenderString;
    std::string defaultCaptureString;

    Microsoft::WRL::ComPtr<IMMDevice> devicePtr;
    HRESULT hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = devicePtr->GetId(&defaultRenderId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default render device Id.";
            goto Exit;
        }
        defaultRenderString = convertCharPointerToStdString(defaultRenderId);
    }
    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &devicePtr);
    if (SUCCEEDED(hr)) {
        hr = devicePtr->GetId(&defaultCaptureId);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDevices: Unable to get default capture device Id.";
            goto Exit;
        }
        defaultCaptureString = convertCharPointerToStdString(defaultCaptureId);
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
Exit:
    CoTaskMemFree(defaultCaptureId);
    CoTaskMemFree(defaultRenderId);
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

    // Allow support for all sample rates as we have a built-in sample rate converter.
    for (unsigned int i = 0; i < MAX_SAMPLE_RATES; i++) {
        info.sampleRates.push_back(SAMPLE_RATES[i]);
    }
    info.preferredSampleRate = deviceFormat->nSamplesPerSec;

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
    std::unique_ptr<WasapiResampler> resampler;

    unsigned int deviceIdx;
    for (deviceIdx = 0; deviceIdx < deviceList_.size(); deviceIdx++) {
        if (deviceList_[deviceIdx].ID == deviceId) {
            id = deviceList_[deviceIdx].busID;
            if (deviceList_[deviceIdx].supportsInput) isInput = true;
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
        goto Exit;
    }

    hr = CONSTRUCT_UNIQUE_FORMAT(audioClient->GetMixFormat, deviceFormat);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device mix format.";
        goto Exit;
    }
    audioClient->GetStreamLatency((long long*)&stream_.latency[mode]);

    REFERENCE_TIME userBufferSize = ((uint64_t)(*bufferSize) * 10000000 / deviceFormat->nSamplesPerSec);
    if (options && options->flags & RTAUDIO_HOG_DEVICE) {
        REFERENCE_TIME defaultBufferDuration = 0;
        REFERENCE_TIME minimumBufferDuration = 0;

        hr = audioClient->GetDevicePeriod(&defaultBufferDuration, &minimumBufferDuration);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to retrieve device period.";
            goto Exit;
        }

        bool res = NegotiateExclusiveFormat(audioClient.Get(), deviceFormat);
        if (res == false) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to negotiate format for exclusive device.";
            goto Exit;
        }
        if (sampleRate != deviceFormat->nSamplesPerSec) {
            errorText_ = "RtApiWasapi::probeDeviceOpen: samplerate exclusive mismatch.";
            goto Exit;
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
                goto Exit;
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
            goto Exit;
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
            goto Exit;
        }
        shareMode = AUDCLNT_SHAREMODE_SHARED;

    }

    UINT32 nFrames = 0;
    hr = audioClient->GetBufferSize(&nFrames);
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::probeDeviceOpen: Unable to get buffer size.";
        goto Exit;
    }
    *bufferSize = nFrames;

    if (!isInput) {
        hr = audioClient->GetService(__uuidof(IAudioRenderClient),
            (void**)&renderClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::wasapiThread: Unable to retrieve render client handle.";
            goto Exit;
        }
    }
    else {
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
            (void**)&captureClient);
        if (FAILED(hr)) {
            errorText_ = "RtApiWasapi::wasapiThread: Unable to retrieve capture client handle.";
            goto Exit;
        }
    }

    streamEvent = MAKE_UNIQUE_EVENT_VALUE(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (!streamEvent) {
        errorType = RTAUDIO_SYSTEM_ERROR;
        errorText_ = "RtApiWasapi::wasapiThread: Unable to create render event.";
        goto Exit;
    }

    hr = audioClient->SetEventHandle(streamEvent.get());
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::wasapiThread: Unable to set render event handle.";
        goto Exit;
    }
    hr = audioClient->Reset();
    if (FAILED(hr)) {
        errorText_ = "RtApiWasapi::wasapiThread: Unable to reset audio stream.";
        goto Exit;
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
        goto Exit;
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
        goto Exit;
    }

    if (options && options->flags & RTAUDIO_SCHEDULE_REALTIME)
        stream_.callbackInfo.priority = 15;
    else
        stream_.callbackInfo.priority = 0;

    if (stream_.sampleRate != deviceFormat->nSamplesPerSec) {
        resampler = std::unique_ptr<WasapiResampler>(new WasapiResampler(stream_.deviceFormat[mode] == RTAUDIO_FLOAT32 || stream_.deviceFormat[mode] == RTAUDIO_FLOAT64,
            formatBytes(stream_.deviceFormat[mode]) * 8, stream_.nDeviceChannels[mode],
            stream_.sampleRate, deviceFormat->nSamplesPerSec));
        if (!resampler) {
            errorType = RTAUDIO_MEMORY_ERROR;
            errorText_ = "RtApiWasapi::probeDeviceOpen: Error allocating resampler.";
            goto Exit;
        }
    }

    ((WasapiHandle*)stream_.apiHandle)->audioClient = audioClient;
    ((WasapiHandle*)stream_.apiHandle)->renderClient = renderClient;
    ((WasapiHandle*)stream_.apiHandle)->captureClient = captureClient;
    ((WasapiHandle*)stream_.apiHandle)->streamEvent = std::move(streamEvent);
    ((WasapiHandle*)stream_.apiHandle)->mode = shareMode;
    ((WasapiHandle*)stream_.apiHandle)->renderFormat = std::move(deviceFormat);
    ((WasapiHandle*)stream_.apiHandle)->bufferDuration = userBufferSize;///wrong, need?       
    ((WasapiHandle*)stream_.apiHandle)->resampler = std::move(resampler);///wrong, need?       

    methodResult = SUCCESS;

Exit:
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

static void markThreadAsProAudio() {
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
    CoInitialize(NULL);

    HRESULT hr = S_OK;

    Microsoft::WRL::ComPtr<IAudioClient> audioClient = ((WasapiHandle*)stream_.apiHandle)->audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient = ((WasapiHandle*)stream_.apiHandle)->captureClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient = ((WasapiHandle*)stream_.apiHandle)->renderClient;
    HANDLE streamEvent = ((WasapiHandle*)stream_.apiHandle)->streamEvent.get();
    AUDCLNT_SHAREMODE shareMode = ((WasapiHandle*)stream_.apiHandle)->mode;
    WAVEFORMATEX* exclusiveFormat = ((WasapiHandle*)stream_.apiHandle)->renderFormat.get();
    WasapiResampler* resampler = ((WasapiHandle*)stream_.apiHandle)->resampler.get();

    float captureSrRatio = 0.0f;
    float renderSrRatio = 0.0f;
    //WasapiBuffer captureBuffer;
    //WasapiBuffer renderBuffer;

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
    markThreadAsProAudio();

    // start capture stream if applicable
    renderSrRatio = ((float)exclusiveFormat->nSamplesPerSec / stream_.sampleRate);

    hr = audioClient->Start();
    if (FAILED(hr)) {
        errorText = "RtApiWasapi::wasapiThread: Unable to start stream.";
        goto Exit;
    }

    /*
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

    convBuffSize *= 2; // allow overflow for *SrRatio remainders
    convBuffer = (char*)calloc(convBuffSize, 1);
    stream_.deviceBuffer = (char*)calloc(deviceBuffSize, 1);
    if (!convBuffer || !stream_.deviceBuffer) {
        errorType = RTAUDIO_MEMORY_ERROR;
        errorText = "RtApiWasapi::wasapiThread: Error allocating device buffer memory.";
        goto Exit;
    }*/

    UINT32 bufferFrameCount = 0;
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr) || bufferFrameCount == 0) {
        errorText = "RtApiWasapi::wasapiThread: Unable to get buffer size.";
        goto Exit;
    }

    // stream process loop
    while (stream_.state != STREAM_STOPPING) {
        DWORD waitResult = WaitForSingleObject(streamEvent, 100);
        if (waitResult == WAIT_TIMEOUT)
            continue;
        if (waitResult != WAIT_TIMEOUT) {
            errorText = "RtApiWasapi::wasapiThread: Unable to wait event.";
            goto Exit;
        }
        if (shareMode == AUDCLNT_SHAREMODE_SHARED) {
            hr = audioClient->GetCurrentPadding(&numFramesPadding);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render buffer padding.";
                goto Exit;
            }
        }
        UINT32 bufferFrameAvailableCount = bufferFrameCount - numFramesPadding;
        if (bufferFrameAvailableCount != 0) {
            hr = renderClient->GetBuffer(bufferFrameAvailableCount, &streamBuffer);
            if (FAILED(hr)) {
                errorText = "RtApiWasapi::wasapiThread: Unable to retrieve render buffer.";
                goto Exit;
            }
            //fill buffer


            callbackResult = callback(stream_.userBuffer[OUTPUT],
                stream_.userBuffer[INPUT],
                stream_.bufferSize,
                getStreamTime(),
                captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY ? RTAUDIO_INPUT_OVERFLOW : 0,
                stream_.callbackInfo.userData);
            RtApi::tickStreamTime();

            if (callbackResult == 1 || callbackResult == 2) {
                //stop it
            }

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
            resampler->Convert()

        }

        hr = renderClient->ReleaseBuffer(bufferFrameAvailableCount, 0);
        if (FAILED(hr)) {
            errorText = "RtApiWasapi::wasapiThread: Unable to release render buffer.";
            goto Exit;
        }
        streamBuffer = NULL;




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
    stream_.state = STREAM_STOPPED; */
}
