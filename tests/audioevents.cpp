#include "RtAudio.h"
#include <thread>

void usage(void)
{
    // Error function in case of incorrect command-line
    // argument specifications
    std::cout << "\nuseage: audioevents apiname <duration>\n";
    std::cout << "    where apiname = an api (ex., 'core')\n";
    std::cout << "          duration = duration in seconds\n\n";
    exit(0);
}

std::string DeviceParamToString(RtAudioDeviceParam param)
{
    switch (param) {
    case DEFAULT_CHANGED:
        return "DEFAULT_CHANGED";
    case DEVICE_ADDED:
        return "ADDED";
    case DEVICE_REMOVED:
        return "REMOVED";
    case DEVICE_STATE_CHANGED:
        return "STATE_CHANGED";
    case DEVICE_PROPERTY_CHANGED:
        return "PROPERTY_CHANGED";
    default:
        return "NONE";
    }
}
int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3)
        usage();
    auto api = RtAudio::getCompiledApiByName(argv[1]);
    int durationSecs = INT_MAX;
    if (argc == 3) {
        durationSecs = atoi(argv[2]);
    }
    if (api == RtAudio::UNSPECIFIED) {
        std::cout << "\nApi not found\n";
        return 1;
    }
    std::cout << "\nWaiting for events\n";
    {
        auto systemCallbacks = RtAudio::GetRtAudioSystemCallback(api,
                                                                 [](const std::string &busId,
                                                                    RtAudioDeviceParam param) {
                                                                     std::cout
                                                                         << DeviceParamToString(
                                                                                param)
                                                                         << " : " << busId
                                                                         << std::endl;
                                                                 });
        if (!systemCallbacks) {
            std::cout << "No audio system events" << std::endl;
            return 1;
        }
        if (systemCallbacks->hasError()) {
            std::cout << "Error in systems callback on start" << std::endl;
            return 1;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        while (1) {
            if (systemCallbacks->hasError()) {
                std::cout << "Has error in system callbacks" << std::endl;
                break;
            }
            auto elapsedMs = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::high_resolution_clock::now() - startTime)
                                 .count();
            if (elapsedMs >= durationSecs) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::cout << "\nFinished receiving events\n";
    return 0;
}
