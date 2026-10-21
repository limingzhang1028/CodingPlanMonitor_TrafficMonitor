#include <cmath>
#include <ctime>
#include <iostream>
#include <string>

#include "QuotaParser.h"
#include "QuotaSnapshot.h"

namespace {

int g_failures{};

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool NearlyEqual(double left, double right)
{
    return std::fabs(left - right) < 0.000001;
}

void TestNumericResponse()
{
    const auto result = quota::ParseQuotaResponse(R"json(
    {
      "limits": [
        {"detail": {"limit": 100, "remaining": 82, "resetTime": 1790000000}}
      ],
      "usage": {"limit": 200, "remaining": 130, "resetTime": "2026-10-25T00:00:00Z"}
    })json");

    Expect(result.success, "numeric response should parse");
    Expect(result.snapshot.five_hour.valid, "five-hour window should be valid");
    Expect(result.snapshot.weekly.valid, "weekly window should be valid");
    Expect(NearlyEqual(result.snapshot.five_hour.remaining, 82.0), "five-hour remaining mismatch");
    Expect(NearlyEqual(result.snapshot.weekly.limit, 200.0), "weekly limit mismatch");
    Expect(result.snapshot.five_hour.RemainingPercent() == 82, "five-hour percent mismatch");
    Expect(result.snapshot.weekly.RemainingPercent() == 65, "weekly percent mismatch");
    Expect(!result.snapshot.five_hour.reset_time.empty(), "numeric reset time should be formatted");
    Expect(result.snapshot.weekly.reset_time == L"2026-10-25T00:00:00Z", "string reset time should be retained");

    const std::time_t five_hour_reset =
        std::chrono::system_clock::to_time_t(result.snapshot.five_hour.reset_at);
    Expect(five_hour_reset == 1790000000, "numeric reset time point mismatch");

    std::tm expected_weekly{};
    expected_weekly.tm_year = 2026 - 1900;
    expected_weekly.tm_mon = 9;
    expected_weekly.tm_mday = 25;
    const std::time_t weekly_reset =
        std::chrono::system_clock::to_time_t(result.snapshot.weekly.reset_at);
    Expect(weekly_reset == _mkgmtime(&expected_weekly), "ISO reset time point mismatch");
}

void TestStringNumbersAndMilliseconds()
{
    const auto result = quota::ParseQuotaResponse(R"json(
    {
      "limits": [
        {"detail": {"limit": "120", "remaining": "60", "resetTime": 1790000000000}}
      ],
      "usage": {"limit": "240", "remaining": "120", "resetTime": 0}
    })json");

    Expect(result.success, "string numbers should parse");
    Expect(result.snapshot.five_hour.RemainingPercent() == 50, "string-number percent mismatch");
    Expect(result.snapshot.weekly.RemainingPercent() == 50, "weekly string-number percent mismatch");
    Expect(!result.snapshot.five_hour.reset_time.empty(), "millisecond reset time should be formatted");
    Expect(result.snapshot.weekly.reset_time.empty(), "zero reset time should be empty");
}

void TestMissingAndInvalidFields()
{
    Expect(!quota::ParseQuotaResponse(R"json({"limits": []})json").success,
        "missing weekly usage should fail");
    Expect(!quota::ParseQuotaResponse("not json").success,
        "invalid JSON should fail");
    Expect(!quota::ParseQuotaResponse(R"json({
        "limits": [{"detail": {"limit": 100, "remaining": -1}}],
        "usage": {"limit": 100, "remaining": 50}
    })json").success,
        "negative remaining value should fail");
}

void TestDisplayText()
{
    quota::QuotaSnapshot snapshot;
    Expect(quota::BuildDisplayText(snapshot) == L"5H--·7D--", "empty display mismatch");

    snapshot.status = quota::QuotaStatus::Ok;
    snapshot.five_hour.valid = true;
    snapshot.five_hour.limit = 100;
    snapshot.five_hour.remaining = 82;
    snapshot.weekly.valid = true;
    snapshot.weekly.limit = 200;
    snapshot.weekly.remaining = 130;
    Expect(quota::BuildDisplayText(snapshot) == L"5H82%·7D65%",
        "labels should fall back without reset times");

    const auto now = std::chrono::system_clock::now();
    snapshot.five_hour.reset_at = now + std::chrono::minutes(132); // 2.2h
    snapshot.weekly.reset_at = now + std::chrono::hours(60);       // 2.5d
    Expect(quota::BuildDisplayText(snapshot) == L"2.2h82%·2.5d65%",
        "remaining-time display mismatch");

    snapshot.status = quota::QuotaStatus::Stale;
    Expect(quota::BuildDisplayText(snapshot) == L"2.2h82%·2.5d65%!", "stale display mismatch");
}

void TestGlmResponse()
{
    const auto result = quota::ParseGlmQuotaResponse(R"json(
    {
      "code": 200, "msg": "操作成功", "success": true,
      "data": {"limits": [
        {"type": "TIME_LIMIT", "unit": 5, "number": 1, "usage": 100, "currentValue": 0,
         "remaining": 100, "percentage": 0, "nextResetTime": 1788921262994},
        {"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 1,
         "nextResetTime": 1786626122911, "level": "lite"},
        {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 31, "nextResetTime": 1787226122911}
      ]}
    })json");

    Expect(result.success, "GLM response should parse");
    Expect(result.snapshot.five_hour.valid, "GLM five-hour window should be valid");
    Expect(result.snapshot.weekly.valid, "GLM weekly window should be valid");
    Expect(result.snapshot.five_hour.RemainingPercent() == 99, "GLM five-hour percent mismatch");
    Expect(result.snapshot.weekly.RemainingPercent() == 69, "GLM weekly percent mismatch");
    Expect(result.snapshot.five_hour.HasResetTime(), "GLM five-hour reset time missing");
    Expect(std::chrono::system_clock::to_time_t(result.snapshot.five_hour.reset_at) ==
        1786626123, "GLM five-hour reset epoch mismatch");

    const auto no_plan = quota::ParseGlmQuotaResponse(R"json(
    {"code": 500, "msg": "当前用户未开通coding plan", "success": false}
    )json");
    Expect(!no_plan.success, "no-plan GLM response should fail");
    Expect(no_plan.error.find(L"coding plan") != std::wstring::npos,
        "no-plan error should carry the server message");

    // A V1 plan returns only the 5-hour window; that alone is a valid result.
    const auto five_only = quota::ParseGlmQuotaResponse(R"json(
    {"code": 200, "msg": "操作成功", "success": true,
     "data": {"limits": [
       {"type": "TOKENS_LIMIT", "unit": 3, "percentage": 1, "nextResetTime": 1786626122911}
     ]}}
    )json");
    Expect(five_only.success, "5h-only GLM response should parse");
    Expect(five_only.snapshot.five_hour.valid && !five_only.snapshot.weekly.valid,
        "5h-only GLM window flags mismatch");
}

void TestProviderLineText()
{
    quota::ProviderQuota provider;
    provider.provider_name = L"Kimi";
    provider.quota.status = quota::QuotaStatus::Ok;
    provider.quota.five_hour.valid = true;
    provider.quota.five_hour.limit = 100;
    provider.quota.five_hour.remaining = 82;
    provider.quota.weekly.valid = true;
    provider.quota.weekly.limit = 200;
    provider.quota.weekly.remaining = 130;
    Expect(quota::BuildProviderLineText(provider) == L"Kimi 82% 65%",
        "provider line without reset times mismatch");

    const auto now = std::chrono::system_clock::now();
    provider.quota.five_hour.reset_at = now + std::chrono::minutes(132); // 2.2h
    provider.quota.weekly.reset_at = now + std::chrono::hours(60);       // 2.5d
    Expect(quota::BuildProviderLineText(provider) == L"Kimi 2.2h 82% 2.5d 65%",
        "provider line with reset times mismatch");

    provider.quota.status = quota::QuotaStatus::Stale;
    Expect(quota::BuildProviderLineText(provider) == L"Kimi 2.2h 82% 2.5d 65%!",
        "stale provider line mismatch");
}

} // namespace

int main()
{
    TestNumericResponse();
    TestStringNumbersAndMilliseconds();
    TestMissingAndInvalidFields();
    TestDisplayText();
    TestGlmResponse();
    TestProviderLineText();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All quota parser tests passed.\n";
    return 0;
}
