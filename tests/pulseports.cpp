#include "pulse/PulsePortProvider.h"
#include "tests/cliutils.h"
#include <thread>

void usage(const CLIParams &params)
{
    std::cout << "\nuseage: pulseports " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

void list_cards(std::shared_ptr<PulsePortProvider> pulsePorts)
{
    fprintf(stderr, "\n----------\n");
    fprintf(stderr, "Cards:\n");
    auto cards = pulsePorts->getCards();
    if (cards.has_value() == false) {
        fprintf(stderr, "No cards\n");
        return;
    }
    for (auto &c : (*cards)) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", c.name.c_str());
        fprintf(stderr, "Id: %u\n", c.index);
        fprintf(stderr, "Desc: %s\n", c.description.c_str());
    }
    fprintf(stderr, "\n----------\n");
}

bool print_card_profiles(std::shared_ptr<PulsePortProvider> pulsePorts, uint32_t card)
{
    fprintf(stderr, "\n----------\n");
    fprintf(stderr, "Device info %d:\n", card);
    auto cardInfo = pulsePorts->getCardInfoById(card);
    if (cardInfo.has_value() == false) {
        fprintf(stderr, "Get card info error\n");
        return false;
    }
    fprintf(stderr, "Name: %s\n", cardInfo->name.c_str());
    fprintf(stderr, "Profiles: %zu\n", cardInfo->profiles.size());
    for (auto &p : cardInfo->profiles) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", p.name.c_str());
        fprintf(stderr, "Desc: %s\n", p.description.c_str());
        fprintf(stderr, "Active: %d\n", p.active);
    }
    fprintf(stderr, "\n----------\n");
    return true;
}

bool print_device_info(std::shared_ptr<PulsePortProvider> pulsePorts, const char *dev)
{
    fprintf(stderr, "\n----------\n");
    fprintf(stderr, "Device info %s:\n", dev);
    auto info = pulsePorts->getSinkSourceInfo(dev, PulseSinkSourceType::SINK);
    if (info.has_value() == false) {
        fprintf(stderr, "Ports error\n");
        return false;
    }
    fprintf(stderr, "Name: %s\n", info->name.c_str());
    fprintf(stderr, "Desc: %s\n", info->description.c_str());
    fprintf(stderr, "Ports %zu\n", info->ports.size());
    fprintf(stderr, "Card %u\n", info->card);
    for (auto &p : info->ports) {
        fprintf(stderr, "----------\n");
        fprintf(stderr, "Name: %s\n", p.name.c_str());
        fprintf(stderr, "Desc: %s\n", p.desc.c_str());
        fprintf(stderr, "Avail: %d\n", p.available);
        fprintf(stderr, "Active: %d\n", p.active);
    }
    fprintf(stderr, "\n----------\n");
    print_card_profiles(pulsePorts, info->card);
    return true;
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
    list_cards(pulsePorts);
    print_device_info(pulsePorts, dev);
    return 0;
}
