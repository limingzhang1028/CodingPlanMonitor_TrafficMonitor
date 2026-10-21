#pragma once

#include <string>

#include "QuotaItem.h"
#include "PluginInterface.h"
#include "QuotaService.h"

namespace quota {

class QuotaPlugin final : public ITMPlugin
{
public:
    static QuotaPlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetTooltipInfo() override;
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    void OnInitialize(ITrafficMonitor* pApp) override;

private:
    QuotaPlugin();

    QuotaService service_;
    QuotaItem item_;
    ITrafficMonitor* app_{};
    mutable std::wstring tooltip_text_;
};

} // namespace quota

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
