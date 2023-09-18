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

class RtApiWasapi : public RtApi
{
public:
    RtApiWasapi();
    virtual ~RtApiWasapi();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    unsigned int getDefaultOutputDevice(void) override;
    unsigned int getDefaultInputDevice(void) override;
    void closeStream(void) override;
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;
    RtAudioErrorType abortStream(void) override;
    RtAudioErrorType registerExtraCallback(RtAudioDeviceCallback callback, void* userData) override;
    RtAudioErrorType unregisterExtraCallback() override;

private:
    bool coInitialized_;
    IMMDeviceEnumerator* deviceEnumerator_;
    RtAudioDeviceCallback callbackExtra_ = nullptr;
    NotificationHandler wasapiNotificationHandler_;
    std::vector< std::pair< std::string, bool> > deviceIds_;

    void probeDevices(void) override;
    bool probeDeviceInfo(RtAudio::DeviceInfo& info, LPWSTR deviceId, bool isCaptureDevice);
    bool probeDeviceOpen(unsigned int deviceId, StreamMode mode, unsigned int channels,
        unsigned int firstChannel, unsigned int sampleRate,
        RtAudioFormat format, unsigned int* bufferSize,
        RtAudio::StreamOptions* options) override;

    static DWORD WINAPI runWasapiThread(void* wasapiPtr);
    static DWORD WINAPI stopWasapiThread(void* wasapiPtr);
    static DWORD WINAPI abortWasapiThread(void* wasapiPtr);
    void wasapiThread();
};

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

// WASAPI dictates stream sample rate, format, channel count, and in some cases, buffer size.
// Therefore we must perform all necessary conversions to user buffers in order to satisfy these
// requirements. WasapiBuffer ring buffers are used between HwIn->UserIn and UserOut->HwOut to
// provide intermediate storage for read / write synchronization.
class WasapiBuffer
{
public:
    WasapiBuffer()
        : buffer_(NULL),
        bufferSize_(0),
        inIndex_(0),
        outIndex_(0) {}

    ~WasapiBuffer() {
        free(buffer_);
    }

    // sets the length of the internal ring buffer
    void setBufferSize(unsigned int bufferSize, unsigned int formatBytes) {
        free(buffer_);

        buffer_ = (char*)calloc(bufferSize, formatBytes);

        bufferSize_ = bufferSize;
        inIndex_ = 0;
        outIndex_ = 0;
    }

    // attempt to push a buffer into the ring buffer at the current "in" index
    bool pushBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format)
    {
        if (!buffer ||                 // incoming buffer is NULL
            bufferSize == 0 ||         // incoming buffer has no data
            bufferSize > bufferSize_) // incoming buffer too large
        {
            return false;
        }

        unsigned int relOutIndex = outIndex_;
        unsigned int inIndexEnd = inIndex_ + bufferSize;
        if (relOutIndex < inIndex_ && inIndexEnd >= bufferSize_) {
            relOutIndex += bufferSize_;
        }

        // the "IN" index CAN BEGIN at the "OUT" index
        // the "IN" index CANNOT END at the "OUT" index
        if (inIndex_ < relOutIndex && inIndexEnd >= relOutIndex) {
            return false; // not enough space between "in" index and "out" index
        }

        // copy buffer from external to internal
        int fromZeroSize = inIndex_ + bufferSize - bufferSize_;
        fromZeroSize = fromZeroSize < 0 ? 0 : fromZeroSize;
        int fromInSize = bufferSize - fromZeroSize;

        switch (format)
        {
        case RTAUDIO_SINT8:
            memcpy(&((char*)buffer_)[inIndex_], buffer, fromInSize * sizeof(char));
            memcpy(buffer_, &((char*)buffer)[fromInSize], fromZeroSize * sizeof(char));
            break;
        case RTAUDIO_SINT16:
            memcpy(&((short*)buffer_)[inIndex_], buffer, fromInSize * sizeof(short));
            memcpy(buffer_, &((short*)buffer)[fromInSize], fromZeroSize * sizeof(short));
            break;
        case RTAUDIO_SINT24:
            memcpy(&((S24*)buffer_)[inIndex_], buffer, fromInSize * sizeof(S24));
            memcpy(buffer_, &((S24*)buffer)[fromInSize], fromZeroSize * sizeof(S24));
            break;
        case RTAUDIO_SINT32:
            memcpy(&((int*)buffer_)[inIndex_], buffer, fromInSize * sizeof(int));
            memcpy(buffer_, &((int*)buffer)[fromInSize], fromZeroSize * sizeof(int));
            break;
        case RTAUDIO_FLOAT32:
            memcpy(&((float*)buffer_)[inIndex_], buffer, fromInSize * sizeof(float));
            memcpy(buffer_, &((float*)buffer)[fromInSize], fromZeroSize * sizeof(float));
            break;
        case RTAUDIO_FLOAT64:
            memcpy(&((double*)buffer_)[inIndex_], buffer, fromInSize * sizeof(double));
            memcpy(buffer_, &((double*)buffer)[fromInSize], fromZeroSize * sizeof(double));
            break;
        }

        // update "in" index
        inIndex_ += bufferSize;
        inIndex_ %= bufferSize_;

        return true;
    }

    // attempt to pull a buffer from the ring buffer from the current "out" index
    bool pullBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format)
    {
        if (!buffer ||                 // incoming buffer is NULL
            bufferSize == 0 ||         // incoming buffer has no data
            bufferSize > bufferSize_) // incoming buffer too large
        {
            return false;
        }

        unsigned int relInIndex = inIndex_;
        unsigned int outIndexEnd = outIndex_ + bufferSize;
        if (relInIndex < outIndex_ && outIndexEnd >= bufferSize_) {
            relInIndex += bufferSize_;
        }

        // the "OUT" index CANNOT BEGIN at the "IN" index
        // the "OUT" index CAN END at the "IN" index
        if (outIndex_ <= relInIndex && outIndexEnd > relInIndex) {
            return false; // not enough space between "out" index and "in" index
        }

        // copy buffer from internal to external
        int fromZeroSize = outIndex_ + bufferSize - bufferSize_;
        fromZeroSize = fromZeroSize < 0 ? 0 : fromZeroSize;
        int fromOutSize = bufferSize - fromZeroSize;

        switch (format)
        {
        case RTAUDIO_SINT8:
            memcpy(buffer, &((char*)buffer_)[outIndex_], fromOutSize * sizeof(char));
            memcpy(&((char*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(char));
            break;
        case RTAUDIO_SINT16:
            memcpy(buffer, &((short*)buffer_)[outIndex_], fromOutSize * sizeof(short));
            memcpy(&((short*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(short));
            break;
        case RTAUDIO_SINT24:
            memcpy(buffer, &((S24*)buffer_)[outIndex_], fromOutSize * sizeof(S24));
            memcpy(&((S24*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(S24));
            break;
        case RTAUDIO_SINT32:
            memcpy(buffer, &((int*)buffer_)[outIndex_], fromOutSize * sizeof(int));
            memcpy(&((int*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(int));
            break;
        case RTAUDIO_FLOAT32:
            memcpy(buffer, &((float*)buffer_)[outIndex_], fromOutSize * sizeof(float));
            memcpy(&((float*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(float));
            break;
        case RTAUDIO_FLOAT64:
            memcpy(buffer, &((double*)buffer_)[outIndex_], fromOutSize * sizeof(double));
            memcpy(&((double*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(double));
            break;
        }

        // update "out" index
        outIndex_ += bufferSize;
        outIndex_ %= bufferSize_;

        return true;
    }

private:
    char* buffer_;
    unsigned int bufferSize_;
    unsigned int inIndex_;
    unsigned int outIndex_;
};

// In order to satisfy WASAPI's buffer requirements, we need a means of converting sample rate
// between HW and the user. The WasapiResampler class is used to perform this conversion between
// HwIn->UserIn and UserOut->HwOut during the stream callback loop.
class WasapiResampler
{
public:
    WasapiResampler(bool isFloat, unsigned int bitsPerSample, unsigned int channelCount,
        unsigned int inSampleRate, unsigned int outSampleRate)
        : _bytesPerSample(bitsPerSample / 8)
        , _channelCount(channelCount)
        , _sampleRatio((float)outSampleRate / inSampleRate)
        , _transformUnk(NULL)
        , _transform(NULL)
        , _mediaType(NULL)
        , _inputMediaType(NULL)
        , _outputMediaType(NULL)

#ifdef __IWMResamplerProps_FWD_DEFINED__
        , _resamplerProps(NULL)
#endif
    {
        // 1. Initialization

        MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);

        // 2. Create Resampler Transform Object

        CoCreateInstance(CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER,
            IID_IUnknown, (void**)&_transformUnk);

        _transformUnk->QueryInterface(IID_PPV_ARGS(&_transform));

#ifdef __IWMResamplerProps_FWD_DEFINED__
        _transformUnk->QueryInterface(IID_PPV_ARGS(&_resamplerProps));
        _resamplerProps->SetHalfFilterLength(60); // best conversion quality
#endif

        // 3. Specify input / output format

        MFCreateMediaType(&_mediaType);
        _mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        _mediaType->SetGUID(MF_MT_SUBTYPE, isFloat ? MFAudioFormat_Float : MFAudioFormat_PCM);
        _mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channelCount);
        _mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, inSampleRate);
        _mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, _bytesPerSample * channelCount);
        _mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, _bytesPerSample * channelCount * inSampleRate);
        _mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
        _mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

        MFCreateMediaType(&_inputMediaType);
        _mediaType->CopyAllItems(_inputMediaType);

        _transform->SetInputType(0, _inputMediaType, 0);

        MFCreateMediaType(&_outputMediaType);
        _mediaType->CopyAllItems(_outputMediaType);

        _outputMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, outSampleRate);
        _outputMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, _bytesPerSample * channelCount * outSampleRate);

        _transform->SetOutputType(0, _outputMediaType, 0);

        // 4. Send stream start messages to Resampler

        _transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        _transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        _transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    }

    ~WasapiResampler()
    {
        // 8. Send stream stop messages to Resampler

        _transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        _transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);

        // 9. Cleanup

        MFShutdown();

        SAFE_RELEASE(_transformUnk);
        SAFE_RELEASE(_transform);
        SAFE_RELEASE(_mediaType);
        SAFE_RELEASE(_inputMediaType);
        SAFE_RELEASE(_outputMediaType);

#ifdef __IWMResamplerProps_FWD_DEFINED__
        SAFE_RELEASE(_resamplerProps);
#endif
    }

    void Convert(char* outBuffer, const char* inBuffer, unsigned int inSampleCount, unsigned int& outSampleCount, int maxOutSampleCount = -1)
    {
        unsigned int inputBufferSize = _bytesPerSample * _channelCount * inSampleCount;
        if (_sampleRatio == 1)
        {
            // no sample rate conversion required
            memcpy(outBuffer, inBuffer, inputBufferSize);
            outSampleCount = inSampleCount;
            return;
        }

        unsigned int outputBufferSize = 0;
        if (maxOutSampleCount != -1)
        {
            outputBufferSize = _bytesPerSample * _channelCount * maxOutSampleCount;
        }
        else
        {
            outputBufferSize = (unsigned int)ceilf(inputBufferSize * _sampleRatio) + (_bytesPerSample * _channelCount);
        }

        IMFMediaBuffer* rInBuffer;
        IMFSample* rInSample;
        BYTE* rInByteBuffer = NULL;

        // 5. Create Sample object from input data

        MFCreateMemoryBuffer(inputBufferSize, &rInBuffer);

        rInBuffer->Lock(&rInByteBuffer, NULL, NULL);
        memcpy(rInByteBuffer, inBuffer, inputBufferSize);
        rInBuffer->Unlock();
        rInByteBuffer = NULL;

        rInBuffer->SetCurrentLength(inputBufferSize);

        MFCreateSample(&rInSample);
        rInSample->AddBuffer(rInBuffer);

        // 6. Pass input data to Resampler

        _transform->ProcessInput(0, rInSample, 0);

        SAFE_RELEASE(rInBuffer);
        SAFE_RELEASE(rInSample);

        // 7. Perform sample rate conversion

        IMFMediaBuffer* rOutBuffer = NULL;
        BYTE* rOutByteBuffer = NULL;

        MFT_OUTPUT_DATA_BUFFER rOutDataBuffer;
        DWORD rStatus;
        DWORD rBytes = outputBufferSize; // maximum bytes accepted per ProcessOutput

        // 7.1 Create Sample object for output data

        memset(&rOutDataBuffer, 0, sizeof rOutDataBuffer);
        MFCreateSample(&(rOutDataBuffer.pSample));
        MFCreateMemoryBuffer(rBytes, &rOutBuffer);
        rOutDataBuffer.pSample->AddBuffer(rOutBuffer);
        rOutDataBuffer.dwStreamID = 0;
        rOutDataBuffer.dwStatus = 0;
        rOutDataBuffer.pEvents = NULL;

        // 7.2 Get output data from Resampler

        if (_transform->ProcessOutput(0, 1, &rOutDataBuffer, &rStatus) == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            outSampleCount = 0;
            SAFE_RELEASE(rOutBuffer);
            SAFE_RELEASE(rOutDataBuffer.pSample);
            return;
        }

        // 7.3 Write output data to outBuffer

        SAFE_RELEASE(rOutBuffer);
        rOutDataBuffer.pSample->ConvertToContiguousBuffer(&rOutBuffer);
        rOutBuffer->GetCurrentLength(&rBytes);

        rOutBuffer->Lock(&rOutByteBuffer, NULL, NULL);
        memcpy(outBuffer, rOutByteBuffer, rBytes);
        rOutBuffer->Unlock();
        rOutByteBuffer = NULL;

        outSampleCount = rBytes / _bytesPerSample / _channelCount;
        SAFE_RELEASE(rOutBuffer);
        SAFE_RELEASE(rOutDataBuffer.pSample);
    }

private:
    unsigned int _bytesPerSample;
    unsigned int _channelCount;
    float _sampleRatio;

    IUnknown* _transformUnk;
    IMFTransform* _transform;
    IMFMediaType* _mediaType;
    IMFMediaType* _inputMediaType;
    IMFMediaType* _outputMediaType;

#ifdef __IWMResamplerProps_FWD_DEFINED__
    IWMResamplerProps* _resamplerProps;
#endif
};
