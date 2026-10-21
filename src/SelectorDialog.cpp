#include "SelectorDialog.h"

#include <string>
#include <vector>
#include <windows.h>

#include "QuotaService.h"

namespace quota {
namespace {

constexpr size_t kMaxSelected = 2;
constexpr int kCheckIdBase = 100;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr wchar_t kClassName[] = L"KimiQuotaProviderSelector";

// Layout metrics at 96 DPI; scaled by the system DPI factor at creation.
struct Layout
{
    int margin;
    int width;
    int check_height;
    int summary_height;
    int summary_indent;
    int row_gap;
    int button_width;
    int button_height;

    explicit Layout(UINT dpi)
    {
        auto s = [dpi](int value) { return MulDiv(value, dpi, 96); };
        margin = s(14);
        width = s(470);
        check_height = s(22);
        summary_height = s(18);
        summary_indent = s(24);
        row_gap = s(12);
        button_width = s(84);
        button_height = s(28);
    }
};

struct SelectorState
{
    QuotaService& service;
    std::vector<ProviderRow> rows;
    std::vector<HWND> checkboxes;
    std::vector<HWND> summaries;
    HFONT font{};
    UINT dpi{ 96 };
    bool changed{};
};

int CountChecked(const SelectorState& state)
{
    int count = 0;
    for (HWND checkbox : state.checkboxes)
    {
        if (SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED)
            ++count;
    }
    return count;
}

void ApplyFont(HWND window, HFONT font)
{
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void UpdateSummaries(HWND window, SelectorState& state)
{
    state.rows = state.service.ProviderRows();
    for (size_t i = 0; i < state.summaries.size() && i < state.rows.size(); ++i)
    {
        const std::wstring summary = BuildSummaryText(state.rows[i].quota);
        std::wstring current(256, L'\0');
        const int length = GetWindowTextW(state.summaries[i], current.data(),
            static_cast<int>(current.size()));
        current.resize(static_cast<size_t>(length));
        if (current != summary)
            SetWindowTextW(state.summaries[i], summary.c_str());
    }
}

void CenterOnScreen(HWND window, HWND anchor_window)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    // Center on the work area of the monitor the anchor window is on. The
    // anchor is the tiny taskbar window, so centering on it would park the
    // dialog in the screen corner.
    HMONITOR monitor = anchor_window != nullptr
        ? MonitorFromWindow(anchor_window, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    RECT work{};
    if (GetMonitorInfoW(monitor, &info))
        work = info.rcWork;
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    const int x = work.left + (work.right - work.left - width) / 2;
    const int y = work.top + (work.bottom - work.top - height) / 3;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

LRESULT CALLBACK SelectorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    SelectorState* state =
        reinterpret_cast<SelectorState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        state = static_cast<SelectorState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->dpi = GetDpiForWindow(GetWindow(window, GW_OWNER));
        if (state->dpi == 0)
            state->dpi = GetDpiForSystem();
        const Layout layout(state->dpi);

        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, state->dpi);
        state->font = CreateFontIndirectW(&metrics.lfMessageFont);

        int y = layout.margin;
        for (size_t i = 0; i < state->rows.size(); ++i)
        {
            const ProviderRow& row = state->rows[i];
            std::wstring label = row.name;
            if (row.is_current)
                label += L"（当前）";
            HWND checkbox = CreateWindowExW(0, L"BUTTON", label.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                layout.margin, y, layout.width - 2 * layout.margin, layout.check_height,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCheckIdBase + i)),
                nullptr, nullptr);
            ApplyFont(checkbox, state->font);
            if (row.selected)
                SendMessageW(checkbox, BM_SETCHECK, BST_CHECKED, 0);
            state->checkboxes.push_back(checkbox);
            y += layout.check_height + MulDiv(4, state->dpi, 96);

            const std::wstring summary = BuildSummaryText(row.quota);
            HWND summary_text = CreateWindowExW(0, L"STATIC", summary.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                layout.margin + layout.summary_indent, y,
                layout.width - 2 * layout.margin - layout.summary_indent,
                layout.summary_height,
                window, nullptr, nullptr, nullptr);
            ApplyFont(summary_text, state->font);
            state->summaries.push_back(summary_text);
            y += layout.summary_height + layout.row_gap;
        }

        if (state->rows.empty())
        {
            HWND empty = CreateWindowExW(0, L"STATIC",
                L"CC Switch 中没有可查询额度的供应商。", WS_CHILD | WS_VISIBLE,
                layout.margin, y, layout.width - 2 * layout.margin, layout.check_height,
                window, nullptr, nullptr, nullptr);
            ApplyFont(empty, state->font);
            y += layout.check_height + layout.row_gap;
        }

        HWND hint = CreateWindowExW(0, L"STATIC",
            L"最多同时显示 2 个供应商。", WS_CHILD | WS_VISIBLE,
            layout.margin, y, layout.width - 2 * layout.margin, layout.summary_height,
            window, nullptr, nullptr, nullptr);
        ApplyFont(hint, state->font);
        y += layout.summary_height + layout.row_gap;

        HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            layout.width - layout.margin - 2 * layout.button_width - MulDiv(10, state->dpi, 96),
            y, layout.button_width, layout.button_height,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            layout.width - layout.margin - layout.button_width, y,
            layout.button_width, layout.button_height,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr);
        ApplyFont(ok, state->font);
        ApplyFont(cancel, state->font);
        y += layout.button_height + layout.margin;

        RECT client{ 0, 0, layout.width, y };
        AdjustWindowRectExForDpi(&client, GetWindowLongW(window, GWL_STYLE), FALSE,
            GetWindowLongW(window, GWL_EXSTYLE), state->dpi);
        SetWindowPos(window, nullptr, 0, 0, client.right - client.left,
            client.bottom - client.top, SWP_NOMOVE | SWP_NOZORDER);
        CenterOnScreen(window, GetWindow(window, GW_OWNER));

        // Fetch fresh details for every row and keep them updating.
        state->service.RequestDetails();
        SetTimer(window, kRefreshTimerId, 500, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (w_param == kRefreshTimerId && state != nullptr)
            UpdateSummaries(window, *state);
        return 0;
    case WM_COMMAND:
    {
        const int id = LOWORD(w_param);
        const int code = HIWORD(w_param);
        if (id == IDOK)
        {
            std::vector<std::string> selected;
            for (size_t i = 0; i < state->checkboxes.size(); ++i)
            {
                if (SendMessageW(state->checkboxes[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    selected.push_back(state->rows[i].id);
            }
            state->service.SetSelection(std::move(selected));
            state->changed = true;
            DestroyWindow(window);
            return 0;
        }
        if (id == IDCANCEL)
        {
            DestroyWindow(window);
            return 0;
        }
        if (code == BN_CLICKED && id >= kCheckIdBase &&
            id < kCheckIdBase + static_cast<int>(state->checkboxes.size()))
        {
            HWND clicked = reinterpret_cast<HWND>(l_param);
            if (SendMessageW(clicked, BM_GETCHECK, 0, 0) == BST_CHECKED &&
                CountChecked(*state) > static_cast<int>(kMaxSelected))
            {
                SendMessageW(clicked, BM_SETCHECK, BST_UNCHECKED, 0);
                MessageBeep(MB_ICONWARNING);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kRefreshTimerId);
        if (state != nullptr && state->font != nullptr)
            DeleteObject(state->font);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void RegisterSelectorClass()
{
    static bool registered = false;
    if (registered)
        return;

    WNDCLASSW wc{};
    wc.lpfnWndProc = SelectorProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    registered = true;
}

} // namespace

bool ShowProviderSelector(void* parent_window, QuotaService& service)
{
    RegisterSelectorClass();

    SelectorState state{ service, service.ProviderRows(), {}, {}, nullptr, false };
    HWND parent = static_cast<HWND>(parent_window);

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kClassName,
        L"选择要显示的供应商",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        parent, nullptr, GetModuleHandleW(nullptr), &state);
    if (window == nullptr)
        return false;

    if (parent != nullptr)
        EnableWindow(parent, FALSE);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (window == nullptr || !IsWindow(window))
            break;
        if (!IsDialogMessageW(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (parent != nullptr)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    return state.changed;
}

} // namespace quota
