#pragma once

#include <string>

#include "CcSwitchSource.h"

namespace quota {

struct QuotaHttpResult
{
    bool success{};
    unsigned long status_code{};
    std::string response_body;
    std::wstring error;
};

QuotaHttpResult FetchProviderQuota(const CcSwitchProvider& provider);

} // namespace quota
