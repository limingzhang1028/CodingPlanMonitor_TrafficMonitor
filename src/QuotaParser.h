#pragma once

#include <string>

#include "CcSwitchSource.h"
#include "QuotaSnapshot.h"

namespace quota {

struct QuotaParseResult
{
    bool success{};
    QuotaSnapshot snapshot;
    std::wstring error;
};

QuotaParseResult ParseQuotaResponse(const std::string& response_text);

QuotaParseResult ParseGlmQuotaResponse(const std::string& response_text);

QuotaParseResult ParseProviderQuotaResponse(ProviderKind kind, const std::string& response_text);

} // namespace quota
