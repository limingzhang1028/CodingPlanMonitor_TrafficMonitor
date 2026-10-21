#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>
#include <windows.h>

#include "CcSwitchSource.h"
#include "QuotaClient.h"
#include "QuotaParser.h"
#include "QuotaSnapshot.h"

namespace {

void ClearSecret(std::string& secret)
{
    if (!secret.empty())
        SecureZeroMemory(secret.data(), secret.size());
    secret.clear();
    secret.shrink_to_fit();
}

} // namespace

int wmain()
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    wchar_t profile[MAX_PATH]{};
    const DWORD profile_length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    const std::wstring database_path =
        (profile_length > 0 && profile_length < MAX_PATH ? profile : L".") +
        std::wstring(L"\\.cc-switch\\cc-switch.db");

    quota::ProviderListResult providers = quota::LoadCcSwitchProviders(database_path);
    if (!providers.success)
    {
        std::wcerr << L"Provider error: " << providers.error << L"\n";
        return 1;
    }
    if (!providers.error.empty())
        std::wcerr << L"Note: " << providers.error << L"\n";

    int failures = 0;
    for (quota::CcSwitchProvider& provider : providers.providers)
    {
        std::wcout << L"== " << provider.name
                   << (provider.is_current ? L" (current)" : L"") << L" ==\n";

        quota::QuotaHttpResult http = quota::FetchProviderQuota(provider);
        ClearSecret(provider.bearer_token);
        if (!http.success)
        {
            std::wcerr << L"HTTP error: " << http.error << L"\n\n";
            ++failures;
            continue;
        }

        quota::QuotaParseResult parsed =
            quota::ParseProviderQuotaResponse(provider.kind, http.response_body);
        if (!parsed.success)
        {
            std::wcerr << L"Parse error: " << parsed.error << L"\n\n";
            ++failures;
            continue;
        }

        std::wcout << quota::BuildDisplayText(parsed.snapshot) << L"\n"
                   << quota::BuildDetailsText(parsed.snapshot) << L"\n\n";
    }

    if (providers.providers.empty())
        return 1;
    return failures == 0 ? 0 : 1;
}
