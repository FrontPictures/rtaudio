#include "CoreCommon.h"
#include "RtAudio.h"

#if defined(MAC_OS_VERSION_12_0) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0)
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMain
#else
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMaster // deprecated with macOS 12
#endif

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
    for (int i = 0; i < nStreams; i++)
        outputChannels += bufferList->mBuffers[i].mNumberChannels;
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
    auto manufacture_opt = CoreCommon::getManufacturerName(id);

    if (!devName_opt || !manufacture_opt) {
        return {};
    }
    return manufacture_opt.value() + ": " + devName_opt.value();
}

} // namespace CoreCommon
