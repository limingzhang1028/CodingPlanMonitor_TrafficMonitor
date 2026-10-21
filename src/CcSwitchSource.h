#pragma once

#include <string>
#include <vector>

namespace quota {

enum class ProviderKind
{
    Unknown,
    Kimi,
    Glm,
};

struct CcSwitchProvider
{
    std::string id;
    std::wstring name;
    std::string base_url;
    std::string bearer_token;
    ProviderKind kind{ ProviderKind::Unknown };
    bool is_current{};
};

struct ProviderListResult
{
    bool success{};
    std::vector<CcSwitchProvider> providers;
    std::wstring error;
};

// Lists every Claude provider in the CC Switch database that has both a usable
// credential and a supported quota API (currently Kimi and Zhipu GLM).
ProviderListResult LoadCcSwitchProviders(const std::wstring& database_path);

} // namespace quota
