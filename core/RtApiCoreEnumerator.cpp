#include "RtApiCoreEnumerator.h"
#include "CoreCommon.h"
#include <CoreAudio/AudioHardware.h>

namespace {}

std::vector<RtAudio::DeviceInfoPartial> RtApiCoreEnumerator::listDevices()
{
    // Find out how many audio devices there are.
    UInt32 dataSize = 0;
    AudioObjectPropertyAddress property = {kAudioHardwarePropertyDevices,
                                           kAudioObjectPropertyScopeGlobal,
                                           KAUDIOOBJECTPROPERTYELEMENT};
    OSStatus result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                                     &property,
                                                     0,
                                                     NULL,
                                                     &dataSize);
    if (result != noErr) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiCore::probeDevices: OS-X system error getting device info!");
        return {};
    }

    unsigned int nDevices = dataSize / sizeof(AudioDeviceID);
    if (nDevices == 0) {
        return {};
    }

    AudioDeviceID ids[nDevices];
    property.mSelector = kAudioHardwarePropertyDevices;
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                        &property,
                                        0,
                                        NULL,
                                        &dataSize,
                                        (void *) &ids);
    if (result != noErr) {
        error(RTAUDIO_SYSTEM_ERROR,
              "RtApiCore::probeDevices: OS-X system error getting device IDs.");
        return {};
    }

    std::vector<RtAudio::DeviceInfoPartial> infos;
    for (unsigned int n = 0; n < nDevices; n++) {
        AudioDeviceID id = ids[n];
        RtAudio::DeviceInfoPartial info{};

        auto busId_opt = CoreCommon::getDeviceBusID(id);
        auto devName_opt = CoreCommon::getDeviceFriendlyName(id);

        if (!busId_opt || !devName_opt) {
            continue;
        }
        info.busID = busId_opt.value();
        info.name = devName_opt.value();
        info.supportsInput = CoreCommon::getInputChannels(id) > 0;
        info.supportsOutput = CoreCommon::getOutputChannels(id) > 0;
        infos.push_back(info);
    }
    return infos;
}
