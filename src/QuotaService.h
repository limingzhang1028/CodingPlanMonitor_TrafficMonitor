#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CcSwitchSource.h"
#include "QuotaSnapshot.h"

namespace quota {

// One row in the provider selection dialog.
struct ProviderRow
{
    std::string id;
    std::wstring name;
    bool selected{};
    bool is_current{};
    ProviderQuota quota;
};

class QuotaService
{
public:
    QuotaService(std::wstring database_path, std::wstring config_path);
    ~QuotaService();

    QuotaService(const QuotaService&) = delete;
    QuotaService& operator=(const QuotaService&) = delete;

    void Start();
    void Stop();
    void RequestRefresh();

    // Refreshes quotas for every provider once (not just selected ones), so
    // the selection dialog can show live details for all rows.
    void RequestDetails();

    // Quotas of the currently selected providers, in selection order.
    std::vector<ProviderQuota> SelectedQuotas() const;

    // Number of selected providers (0-2), from the persisted selection only.
    // Stable while providers and quotas are still loading, unlike
    // SelectedQuotas().size().
    int SelectedDisplayCount() const;

    // All selectable providers with their selection state and last snapshot.
    std::vector<ProviderRow> ProviderRows() const;

    // Replaces the selection (at most two ids), persists it and refreshes.
    void SetSelection(std::vector<std::string> ids);

    std::wstring TooltipText() const;
    std::wstring DetailsText() const;

private:
    void LoadSelection();
    void SaveSelection() const;
    void ApplySelectionLocked();
    QuotaSnapshot QueryProviderQuota(const CcSwitchProvider& provider) const;
    void WorkerMain();

    const std::wstring database_path_;
    const std::wstring config_path_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool stop_{};
    bool refresh_requested_{};
    bool details_requested_{};
    std::chrono::steady_clock::time_point next_poll_{ std::chrono::steady_clock::now() };
    std::vector<CcSwitchProvider> providers_;
    std::vector<std::string> selected_ids_;
    // Last query result per provider id, for any provider ever queried.
    std::vector<ProviderQuota> quotas_;
};

} // namespace quota
