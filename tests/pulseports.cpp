#include "pulse/PulsePortProvider.h"
#include "tests/cliutils.h"
#include <thread>

void usage(const CLIParams &params)
{
    std::cout << "\nuseage: pulseports " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

int main(int argc, char **argv)
{
    CLIParams params({
        {"device", "device busID to use", false},
    });

    if (params.checkCountArgc(argc) == false) {
        usage(params);
        return 1;
    }
    const char *dev = params.getParamValue("device", argv, argc);

    auto pulsePorts = PulsePortProvider::Create();
    while (1) {
        auto ports = pulsePorts->getPortsForDevice(dev, PulseSinkSourceType::SINK);
        if (ports.has_value() == false) {
            fprintf(stderr, "Ports error\n");
            break;
        }
        fprintf(stderr, "Ports %zu\n", ports->size());
        if (pulsePorts->hasError()) {
            fprintf(stderr, "Has error\n");
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
