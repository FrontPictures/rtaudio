#include "AsioCommon.h"

AsioDrivers drivers;

const char* getAsioErrorString(ASIOError result) {
    struct Messages
    {
        ASIOError value;
        const char* message;
    };

    static const Messages m[] =
    {
        {   ASE_NotPresent,    "Hardware input or output is not present or available." },
        {   ASE_HWMalfunction,  "Hardware is malfunctioning." },
        {   ASE_InvalidParameter, "Invalid input parameter." },
        {   ASE_InvalidMode,      "Invalid mode." },
        {   ASE_SPNotAdvancing,     "Sample position not advancing." },
        {   ASE_NoClock,            "Sample clock or rate cannot be determined or is not present." },
        {   ASE_NoMemory,           "Not enough memory to complete the request." }
    };

    for (unsigned int i = 0; i < sizeof(m) / sizeof(m[0]); ++i)
        if (m[i].value == result) return m[i].message;

    return "Unknown error.";
}

namespace {
    inline std::string IntToHex(uint64_t v, int symbols) {
        char buf[17]{ 0 };
        _ui64toa(v, buf, 16);
        std::string prefix;
        for (auto c = 0; c < symbols - strlen(buf); c++) {
            prefix += "0";
        }
        return prefix + buf;
    }
    inline uint8_t CharToNumber(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return 0;
    }

    inline uint64_t HexToInt(const std::string& v) {
        char buf[17]{ 0 };
        uint64_t res = 0;
        for (auto& c : v) {
            res = res * 16;
            res = res + CharToNumber(c);
        }
        return res;
    }
}

std::optional<CLSID> HexToCLSID(const std::string& v) {
    if (v.size() != 32) {
        return {};
    }
    CLSID id{};
    int ofs = 0;
    id.Data1 = HexToInt(v.substr(ofs, sizeof(id.Data1) * 2));
    ofs += sizeof(id.Data1) * 2;

    id.Data2 = HexToInt(v.substr(ofs, sizeof(id.Data2) * 2));
    ofs += sizeof(id.Data2) * 2;

    id.Data3 = HexToInt(v.substr(ofs, sizeof(id.Data3) * 2));
    ofs += sizeof(id.Data3) * 2;

    for (int e = 0; e < 8; e++) {
        id.Data4[e] = HexToInt(v.substr(ofs, sizeof(id.Data4[0]) * 2));
        ofs += sizeof(id.Data4[0]) * 2;
    }
    return id;
}

std::string CLSIDToHex(CLSID id) {
    std::string res;
    res += IntToHex(id.Data1, sizeof(id.Data1) * 2);
    res += IntToHex(id.Data2, sizeof(id.Data2) * 2);
    res += IntToHex(id.Data3, sizeof(id.Data3) * 2);
    for (int e = 0; e < 8; e++) {
        res += IntToHex(id.Data4[e], sizeof(id.Data4[0]) * 2);
    }
    return res;
}