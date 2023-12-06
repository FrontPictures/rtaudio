#include "RtApiCoreStreamFactory.h"
#include "CoreCommon.h"

namespace {

bool setDesciptionParameters(AudioStreamBasicDescription &description, unsigned int sampleRate)
{
    bool updateFormat = false;
    Float64 sampleRateFloat = (Float64) sampleRate;
    if (std::fabs(description.mSampleRate - sampleRateFloat > 1.0)) {
        description.mSampleRate = sampleRateFloat;
        updateFormat = true;
    }

    if (description.mFormatID != kAudioFormatLinearPCM) {
        description.mFormatID = kAudioFormatLinearPCM;
        updateFormat = true;
    }
    return updateFormat;
}

std::vector<std::pair<UInt32, UInt32>> getPhysicalTryFormats(AudioFormatFlags flags)
{
    std::vector<std::pair<UInt32, UInt32>> physicalFormats;
    UInt32 formatFlags = 0;

    formatFlags = (flags | kLinearPCMFormatFlagIsFloat) & ~kLinearPCMFormatFlagIsSignedInteger;
    physicalFormats.push_back(std::pair<Float32, UInt32>(32, formatFlags));
    formatFlags = (flags | kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked)
                  & ~kLinearPCMFormatFlagIsFloat;
    physicalFormats.push_back(std::pair<Float32, UInt32>(32, formatFlags));
    physicalFormats.push_back(std::pair<Float32, UInt32>(24, formatFlags)); // 24-bit packed
    formatFlags &= ~(kAudioFormatFlagIsPacked | kAudioFormatFlagIsAlignedHigh);
    physicalFormats.push_back(
        std::pair<Float32, UInt32>(24.2, formatFlags)); // 24-bit in 4 bytes, aligned low
    formatFlags |= kAudioFormatFlagIsAlignedHigh;
    physicalFormats.push_back(
        std::pair<Float32, UInt32>(24.4, formatFlags)); // 24-bit in 4 bytes, aligned high
    formatFlags = (flags | kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked)
                  & ~kLinearPCMFormatFlagIsFloat;
    physicalFormats.push_back(std::pair<Float32, UInt32>(16, formatFlags));
    physicalFormats.push_back(std::pair<Float32, UInt32>(8, formatFlags));
    return physicalFormats;
}

bool setupPhysicalDescription(AudioDeviceID id,
                              AudioObjectPropertyScope scope,
                              AudioStreamBasicDescription description)
{
    if (description.mFormatID == kAudioFormatLinearPCM && description.mBitsPerChannel >= 16) {
        return true;
    }

    description.mFormatID = kAudioFormatLinearPCM;
    //description.mSampleRate = (Float64) sampleRate;

    std::vector<std::pair<UInt32, UInt32>> physicalFormats = getPhysicalTryFormats(
        description.mFormatFlags);

    bool setPhysicalFormat = false;
    for (unsigned int i = 0; i < physicalFormats.size(); i++) {
        AudioStreamBasicDescription testDescription = description;
        testDescription.mBitsPerChannel = (UInt32) physicalFormats[i].first;
        testDescription.mFormatFlags = physicalFormats[i].second;
        if ((24 == (UInt32) physicalFormats[i].first)
            && ~(physicalFormats[i].second & kAudioFormatFlagIsPacked))
            testDescription.mBytesPerFrame = 4 * testDescription.mChannelsPerFrame;
        else
            testDescription.mBytesPerFrame = testDescription.mBitsPerChannel / 8
                                             * testDescription.mChannelsPerFrame;
        testDescription.mBytesPerPacket = testDescription.mBytesPerFrame
                                          * testDescription.mFramesPerPacket;
        if (CoreCommon::setPhyisicalStreamDescription(id, scope, testDescription) == true) {
            return true;
        }
    }
    return false;
}

AudioBufferList *getDeviceStreamConfig(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    UInt32 dataSize = 0;
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {kAudioDevicePropertyStreamConfiguration,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    AudioBufferList *bufferList = nil;
    result = AudioObjectGetPropertyDataSize(id, &property, 0, NULL, &dataSize);
    if (result != noErr || dataSize == 0) {
        return nullptr;
    }

    // Allocate the AudioBufferList.
    bufferList = (AudioBufferList *) malloc(dataSize);
    if (bufferList == NULL) {
        return nullptr;
    }

    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, bufferList);
    if (result != noErr || dataSize == 0) {
        free(bufferList);
        return nullptr;
    }
    return bufferList;
}

std::optional<UInt32> getFirstStreamWithChannels(AudioBufferList *bufferList, unsigned int channels)
{
    // Look for a single stream meeting our needs.
    UInt32 nStreams = bufferList->mNumberBuffers;
    for (int iStream = 0; iStream < nStreams; iStream++) {
        UInt32 streamChannels = bufferList->mBuffers[iStream].mNumberChannels;
        if (streamChannels >= channels) {
            return iStream;
        }
    }
    return {};
}

void cht(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    auto *config = getDeviceStreamConfig(id, scope);
    if (!config) {
        return;
    }

    free(config);
}
} // namespace

std::shared_ptr<RtApiStreamClass> RtApiCoreStreamFactory::createStream(CreateStreamParams params)
{
    AudioDeviceID deviceId = 0;
    unsigned int deviceChannelsInput = 0;
    unsigned int deviceChannelsOutput = 0;

    auto dev_opt = CoreCommon::getDeviceByBusID(params.busId);
    if (!dev_opt) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiCore::probeDevices: OS-X system error getting device IDs.");
        return {};
    }
    deviceId = dev_opt.value();
    AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;

    if (params.options && params.options->flags & RTAUDIO_HOG_DEVICE) {
        if (CoreCommon::coreAudioHog(deviceId, scope)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error set hog device.");
            return {};
        }
    }

    deviceChannelsOutput = CoreCommon::getOutputChannels(deviceId);
    deviceChannelsInput = CoreCommon::getInputChannels(deviceId);

    auto nominalRate_opt = CoreCommon::getCurrentSamplerate(deviceId, scope);
    if (!nominalRate_opt) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error get samplerate.");
        return {};
    }
    if (std::fabs(nominalRate_opt.value() - (double) params.sampleRate) > 1.0) {
        if (CoreCommon::setDeviceSamplerate(deviceId, scope, params.sampleRate) == false) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error set samplerate.");
            return {};
        }
        if (CoreCommon::waitDeviceSamplerate(deviceId, scope, params.sampleRate, 2000) == false) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error wait samplerate changed.");
            return {};
        }
    }

    auto bufferRange_opt = CoreCommon::getDeviceBufferSizeRange(deviceId, scope);
    if (!bufferRange_opt) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error get buffer range.");
        return {};
    }
    params.bufferSize = std::fmax(params.bufferSize, bufferRange_opt->mMinimum);
    params.bufferSize = std::fminf(params.bufferSize, bufferRange_opt->mMaximum);
    if (params.options && params.options->flags & RTAUDIO_MINIMIZE_LATENCY)
        params.bufferSize = (unsigned int) bufferRange_opt->mMinimum;

    if (CoreCommon::setDeviceBufferSize(deviceId, scope, params.bufferSize) == false) {
        errorStream_
            << "RtApiCore::probeDeviceOpen: system error setting the buffer size for device ("
            << deviceId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    auto streamDesc_opt = CoreCommon::getVirtualStreamDescription(deviceId, scope);
    if (!streamDesc_opt) {
        errorStream_
            << "RtApiCore::probeDeviceOpen: system error getting stream format for device ("
            << deviceId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    if (setDesciptionParameters(streamDesc_opt.value(), params.sampleRate)) {
        if (CoreCommon::setVirtualStreamDescription(deviceId, scope, streamDesc_opt.value())
            == false) {
            errorStream_ << "RtApiCore::probeDeviceOpen: system error setting sample rate or data "
                            "format for device ("
                         << deviceId << ").";
            error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
            return {};
        }
    }

    streamDesc_opt = CoreCommon::getPhysicalStreamDescription(deviceId, scope);
    if (!streamDesc_opt) {
        errorStream_ << "RtApiCore::probeDeviceOpen: system error getting stream physical format "
                        "for device ("
                     << deviceId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    if (setupPhysicalDescription(deviceId, scope, streamDesc_opt.value()) == false) {
        errorStream_
            << "RtApiCore::probeDeviceOpen: system error setting physical data format for device ("
            << deviceId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    auto latency_opt = CoreCommon::getDeviceLatency(deviceId, scope);

    RtApi::RtApiStream stream_{};
    stream_.nDeviceChannels[RtApi::INPUT] = deviceChannelsInput;
    stream_.nDeviceChannels[RtApi::OUTPUT] = deviceChannelsOutput;

    stream_.deviceFormat[RtApi::INPUT] = RTAUDIO_FLOAT32;
    stream_.deviceFormat[RtApi::OUTPUT] = RTAUDIO_FLOAT32;

    stream_.doByteSwap[RtApi::INPUT] = false;
    stream_.doByteSwap[RtApi::OUTPUT] = false;

    stream_.deviceInterleaved[RtApi::INPUT] = true;
    stream_.deviceInterleaved[RtApi::OUTPUT] = true;

    stream_.latency[RtApi::INPUT] = latency_opt ? latency_opt.value() : 0;
    stream_.latency[RtApi::OUTPUT] = latency_opt ? latency_opt.value() : 0;

    stream_.nBuffers = 1;

    if (setupStreamWithParams(stream_, params) == false) {
        return {};
    }
    if (setupStreamCommon(stream_) == false) {
        return {};
    }

    return {};
}
