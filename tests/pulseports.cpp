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
    auto info = pulsePorts->getSinkSourceInfo(dev, PulseSinkSourceType::SINK);
    if (info.has_value() == false) {
        fprintf(stderr, "Ports error\n");
        return 1;
    }
    auto cardInfo = pulsePorts->getCardInfoById(info->card);
    if (cardInfo.has_value() == false) {
        fprintf(stderr, "Get card info error\n");
        return 1;
    }

    fprintf(stderr, "Name: %s\n", info->name.c_str());
    fprintf(stderr, "Desc: %s\n", info->description.c_str());
    fprintf(stderr, "Ports %zu\n", info->ports.size());

    for (auto &p : info->ports) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", p.name.c_str());
        fprintf(stderr, "Desc: %s\n", p.desc.c_str());
        fprintf(stderr, "Avail: %d\n", p.available);
        fprintf(stderr, "Active: %d\n", p.active);
    }

    fprintf(stderr, "\nCard\n");
    fprintf(stderr, "Name: %s\n", cardInfo->name.c_str());
    fprintf(stderr, "Profiles: %zu\n", cardInfo->profiles.size());

    for (auto &p : cardInfo->profiles) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", p.name.c_str());
        fprintf(stderr, "Desc: %s\n", p.description.c_str());
        fprintf(stderr, "Active: %d\n", p.active);
    }

    if (pulsePorts->hasError()) {
        fprintf(stderr, "Has error\n");
        return 1;
    }
    fprintf(stderr, "----------\n\n\n");

    fprintf(stderr, "Cards:\n");
    auto cards = pulsePorts->getCards();
    if (cards.has_value() == false) {
        fprintf(stderr, "No cards\n");
        return 1;
    }
    for (auto &c : (*cards)) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", c.name.c_str());
        fprintf(stderr, "Desc: %s\n", c.description.c_str());
    }
    return 0;
}
