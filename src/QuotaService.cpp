#include "QuotaService.h"

#include <algorithm>
#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include "QuotaClient.h"
#include "QuotaParser.h"
#include "Utf8.h"

namespace quota {
namespace {

constexpr auto kPollInterval = std::chrono::minutes(5);
constexpr size_t kMaxSelected = 2;

void ClearSecret(std::string& secret)
{
    if (!secret.empty())
        SecureZeroMemory(secret.data(), secret.size());
    secret.clear();
    secret.shrink_to_fit();
}

void ClearSecrets(std::vector<CcSwitchProvider>& providers)
{
    for (CcSwitchProvider& provider : providers)
        ClearSecret(provider.bearer_token);
}

// Stores one provider's query result. On failure keeps the last good data and
// marks it stale; only stores the failure when there is no data at all.
void StoreQuota(std::vector<ProviderQuota>& quotas, std::string id,
    std::wstring name, QuotaSnapshot latest)
{
    const auto existing = std::find_if(quotas.begin(), quotas.end(),
        [&id](const ProviderQuota& quota) { return quota.provider_id == id; });

    const bool has_data = latest.five_hour.valid || latest.weekly.valid;
    if (!has_data && existing != quotas.end() &&
        (existing->quota.five_hour.valid || existing->quota.weekly.valid))
    {
        existing->quota.status = QuotaStatus::Stale;
        existing->quota.error = std::move(latest.error);
        return;
    }

    ProviderQuota updated;
    updated.provider_id = std::move(id);
    updated.provider_name = std::move(name);
    updated.quota = std::move(latest);
    if (existing != quotas.end())
        *existing = std::move(updated);
    else
        quotas.push_back(std::move(updated));
}

} // namespace

QuotaService::QuotaService(std::wstring database_path, std::wstring config_path)
    : database_path_(std::move(database_path))
    , config_path_(std::move(config_path))
{
}

QuotaService::~QuotaService()
{
    Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    ClearSecrets(providers_);
}

void QuotaService::Start()
{
    LoadSelection();
    if (worker_.joinable())
        return;
    worker_ = std::thread(&QuotaService::WorkerMain, this);
}

void QuotaService::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void QuotaService::RequestRefresh()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_requested_ = true;
    }
    condition_.notify_all();
}

void QuotaService::RequestDetails()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        details_requested_ = true;
    }
    condition_.notify_all();
}

std::vector<ProviderQuota> QuotaService::SelectedQuotas() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProviderQuota> result;
    for (const std::string& id : selected_ids_)
    {
        const auto found = std::find_if(quotas_.begin(), quotas_.end(),
            [&id](const ProviderQuota& quota) { return quota.provider_id == id; });
        if (found != quotas_.end())
            result.push_back(*found);
        else
        {
            const auto provider = std::find_if(providers_.begin(), providers_.end(),
                [&id](const CcSwitchProvider& p) { return p.id == id; });
            if (provider != providers_.end())
            {
                ProviderQuota empty;
                empty.provider_id = id;
                empty.provider_name = provider->name;
                result.push_back(std::move(empty));
            }
        }
    }
    return result;
}

std::vector<ProviderRow> QuotaService::ProviderRows() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProviderRow> rows;
    for (const CcSwitchProvider& provider : providers_)
    {
        ProviderRow row;
        row.id = provider.id;
        row.name = provider.name;
        row.is_current = provider.is_current;
        row.selected = std::find(selected_ids_.begin(), selected_ids_.end(), provider.id) !=
            selected_ids_.end();
        row.quota.provider_id = provider.id;
        row.quota.provider_name = provider.name;
        for (const ProviderQuota& quota : quotas_)
        {
            if (quota.provider_id == provider.id)
            {
                row.quota.quota = quota.quota;
                break;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void QuotaService::SetSelection(std::vector<std::string> ids)
{
    if (ids.size() > kMaxSelected)
        ids.resize(kMaxSelected);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        selected_ids_ = std::move(ids);
        ApplySelectionLocked();
        next_poll_ = std::chrono::steady_clock::now();
        refresh_requested_ = true;
    }
    SaveSelection();
    condition_.notify_all();
}

std::wstring QuotaService::TooltipText() const
{
    const std::vector<ProviderQuota> quotas = SelectedQuotas();
    if (quotas.empty())
        return L"未选择供应商，双击任务栏数值进行选择";

    std::wstring text;
    for (const ProviderQuota& quota : quotas)
    {
        if (!text.empty())
            text += L"\r\n";
        text += BuildProviderLineText(quota);
        if (!quota.quota.error.empty() && quota.quota.status == QuotaStatus::Stale)
            text += L"\r\n" + quota.quota.error;
    }
    return text;
}

std::wstring QuotaService::DetailsText() const
{
    const std::vector<ProviderQuota> quotas = SelectedQuotas();
    if (quotas.empty())
        return L"尚未选择供应商。";

    std::wstring text;
    for (const ProviderQuota& quota : quotas)
    {
        if (!text.empty())
            text += L"\r\n\r\n----------------\r\n\r\n";
        text += quota.provider_name;
        text += L"\r\n\r\n";
        text += BuildDetailsText(quota.quota);
    }
    return text;
}

void QuotaService::LoadSelection()
{
    std::vector<std::string> ids;
    HANDLE file = CreateFileW(config_path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        char buffer[4096]{};
        DWORD bytes_read{};
        std::string text;
        while (ReadFile(file, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
            text.append(buffer, bytes_read);
        CloseHandle(file);

        try
        {
            const nlohmann::json root = nlohmann::json::parse(text);
            if (root.contains("selected") && root.at("selected").is_array())
            {
                for (const nlohmann::json& item : root.at("selected"))
                {
                    if (item.is_string() && ids.size() < kMaxSelected)
                        ids.push_back(item.get<std::string>());
                }
            }
        }
        catch (...)
        {
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    selected_ids_ = std::move(ids);
}

void QuotaService::SaveSelection() const
{
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ids = selected_ids_;
    }

    nlohmann::json root;
    root["selected"] = ids;
    const std::string text = root.dump();

    const size_t slash = config_path_.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        SHCreateDirectoryExW(nullptr, config_path_.substr(0, slash).c_str(), nullptr);

    HANDLE file = CreateFileW(config_path_.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written{};
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

// Keeps the stored selection consistent with the provider list: drops ids that
// no longer exist, and when nothing is selected falls back to the current
// provider (or the first one).
void QuotaService::ApplySelectionLocked()
{
    std::vector<std::string> valid;
    for (const std::string& id : selected_ids_)
    {
        const auto found = std::find_if(providers_.begin(), providers_.end(),
            [&id](const CcSwitchProvider& provider) { return provider.id == id; });
        if (found != providers_.end() &&
            std::find(valid.begin(), valid.end(), id) == valid.end())
        {
            valid.push_back(id);
        }
    }
    if (valid.empty() && !providers_.empty())
    {
        const auto current = std::find_if(providers_.begin(), providers_.end(),
            [](const CcSwitchProvider& provider) { return provider.is_current; });
        valid.push_back(current != providers_.end() ? current->id : providers_.front().id);
    }
    if (valid.size() > kMaxSelected)
        valid.resize(kMaxSelected);
    selected_ids_ = std::move(valid);
}

QuotaSnapshot QuotaService::QueryProviderQuota(const CcSwitchProvider& provider) const
{
    QuotaSnapshot snapshot;

    QuotaHttpResult http = FetchProviderQuota(provider);
    if (!http.success)
    {
        snapshot.status = QuotaStatus::Error;
        snapshot.error = std::move(http.error);
        return snapshot;
    }

    QuotaParseResult parsed = ParseProviderQuotaResponse(provider.kind, http.response_body);
    if (!parsed.success)
    {
        snapshot.status = QuotaStatus::Error;
        snapshot.error = std::move(parsed.error);
        return snapshot;
    }

    return std::move(parsed.snapshot);
}

void QuotaService::WorkerMain()
{
    for (;;)
    {
        std::vector<CcSwitchProvider> providers;
        std::vector<std::string> query_ids;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return stop_ || refresh_requested_ || details_requested_ ||
                    std::chrono::steady_clock::now() >= next_poll_;
            });
            if (stop_)
                return;
            refresh_requested_ = false;

            ProviderListResult loaded = LoadCcSwitchProviders(database_path_);
            if (loaded.success)
            {
                ClearSecrets(providers_);
                providers_ = std::move(loaded.providers);
                ApplySelectionLocked();
            }
            providers = providers_;

            // Selected providers refresh on every poll; a details request
            // (selection dialog open) queries every provider once.
            query_ids = selected_ids_;
            if (details_requested_)
            {
                details_requested_ = false;
                for (const CcSwitchProvider& provider : providers)
                {
                    if (std::find(query_ids.begin(), query_ids.end(), provider.id) == query_ids.end())
                        query_ids.push_back(provider.id);
                }
            }
        }

        // Query without holding the lock.
        for (const std::string& id : query_ids)
        {
            const auto found = std::find_if(providers.begin(), providers.end(),
                [&id](const CcSwitchProvider& provider) { return provider.id == id; });
            if (found == providers.end())
                continue;

            CcSwitchProvider provider = *found;
            QuotaSnapshot latest = QueryProviderQuota(provider);
            ClearSecret(provider.bearer_token);

            std::lock_guard<std::mutex> lock(mutex_);
            StoreQuota(quotas_, std::move(provider.id), std::move(provider.name),
                std::move(latest));
        }

        // The local provider copies hold bearer tokens; wipe them before
        // the strings are freed.
        ClearSecrets(providers);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_poll_ = std::chrono::steady_clock::now() + kPollInterval;
        }
    }
}

} // namespace quota
