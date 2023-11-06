#include "asiosys.h"
#include "include/asio.h"
#include "asiodrivers.h"
#include <optional>
#include <string>

extern AsioDrivers drivers;
const char* getAsioErrorString(ASIOError result);

std::optional<CLSID> HexToCLSID(const std::string& v);
std::string CLSIDToHex(CLSID id);
