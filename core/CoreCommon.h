#pragma once
#include <CoreAudio/AudioHardware.h>
#include <optional>
#include <string>

#if defined(MAC_OS_VERSION_12_0) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0)
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMain
#else
#define KAUDIOOBJECTPROPERTYELEMENT kAudioObjectPropertyElementMaster // deprecated with macOS 12
#endif

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
bool coreAudioHog(AudioDeviceID id, AudioObjectPropertyScope scope);
bool setDeviceSamplerate(AudioDeviceID id, AudioObjectPropertyScope scope, unsigned int samplerate);
bool waitDeviceSamplerate(AudioDeviceID id,
                          AudioObjectPropertyScope scope,
                          unsigned int samplerates,
                          unsigned int timeout_ms);
std::optional<AudioValueRange> getDeviceBufferSizeRange(AudioDeviceID id,
                                                        AudioObjectPropertyScope scope);
bool setDeviceBufferSize(AudioDeviceID id, AudioObjectPropertyScope scope, unsigned int bufferSize);
std::optional<AudioStreamBasicDescription> getVirtualStreamDescription(
    AudioDeviceID id, AudioObjectPropertyScope scope);
std::optional<AudioStreamBasicDescription> getPhysicalStreamDescription(
    AudioDeviceID id, AudioObjectPropertyScope scope);
bool setVirtualStreamDescription(AudioDeviceID id,
                                 AudioObjectPropertyScope scope,
                                 AudioStreamBasicDescription desc);
bool setPhyisicalStreamDescription(AudioDeviceID id,
                                   AudioObjectPropertyScope scope,
                                   AudioStreamBasicDescription desc);
std::optional<unsigned int> getDeviceLatency(AudioDeviceID id, AudioObjectPropertyScope scope);
const char *getErrorCode(OSStatus code);
bool registerDeviceAliveCallback(AudioDeviceID id,
                                 AudioObjectPropertyListenerProc listener,
                                 void *userdata);
bool unregisterDeviceAliveCallback(AudioDeviceID id,
                                   AudioObjectPropertyListenerProc listener,
                                   void *userdata);

bool registerDeviceOverloadCallback(AudioDeviceID id,
                                    AudioObjectPropertyListenerProc listener,
                                    void *userdata);

bool unregisterDeviceOverloadCallback(AudioDeviceID id,
                                      AudioObjectPropertyListenerProc listener,
                                      void *userdata);
} // namespace CoreCommon
