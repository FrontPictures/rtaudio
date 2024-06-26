#include "RtApiCoreStreamFactory.h"
#include "CoreCommon.h"
#include "RtApiCoreStream.h"
#include <cmath>

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

    deviceChannelsOutput = CoreCommon::getOutputChannels(deviceId);
    deviceChannelsInput = CoreCommon::getInputChannels(deviceId);
    if (deviceChannelsOutput == 0 && deviceChannelsInput == 0) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: no device channels.");
        return {};
    }

    std::optional<ScopeStreamStruct> outputContext;
    std::optional<ScopeStreamStruct> inputContext;

    if (params.mode == RtApi::INPUT || params.mode == RtApi::DUPLEX) {
        inputContext = setupStreamScope(params, kAudioDevicePropertyScopeInput, deviceId);
        if (!inputContext) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: failed to setup input context.");
            return {};
        }
    }
    if (params.mode == RtApi::OUTPUT || params.mode == RtApi::DUPLEX) {
        outputContext = setupStreamScope(params, kAudioDevicePropertyScopeOutput, deviceId);
        if (!outputContext) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: failed to setup input context.");
            return {};
        }
    }

    if (params.mode == RtApi::DUPLEX) {
        if (outputContext->bufferSize != inputContext->bufferSize) {
            error(RTAUDIO_SYSTEM_ERROR,
                  "RtApiCore::probeDevices: duplex mode buffer size mismatch.");
            return {};
        }
    }

    RtApi::RtApiStream stream_{};
    stream_.nDeviceChannels[RtApi::INPUT] = deviceChannelsInput;
    stream_.nDeviceChannels[RtApi::OUTPUT] = deviceChannelsOutput;

    stream_.deviceFormat[RtApi::INPUT] = RTAUDIO_FLOAT32;
    stream_.deviceFormat[RtApi::OUTPUT] = RTAUDIO_FLOAT32;

    // Byte-swapping: According to AudioHardware.h, the stream data will
    // always be presented in native-endian format, so we should never
    // need to byte swap.
    stream_.doByteSwap[RtApi::INPUT] = false;
    stream_.doByteSwap[RtApi::OUTPUT] = false;

    stream_.deviceInterleaved[RtApi::INPUT] = true;
    stream_.deviceInterleaved[RtApi::OUTPUT] = true;

    stream_.latency[RtApi::INPUT] = inputContext ? inputContext->latency : 0;
    stream_.latency[RtApi::OUTPUT] = outputContext ? outputContext->latency : 0;

    stream_.nBuffers = 1;

    if (setupStreamWithParams(stream_, params) == false) {
        return {};
    }
    if (setupStreamCommon(stream_) == false) {
        return {};
    }

    std::shared_ptr<RtApiCoreStream> coreStream = std::make_shared<RtApiCoreStream>(std::move(
                                                                                        stream_),
                                                                                    deviceId);
    if (coreStream->isValid() == false) {
        return {};
    }
    return coreStream;
}

std::optional<RtApiCoreStreamFactory::ScopeStreamStruct> RtApiCoreStreamFactory::setupStreamScope(
    const CreateStreamParams &params, AudioObjectPropertyScope scope, AudioDeviceID deviceId)
{
    unsigned int bufferSize = params.bufferSize;
    unsigned int latency = 0;

    if (params.options && params.options->flags & RTAUDIO_HOG_DEVICE) {
        if (CoreCommon::coreAudioHog(deviceId, scope)) {
            error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: error set hog device.");
            return {};
        }
    }

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
    bufferSize = std::fmax(params.bufferSize, bufferRange_opt->mMinimum);
    bufferSize = std::fminf(params.bufferSize, bufferRange_opt->mMaximum);
    if (params.options && params.options->flags & RTAUDIO_MINIMIZE_LATENCY)
        bufferSize = (unsigned int) bufferRange_opt->mMinimum;

    if (CoreCommon::setDeviceBufferSize(deviceId, scope, bufferSize) == false) {
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
    if (latency_opt) {
        latency = latency_opt.value();
    }

    ScopeStreamStruct scopeResult{};
    scopeResult.bufferSize = bufferSize;
    scopeResult.latency = latency;
    return scopeResult;
}
