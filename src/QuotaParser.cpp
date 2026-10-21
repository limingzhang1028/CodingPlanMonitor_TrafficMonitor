#include "QuotaParser.h"

#include <cmath>
#include <ctime>
#include <nlohmann/json.hpp>

#include "Utf8.h"

namespace quota {
namespace {

using nlohmann::json;

std::string Trimmed(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool NumberFromJson(const json& value, double& result)
{
    try
    {
        if (value.is_number())
        {
            result = value.get<double>();
        }
        else if (value.is_string())
        {
            const std::string text = Trimmed(value.get<std::string>());
            if (text.empty())
                return false;

            size_t parsed_length{};
            result = std::stod(text, &parsed_length);
            if (parsed_length != text.size())
                return false;
        }
        else
        {
            return false;
        }
    }
    catch (...)
    {
        return false;
    }

    return std::isfinite(result);
}

// Parses an ISO 8601 timestamp such as "2026-10-25T00:00:00Z" or
// "2026-10-25T08:00:00+08:00". Returns false when the string is not a
// recognizable timestamp.
bool ParseIsoTime(const std::string& text, std::chrono::system_clock::time_point& result)
{
    int year{}, month{}, day{}, hour{}, minute{}, second{};
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d",
            &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    std::tm utc_time{};
    utc_time.tm_year = year - 1900;
    utc_time.tm_mon = month - 1;
    utc_time.tm_mday = day;
    utc_time.tm_hour = hour;
    utc_time.tm_min = minute;
    utc_time.tm_sec = second;
    const std::time_t utc_seconds = _mkgmtime(&utc_time);
    if (utc_seconds == -1)
        return false;

    // An explicit offset like "+08:00" or "-0500" shifts the local fields back
    // to UTC. Anything else ("Z", fractional seconds, no suffix) is UTC.
    int offset_seconds{};
    const size_t offset_pos = text.find_first_of("+-", 11);
    if (offset_pos != std::string::npos)
    {
        int offset_hour{}, offset_minute{};
        const int fields = sscanf_s(text.c_str() + offset_pos + 1, "%2d:%2d",
            &offset_hour, &offset_minute);
        if (fields >= 1)
        {
            if (fields < 2)
                offset_minute = 0;
            offset_seconds = offset_hour * 3600 + offset_minute * 60;
            if (text[offset_pos] == '-')
                offset_seconds = -offset_seconds;
        }
    }

    result = std::chrono::system_clock::from_time_t(utc_seconds - offset_seconds);
    return true;
}

std::wstring ResetTimeFromJson(const json& value,
    std::chrono::system_clock::time_point& reset_at)
{
    if (value.is_string())
    {
        const std::string text = Trimmed(value.get<std::string>());
        if (text.empty())
            return {};
        std::chrono::system_clock::time_point parsed{};
        if (ParseIsoTime(text, parsed))
            reset_at = parsed;
        return WideFromUtf8(text);
    }

    double timestamp{};
    if (!NumberFromJson(value, timestamp) || timestamp <= 0.0)
        return {};

    // Kimi/CC Switch accepts both seconds and milliseconds. Current Unix time
    // is about 1.7e9 seconds and 1.7e12 milliseconds.
    if (timestamp > 100000000000.0)
        timestamp /= 1000.0;

    reset_at = std::chrono::system_clock::from_time_t(
        static_cast<std::time_t>(std::llround(timestamp)));
    const std::wstring formatted = FormatTimePoint(reset_at);
    return formatted == L"--" ? std::wstring{} : formatted;
}

bool ParseWindow(const json& value, QuotaWindow& result)
{
    if (!value.is_object())
        return false;

    double limit{};
    double remaining{};
    if (!value.contains("limit") || !NumberFromJson(value.at("limit"), limit))
        return false;
    if (!value.contains("remaining") || !NumberFromJson(value.at("remaining"), remaining))
        return false;
    if (limit < 0.0 || remaining < 0.0)
        return false;

    result.valid = true;
    result.limit = limit;
    result.remaining = remaining;
    if (value.contains("resetTime"))
        result.reset_time = ResetTimeFromJson(value.at("resetTime"), result.reset_at);
    return true;
}

bool ParseFiveHourWindow(const json& root, QuotaWindow& result)
{
    if (!root.contains("limits") || !root.at("limits").is_array())
        return false;

    for (const json& item : root.at("limits"))
    {
        if (item.is_object() && item.contains("detail") && ParseWindow(item.at("detail"), result))
            return true;
    }
    return false;
}

// Parses one GLM quota entry (type TOKENS_LIMIT/CREDIT_LIMIT) into a window.
// GLM reports the used percentage rather than absolute token amounts, so the
// window is normalized to limit=100, remaining=100-percentage.
bool ParseGlmLimitWindow(const json& value, QuotaWindow& result)
{
    if (!value.is_object())
        return false;

    double used_percent{};
    if (!value.contains("percentage") || !NumberFromJson(value.at("percentage"), used_percent))
        return false;
    if (used_percent < 0.0 || used_percent > 100.0)
        return false;

    result.valid = true;
    result.limit = 100.0;
    result.remaining = 100.0 - used_percent;
    if (value.contains("nextResetTime"))
        result.reset_time = ResetTimeFromJson(value.at("nextResetTime"), result.reset_at);
    return true;
}

} // namespace

QuotaParseResult ParseQuotaResponse(const std::string& response_text)
{
    QuotaParseResult result;

    try
    {
        const json root = json::parse(response_text);
        if (!root.is_object())
        {
            result.error = L"Kimi 响应不是有效的额度对象。";
            return result;
        }

        QuotaSnapshot snapshot;
        snapshot.status = QuotaStatus::Ok;
        snapshot.updated_at = std::chrono::system_clock::now();

        const bool has_five_hour = ParseFiveHourWindow(root, snapshot.five_hour);
        const bool has_weekly = root.contains("usage") && ParseWindow(root.at("usage"), snapshot.weekly);
        if (!has_five_hour || !has_weekly)
        {
            result.error = L"Kimi 响应缺少 5 小时或每周额度字段。";
            return result;
        }

        result.success = true;
        result.snapshot = std::move(snapshot);
    }
    catch (const std::exception& ex)
    {
        result.error = L"Kimi 响应解析失败：" + WideFromUtf8(ex.what());
    }

    return result;
}

QuotaParseResult ParseGlmQuotaResponse(const std::string& response_text)
{
    QuotaParseResult result;

    try
    {
        const json root = json::parse(response_text);
        if (!root.is_object())
        {
            result.error = L"GLM 响应不是有效的额度对象。";
            return result;
        }

        double code{};
        const bool business_ok = root.contains("success") && root.at("success").is_boolean() &&
            root.at("success").get<bool>() &&
            (!root.contains("code") || (NumberFromJson(root.at("code"), code) && code == 200.0));
        if (!business_ok)
        {
            std::wstring message = L"GLM 额度查询失败";
            if (root.contains("msg") && root.at("msg").is_string())
                message += L"：" + WideFromUtf8(root.at("msg").get<std::string>());
            result.error = message + L"。";
            return result;
        }

        if (!root.contains("data") || !root.at("data").is_object() ||
            !root.at("data").contains("limits") || !root.at("data").at("limits").is_array())
        {
            result.error = L"GLM 响应缺少 limits 字段。";
            return result;
        }

        QuotaSnapshot snapshot;
        snapshot.status = QuotaStatus::Ok;
        snapshot.updated_at = std::chrono::system_clock::now();

        for (const json& item : root.at("data").at("limits"))
        {
            if (!item.is_object() || !item.contains("type") || !item.at("type").is_string())
                continue;

            const std::string type = item.at("type").get<std::string>();
            if (type != "TOKENS_LIMIT" && type != "CREDIT_LIMIT")
                continue;

            // unit 3 is the rolling 5-hour window, unit 6 the weekly window.
            double unit{};
            const bool has_unit = item.contains("unit") && NumberFromJson(item.at("unit"), unit);
            QuotaWindow* window = nullptr;
            if (has_unit && unit == 3.0 && !snapshot.five_hour.valid)
                window = &snapshot.five_hour;
            else if (has_unit && unit == 6.0 && !snapshot.weekly.valid)
                window = &snapshot.weekly;
            else if (!has_unit && !snapshot.five_hour.valid)
                window = &snapshot.five_hour;
            if (window == nullptr)
                continue;

            ParseGlmLimitWindow(item, *window);
        }

        if (!snapshot.five_hour.valid && !snapshot.weekly.valid)
        {
            result.error = L"GLM 响应中没有可用的额度窗口。";
            return result;
        }

        result.success = true;
        result.snapshot = std::move(snapshot);
    }
    catch (const std::exception& ex)
    {
        result.error = L"GLM 响应解析失败：" + WideFromUtf8(ex.what());
    }

    return result;
}

QuotaParseResult ParseProviderQuotaResponse(ProviderKind kind, const std::string& response_text)
{
    switch (kind)
    {
    case ProviderKind::Glm:
        return ParseGlmQuotaResponse(response_text);
    case ProviderKind::Kimi:
    default:
        return ParseQuotaResponse(response_text);
    }
}

} // namespace quota
