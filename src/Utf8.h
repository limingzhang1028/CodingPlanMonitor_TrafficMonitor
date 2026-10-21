#pragma once

#include <string>

namespace quota {

std::wstring WideFromUtf8(const std::string& value);
std::string Utf8FromWide(const std::wstring& value);

} // namespace quota
