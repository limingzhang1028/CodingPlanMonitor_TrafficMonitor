#pragma once

#include <string>

#include "PluginInterface.h"
#include "QuotaService.h"

namespace quota {

class QuotaItem final : public IPluginItem
{
public:
    explicit QuotaItem(QuotaService& service);

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    bool IsCustomDraw() const override;
    int GetItemWidth() const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

    void ShowDetails(void* parent_window) const;

private:
    QuotaService& service_;
    mutable std::wstring value_text_;
};

} // namespace quota
