#include "RtApiCoreProber.h"
#include "CoreCommon.h"
#include <CoreAudio/AudioHardware.h>

std::optional<RtAudio::DeviceInfo> RtApiCoreProber::probeDevice(const std::string &busId)
{
    auto dev_opt = CoreCommon::getDeviceByBusID(busId);
    if (!dev_opt) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiCore::probeDevices: OS-X system error getting device IDs.");
        return {};
    }
    AudioDeviceID id = dev_opt.value();

    auto name_opt = CoreCommon::getDeviceFriendlyName(id);
    if (!name_opt) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: Failed to get name.");
        return {};
    }
    unsigned int inputChannels = CoreCommon::getInputChannels(id);
    unsigned int outputChannels = CoreCommon::getOutputChannels(id);
    if (inputChannels == 0 && outputChannels == 0) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: No channels for device.");
        return {};
    }
    AudioObjectPropertyScope scope = kAudioDevicePropertyScopeInput;
    if (outputChannels > 0) {
        scope = kAudioDevicePropertyScopeOutput;
    }
    auto sampleRates = CoreCommon::getSamplerates(id, scope);
    if (sampleRates.empty()) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiCore::probeDevices: No samplerates for device.");
        return {};
    }
    unsigned int currentSamplerate = sampleRates[sampleRates.size() - 1];
    auto currentSamplerate_opt = CoreCommon::getCurrentSamplerate(id, scope);
    if (currentSamplerate_opt) {
        currentSamplerate = currentSamplerate_opt.value();
    }

    RtAudio::DeviceInfo info{};
    info.partial.busID = busId;
    info.partial.name = name_opt.value();
    info.partial.supportsInput = inputChannels > 0;
    info.partial.supportsOutput = outputChannels > 0;
    info.outputChannels = outputChannels;
    info.inputChannels = inputChannels;
    info.sampleRates = std::move(sampleRates);
    info.currentSampleRate = currentSamplerate;
    info.preferredSampleRate = currentSamplerate;
    info.nativeFormats = RTAUDIO_FLOAT32;

    return info;
}
