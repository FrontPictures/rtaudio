#include "RtApiAsioStreamFactory.h"
#include "RtApiAsioStream.h"

namespace {
    ASIOCallbacks asioCallbacks;

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

    RtApi::RtApiStream stream_{};
    auto stream = createAsioStream(driverName, params, stream_);
    apiAsioStream = stream.get();
    if (stream)
        return stream;
    apiAsioStream = nullptr;
    ASIODisposeBuffers();
    ASIOExit();
    drivers.removeCurrentDriver();
    return {};
}

std::shared_ptr<RtApiAsioStream> RtApiAsioStreamFactory::createAsioStream(const char* driverName, CreateStreamParams params, RtApi::RtApiStream& stream_)
{
    ASIOError result = 0;
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

    asioCallbacks.bufferSwitch = &asioBufferSwitch;
    asioCallbacks.sampleRateDidChange = &asioSampleRateChangedGlobal;
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
    if (setupStreamWithParams(stream_, params) == false) {
        return {};
    }
    if (setupStreamCommon(stream_) == false) {
        return {};
    }
    return std::make_shared<RtApiAsioStream>(std::move(stream_), std::move(infos));
}
