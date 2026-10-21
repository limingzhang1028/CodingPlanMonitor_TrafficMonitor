#include "QuotaSnapshot.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace quota {
namespace {

std::wstring FormatNumber(double value)
{
    if (!std::isfinite(value))
        return L"--";

    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 0.000001)
        return std::to_wstring(static_cast<long long>(rounded));

    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::wstring text = stream.str();
    while (!text.empty() && text.back() == L'0')
        text.pop_back();
    if (!text.empty() && text.back() == L'.')
        text.pop_back();
    return text;
}

std::wstring FormatWindow(const wchar_t* name, const QuotaWindow& window)
{
    std::wostringstream stream;
    stream << name << L"：剩余 " << FormatNumber(window.remaining) << L" / "
           << FormatNumber(window.limit) << L"（" << window.RemainingPercent() << L"%）";
    if (!window.reset_time.empty())
        stream << L"\r\n重置时间：" << window.reset_time;
    return stream.str();
}

// Writes the leading label for a display line: the remaining time until the
// window resets (with one decimal), or the fallback label when unknown.
void FormatRemainingLabel(const QuotaWindow& window, const wchar_t* fallback,
    double hours_per_unit, const wchar_t* unit, wchar_t* buffer, size_t size,
    std::chrono::system_clock::time_point now)
{
    if (window.HasResetTime())
        _snwprintf_s(buffer, size, _TRUNCATE, L"%.1f%ls",
            window.RemainingHours(now) / hours_per_unit, unit);
    else
        wcscpy_s(buffer, size, fallback);
}

} // namespace

int QuotaWindow::RemainingPercent() const
{
    if (!valid || limit <= 0.0 || !std::isfinite(limit) || !std::isfinite(remaining))
        return 0;

    const double percent = remaining / limit * 100.0;
    return static_cast<int>(std::lround(std::clamp(percent, 0.0, 100.0)));
}

bool QuotaWindow::HasResetTime() const
{
    return reset_at.time_since_epoch() != std::chrono::system_clock::duration::zero();
}

double QuotaWindow::RemainingHours(std::chrono::system_clock::time_point now) const
{
    if (!HasResetTime())
        return 0.0;

    const double hours =
        std::chrono::duration<double, std::ratio<3600>>(reset_at - now).count();
    return std::max(hours, 0.0);
}

std::wstring FormatTimePoint(std::chrono::system_clock::time_point value)
{
    if (value.time_since_epoch() == std::chrono::system_clock::duration::zero())
        return L"--";

    const std::time_t raw_time = std::chrono::system_clock::to_time_t(value);
    std::tm local_time{};
    if (localtime_s(&local_time, &raw_time) != 0)
        return L"--";

    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d",
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday,
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec);
    return buffer;
}

std::wstring BuildDisplayText(const QuotaSnapshot& snapshot)
{
    if (!snapshot.five_hour.valid && !snapshot.weekly.valid)
        return L"5H--·7D--";

    const auto now = std::chrono::system_clock::now();
    wchar_t five_hour_part[24]{};
    wchar_t weekly_part[24]{};
    if (snapshot.five_hour.valid)
    {
        wchar_t label[16]{};
        FormatRemainingLabel(snapshot.five_hour, L"5H", 1.0, L"h", label, 16, now);
        swprintf_s(five_hour_part, L"%ls%d%%", label, snapshot.five_hour.RemainingPercent());
    }
    else
    {
        wcscpy_s(five_hour_part, L"5H--");
    }
    if (snapshot.weekly.valid)
    {
        wchar_t label[16]{};
        FormatRemainingLabel(snapshot.weekly, L"7D", 24.0, L"d", label, 16, now);
        swprintf_s(weekly_part, L"%ls%d%%", label, snapshot.weekly.RemainingPercent());
    }
    else
    {
        wcscpy_s(weekly_part, L"7D--");
    }

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%ls·%ls%ls",
        five_hour_part,
        weekly_part,
        snapshot.status == QuotaStatus::Stale ? L"!" : L"");
    return buffer;
}

std::wstring FormatWindowValue(const QuotaWindow& window, double hours_per_unit,
    const wchar_t* unit, std::chrono::system_clock::time_point now)
{
    if (!window.valid)
        return L"--";

    wchar_t buffer[24]{};
    if (window.HasResetTime())
        swprintf_s(buffer, L"%.1f%ls %d%%",
            window.RemainingHours(now) / hours_per_unit, unit,
            window.RemainingPercent());
    else
        swprintf_s(buffer, L"%d%%", window.RemainingPercent());
    return buffer;
}

std::wstring BuildProviderLineText(const ProviderQuota& provider)
{
    const QuotaSnapshot& snapshot = provider.quota;
    const auto now = std::chrono::system_clock::now();

    wchar_t buffer[80]{};
    swprintf_s(buffer, L"%ls %ls %ls%ls",
        provider.provider_name.c_str(),
        FormatWindowValue(snapshot.five_hour, 1.0, L"h", now).c_str(),
        FormatWindowValue(snapshot.weekly, 24.0, L"d", now).c_str(),
        snapshot.status == QuotaStatus::Stale ? L"!" : L"");
    return buffer;
}

std::wstring BuildSummaryText(const ProviderQuota& provider)
{
    const QuotaSnapshot& snapshot = provider.quota;
    if (snapshot.status == QuotaStatus::Error ||
        (!snapshot.five_hour.valid && !snapshot.weekly.valid))
    {
        return snapshot.error.empty() ? L"尚无数据" : snapshot.error;
    }

    const auto now = std::chrono::system_clock::now();
    std::wostringstream stream;
    if (snapshot.five_hour.valid)
    {
        stream << L"5h " << snapshot.five_hour.RemainingPercent() << L"%";
        if (snapshot.five_hour.HasResetTime())
        {
            stream << L"（" << std::fixed << std::setprecision(1)
                   << snapshot.five_hour.RemainingHours(now) << L"h 后重置）";
        }
    }
    else
    {
        stream << L"5h 无数据";
    }
    stream << L"｜";
    if (snapshot.weekly.valid)
    {
        stream << L"7d " << snapshot.weekly.RemainingPercent() << L"%";
        if (snapshot.weekly.HasResetTime())
        {
            stream << L"（" << std::fixed << std::setprecision(1)
                   << snapshot.weekly.RemainingHours(now) / 24.0 << L"d 后重置）";
        }
    }
    else
    {
        stream << L"7d 无数据";
    }
    if (snapshot.status == QuotaStatus::Stale)
        stream << L"（已过期）";
    return stream.str();
}

std::wstring BuildDetailsText(const QuotaSnapshot& snapshot)
{
    std::wostringstream stream;
    stream << L"Kimi 剩余额度\r\n\r\n";

    if (snapshot.five_hour.valid || snapshot.weekly.valid)
    {
        if (snapshot.five_hour.valid)
            stream << FormatWindow(L"5 小时额度", snapshot.five_hour);
        if (snapshot.five_hour.valid && snapshot.weekly.valid)
            stream << L"\r\n\r\n";
        if (snapshot.weekly.valid)
            stream << FormatWindow(L"每周额度", snapshot.weekly);
        stream << L"\r\n\r\n上次更新：" << FormatTimePoint(snapshot.updated_at);

        if (snapshot.status == QuotaStatus::Stale)
            stream << L"\r\n状态：刷新失败，当前显示的是最后一次成功数据。";
    }
    else
    {
        stream << L"尚未获取到额度数据。";
    }

    if (!snapshot.error.empty())
        stream << L"\r\n错误：" << snapshot.error;

    return stream.str();
}

std::wstring BuildTooltipText(const QuotaSnapshot& snapshot)
{
    std::wostringstream stream;
    stream << BuildDisplayText(snapshot) << L"\r\n";

    if (snapshot.five_hour.valid || snapshot.weekly.valid)
    {
        if (snapshot.five_hour.valid)
        {
            stream << L"5H " << FormatNumber(snapshot.five_hour.remaining) << L"/"
                   << FormatNumber(snapshot.five_hour.limit);
        }
        if (snapshot.weekly.valid)
        {
            if (snapshot.five_hour.valid)
                stream << L"；";
            stream << L"7D " << FormatNumber(snapshot.weekly.remaining) << L"/"
                   << FormatNumber(snapshot.weekly.limit);
        }
        stream << L"\r\n上次更新：" << FormatTimePoint(snapshot.updated_at);
        if (snapshot.status == QuotaStatus::Stale)
            stream << L"\r\n刷新失败，数据可能已过期";
    }
    else
    {
        stream << L"尚未获取到额度数据";
    }

    if (!snapshot.error.empty())
        stream << L"\r\n" << snapshot.error;

    return stream.str();
}

} // namespace quota
