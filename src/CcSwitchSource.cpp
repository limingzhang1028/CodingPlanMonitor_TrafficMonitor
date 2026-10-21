#include "CcSwitchSource.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <nlohmann/json.hpp>
#include <winsqlite/winsqlite3.h>

#include "Utf8.h"

namespace quota {
namespace {

using nlohmann::json;

std::string ColumnText(sqlite3_stmt* statement, int column)
{
    const unsigned char* text = sqlite3_column_text(statement, column);
    return text == nullptr ? std::string{} : reinterpret_cast<const char*>(text);
}

std::wstring SqliteError(sqlite3* database, const wchar_t* prefix)
{
    std::wstring message = prefix;
    if (database != nullptr)
        message += L"：" + WideFromUtf8(sqlite3_errmsg(database));
    return message;
}

std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trimmed(std::string value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string UrlHost(const std::string& url)
{
    const size_t scheme = url.find("://");
    const size_t host_begin = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t host_end = url.find_first_of("/:?", host_begin);
    return url.substr(host_begin, host_end == std::string::npos ? std::string::npos : host_end - host_begin);
}

bool HasKimiUsageScript(const std::string& meta_text)
{
    try
    {
        const json meta = json::parse(meta_text);
        if (meta.contains("usage_script") && meta.at("usage_script").is_object())
        {
            const json& usage_script = meta.at("usage_script");
            if (usage_script.contains("codingPlanProvider") &&
                usage_script.at("codingPlanProvider").is_string() &&
                Lowercase(usage_script.at("codingPlanProvider").get<std::string>()) == "kimi")
            {
                return true;
            }
        }
    }
    catch (...)
    {
    }
    return false;
}

ProviderKind DetectProviderKind(const std::string& name, const std::string& base_url,
    const std::string& meta_text)
{
    const std::string host = Lowercase(UrlHost(base_url));
    if (host == "api.kimi.com" || HasKimiUsageScript(meta_text) ||
        Lowercase(name).find("kimi") != std::string::npos)
    {
        return ProviderKind::Kimi;
    }
    if (host == "open.bigmodel.cn" || host == "bigmodel.cn" || host == "z.ai" ||
        host.size() > 4 && host.compare(host.size() - 4, 4, ".z.ai") == 0)
    {
        return ProviderKind::Glm;
    }
    return ProviderKind::Unknown;
}

} // namespace

ProviderListResult LoadCcSwitchProviders(const std::wstring& database_path)
{
    ProviderListResult result;

    sqlite3* raw_database{};
    const std::string path_utf8 = Utf8FromWide(database_path);
    int code = sqlite3_open_v2(path_utf8.c_str(), &raw_database,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> database(raw_database, sqlite3_close_v2);
    if (code != SQLITE_OK)
    {
        result.error = SqliteError(database.get(), L"无法打开 CC Switch 数据库");
        return result;
    }

    sqlite3_busy_timeout(database.get(), 1000);

    constexpr const char* query =
        "SELECT id, name, settings_config, meta, is_current "
        "FROM providers "
        "WHERE app_type = 'claude' "
        "ORDER BY is_current DESC, sort_index ASC";

    sqlite3_stmt* raw_statement{};
    code = sqlite3_prepare_v2(database.get(), query, -1, &raw_statement, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, sqlite3_finalize);
    if (code != SQLITE_OK)
    {
        result.error = SqliteError(database.get(), L"无法读取 CC Switch 供应商");
        return result;
    }

    while ((code = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        const std::string id = ColumnText(statement.get(), 0);
        const std::string name = ColumnText(statement.get(), 1);
        const std::string settings_text = ColumnText(statement.get(), 2);
        const std::string meta_text = ColumnText(statement.get(), 3);
        const bool is_current = sqlite3_column_int(statement.get(), 4) != 0;

        json settings;
        try
        {
            settings = json::parse(settings_text);
        }
        catch (...)
        {
            continue;
        }

        std::string base_url;
        std::string token;
        try
        {
            const json& env = settings.at("env");
            if (env.contains("ANTHROPIC_BASE_URL") && env.at("ANTHROPIC_BASE_URL").is_string())
                base_url = Trimmed(env.at("ANTHROPIC_BASE_URL").get<std::string>());
            if (env.contains("ANTHROPIC_AUTH_TOKEN") && env.at("ANTHROPIC_AUTH_TOKEN").is_string())
                token = Trimmed(env.at("ANTHROPIC_AUTH_TOKEN").get<std::string>());
        }
        catch (...)
        {
        }

        const ProviderKind kind = DetectProviderKind(name, base_url, meta_text);
        if (token.empty() || kind == ProviderKind::Unknown)
            continue;

        CcSwitchProvider provider;
        provider.id = std::move(id);
        provider.name = WideFromUtf8(name);
        provider.base_url = std::move(base_url);
        provider.bearer_token = std::move(token);
        provider.kind = kind;
        provider.is_current = is_current;
        result.providers.push_back(std::move(provider));
    }
    if (code != SQLITE_DONE)
    {
        result.error = SqliteError(database.get(), L"读取 CC Switch 供应商失败");
        return result;
    }

    result.success = true;
    if (result.providers.empty())
        result.error = L"CC Switch 中没有可查询额度的供应商（支持 Kimi 和智谱 GLM）。";
    return result;
}

} // namespace quota
