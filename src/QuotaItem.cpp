#include "QuotaItem.h"

#include <cstdio>
#include <cwchar>
#include <windows.h>

#include "SelectorDialog.h"

namespace quota {
namespace {

// The display font size in DC units: 9 pt, the same size TrafficMonitor uses
// for its own taskbar text. Derived from the DC's DPI (not from the item
// height) so that measuring in GetItemWidthEx and drawing in DrawItem always
// use the same font and the returned width is exact.
int BaseFontHeight(HDC dc)
{
    return MulDiv(9, GetDeviceCaps(dc, LOGPIXELSY), 72);
}

HFONT CreateItemFont(int height)
{
    LOGFONTW font_info{};
    font_info.lfHeight = -height;
    font_info.lfWeight = FW_NORMAL;
    font_info.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(font_info.lfFaceName, L"Microsoft YaHei UI");
    return CreateFontIndirectW(&font_info);
}

int MeasureTextWidth(HDC dc, const wchar_t* text)
{
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    return size.cx;
}

// Draws text centered in rect, shrinking the font proportionally when the text
// is wider than the rect. With GetItemWidthEx returning the measured width
// this should never trigger; it stays as a fallback for narrow layouts.
void DrawFittedText(HDC dc, const wchar_t* text, const RECT& rect, int base_font_height)
{
    HFONT font = CreateItemFont(base_font_height);
    HGDIOBJ previous_font = SelectObject(dc, font);

    const int available = rect.right - rect.left - 4;
    const int text_width = MeasureTextWidth(dc, text);
    if (text_width > available && base_font_height > 8)
    {
        const int scaled = max(8, base_font_height * available / text_width);
        SelectObject(dc, previous_font);
        DeleteObject(font);
        font = CreateItemFont(scaled);
        previous_font = SelectObject(dc, font);
    }

    RECT draw_rect = rect;
    DrawTextW(dc, text, -1, &draw_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous_font);
    DeleteObject(font);
}

// One line of the single-provider layout: "4.7h 98%", falling back to
// "5H 98%" without a reset time and "5H --" without data.
std::wstring SingleProviderLine(const QuotaWindow& window, const wchar_t* fallback,
    double hours_per_unit, const wchar_t* unit, bool stale,
    std::chrono::system_clock::time_point now)
{
    std::wstring text;
    if (window.valid)
    {
        if (!window.HasResetTime())
        {
            text = fallback;
            text += L" ";
        }
        text += FormatWindowValue(window, hours_per_unit, unit, now);
    }
    else
    {
        text = fallback;
        text += L" --";
    }
    if (stale)
        text += L"!";
    return text;
}

} // namespace

QuotaItem::QuotaItem(QuotaService& service)
    : service_(service)
{
}

const wchar_t* QuotaItem::GetItemName() const
{
    return L"API 接口额度";
}

const wchar_t* QuotaItem::GetItemId() const
{
    // Kept stable so TrafficMonitor remembers the enabled-state setting.
    return L"kimi_remaining_quota_v1";
}

const wchar_t* QuotaItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* QuotaItem::GetItemValueText() const
{
    value_text_ = service_.TooltipText();
    return value_text_.c_str();
}

const wchar_t* QuotaItem::GetItemValueSampleText() const
{
    return L"Kimi 5.0h 100% 7.0d 100%!";
}

bool QuotaItem::IsCustomDraw() const
{
    return true;
}

int QuotaItem::GetItemWidth() const
{
    // Legacy fallback for plugin API < 3 hosts; TrafficMonitor >= 1.84 uses
    // GetItemWidthEx. Based on the persisted selection count so the value does
    // not flip while providers are still loading.
    return service_.SelectedDisplayCount() >= 2 ? 150 : 100;
}

int QuotaItem::GetItemWidthEx(void* hDC) const
{
    HDC dc = static_cast<HDC>(hDC);
    if (dc == nullptr)
        return 0;

    // Measure with the same font DrawItem uses; the returned width is in DC
    // units and TrafficMonitor does not scale it any further.
    HFONT font = CreateItemFont(BaseFontHeight(dc));
    HGDIOBJ previous_font = SelectObject(dc, font);

    int width = 0;
    if (service_.SelectedDisplayCount() >= 2)
    {
        // Real provider names + worst-case values, so the width stays stable
        // as numbers change but follows provider renames.
        for (const ProviderQuota& provider : service_.SelectedQuotas())
        {
            const std::wstring sample =
                provider.provider_name + L" 5.0h 100% 7.0d 100%!";
            width = max(width, MeasureTextWidth(dc, sample.c_str()));
        }
        if (width == 0)
        {
            // Providers not loaded yet (startup): keep two-provider room.
            width = MeasureTextWidth(dc, L"Kimi S 5.0h 100% 7.0d 100%!");
        }
    }
    else
    {
        width = max(MeasureTextWidth(dc, L"5.0h 100%"),
            MeasureTextWidth(dc, L"7.0d 100%!"));
    }

    SelectObject(dc, previous_font);
    DeleteObject(font);
    // DrawFittedText reserves 4 px; add a little breathing room on top.
    return width + 8;
}

void QuotaItem::DrawItem(void* hDC, int x, int y, int w, int h, bool)
{
    HDC dc = static_cast<HDC>(hDC);
    const std::vector<ProviderQuota> quotas = service_.SelectedQuotas();

    const int previous_mode = SetBkMode(dc, TRANSPARENT);
    const int base_font_height = BaseFontHeight(dc);
    const RECT top_rect{ x, y, x + w, y + h / 2 };
    const RECT bottom_rect{ x, y + h / 2, x + w, y + h };

    std::wstring top_text;
    std::wstring bottom_text;
    if (quotas.size() >= 2)
    {
        // Two providers: one line each with full info.
        top_text = BuildProviderLineText(quotas[0]);
        bottom_text = BuildProviderLineText(quotas[1]);
    }
    else
    {
        // Zero or one provider: the classic two-window layout.
        QuotaSnapshot snapshot;
        if (!quotas.empty())
            snapshot = quotas.front().quota;

        const auto now = std::chrono::system_clock::now();
        const bool stale = snapshot.status == QuotaStatus::Stale;
        top_text = SingleProviderLine(snapshot.five_hour, L"5H", 1.0, L"h", false, now);
        bottom_text = SingleProviderLine(snapshot.weekly, L"7D", 24.0, L"d", stale, now);
    }

    DrawFittedText(dc, top_text.c_str(), top_rect, base_font_height);
    DrawFittedText(dc, bottom_text.c_str(), bottom_rect, base_font_height);
    SetBkMode(dc, previous_mode);
}

int QuotaItem::OnMouseEvent(MouseEventType type, int, int, void* hWnd, int)
{
    switch (type)
    {
    case MT_LCLICKED:
        service_.RequestRefresh();
        return 1;
    case MT_DBCLICKED:
        ShowProviderSelector(hWnd, service_);
        return 1;
    default:
        return 0;
    }
}

void QuotaItem::ShowDetails(void* parent_window) const
{
    const std::wstring details = service_.DetailsText();
    MessageBoxW(static_cast<HWND>(parent_window), details.c_str(),
        L"API 额度详情", MB_OK | MB_ICONINFORMATION);
}

} // namespace quota
