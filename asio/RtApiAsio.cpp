#include "RtApiAsio.h"

static AsioDrivers drivers;
static ASIOCallbacks asioCallbacks;
static ASIODriverInfo driverInfo;
static CallbackInfo* asioCallbackInfo;

static bool asioXRun;

namespace {
    template<class T>
    bool vector_contains(const std::vector<T>& vec, const T& val) {
        for (auto& e : vec) {
            if (e == val)
                return true;
        }
        return false;
    }
}

inline static std::string IntToHex(uint64_t v) {
    char buf[17]{ 0 };
    _ui64toa(v, buf, 16);
    if (strlen(buf) % 2 == 0) {
        return buf;
    }
    return std::string("0") + buf;
}

inline static std::string CLSIDToHex(CLSID id) {
    std::string res;
    res += IntToHex(id.Data1);
    res += IntToHex(id.Data2);
    res += IntToHex(id.Data3);
    for (int e = 0; e < 8; e++) {
        res += IntToHex(id.Data4[e]);
    }
    return res;
}

static long asioMessagesGlobal(long selector, long value, void* /*message*/, double* /*opt*/)
{
    RtApiAsio* object = (RtApiAsio*)asioCallbackInfo->object;
    if (object)
        return object->asioMessages(selector, value, nullptr, nullptr);
    return 0;
}

static const char* getAsioErrorString(ASIOError result)
{
    struct Messages
    {
        ASIOError value;
        const char* message;
    };

    static const Messages m[] =
    {
        {   ASE_NotPresent,    "Hardware input or output is not present or available." },
        {   ASE_HWMalfunction,  "Hardware is malfunctioning." },
        {   ASE_InvalidParameter, "Invalid input parameter." },
        {   ASE_InvalidMode,      "Invalid mode." },
        {   ASE_SPNotAdvancing,     "Sample position not advancing." },
        {   ASE_NoClock,            "Sample clock or rate cannot be determined or is not present." },
        {   ASE_NoMemory,           "Not enough memory to complete the request." }
    };

    for (unsigned int i = 0; i < sizeof(m) / sizeof(m[0]); ++i)
        if (m[i].value == result) return m[i].message;

    return "Unknown error.";
}

static void sampleRateChangedGlobal(ASIOSampleRate sRate)
{
    RtApiAsio* object = (RtApiAsio*)asioCallbackInfo->object;
    object->sampleRateChanged(sRate);
}

RtApiAsio::RtApiAsio()
{
    // ASIO cannot run on a multi-threaded apartment. You can call
    // CoInitialize beforehand, but it must be for apartment threading
    // (in which case, CoInitilialize will return S_FALSE here).
    coInitialized_ = false;
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        errorText_ = "RtApiAsio::ASIO requires a single-threaded apartment. Call CoInitializeEx(0,COINIT_APARTMENTTHREADED)";
        error(RTAUDIO_WARNING);
    }
    coInitialized_ = true;
    drivers.removeCurrentDriver();
    driverInfo.asioVersion = 2;
    driverInfo.sysRef = nullptr;
    listAsioDevices();
}

RtApiAsio::~RtApiAsio()
{
    closeStream();
    if (coInitialized_) CoUninitialize();
}

void RtApiAsio::listDevices(void) {}

void RtApiAsio::listAsioDevices()
{
    unsigned int nDevices = drivers.asioGetNumDev();
    if (nDevices == 0) {
        deviceList_.clear();
        return;
    }
    char tmp[64]{};
    CLSID driver_clsid{};
    unsigned int n = 0;
    for (n = 0; n < nDevices; n++) {
        ASIOError result = drivers.asioGetDriverName((int)n, tmp, 64);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDevices: unable to get driver name (" << getAsioErrorString(result) << ").";
            errorText_ = errorStream_.str();
            error(RTAUDIO_WARNING);
            continue;
        }
        result = drivers.asioGetDriverCLSID((int)n, &driver_clsid);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDevices: unable to get driver class id (" << getAsioErrorString(result) << ").";
            errorText_ = errorStream_.str();
            error(RTAUDIO_WARNING);
            continue;
        }

        RtAudio::DeviceInfo info;
        info.name = tmp;
        info.ID = currentDeviceId_++;  // arbitrary internal device ID
        info.busID = CLSIDToHex(driver_clsid);
        deviceList_.push_back(info);
    }
}

bool RtApiAsio::probeSingleDeviceInfo(RtAudio::DeviceInfo& info)
{
    if (!drivers.loadDriver(const_cast<char*>(info.name.c_str()))) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: unable to load driver (" << info.name << ").";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING);
        return false;
    }

    ASIOError result = ASIOInit(&driverInfo);
    if (result != ASE_OK) {
        drivers.removeCurrentDriver();
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") initializing driver (" << info.name << ").";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING);
        return false;
    }

    // Determine the device channel information.
    long inputChannels, outputChannels;
    result = ASIOGetChannels(&inputChannels, &outputChannels);
    if (result != ASE_OK) {
        ASIOExit();
        drivers.removeCurrentDriver();
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") getting channel count (" << info.name << ").";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING);
        return false;
    }

    info.outputChannels = outputChannels;
    info.inputChannels = inputChannels;
    if (info.outputChannels > 0 && info.inputChannels > 0)
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;

    // Determine the supported sample rates.
    info.sampleRates.clear();
    ASIOSampleRate currentRate = 0;
    result = ASIOGetSampleRate(&currentRate);
    if (result != ASE_OK && result != ASE_NoClock) {
        ASIOExit();
        drivers.removeCurrentDriver();
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") get samplerate (" << info.name << ").";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING);
        return false;
    }
    info.preferredSampleRate = currentRate;
    bool preferredSampleRateFound = info.preferredSampleRate ? true : false;

    for (unsigned int i = 0; i < MAX_SAMPLE_RATES; i++) {
        result = ASIOCanSampleRate((ASIOSampleRate)SAMPLE_RATES[i]);
        if (result == ASE_OK) {
            info.sampleRates.push_back(SAMPLE_RATES[i]);
            if (!preferredSampleRateFound) {
                if (!info.preferredSampleRate || (SAMPLE_RATES[i] <= 48000 && SAMPLE_RATES[i] > info.preferredSampleRate))
                    info.preferredSampleRate = SAMPLE_RATES[i];
            }
        }
    }
    if (vector_contains(info.sampleRates, info.preferredSampleRate) == false) {
        info.sampleRates.push_back(info.preferredSampleRate);
    }

    // Determine supported data types ... just check first channel and assume rest are the same.
    ASIOChannelInfo channelInfo;
    channelInfo.channel = 0;
    channelInfo.isInput = true;
    if (info.inputChannels <= 0) channelInfo.isInput = false;
    result = ASIOGetChannelInfo(&channelInfo);
    if (result != ASE_OK) {
        ASIOExit();
        drivers.removeCurrentDriver();
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") getting driver channel info (" << info.name << ").";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING);
        return false;
    }

    info.nativeFormats = 0;
    if (channelInfo.type == ASIOSTInt16MSB || channelInfo.type == ASIOSTInt16LSB)
        info.nativeFormats |= RTAUDIO_SINT16;
    else if (channelInfo.type == ASIOSTInt32MSB || channelInfo.type == ASIOSTInt32LSB)
        info.nativeFormats |= RTAUDIO_SINT32;
    else if (channelInfo.type == ASIOSTFloat32MSB || channelInfo.type == ASIOSTFloat32LSB)
        info.nativeFormats |= RTAUDIO_FLOAT32;
    else if (channelInfo.type == ASIOSTFloat64MSB || channelInfo.type == ASIOSTFloat64LSB)
        info.nativeFormats |= RTAUDIO_FLOAT64;
    else if (channelInfo.type == ASIOSTInt24MSB || channelInfo.type == ASIOSTInt24LSB)
        info.nativeFormats |= RTAUDIO_SINT24;

    ASIOExit();
    drivers.removeCurrentDriver();
    return true;
}

static void bufferSwitch(long index, ASIOBool /*processNow*/)
{
    RtApiAsio* object = (RtApiAsio*)asioCallbackInfo->object;
    object->callbackEvent(index);
}

bool RtApiAsio::probeDeviceOpen(unsigned int deviceId, StreamMode mode, unsigned int channels,
    unsigned int firstChannel, unsigned int sampleRate,
    RtAudioFormat format, unsigned int* bufferSize,
    RtAudio::StreamOptions* options)
{
    bool isDuplexInput = mode == INPUT && stream_.mode == OUTPUT;

    // For ASIO, a duplex stream MUST use the same driver.
    if (isDuplexInput && stream_.deviceId[0] != deviceId) {
        errorText_ = "RtApiAsio::probeDeviceOpen: an ASIO duplex stream must use the same device for input and output!";
        return FAILURE;
    }

    std::string driverName;
    for (unsigned int m = 0; m < deviceList_.size(); m++) {
        if (deviceList_[m].ID == deviceId) {
            driverName = deviceList_[m].name;
            break;
        }
    }

    if (driverName.empty()) {
        errorText_ = "RtApiAsio::probeDeviceOpen: device ID is invalid!";
        return FAILURE;
    }

    // Only load the driver once for duplex stream.
    ASIOError result;
    if (!isDuplexInput) {
        if (!drivers.loadDriver(const_cast<char*>(driverName.c_str()))) {
            errorStream_ << "RtApiAsio::probeDeviceOpen: unable to load driver (" << driverName << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }

        result = ASIOInit(&driverInfo);
        if (result != ASE_OK) {
            drivers.removeCurrentDriver();
            errorStream_ << "RtApiAsio::probeDeviceOpen: error (" << getAsioErrorString(result) << ") initializing driver (" << driverName << ").";
            errorText_ = errorStream_.str();
            return FAILURE;
        }
    }

    bool buffersAllocated = false;
    AsioHandle* handle = (AsioHandle*)stream_.apiHandle;
    unsigned int nChannels;

    // Check the device channel count.
    long inputChannels, outputChannels;
    result = ASIOGetChannels(&inputChannels, &outputChannels);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: error (" << getAsioErrorString(result) << ") getting channel count (" << driverName << ").";
        errorText_ = errorStream_.str();
        goto error;
    }

    if ((mode == OUTPUT && (channels + firstChannel) > (unsigned int)outputChannels) ||
        (mode == INPUT && (channels + firstChannel) > (unsigned int)inputChannels)) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") does not support requested channel count (" << channels << ") + offset (" << firstChannel << ").";
        errorText_ = errorStream_.str();
        goto error;
    }
    stream_.nDeviceChannels[mode] = channels;
    stream_.nUserChannels[mode] = channels;
    stream_.channelOffset[mode] = firstChannel;

    // Verify the sample rate is supported.
    result = ASIOCanSampleRate((ASIOSampleRate)sampleRate);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") does not support requested sample rate (" << sampleRate << ").";
        errorText_ = errorStream_.str();
        goto error;
    }

    // Get the current sample rate
    ASIOSampleRate currentRate;
    result = ASIOGetSampleRate(&currentRate);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error getting sample rate.";
        errorText_ = errorStream_.str();
        goto error;
    }

    // Set the sample rate only if necessary
    if (currentRate != sampleRate) {
        result = ASIOSetSampleRate((ASIOSampleRate)sampleRate);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error setting sample rate (" << sampleRate << ").";
            errorText_ = errorStream_.str();
            goto error;
        }
    }

    // Determine the driver data type.
    ASIOChannelInfo channelInfo;
    channelInfo.channel = 0;
    if (mode == OUTPUT) channelInfo.isInput = false;
    else channelInfo.isInput = true;
    result = ASIOGetChannelInfo(&channelInfo);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting data format.";
        errorText_ = errorStream_.str();
        goto error;
    }

    // Assuming WINDOWS host is always little-endian.
    stream_.doByteSwap[mode] = false;
    stream_.userFormat = format;
    stream_.deviceFormat[mode] = 0;
    if (channelInfo.type == ASIOSTInt16MSB || channelInfo.type == ASIOSTInt16LSB) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT16;
        if (channelInfo.type == ASIOSTInt16MSB) stream_.doByteSwap[mode] = true;
    }
    else if (channelInfo.type == ASIOSTInt32MSB || channelInfo.type == ASIOSTInt32LSB) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT32;
        if (channelInfo.type == ASIOSTInt32MSB) stream_.doByteSwap[mode] = true;
    }
    else if (channelInfo.type == ASIOSTFloat32MSB || channelInfo.type == ASIOSTFloat32LSB) {
        stream_.deviceFormat[mode] = RTAUDIO_FLOAT32;
        if (channelInfo.type == ASIOSTFloat32MSB) stream_.doByteSwap[mode] = true;
    }
    else if (channelInfo.type == ASIOSTFloat64MSB || channelInfo.type == ASIOSTFloat64LSB) {
        stream_.deviceFormat[mode] = RTAUDIO_FLOAT64;
        if (channelInfo.type == ASIOSTFloat64MSB) stream_.doByteSwap[mode] = true;
    }
    else if (channelInfo.type == ASIOSTInt24MSB || channelInfo.type == ASIOSTInt24LSB) {
        stream_.deviceFormat[mode] = RTAUDIO_SINT24;
        if (channelInfo.type == ASIOSTInt24MSB) stream_.doByteSwap[mode] = true;
    }

    if (stream_.deviceFormat[mode] == 0) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") data format not supported by RtAudio.";
        errorText_ = errorStream_.str();
        goto error;
    }

    // Set the buffer size.  For a duplex stream, this will end up
    // setting the buffer size based on the input constraints, which
    // should be ok.
    long minSize, maxSize, preferSize, granularity;
    result = ASIOGetBufferSize(&minSize, &maxSize, &preferSize, &granularity);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting buffer size.";
        errorText_ = errorStream_.str();
        goto error;
    }

    if (isDuplexInput) {
        // When this is the duplex input (output was opened before), then we have to use the same
        // buffersize as the output, because it might use the preferred buffer size, which most
        // likely wasn't passed as input to this. The buffer sizes have to be identically anyway,
        // So instead of throwing an error, make them equal. The caller uses the reference
        // to the "bufferSize" param as usual to set up processing buffers.

        *bufferSize = stream_.bufferSize;

    }
    else {
        if (*bufferSize == 0) *bufferSize = preferSize;
        else if (*bufferSize < (unsigned int)minSize) *bufferSize = (unsigned int)minSize;
        else if (*bufferSize > (unsigned int) maxSize) *bufferSize = (unsigned int)maxSize;
        else if (granularity == -1) {
            // Make sure bufferSize is a power of two.
            int log2_of_min_size = 0;
            int log2_of_max_size = 0;

            for (unsigned int i = 0; i < sizeof(long) * 8; i++) {
                if (minSize & ((long)1 << i)) log2_of_min_size = i;
                if (maxSize & ((long)1 << i)) log2_of_max_size = i;
            }

            long min_delta = std::abs((long)*bufferSize - ((long)1 << log2_of_min_size));
            int min_delta_num = log2_of_min_size;

            for (int i = log2_of_min_size + 1; i <= log2_of_max_size; i++) {
                long current_delta = std::abs((long)*bufferSize - ((long)1 << i));
                if (current_delta < min_delta) {
                    min_delta = current_delta;
                    min_delta_num = i;
                }
            }

            *bufferSize = ((unsigned int)1 << min_delta_num);
            if (*bufferSize < (unsigned int)minSize) *bufferSize = (unsigned int)minSize;
            else if (*bufferSize > (unsigned int) maxSize) *bufferSize = (unsigned int)maxSize;
        }
        else if (granularity != 0) {
            // Set to an even multiple of granularity, rounding up.
            *bufferSize = (*bufferSize + granularity - 1) / granularity * granularity;
        }
    }

    /*
    // we don't use it anymore, see above!
    // Just left it here for the case...
    if ( isDuplexInput && stream_.bufferSize != *bufferSize ) {
    errorText_ = "RtApiAsio::probeDeviceOpen: input/output buffersize discrepancy!";
    goto error;
    }
    */

    stream_.bufferSize = *bufferSize;
    stream_.nBuffers = 2;

    if (options && options->flags & RTAUDIO_NONINTERLEAVED) stream_.userInterleaved = false;
    else stream_.userInterleaved = true;

    // ASIO always uses non-interleaved buffers.
    stream_.deviceInterleaved[mode] = false;

    // Allocate, if necessary, our AsioHandle structure for the stream.
    if (handle == 0) {
        try {
            handle = new AsioHandle;
        }
        catch (std::bad_alloc&) {
            errorText_ = "RtApiAsio::probeDeviceOpen: error allocating AsioHandle memory.";
            goto error;
        }
        handle->bufferInfos = 0;
        stream_.apiHandle = (void*)handle;
    }

    // Create the ASIO internal buffers.  Since RtAudio sets up input
    // and output separately, we'll have to dispose of previously
    // created output buffers for a duplex stream.
    if (mode == INPUT && stream_.mode == OUTPUT) {
        ASIODisposeBuffers();
        if (handle->bufferInfos) free(handle->bufferInfos);
    }

    // Allocate, initialize, and save the bufferInfos in our stream callbackInfo structure.
    unsigned int i;
    nChannels = stream_.nDeviceChannels[0] + stream_.nDeviceChannels[1];
    handle->bufferInfos = (ASIOBufferInfo*)malloc(nChannels * sizeof(ASIOBufferInfo));
    if (handle->bufferInfos == NULL) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: error allocating bufferInfo memory for driver (" << driverName << ").";
        errorText_ = errorStream_.str();
        goto error;
    }

    ASIOBufferInfo* infos;
    infos = handle->bufferInfos;
    for (i = 0; i < stream_.nDeviceChannels[0]; i++, infos++) {
        infos->isInput = ASIOFalse;
        infos->channelNum = i + stream_.channelOffset[0];
        infos->buffers[0] = infos->buffers[1] = 0;
    }
    for (i = 0; i < stream_.nDeviceChannels[1]; i++, infos++) {
        infos->isInput = ASIOTrue;
        infos->channelNum = i + stream_.channelOffset[1];
        infos->buffers[0] = infos->buffers[1] = 0;
    }

    // prepare for callbacks
    stream_.sampleRate = sampleRate;
    stream_.deviceId[mode] = deviceId;
    stream_.mode = isDuplexInput ? DUPLEX : mode;

    // store this class instance before registering callbacks, that are going to use it
    asioCallbackInfo = &stream_.callbackInfo;
    stream_.callbackInfo.object = (void*)this;

    // Set up the ASIO callback structure and create the ASIO data buffers.
    asioCallbacks.bufferSwitch = &bufferSwitch;
    asioCallbacks.sampleRateDidChange = &sampleRateChangedGlobal;
    asioCallbacks.asioMessage = &asioMessagesGlobal;
    asioCallbacks.bufferSwitchTimeInfo = NULL;
    result = ASIOCreateBuffers(handle->bufferInfos, nChannels, stream_.bufferSize, &asioCallbacks);
    if (result != ASE_OK) {
        // Standard method failed. This can happen with strict/misbehaving drivers that return valid buffer size ranges
        // but only accept the preferred buffer size as parameter for ASIOCreateBuffers (e.g. Creative's ASIO driver).
        // In that case, let's be naïve and try that instead.
        *bufferSize = preferSize;
        stream_.bufferSize = *bufferSize;
        result = ASIOCreateBuffers(handle->bufferInfos, nChannels, stream_.bufferSize, &asioCallbacks);
    }

    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") creating buffers.";
        errorText_ = errorStream_.str();
        goto error;
    }
    buffersAllocated = true;
    stream_.state = STREAM_STOPPED;

    // Set flags for buffer conversion.
    stream_.doConvertBuffer[mode] = false;
    if (stream_.userFormat != stream_.deviceFormat[mode])
        stream_.doConvertBuffer[mode] = true;
    if (stream_.userInterleaved != stream_.deviceInterleaved[mode] &&
        stream_.nUserChannels[mode] > 1)
        stream_.doConvertBuffer[mode] = true;

    // Allocate necessary internal buffers
    unsigned long bufferBytes;
    bufferBytes = stream_.nUserChannels[mode] * *bufferSize * formatBytes(stream_.userFormat);
    stream_.userBuffer[mode] = (char*)calloc(bufferBytes, 1);
    if (stream_.userBuffer[mode] == NULL) {
        errorText_ = "RtApiAsio::probeDeviceOpen: error allocating user buffer memory.";
        goto error;
    }

    if (stream_.doConvertBuffer[mode]) {

        bool makeBuffer = true;
        bufferBytes = stream_.nDeviceChannels[mode] * formatBytes(stream_.deviceFormat[mode]);
        if (isDuplexInput && stream_.deviceBuffer) {
            unsigned long bytesOut = stream_.nDeviceChannels[0] * formatBytes(stream_.deviceFormat[0]);
            if (bufferBytes <= bytesOut) makeBuffer = false;
        }

        if (makeBuffer) {
            bufferBytes *= *bufferSize;
            if (stream_.deviceBuffer) free(stream_.deviceBuffer);
            stream_.deviceBuffer = (char*)calloc(bufferBytes, 1);
            if (stream_.deviceBuffer == NULL) {
                errorText_ = "RtApiAsio::probeDeviceOpen: error allocating device buffer memory.";
                goto error;
            }
        }
    }

    // Determine device latencies
    long inputLatency, outputLatency;
    result = ASIOGetLatencies(&inputLatency, &outputLatency);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting latency.";
        errorText_ = errorStream_.str();
        error(RTAUDIO_WARNING); // warn but don't fail
    }
    else {
        stream_.latency[0] = outputLatency;
        stream_.latency[1] = inputLatency;
    }

    // Setup the buffer conversion information structure.  We don't use
    // buffers to do channel offsets, so we override that parameter
    // here.
    if (stream_.doConvertBuffer[mode]) setConvertInfo(mode, 0);

    return SUCCESS;

error:
    if (!isDuplexInput) {
        // the cleanup for error in the duplex input, is done by RtApi::openStream
        // So we clean up for single channel only

        if (buffersAllocated)
            ASIODisposeBuffers();

        ASIOExit();
        drivers.removeCurrentDriver();

        if (handle) {
            if (handle->bufferInfos)
                free(handle->bufferInfos);

            delete handle;
            stream_.apiHandle = 0;
        }


        if (stream_.userBuffer[mode]) {
            free(stream_.userBuffer[mode]);
            stream_.userBuffer[mode] = 0;
        }

        if (stream_.deviceBuffer) {
            free(stream_.deviceBuffer);
            stream_.deviceBuffer = 0;
        }
    }

    return FAILURE;
}

void RtApiAsio::closeStream()
{
    if (stream_.state == STREAM_CLOSED) {
        errorText_ = "RtApiAsio::closeStream(): no open stream to close!";
        error(RTAUDIO_WARNING);
        return;
    }

    if (stream_.state == STREAM_RUNNING) {
        ASIOStop();//TODO: do we need stop when reset request received?
    }

    CallbackInfo* info = (CallbackInfo*)&stream_.callbackInfo;
    if (info->deviceDisconnected) {
        // This could be either a disconnect or a sample rate change.
        errorText_ = "RtApiAsio: the streaming device was disconnected or the sample rate changed, closing stream!";
        error(RTAUDIO_DEVICE_DISCONNECT);
    }

    ASIODisposeBuffers();
    ASIOExit();
    drivers.removeCurrentDriver();

    AsioHandle* handle = (AsioHandle*)stream_.apiHandle;
    if (handle) {
        if (handle->bufferInfos)
            free(handle->bufferInfos);
        delete handle;
        stream_.apiHandle = 0;
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
    stream_.state = STREAM_CLOSED;;
    //stream_.mode = UNINITIALIZED;
    //stream_.state = STREAM_CLOSED;
}

RtAudioErrorType RtApiAsio::startStream()
{
    if (stream_.state != STREAM_STOPPED) {
        if (stream_.state == STREAM_RUNNING)
            errorText_ = "RtApiAsio::startStream(): the stream is already running!";
        else if (stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiAsio::startStream(): the stream is stopping or closed!";
        return error(RTAUDIO_WARNING);
    }

    asioXRun = false;
    stream_.state = STREAM_RUNNING;
    ASIOError result = ASIOStart();
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::startStream: error (" << getAsioErrorString(result) << ") starting device.";
        errorText_ = errorStream_.str();
        goto unlock;
    }

unlock:
    if (result == ASE_OK) return RTAUDIO_NO_ERROR;
    return error(RTAUDIO_SYSTEM_ERROR);
}

RtAudioErrorType RtApiAsio::stopStream()
{
    if (stream_.state != STREAM_RUNNING && stream_.state != STREAM_STOPPING) {
        if (stream_.state == STREAM_STOPPED)
            errorText_ = "RtApiAsio::stopStream(): the stream is already stopped!";
        else if (stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiAsio::stopStream(): the stream is closed!";
        return error(RTAUDIO_WARNING);
    }
    ASIOError result = ASIOStop();
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::stopStream: error (" << getAsioErrorString(result) << ") stopping device.";
        errorText_ = errorStream_.str();
    }
    stream_.state = STREAM_STOPPED;
    if (result == ASE_OK) return RTAUDIO_NO_ERROR;
    return error(RTAUDIO_SYSTEM_ERROR);
}

RtAudioErrorType RtApiAsio::abortStream()
{
    if (stream_.state != STREAM_RUNNING) {
        if (stream_.state == STREAM_STOPPED)
            errorText_ = "RtApiAsio::abortStream(): the stream is already stopped!";
        else if (stream_.state == STREAM_STOPPING || stream_.state == STREAM_CLOSED)
            errorText_ = "RtApiAsio::abortStream(): the stream is stopping or closed!";
        return error(RTAUDIO_WARNING);
    }
    // The following lines were commented-out because some behavior was
    // noted where the device buffers need to be zeroed to avoid
    // continuing sound, even when the device buffers are completely
    // disposed.  So now, calling abort is the same as calling stop.
    // AsioHandle *handle = (AsioHandle *) stream_.apiHandle;
    // handle->drainCounter = 2;
    stopStream();
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiAsio::openAsioControlPanel(void)
{
    if (stream_.state == STREAM_CLOSED) {
        errorText_ = "RtApiAsio::openAsioControlPanel(): the stream is closed!";
        return error(RTAUDIO_WARNING);
    }
    ASIOControlPanel();
}

bool RtApiAsio::callbackEvent(long bufferIndex)
{
    if (stream_.state != STREAM_RUNNING) {
        errorText_ = "RtApiAsio::callbackEvent(): the stream is not running ... this shouldn't happen!";
        error(RTAUDIO_WARNING);
        return FAILURE;
    }

    CallbackInfo* info = (CallbackInfo*)&stream_.callbackInfo;
    AsioHandle* handle = (AsioHandle*)stream_.apiHandle;

    RtAudioCallback callback = (RtAudioCallback)info->callback;
    double streamTime = getStreamTime();
    RtAudioStreamStatus status = 0;
    if (stream_.mode != INPUT && asioXRun == true) {
        status |= RTAUDIO_OUTPUT_UNDERFLOW;
        asioXRun = false;
    }
    if (stream_.mode != OUTPUT && asioXRun == true) {
        status |= RTAUDIO_INPUT_OVERFLOW;
        asioXRun = false;
    }

    unsigned int nChannels = stream_.nDeviceChannels[0] + stream_.nDeviceChannels[1];
    unsigned int bufferBytes = 0, i = 0, j = 0;

    if (stream_.mode == INPUT || stream_.mode == DUPLEX) {

        bufferBytes = stream_.bufferSize * formatBytes(stream_.deviceFormat[1]);

        if (stream_.doConvertBuffer[1]) {

            // Always interleave ASIO input data.
            for (i = 0, j = 0; i < nChannels; i++) {
                if (handle->bufferInfos[i].isInput == ASIOTrue)
                    memcpy(&stream_.deviceBuffer[j++ * bufferBytes],
                        handle->bufferInfos[i].buffers[bufferIndex],
                        bufferBytes);
            }

            if (stream_.doByteSwap[1])
                byteSwapBuffer(stream_.deviceBuffer,
                    stream_.bufferSize * stream_.nDeviceChannels[1],
                    stream_.deviceFormat[1]);
            convertBuffer(stream_.userBuffer[1], stream_.deviceBuffer, stream_.convertInfo[1], stream_.bufferSize);

        }
        else {
            for (i = 0, j = 0; i < nChannels; i++) {
                if (handle->bufferInfos[i].isInput == ASIOTrue) {
                    memcpy(&stream_.userBuffer[1][bufferBytes * j++],
                        handle->bufferInfos[i].buffers[bufferIndex],
                        bufferBytes);
                }
            }

            if (stream_.doByteSwap[1])
                byteSwapBuffer(stream_.userBuffer[1],
                    stream_.bufferSize * stream_.nUserChannels[1],
                    stream_.userFormat);
        }
    }


    int cbReturnValue = callback(stream_.userBuffer[0], stream_.userBuffer[1],
        stream_.bufferSize, streamTime, status, info->userData);
    if (cbReturnValue == 2 || cbReturnValue == 1) {
        stopStream();
        return SUCCESS;
    }

    if (stream_.mode == OUTPUT || stream_.mode == DUPLEX) {

        bufferBytes = stream_.bufferSize * formatBytes(stream_.deviceFormat[0]);

        if (stream_.doConvertBuffer[0]) {

            convertBuffer(stream_.deviceBuffer, stream_.userBuffer[0], stream_.convertInfo[0], stream_.bufferSize);
            if (stream_.doByteSwap[0])
                byteSwapBuffer(stream_.deviceBuffer,
                    stream_.bufferSize * stream_.nDeviceChannels[0],
                    stream_.deviceFormat[0]);

            for (i = 0, j = 0; i < nChannels; i++) {
                if (handle->bufferInfos[i].isInput != ASIOTrue)
                    memcpy(handle->bufferInfos[i].buffers[bufferIndex],
                        &stream_.deviceBuffer[j++ * bufferBytes], bufferBytes);
            }

        }
        else {

            if (stream_.doByteSwap[0])
                byteSwapBuffer(stream_.userBuffer[0],
                    stream_.bufferSize * stream_.nUserChannels[0],
                    stream_.userFormat);

            for (i = 0, j = 0; i < nChannels; i++) {
                if (handle->bufferInfos[i].isInput != ASIOTrue)
                    memcpy(handle->bufferInfos[i].buffers[bufferIndex],
                        &stream_.userBuffer[0][bufferBytes * j++], bufferBytes);
            }

        }
    }

unlock:
    // The following call was suggested by Malte Clasen.  While the API
    // documentation indicates it should not be required, some device
    // drivers apparently do not function correctly without it.
    ASIOOutputReady();

    RtApi::tickStreamTime();
    return SUCCESS;
}

long RtApiAsio::asioMessages(long selector, long value, void* message, double* opt)
{
    long ret = 0;
    switch (selector) {
    case kAsioSelectorSupported:
        if (value == kAsioResetRequest
            || value == kAsioEngineVersion
            || value == kAsioResyncRequest
            || value == kAsioLatenciesChanged
            // The following three were added for ASIO 2.0, you don't
            // necessarily have to support them.
            || value == kAsioSupportsTimeInfo
            || value == kAsioSupportsTimeCode
            || value == kAsioSupportsInputMonitor)
            ret = 1L;
        break;
    case kAsioResetRequest:
        // This message is received when a device is disconnected (and
        // perhaps when the sample rate changes). It indicates that the
        // driver should be reset, which is accomplished by calling
        // ASIOStop(), ASIODisposeBuffers() and removing the driver. But
        // since this message comes from the driver, we need to let this
        // function return before attempting to close the stream and
        // remove the driver. Thus, we invoke a thread to initiate the
        // stream closing.        
        // std::cerr << "\nRtApiAsio: driver reset requested!!!" << std::endl;
        asioCallbackInfo->deviceDisconnected = true; // flag for either rate change or disconnect
        stream_.state = STREAM_ERROR;
        ret = 1L;
        break;
    case kAsioResyncRequest:
        // This informs the application that the driver encountered some
        // non-fatal data loss.  It is used for synchronization purposes
        // of different media.  Added mainly to work around the Win16Mutex
        // problems in Windows 95/98 with the Windows Multimedia system,
        // which could lose data because the Mutex was held too long by
        // another thread.  However a driver can issue it in other
        // situations, too.
        // std::cerr << "\nRtApiAsio: driver resync requested!!!" << std::endl;
        asioXRun = true;
        ret = 1L;
        break;
    case kAsioLatenciesChanged:
        // This will inform the host application that the drivers were
        // latencies changed.  Beware, it this does not mean that the
        // buffer sizes have changed!  You might need to update internal
        // delay data.
        // std::cerr << "\nRtApiAsio: driver latency may have changed!!!" << std::endl;
        ret = 1L;
        break;
    case kAsioEngineVersion:
        // Return the supported ASIO version of the host application.  If
        // a host application does not implement this selector, ASIO 1.0
        // is assumed by the driver.
        ret = 2L;
        break;
    case kAsioSupportsTimeInfo:
        // Informs the driver whether the
        // asioCallbacks.bufferSwitchTimeInfo() callback is supported.
        // For compatibility with ASIO 1.0 drivers the host application
        // should always support the "old" bufferSwitch method, too.
        ret = 0;
        break;
    case kAsioSupportsTimeCode:
        // Informs the driver whether application is interested in time
        // code info.  If an application does not need to know about time
        // code, the driver has less work to do.
        ret = 0;
        break;
    }
    return ret;
}

void RtApiAsio::sampleRateChanged(ASIOSampleRate sRate)
{
    // The ASIO documentation says that this usually only happens during
    // external sync.  Audio processing is not stopped by the driver,
    // actual sample rate might not have even changed, maybe only the
    // sample rate status of an AES/EBU or S/PDIF digital input at the
    // audio device.    
    if (getStreamSampleRate() != sRate) {
        asioCallbackInfo->deviceDisconnected = true; // flag for either rate change or disconnect
        stream_.state = STREAM_ERROR;
    }
}
