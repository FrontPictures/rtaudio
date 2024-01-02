#pragma once
#include <string>
#include <vector>

struct PulseProfileInfo
{
    std::string name;
    std::string description;
    uint32_t priority = 0;
    bool active = false;
    bool available = false;
};

struct PulseCardInfo
{
    uint32_t index = 0;
    std::string name;
    std::string description;
    std::string driver;
    std::vector<PulseProfileInfo> profiles;
};

struct PulsePortInfo
{
    std::string name;
    std::string desc;
    uint32_t priority = 0;
    bool available = false;
    bool active = false;
};

enum class PulseSinkSourceType { SINK, SOURCE };

struct PulseSinkSourceInfo
{
    std::string name;
    uint32_t index = 0;
    std::string description;
    std::string driver;
    uint32_t card = 0;
    std::vector<PulsePortInfo> ports;
    PulseSinkSourceType type;
};
