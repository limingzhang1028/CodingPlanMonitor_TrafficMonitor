#include "QuotaPlugin.h"

#include <windows.h>

#include "SelectorDialog.h"

namespace quota {
namespace {

std::wstring CcSwitchDatabasePath()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    std::wstring path = length > 0 && length < MAX_PATH ? buffer : L".";
    path += L"\\.cc-switch\\cc-switch.db";
    return path;
}

std::wstring ConfigPath()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::wstring path = length > 0 && length < MAX_PATH ? buffer : L".";
    path += L"\\KimiQuotaTaskbar\\settings.json";
    return path;
}

} // namespace

QuotaPlugin::QuotaPlugin()
    : service_(CcSwitchDatabasePath(), ConfigPath())
    , item_(service_)
{
}

QuotaPlugin& QuotaPlugin::Instance()
{
    static QuotaPlugin instance;
    return instance;
}

IPluginItem* QuotaPlugin::GetItem(int index)
{
    return index == 0 ? &item_ : nullptr;
}

void QuotaPlugin::DataRequired()
{
    // The worker thread owns polling. This method is intentionally cheap because
    // TrafficMonitor calls it on its UI update cadence.
}

const wchar_t* QuotaPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return L"API 接口额度";
    case TMI_DESCRIPTION:
        return L"在任务栏显示 CC Switch 供应商（Kimi、智谱 GLM）的 5 小时和每周剩余额度，最多同时显示两个供应商。";
    case TMI_AUTHOR:
        return L"Lim_z";
    case TMI_COPYRIGHT:
        return L"";
    case TMI_VERSION:
        return L"1.1.1";
    case TMI_URL:
        return L"";
    default:
        return L"";
    }
}

ITMPlugin::OptionReturn QuotaPlugin::ShowOptionsDialog(void* hParent)
{
    ShowProviderSelector(hParent, service_);
    return OR_OPTION_UNCHANGED;
}

const wchar_t* QuotaPlugin::GetTooltipInfo()
{
    tooltip_text_ = service_.TooltipText();
    return tooltip_text_.c_str();
}

int QuotaPlugin::GetCommandCount()
{
    return 3;
}

const wchar_t* QuotaPlugin::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0:
        return L"刷新额度";
    case 1:
        return L"选择供应商";
    case 2:
        return L"额度详情";
    default:
        return nullptr;
    }
}

void QuotaPlugin::OnPluginCommand(int command_index, void* hWnd, void*)
{
    switch (command_index)
    {
    case 0:
        service_.RequestRefresh();
        break;
    case 1:
        ShowProviderSelector(hWnd, service_);
        break;
    case 2:
        item_.ShowDetails(hWnd);
        break;
    default:
        break;
    }
}

void QuotaPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    app_ = pApp;

    // The polling thread runs for the host process lifetime. Pin the module so
    // TrafficMonitor cannot unload the DLL while that thread is active.
    HMODULE module{};
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Instance()),
        &module);

    service_.Start();
}

} // namespace quota

ITMPlugin* TMPluginGetInstance()
{
    return &quota::QuotaPlugin::Instance();
}
