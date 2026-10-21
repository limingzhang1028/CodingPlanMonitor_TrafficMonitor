#include "Utf8.h"

#include <stdexcept>
#include <vector>
#include <windows.h>

namespace quota {

std::wstring WideFromUtf8(const std::string& value)
{
    if (value.empty())
        return {};

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
        throw std::runtime_error("Invalid UTF-8 text.");

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string Utf8FromWide(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        throw std::runtime_error("Invalid UTF-16 text.");

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

} // namespace quota
