#pragma once

#include <chrono>
#include <string>

namespace quota {

enum class QuotaStatus
{
    NoData,
    Ok,
    Stale,
    Error,
};

struct QuotaWindow
{
    bool valid{};
    double limit{};
    double remaining{};
    std::wstring reset_time;
    std::chrono::system_clock::time_point reset_at{};

    int RemainingPercent() const;
    bool HasResetTime() const;

    // Hours from now until the window resets, clamped to >= 0. Returns 0 when
    // the reset time is unknown.
    double RemainingHours(std::chrono::system_clock::time_point now) const;
};

struct QuotaSnapshot
{
    QuotaStatus status{ QuotaStatus::NoData };
    QuotaWindow five_hour;
    QuotaWindow weekly;
    std::wstring error;
    std::chrono::system_clock::time_point updated_at{};
};

struct ProviderQuota
{
    std::string provider_id;
    std::wstring provider_name;
    QuotaSnapshot quota;
};

std::wstring BuildDisplayText(const QuotaSnapshot& snapshot);
std::wstring BuildDetailsText(const QuotaSnapshot& snapshot);
std::wstring BuildTooltipText(const QuotaSnapshot& snapshot);
std::wstring FormatTimePoint(std::chrono::system_clock::time_point value);

// One window's value with its remaining time, e.g. "4.7h 98%" (unit "h",
// hours_per_unit 1) or "4.8d 84%" (unit "d", hours_per_unit 24). Returns "--"
// for invalid windows and just "98%" when the reset time is unknown.
std::wstring FormatWindowValue(const QuotaWindow& window, double hours_per_unit,
    const wchar_t* unit, std::chrono::system_clock::time_point now);

// One taskbar line for a provider, e.g. "Kimi 4.7h 98% 4.8d 84%".
std::wstring BuildProviderLineText(const ProviderQuota& provider);

// One summary line for the selection dialog, e.g.
// "5h 98%（4.7h 后重置）｜7d 84%（4.8d 后重置）".
std::wstring BuildSummaryText(const ProviderQuota& provider);

} // namespace quota
