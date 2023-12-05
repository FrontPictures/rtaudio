#pragma once
#include <CoreAudio/AudioHardware.h>
#include <optional>
#include <string>

namespace CoreCommon {
unsigned int getInputChannels(AudioDeviceID id);
unsigned int getOutputChannels(AudioDeviceID id);
std::optional<std::string> getDeviceBusID(AudioDeviceID id);
std::optional<std::string> getManufacturerName(AudioDeviceID id);
std::optional<std::string> getDeviceName(AudioDeviceID id);
std::optional<std::string> getDeviceFriendlyName(AudioDeviceID id);
std::vector<unsigned int> getSamplerates(AudioDeviceID id, AudioObjectPropertyScope scope);
std::optional<unsigned int> getCurrentSamplerate(AudioDeviceID id, AudioObjectPropertyScope scope);
std::optional<AudioDeviceID> getDeviceByBusID(const std::string &id);
} // namespace CoreCommon
