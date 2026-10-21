#include "QuotaClient.h"

#include <array>
#include <memory>
#include <windows.h>
#include <winhttp.h>

#include "Utf8.h"

namespace quota {
namespace {

struct InternetHandleDeleter
{
    void operator()(HINTERNET handle) const
    {
        if (handle != nullptr)
            WinHttpCloseHandle(handle);
    }
};

using InternetHandle = std::unique_ptr<std::remove_pointer_t<HINTERNET>, InternetHandleDeleter>;

std::wstring Win32Error(const wchar_t* prefix, DWORD error_code = GetLastError())
{
    LPWSTR buffer{};
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = prefix;
    message += L"（" + std::to_wstring(error_code) + L"）";
    if (length > 0 && buffer != nullptr)
    {
        message += L"：" + std::wstring(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
            message.pop_back();
    }
    if (buffer != nullptr)
        LocalFree(buffer);
    return message;
}

// Resolves the quota endpoint for a provider: host plus request path.
bool ResolveEndpoint(const CcSwitchProvider& provider, std::wstring& host, std::wstring& path)
{
    switch (provider.kind)
    {
    case ProviderKind::Kimi:
    {
        // base_url looks like "https://api.kimi.com/coding/"; the quota endpoint
        // is "<base>v1/usages".
        const std::wstring wide_url = WideFromUtf8(provider.base_url);
        URL_COMPONENTSW components{};
        components.dwStructSize = sizeof(components);
        wchar_t host_buffer[256]{};
        wchar_t path_buffer[512]{};
        components.lpszHostName = host_buffer;
        components.dwHostNameLength = static_cast<DWORD>(std::size(host_buffer));
        components.lpszUrlPath = path_buffer;
        components.dwUrlPathLength = static_cast<DWORD>(std::size(path_buffer));
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components))
            return false;

        host = host_buffer;
        path = path_buffer;
        if (!path.empty() && path.back() != L'/')
            path += L'/';
        path += L"v1/usages";
        return !host.empty();
    }
    case ProviderKind::Glm:
        host = L"open.bigmodel.cn";
        path = L"/api/monitor/usage/quota/limit";
        return true;
    default:
        return false;
    }
}

} // namespace

QuotaHttpResult FetchProviderQuota(const CcSwitchProvider& provider)
{
    QuotaHttpResult result;

    std::wstring host;
    std::wstring path;
    if (!ResolveEndpoint(provider, host, path))
    {
        result.error = L"该供应商没有已知的额度查询接口。";
        return result;
    }

    InternetHandle session(WinHttpOpen(
        L"KimiTaskbar/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session)
    {
        result.error = Win32Error(L"初始化网络组件失败");
        return result;
    }

    constexpr DWORD timeout_ms = 15000;
    WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
    {
        result.error = Win32Error(L"连接额度服务器失败");
        return result;
    }

    InternetHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request)
    {
        result.error = Win32Error(L"创建额度请求失败");
        return result;
    }

    const std::wstring headers =
        L"Authorization: Bearer " + WideFromUtf8(provider.bearer_token) + L"\r\n"
        L"Accept: application/json\r\n";
    if (!WinHttpAddRequestHeaders(request.get(), headers.c_str(),
        static_cast<DWORD>(headers.size()), WINHTTP_ADDREQ_FLAG_ADD))
    {
        result.error = Win32Error(L"设置请求头失败");
        return result;
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        result.error = Win32Error(L"发送额度请求失败");
        return result;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr))
    {
        result.error = Win32Error(L"接收额度响应失败");
        return result;
    }

    DWORD status_code{};
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(request.get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX))
    {
        result.error = Win32Error(L"读取响应状态失败");
        return result;
    }
    result.status_code = status_code;

    std::string body;
    std::array<char, 16384> buffer{};
    for (;;)
    {
        DWORD bytes_read{};
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read))
        {
            result.error = Win32Error(L"读取响应内容失败");
            return result;
        }
        if (bytes_read == 0)
            break;
        body.append(buffer.data(), bytes_read);
    }

    if (status_code == 401 || status_code == 403)
    {
        result.error = L"凭据无效或已过期。";
        return result;
    }
    if (status_code < 200 || status_code >= 300)
    {
        result.error = L"服务器返回 HTTP " + std::to_wstring(status_code) + L"。";
        return result;
    }

    result.success = true;
    result.response_body = std::move(body);
    return result;
}

} // namespace quota
