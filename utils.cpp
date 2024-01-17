#include <locale>
#include <codecvt>
#include "utils.h"

std::string convertCharPointerToStdString(const char* text)
{
    return text;
}

std::string convertCharPointerToStdString(const wchar_t* text)
{
    if (text == nullptr)
        return {};
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.to_bytes(text);
}

std::wstring convertStdStringToWString(const std::string& text)
{
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.from_bytes(text);
}
