#include "CoreCommon.h"
#include "RtAudio.h"
#include <cmath>

namespace CoreCommon {

unsigned int getDeviceChannels(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress property = {kAudioDevicePropertyStreamConfiguration,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    UInt32 dataSize = 0;
    OSStatus result = AudioObjectGetPropertyDataSize(id, &property, 0, NULL, &dataSize);
    if (result != noErr || dataSize == 0) {
        return 0;
    }
    std::unique_ptr<char[]> rawBuffer = std::make_unique<char[]>(dataSize);
    AudioBufferList *bufferList = reinterpret_cast<AudioBufferList *>(rawBuffer.get());

    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, bufferList);
    if (result != noErr || dataSize == 0) {
        return 0;
    }
    unsigned int nStreams = bufferList->mNumberBuffers;
    unsigned int outputChannels = 0;
    if (nStreams == 0) {
        return 0;
    }
    outputChannels += bufferList->mBuffers[0].mNumberChannels;
    return outputChannels;
}

unsigned int getInputChannels(AudioDeviceID id)
{
    return getDeviceChannels(id, kAudioDevicePropertyScopeInput);
}

unsigned int getOutputChannels(AudioDeviceID id)
{
    return getDeviceChannels(id, kAudioDevicePropertyScopeOutput);
}

std::string GetSTDStringFromCString(CFStringRef str)
{
    long length = CFStringGetLength(str);
    char *mname = (char *) malloc(length * 3 + 1);
#if defined(UNICODE) || defined(_UNICODE)
    CFStringGetCString(str, mname, length * 3 + 1, kCFStringEncodingUTF8);
#else
    CFStringGetCString(str, mname, length * 3 + 1, CFStringGetSystemEncoding());
#endif
    std::string res = mname;
    free(mname);
    return res;
}

std::optional<std::string> getStringProperty(AudioDeviceID id, AudioObjectPropertySelector selector)
{
    CFStringRef cfname = nullptr;
    UInt32 dataSize = sizeof(CFStringRef);
    OSStatus result = 0;

    AudioObjectPropertyAddress property = {selector,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &cfname);
    if (result != noErr) {
        return {};
    }
    auto mname_str = GetSTDStringFromCString(cfname);
    CFRelease(cfname);
    return mname_str;
}

std::optional<std::string> getDeviceBusID(AudioDeviceID id)
{
    return getStringProperty(id, kAudioDevicePropertyDeviceUID);
}
std::optional<std::string> getManufacturerName(AudioDeviceID id)
{
    return getStringProperty(id, kAudioObjectPropertyManufacturer);
}

std::optional<std::string> getDeviceName(AudioDeviceID id)
{
    return getStringProperty(id, kAudioObjectPropertyName);
}

std::vector<unsigned int> getSamplerates(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    OSStatus result = 0;
    UInt32 dataSize = 0;
    AudioObjectPropertyAddress property = {kAudioDevicePropertyAvailableNominalSampleRates,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    result = AudioObjectGetPropertyDataSize(id, &property, 0, NULL, &dataSize);
    if (result != kAudioHardwareNoError || dataSize == 0) {
        return {};
    }

    UInt32 nRanges = dataSize / sizeof(AudioValueRange);
    AudioValueRange rangeList[nRanges];
    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &rangeList);
    if (result != kAudioHardwareNoError) {
        return {};
    }

    // The sample rate reporting mechanism is a bit of a mystery.  It
    // seems that it can either return individual rates or a range of
    // rates.  I assume that if the min / max range values are the same,
    // then that represents a single supported rate and if the min / max
    // range values are different, the device supports an arbitrary
    // range of values (though there might be multiple ranges, so we'll
    // use the most conservative range).
    Float64 minimumRate = 1.0, maximumRate = 10000000000.0;
    bool haveValueRange = false;
    std::vector<unsigned int> samplerates;
    for (UInt32 i = 0; i < nRanges; i++) {
        if (rangeList[i].mMinimum == rangeList[i].mMaximum) {
            unsigned int tmpSr = (unsigned int) rangeList[i].mMinimum;
            samplerates.push_back(tmpSr);
        } else {
            haveValueRange = true;
            if (rangeList[i].mMinimum > minimumRate)
                minimumRate = rangeList[i].mMinimum;
            if (rangeList[i].mMaximum < maximumRate)
                maximumRate = rangeList[i].mMaximum;
        }
    }

    if (haveValueRange) {
        for (unsigned int k = 0; k < RtAudio::MAX_SAMPLE_RATES; k++) {
            if (RtAudio::SAMPLE_RATES[k] >= (unsigned int) minimumRate
                && RtAudio::SAMPLE_RATES[k] <= (unsigned int) maximumRate) {
                samplerates.push_back(RtAudio::SAMPLE_RATES[k]);
            }
        }
    }

    // Sort and remove any redundant values
    std::sort(samplerates.begin(), samplerates.end());
    samplerates.erase(unique(samplerates.begin(), samplerates.end()), samplerates.end());
    return samplerates;
}

std::optional<unsigned int> getCurrentSamplerate(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    Float64 nominalRate = 0;
    UInt32 dataSize = sizeof(Float64);
    OSStatus result = 0;

    AudioObjectPropertyAddress property = {kAudioDevicePropertyNominalSampleRate,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &nominalRate);
    if (result != noErr) {
        return {};
    }
    return (unsigned int) nominalRate;
}

std::optional<AudioDeviceID> getDeviceByBusID(const std::string &busID)
{
    UInt32 dataSize = sizeof(AudioDeviceID);
    OSStatus result = 0;
    AudioDeviceID id = 0;

    AudioObjectPropertyAddress property = {kAudioHardwarePropertyTranslateUIDToDevice,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    CFStringRef cfbusid = nullptr;
    cfbusid = CFStringCreateWithCString(nullptr, busID.c_str(), CFStringGetSystemEncoding());
    if (!cfbusid)
        return {};

    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &property,
                                        sizeof(CFStringRef),
                                        &cfbusid,
                                        &dataSize,
                                        &id);
    CFRelease(cfbusid);
    if (result != noErr)
        return {};
    return id;
}

std::optional<std::string> getDeviceFriendlyName(AudioDeviceID id)
{
    auto devName_opt = CoreCommon::getDeviceName(id);
    if (!devName_opt) {
        return {};
    }
    return devName_opt.value();
}

bool coreAudioHog(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    pid_t hog_pid = 0;
    UInt32 dataSize = sizeof(hog_pid);
    OSStatus result = 0;

    AudioObjectPropertyAddress property = {kAudioDevicePropertyHogMode,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &hog_pid);
    if (result != noErr) {
        return false;
    }

    if (hog_pid != getpid()) {
        hog_pid = getpid();
        result = AudioObjectSetPropertyData(id, &property, 0, NULL, dataSize, &hog_pid);
        if (result != noErr) {
            return false;
        }
    }
    return true;
}

bool setDeviceSamplerate(AudioDeviceID id, AudioObjectPropertyScope scope, unsigned int samplerate)
{
    UInt32 dataSize = sizeof(Float64);
    OSStatus result = 0;

    AudioObjectPropertyAddress property = {kAudioDevicePropertyNominalSampleRate,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    Float64 nominalRate = samplerate;
    result = AudioObjectSetPropertyData(id, &property, 0, NULL, dataSize, &nominalRate);
    if (result != noErr) {
        return false;
    }
    return true;
}

bool waitDeviceSamplerate(AudioDeviceID id,
                          AudioObjectPropertyScope scope,
                          unsigned int samplerate,
                          unsigned int timeout_ms)
{
    UInt32 dataSize = sizeof(Float64);
    OSStatus result = 0;
    constexpr int sleepMicrosecs = 5000;

    AudioObjectPropertyAddress property = {kAudioDevicePropertyNominalSampleRate,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    UInt32 microCounter = 0;
    Float64 reportedRate = 0.0;
    Float64 nominalRate = samplerate;
    while (std::fabs(reportedRate - nominalRate) > 1) {
        microCounter += sleepMicrosecs;
        if (microCounter > timeout_ms * 1000)
            return false;
        usleep(sleepMicrosecs);
        result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &reportedRate);
        if (result != noErr) {
            return false;
        }
    }
    return true;
}

std::optional<AudioValueRange> getDeviceBufferSizeRange(AudioDeviceID id,
                                                        AudioObjectPropertyScope scope)
{
    UInt32 dataSize = sizeof(AudioValueRange);
    OSStatus result = 0;

    AudioObjectPropertyAddress property = {kAudioDevicePropertyBufferFrameSizeRange,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    AudioValueRange bufferRange{};
    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &bufferRange);

    if (result != noErr) {
        return {};
    }
    return bufferRange;
}

bool setDeviceBufferSize(AudioDeviceID id, AudioObjectPropertyScope scope, unsigned int bufferSize)
{
    UInt32 dataSize = sizeof(UInt32);
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {kAudioDevicePropertyBufferFrameSize,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    // Set the buffer size.  For multiple streams, I'm assuming we only
    // need to make this setting for the master channel.
    UInt32 theSize = (UInt32) bufferSize;
    result = AudioObjectSetPropertyData(id, &property, 0, NULL, dataSize, &theSize);
    if (result != noErr) {
        return false;
    }
    return true;
}

std::optional<AudioStreamBasicDescription> getStreamDescription(AudioDeviceID id,
                                                                AudioObjectPropertyScope scope,
                                                                AudioObjectPropertySelector selector)
{
    UInt32 dataSize = sizeof(AudioStreamBasicDescription);
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {selector, scope, KAUDIOOBJECTPROPERTYELEMENT};
    AudioStreamBasicDescription description{};
    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &description);
    if (result != noErr) {
        return {};
    }
    return description;
}
std::optional<AudioStreamBasicDescription> getVirtualStreamDescription(
    AudioDeviceID id, AudioObjectPropertyScope scope)
{
    return getStreamDescription(id, scope, kAudioStreamPropertyVirtualFormat);
}

std::optional<AudioStreamBasicDescription> getPhysicalStreamDescription(
    AudioDeviceID id, AudioObjectPropertyScope scope)
{
    return getStreamDescription(id, scope, kAudioStreamPropertyPhysicalFormat);
}

bool setStreamDescription(AudioDeviceID id,
                          AudioObjectPropertyScope scope,
                          AudioObjectPropertySelector selector,
                          AudioStreamBasicDescription description)
{
    UInt32 dataSize = sizeof(AudioStreamBasicDescription);
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {selector, scope, KAUDIOOBJECTPROPERTYELEMENT};
    result = AudioObjectSetPropertyData(id, &property, 0, NULL, dataSize, &description);
    if (result != noErr) {
        return false;
    }
    return true;
}

bool setVirtualStreamDescription(AudioDeviceID id,
                                 AudioObjectPropertyScope scope,
                                 AudioStreamBasicDescription description)
{
    return setStreamDescription(id, scope, kAudioStreamPropertyVirtualFormat, description);
}

bool setPhyisicalStreamDescription(AudioDeviceID id,
                                   AudioObjectPropertyScope scope,
                                   AudioStreamBasicDescription desc)
{
    return setStreamDescription(id, scope, kAudioStreamPropertyPhysicalFormat, desc);
}

std::optional<unsigned int> getDeviceLatency(AudioDeviceID id, AudioObjectPropertyScope scope)
{
    UInt32 dataSize = sizeof(UInt32);
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {kAudioDevicePropertyLatency,
                                           scope,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    if (AudioObjectHasProperty(id, &property) == false) {
        return {};
    }
    UInt32 latency = 0;
    result = AudioObjectGetPropertyData(id, &property, 0, NULL, &dataSize, &latency);
    if (result != kAudioHardwareNoError) {
        return {};
    }
    return latency;
}

const char *getErrorCode(OSStatus code)
{
    switch (code) {
    case kAudioHardwareNotRunningError:
        return "kAudioHardwareNotRunningError";

    case kAudioHardwareUnspecifiedError:
        return "kAudioHardwareUnspecifiedError";

    case kAudioHardwareUnknownPropertyError:
        return "kAudioHardwareUnknownPropertyError";

    case kAudioHardwareBadPropertySizeError:
        return "kAudioHardwareBadPropertySizeError";

    case kAudioHardwareIllegalOperationError:
        return "kAudioHardwareIllegalOperationError";

    case kAudioHardwareBadObjectError:
        return "kAudioHardwareBadObjectError";

    case kAudioHardwareBadDeviceError:
        return "kAudioHardwareBadDeviceError";

    case kAudioHardwareBadStreamError:
        return "kAudioHardwareBadStreamError";

    case kAudioHardwareUnsupportedOperationError:
        return "kAudioHardwareUnsupportedOperationError";

    case kAudioDeviceUnsupportedFormatError:
        return "kAudioDeviceUnsupportedFormatError";

    case kAudioDevicePermissionsError:
        return "kAudioDevicePermissionsError";

    default:
        return "CoreAudio unknown error";
    }
}

bool registerListener(AudioDeviceID id,
                      AudioObjectPropertySelector selector,
                      AudioObjectPropertyListenerProc listener,
                      void *userdata)
{
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {selector,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    result = AudioObjectAddPropertyListener(id, &property, listener, userdata);
    if (result != noErr) {
        return false;
    }
    return true;
}

bool unregisterListener(AudioDeviceID id,
                        AudioObjectPropertySelector selector,
                        AudioObjectPropertyListenerProc listener,
                        void *userdata)
{
    OSStatus result = 0;
    AudioObjectPropertyAddress property = {selector,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT};

    result = AudioObjectRemovePropertyListener(id, &property, listener, userdata);
    if (result != noErr) {
        return false;
    }
    return true;
}

bool registerDeviceAliveCallback(AudioDeviceID id,
                                 AudioObjectPropertyListenerProc listener,
                                 void *userdata)
{
    return registerListener(id, kAudioDevicePropertyDeviceIsAlive, listener, userdata);
}

bool unregisterDeviceAliveCallback(AudioDeviceID id,
                                   AudioObjectPropertyListenerProc listener,
                                   void *userdata)
{
    return unregisterListener(id, kAudioDevicePropertyDeviceIsAlive, listener, userdata);
}

bool registerDeviceOverloadCallback(AudioDeviceID id,
                                    AudioObjectPropertyListenerProc listener,
                                    void *userdata)
{
    return registerListener(id, kAudioDeviceProcessorOverload, listener, userdata);
}

bool unregisterDeviceOverloadCallback(AudioDeviceID id,
                                      AudioObjectPropertyListenerProc listener,
                                      void *userdata)
{
    return unregisterListener(id, kAudioDeviceProcessorOverload, listener, userdata);
}

} // namespace CoreCommon
