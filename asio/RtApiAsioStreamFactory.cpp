#include "RtApiAsioStreamFactory.h"

namespace {
    unsigned int calculateBufferSize(unsigned int bufferSize, long preferSize, long minSize, long maxSize, long granularity)
    {
        if (bufferSize == 0) bufferSize = preferSize;
        else if (bufferSize < (unsigned int)minSize) bufferSize = (unsigned int)minSize;
        else if (bufferSize > (unsigned int) maxSize) bufferSize = (unsigned int)maxSize;
        else if (granularity == -1) {
            // Make sure bufferSize is a power of two.
            int log2_of_min_size = 0;
            int log2_of_max_size = 0;

            for (unsigned int i = 0; i < sizeof(long) * 8; i++) {
                if (minSize & ((long)1 << i)) log2_of_min_size = i;
                if (maxSize & ((long)1 << i)) log2_of_max_size = i;
            }

            long min_delta = std::abs((long)bufferSize - ((long)1 << log2_of_min_size));
            int min_delta_num = log2_of_min_size;

            for (int i = log2_of_min_size + 1; i <= log2_of_max_size; i++) {
                long current_delta = std::abs((long)bufferSize - ((long)1 << i));
                if (current_delta < min_delta) {
                    min_delta = current_delta;
                    min_delta_num = i;
                }
            }

            bufferSize = ((unsigned int)1 << min_delta_num);
            if (bufferSize < (unsigned int)minSize) bufferSize = (unsigned int)minSize;
            else if (bufferSize > (unsigned int) maxSize) bufferSize = (unsigned int)maxSize;
        }
        else if (granularity != 0) {
            // Set to an even multiple of granularity, rounding up.
            bufferSize = (bufferSize + granularity - 1) / granularity * granularity;
        }
        return bufferSize;
    }

    static void bufferSwitch(long index, ASIOBool /*processNow*/)
    {

    }

    static void sampleRateChangedGlobal(ASIOSampleRate sRate)
    {

    }

    static long asioMessagesGlobal(long selector, long value, void* /*message*/, double* /*opt*/)
    {
        return 0;
    }

    bool setupFormat(const ASIOChannelInfo channelInfo, RtApi::RtApiStream& stream_)
    {
        RtAudioFormat deviceFormat = 0;
        bool doByteSwap = false;

        if (channelInfo.type == ASIOSTInt16MSB || channelInfo.type == ASIOSTInt16LSB) {
            deviceFormat = RTAUDIO_SINT16;
            if (channelInfo.type == ASIOSTInt16MSB) doByteSwap = true;
        }
        else if (channelInfo.type == ASIOSTInt32MSB || channelInfo.type == ASIOSTInt32LSB) {
            deviceFormat = RTAUDIO_SINT32;
            if (channelInfo.type == ASIOSTInt32MSB) doByteSwap = true;
        }
        else if (channelInfo.type == ASIOSTFloat32MSB || channelInfo.type == ASIOSTFloat32LSB) {
            deviceFormat = RTAUDIO_FLOAT32;
            if (channelInfo.type == ASIOSTFloat32MSB) doByteSwap = true;
        }
        else if (channelInfo.type == ASIOSTFloat64MSB || channelInfo.type == ASIOSTFloat64LSB) {
            deviceFormat = RTAUDIO_FLOAT64;
            if (channelInfo.type == ASIOSTFloat64MSB) doByteSwap = true;
        }
        else if (channelInfo.type == ASIOSTInt24MSB || channelInfo.type == ASIOSTInt24LSB) {
            deviceFormat = RTAUDIO_SINT24;
            if (channelInfo.type == ASIOSTInt24MSB) doByteSwap = true;
        }

        stream_.deviceFormat[RtApi::OUTPUT] = deviceFormat;
        stream_.deviceFormat[RtApi::INPUT] = deviceFormat;

        stream_.doByteSwap[RtApi::OUTPUT] = doByteSwap;
        stream_.doByteSwap[RtApi::INPUT] = doByteSwap;

        if (deviceFormat == 0)
            return false;
        return true;
    }

    void setupStreamWithParams(RtApi::RtApiStream& stream_, const CreateStreamParams& params)
    {
        stream_.userFormat = params.format;
        if (params.options && params.options->flags & RTAUDIO_NONINTERLEAVED) stream_.userInterleaved = false;
        else stream_.userInterleaved = true;
        stream_.sampleRate = params.sampleRate;
        stream_.deviceId = params.busId;
        stream_.mode = params.mode;
        stream_.bufferSize = params.bufferSize;
        stream_.nUserChannels[RtApi::OUTPUT] = params.channelsOutput;
        stream_.nUserChannels[RtApi::INPUT] = params.channelsInput;
    }

    void setupStreamCommon(RtApi::RtApiStream& stream_)
    {
        stream_.channelOffset[RtApi::OUTPUT] = 0;
        stream_.channelOffset[RtApi::INPUT] = 0;
        stream_.doConvertBuffer[RtApi::OUTPUT] = false;
        stream_.doConvertBuffer[RtApi::INPUT] = false;
        if (stream_.userFormat != stream_.deviceFormat[RtApi::OUTPUT])
            stream_.doConvertBuffer[RtApi::OUTPUT] = true;
        if (stream_.userFormat != stream_.deviceFormat[RtApi::INPUT])
            stream_.doConvertBuffer[RtApi::INPUT] = true;
        if (stream_.userInterleaved != stream_.deviceInterleaved[RtApi::OUTPUT] &&
            stream_.nUserChannels[RtApi::OUTPUT] > 1)
            stream_.doConvertBuffer[RtApi::OUTPUT] = true;
        if (stream_.userInterleaved != stream_.deviceInterleaved[RtApi::INPUT] &&
            stream_.nUserChannels[RtApi::INPUT] > 1)
            stream_.doConvertBuffer[RtApi::INPUT] = true;
    }

    bool allocateUserBuffer(RtApi::RtApiStream& stream_, RtApi::StreamMode mode)
    {
        unsigned long bufferBytesOutput = stream_.nUserChannels[mode] * stream_.bufferSize * RtApi::formatBytes(stream_.userFormat);
        if (bufferBytesOutput == 0) {
            return true;
        }
        stream_.userBuffer[mode] = (char*)calloc(bufferBytesOutput, 1);
        if (stream_.userBuffer[mode] == nullptr) {
            return false;
        }
    }

    bool allocateDeviceBuffer(RtApi::RtApiStream& stream_) {
        unsigned long maxBuffferSize = 0;

        if (stream_.doConvertBuffer[RtApi::OUTPUT] && stream_.nDeviceChannels[RtApi::OUTPUT] > 0) {
            unsigned long bufferBytesOutput = stream_.nDeviceChannels[RtApi::OUTPUT] * stream_.bufferSize * RtApi::formatBytes(stream_.deviceFormat[RtApi::OUTPUT]);
            maxBuffferSize = std::max(maxBuffferSize, bufferBytesOutput);
        }

        if (stream_.doConvertBuffer[RtApi::INPUT] && stream_.nDeviceChannels[RtApi::INPUT] > 0) {
            unsigned long bufferBytesOutput = stream_.nDeviceChannels[RtApi::INPUT] * stream_.bufferSize * RtApi::formatBytes(stream_.deviceFormat[RtApi::INPUT]);
            maxBuffferSize = std::max(maxBuffferSize, bufferBytesOutput);
        }

        if (maxBuffferSize == 0)
            return true;

        stream_.deviceBuffer = (char*)calloc(maxBuffferSize, 1);
        if (!stream_.deviceBuffer) {
            return false;
        }
        return true;
    }
}


std::shared_ptr<RtApiStreamClass> RtApiAsioStreamFactory::createStream(CreateStreamParams params)
{
    CLSID clsid{};
    auto c_opt = HexToCLSID(params.busId);
    if (!c_opt) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: unable to get class id for (" << params.busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    clsid = *c_opt;

    if (!drivers.loadDriverByCLSID(clsid)) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: unable to load driver (" << params.busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    char driverName[64]{};
    if (drivers.getCurrentDriverName(driverName) == false) {
        errorStream_ << "RtApiAsio::probeDevices: unable to get driver name.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    ASIODriverInfo driverInfo{};
    driverInfo.asioVersion = 2;
    driverInfo.sysRef = nullptr;
    ASIOError result = ASIOInit(&driverInfo);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") initializing driver (" << driverName << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    long minSize = 0, maxSize = 0, preferSize = 0, granularity = 0;
    long inputChannels = 0, outputChannels = 0;
    long inputLatency = 0, outputLatency = 0;

    result = ASIOGetChannels(&inputChannels, &outputChannels);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") getting channel count (" << driverName << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    int nChannels = inputChannels + outputChannels;
    if (nChannels == 0) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: no channels (" << driverName << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    result = ASIOCanSampleRate((ASIOSampleRate)params.sampleRate);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") does not support requested sample rate (" << params.sampleRate << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    ASIOSampleRate currentRate = 0;
    result = ASIOGetSampleRate(&currentRate);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error getting sample rate.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    if (currentRate != params.sampleRate) {
        result = ASIOSetSampleRate((ASIOSampleRate)params.sampleRate);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error setting sample rate (" << params.sampleRate << ").";
            error(RTAUDIO_WARNING, errorStream_.str());
            return {};
        }
    }

    ASIOChannelInfo channelInfo{};
    channelInfo.channel = 0;
    if (params.mode == RtApi::StreamMode::OUTPUT) channelInfo.isInput = false;
    else channelInfo.isInput = true;
    result = ASIOGetChannelInfo(&channelInfo);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting data format.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    result = ASIOGetBufferSize(&minSize, &maxSize, &preferSize, &granularity);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting buffer size.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    params.bufferSize = calculateBufferSize(params.bufferSize, preferSize, minSize, maxSize, granularity);

    std::vector<ASIOBufferInfo> infos;
    try {
        infos.resize(nChannels);
    }
    catch (...) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: error allocating bufferInfo memory for driver (" << driverName << ").";
        error(RTAUDIO_MEMORY_ERROR, errorStream_.str());
        return {};
    }

    for (int i = 0; i < outputChannels; i++) {
        infos[i].isInput = ASIOFalse;
        infos[i].channelNum = i;
    }
    for (int i = 0; i < inputChannels; i++) {
        infos[i + outputChannels].isInput = ASIOTrue;
        infos[i + outputChannels].channelNum = i;
    }

    ASIOCallbacks asioCallbacks{};
    asioCallbacks.bufferSwitch = &bufferSwitch;
    asioCallbacks.sampleRateDidChange = &sampleRateChangedGlobal;
    asioCallbacks.asioMessage = &asioMessagesGlobal;
    asioCallbacks.bufferSwitchTimeInfo = NULL;

    result = ASIOCreateBuffers(infos.data(), nChannels, params.bufferSize, &asioCallbacks);
    if (result != ASE_OK) {
        // Standard method failed. This can happen with strict/misbehaving drivers that return valid buffer size ranges
        // but only accept the preferred buffer size as parameter for ASIOCreateBuffers (e.g. Creative's ASIO driver).
        // In that case, let's be naive and try that instead
        params.bufferSize = preferSize;
        result = ASIOCreateBuffers(infos.data(), nChannels, params.bufferSize, &asioCallbacks);
    }
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") creating buffers.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    result = ASIOGetLatencies(&inputLatency, &outputLatency);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: driver (" << driverName << ") error (" << getAsioErrorString(result) << ") getting latency.";
        error(RTAUDIO_WARNING, errorStream_.str()); // warn but don't fail
    }

    RtApi::RtApiStream stream_{};
    stream_.nDeviceChannels[RtApi::OUTPUT] = outputChannels;
    stream_.nDeviceChannels[RtApi::INPUT] = inputChannels;
    // ASIO always uses non-interleaved buffers.
    stream_.deviceInterleaved[RtApi::OUTPUT] = false;
    stream_.deviceInterleaved[RtApi::INPUT] = false;
    stream_.nBuffers = 2;
    stream_.latency[RtApi::OUTPUT] = outputLatency;
    stream_.latency[RtApi::INPUT] = inputLatency;
    if (setupFormat(channelInfo, stream_) == false) {
        errorStream_ << "RtApiAsio::probeDeviceOpen: failed to set sample format (" << driverName << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    setupStreamWithParams(stream_, params);
    setupStreamCommon(stream_);

    if (allocateUserBuffer(stream_, RtApi::OUTPUT) == false) {
        error(RTAUDIO_MEMORY_ERROR, "RtApiAsio::probeDeviceOpen: error allocating user buffer memory.");
        return {};
    }
    if (allocateUserBuffer(stream_, RtApi::INPUT) == false) {
        error(RTAUDIO_MEMORY_ERROR, "RtApiAsio::probeDeviceOpen: error allocating user buffer memory.");
        return {};
    }
    if (allocateDeviceBuffer(stream_) == false) {
        error(RTAUDIO_MEMORY_ERROR, "RtApiAsio::probeDeviceOpen: error allocating user buffer memory.");
        return {};
    }






    //error
    free(stream_.userBuffer[RtApi::OUTPUT]);
    free(stream_.userBuffer[RtApi::INPUT]);
    ASIODisposeBuffers();
    ASIOExit();
    drivers.removeCurrentDriver();

    return std::shared_ptr<RtApiStreamClass>();
}
