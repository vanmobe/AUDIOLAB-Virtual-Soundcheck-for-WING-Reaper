/*
 * Windows Native AUDIOLAB.wing.reaper.virtualsoundcheck Dialog Implementation
 */

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>

#include "internal/wing_connector_dialog_windows.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "internal/logger.h"
#include "reaper_plugin_functions.h"
#include "wingconnector/reaper_extension.h"

using namespace WingConnector;

extern REAPER_PLUGIN_HINSTANCE g_hInst;
extern HWND g_hwndParent;

namespace {

constexpr wchar_t kDialogClassName[] = L"WINGuardWindowsDialog";
constexpr wchar_t kSourceDialogClassName[] = L"WINGuardSourcePickerDialog";
constexpr wchar_t kPageClassName[] = L"WINGuardWindowsPage";
constexpr int kWindowWidth = 860;
constexpr int kWindowHeight = 780;
constexpr UINT_PTR kRefreshTimerId = 101;
constexpr UINT kRefreshTimerMs = 1500;

enum ControlId {
    kIdTab = 100,
    kIdBannerGroup,
    kIdTitle,
    kIdSubtitle,
    kIdStatusGroup,
    kIdHeaderConsoleStatus,
    kIdHeaderValidationStatus,
    kIdHeaderRecorderStatus,
    kIdHeaderMidiStatus,
    kIdConsoleStatusChip,
    kIdReaperStatusChip,
    kIdWingStatusChip,
    kIdControlStatusChip,
    kIdWingCombo,
    kIdScanButton,
    kIdManualIpEdit,
    kIdConnectButton,
    kIdConsoleHelp,
    kIdReaperOutputUsb,
    kIdReaperOutputCard,
    kIdPendingSummary,
    kIdReadinessDetail,
    kIdChooseSourcesButton,
    kIdApplySetupButton,
    kIdDiscardSetupButton,
    kIdToggleSoundcheckButton,
    kIdReaperHelp,
    kIdWingPlaceholder,
    kIdControlPlaceholder,
    kIdFooterStatus,
    kIdSourceList,
    kIdSourceSelectAll,
    kIdSourceSelectChannels,
    kIdSourceClear,
    kIdSourceModeSoundcheck,
    kIdSourceModeRecord,
    kIdSourceReplace,
    kIdSourceOk,
    kIdSourceCancel,
    kIdSourceCount
};

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], length);
    wide.resize(static_cast<size_t>(length - 1));
    return wide;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return std::string(text.begin(), text.end());
    }
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], length, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(length - 1));
    return utf8;
}

std::wstring ReadWindowText(HWND hwnd) {
    if (!hwnd) {
        return std::wstring();
    }
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(std::max(length, 0) + 1), L'\0');
    if (length > 0) {
        GetWindowTextW(hwnd, &text[0], length + 1);
    }
    text.resize(static_cast<size_t>(std::max(length, 0)));
    return text;
}

void SetWindowFontRecursive(HWND hwnd, HFONT font) {
    if (!hwnd || !font) {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SetWindowFontRecursive(child, font);
    }
}

void SaveConfigIfPossible(ReaperExtension& extension) {
    const std::string path = WingConfig::GetConfigPath();
    if (!extension.GetConfig().SaveToFile(path)) {
        Logger::Error("Failed to save WINGuard config to %s", path.c_str());
    }
}

struct HeaderStatusVisual {
    COLORREF color = RGB(110, 110, 110);
    std::wstring text;
};

struct SourcePickerResult {
    bool confirmed = false;
    bool setup_soundcheck = true;
    bool replace_existing = true;
    std::vector<SourceSelectionInfo> channels;
};

class SourcePickerDialog {
public:
    SourcePickerDialog(HWND owner,
                       std::vector<SourceSelectionInfo> channels,
                       bool setup_soundcheck,
                       bool replace_existing)
        : owner_(owner) {
        result_.channels = std::move(channels);
        result_.setup_soundcheck = setup_soundcheck;
        result_.replace_existing = replace_existing;
    }

    SourcePickerResult Run() {
        RegisterClass();
        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kSourceDialogClassName,
            L"Review Sources For Live Setup",
            WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 760, 620,
            owner_,
            nullptr,
            g_hInst,
            this);
        if (!hwnd_) {
            return result_;
        }

        EnableWindow(owner_, FALSE);
        MSG msg{};
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (owner_) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        return result_;
    }

private:
    static void RegisterClass() {
        static bool registered = false;
        if (registered) {
            return;
        }
        WNDCLASSW wc{};
        wc.lpfnWndProc = &SourcePickerDialog::WndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = kSourceDialogClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        SourcePickerDialog* self = reinterpret_cast<SourcePickerDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = reinterpret_cast<SourcePickerDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (!self) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        switch (msg) {
            case WM_CREATE: return self->OnCreate();
            case WM_COMMAND: return self->OnCommand(LOWORD(wparam), HIWORD(wparam));
            case WM_CLOSE:
                self->done_ = true;
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    LRESULT OnCreate() {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        CreateWindowW(L"STATIC",
                      L"Choose which channels, buses, or matrices should be included in the next apply. No routing changes happen until you confirm.",
                      WS_CHILD | WS_VISIBLE,
                      20, 18, 700, 32,
                      hwnd_,
                      nullptr,
                      g_hInst,
                      nullptr);

        listbox_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                   L"LISTBOX",
                                   nullptr,
                                   WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | WS_VSCROLL | LBS_NOTIFY,
                                   20, 60, 700, 380,
                                   hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceList)),
                                   g_hInst,
                                   nullptr);

        CreateWindowW(L"BUTTON", L"Select All", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      20, 455, 110, 28, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceSelectAll)), g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Channels Only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      140, 455, 120, 28, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceSelectChannels)), g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      270, 455, 90, 28, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceClear)), g_hInst, nullptr);

        count_label_ = CreateWindowW(L"STATIC", L"0 sources selected", WS_CHILD | WS_VISIBLE,
                                     390, 460, 220, 20, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceCount)), g_hInst, nullptr);

        CreateWindowW(L"BUTTON", L"Soundcheck", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                      20, 500, 120, 22, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceModeSoundcheck)), g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Record", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                      150, 500, 100, 22, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceModeRecord)), g_hInst, nullptr);
        replace_checkbox_ = CreateWindowW(L"BUTTON", L"Replace managed REAPER tracks", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          20, 530, 220, 22, hwnd_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceReplace)), g_hInst, nullptr);

        CreateWindowW(L"BUTTON", L"Apply Draft", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      500, 530, 110, 30, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceOk)), g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      620, 530, 100, 30, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceCancel)), g_hInst, nullptr);

        SetWindowFontRecursive(hwnd_, font);
        CheckRadioButton(hwnd_,
                         kIdSourceModeSoundcheck,
                         kIdSourceModeRecord,
                         result_.setup_soundcheck ? kIdSourceModeSoundcheck : kIdSourceModeRecord);
        SendMessageW(replace_checkbox_, BM_SETCHECK,
                     result_.replace_existing ? BST_CHECKED : BST_UNCHECKED, 0);
        PopulateList();
        CenterWindow();
        return 0;
    }

    void CenterWindow() {
        RECT owner_rect{};
        RECT dialog_rect{};
        GetWindowRect(owner_ ? owner_ : GetDesktopWindow(), &owner_rect);
        GetWindowRect(hwnd_, &dialog_rect);
        const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - (dialog_rect.right - dialog_rect.left)) / 2;
        const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - (dialog_rect.bottom - dialog_rect.top)) / 2;
        SetWindowPos(hwnd_, nullptr, std::max(0, x), std::max(0, y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    void PopulateList() {
        for (size_t i = 0; i < result_.channels.size(); ++i) {
            const auto& source = result_.channels[i];
            std::string kind = "SRC";
            switch (source.kind) {
                case SourceKind::Channel: kind = "CH"; break;
                case SourceKind::Bus: kind = "BUS"; break;
                case SourceKind::Main: kind = "MAIN"; break;
                case SourceKind::Matrix: kind = "MTX"; break;
            }
            std::string name = source.name.empty() ? (kind + " " + std::to_string(source.source_number)) : source.name;
            std::string line = kind + " " + std::to_string(source.source_number) + "  |  " + name;
            if (!source.source_group.empty() && source.source_input > 0) {
                line += "  |  " + source.source_group + ":" + std::to_string(source.source_input);
            }
            if (!source.soundcheck_capable) {
                line += "  |  Record only";
            }
            const std::wstring wide = ToWide(line);
            SendMessageW(listbox_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
            if (source.selected) {
                SendMessageW(listbox_, LB_SETSEL, TRUE, static_cast<LPARAM>(i));
            }
        }
        UpdateSelectionCount();
    }

    void UpdateSelectionCount() {
        const LRESULT selected = SendMessageW(listbox_, LB_GETSELCOUNT, 0, 0);
        wchar_t buffer[64];
        std::swprintf(buffer, sizeof(buffer) / sizeof(wchar_t), L"%ld sources selected", static_cast<long>(selected));
        SetWindowTextW(count_label_, buffer);
    }

    void SelectMatching(bool channels_only) {
        const int count = static_cast<int>(result_.channels.size());
        for (int i = 0; i < count; ++i) {
            const bool enable = !channels_only || result_.channels[static_cast<size_t>(i)].kind == SourceKind::Channel;
            SendMessageW(listbox_, LB_SETSEL, enable ? TRUE : FALSE, i);
        }
        UpdateSelectionCount();
    }

    void CommitSelection() {
        std::vector<int> selected_indices(result_.channels.size());
        const LRESULT selected_count = SendMessageW(listbox_, LB_GETSELITEMS,
                                                   static_cast<WPARAM>(selected_indices.size()),
                                                   reinterpret_cast<LPARAM>(selected_indices.data()));
        std::set<int> selected_set;
        for (LRESULT i = 0; i < selected_count; ++i) {
            selected_set.insert(selected_indices[static_cast<size_t>(i)]);
        }
        for (size_t i = 0; i < result_.channels.size(); ++i) {
            result_.channels[i].selected = selected_set.count(static_cast<int>(i)) > 0;
        }
        result_.setup_soundcheck =
            (IsDlgButtonChecked(hwnd_, kIdSourceModeSoundcheck) == BST_CHECKED);
        result_.replace_existing =
            (SendMessageW(replace_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        result_.confirmed = true;
        done_ = true;
    }

    LRESULT OnCommand(WORD id, WORD notify_code) {
        switch (id) {
            case kIdSourceSelectAll:
                SelectMatching(false);
                return 0;
            case kIdSourceSelectChannels:
                SelectMatching(true);
                return 0;
            case kIdSourceClear:
                SendMessageW(listbox_, LB_SETSEL, FALSE, -1);
                UpdateSelectionCount();
                return 0;
            case kIdSourceList:
                if (notify_code == LBN_SELCHANGE) {
                    UpdateSelectionCount();
                }
                return 0;
            case kIdSourceOk:
                CommitSelection();
                return 0;
            case kIdSourceCancel:
                done_ = true;
                return 0;
            default:
                return 0;
        }
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND listbox_ = nullptr;
    HWND replace_checkbox_ = nullptr;
    HWND count_label_ = nullptr;
    bool done_ = false;
    SourcePickerResult result_;
};

class WingConnectorWindowsDialog {
public:
    static void Show() {
        if (!instance_) {
            instance_ = new WingConnectorWindowsDialog();
        }
        instance_->ShowInternal();
    }

private:
    struct StatusSnapshot {
        HeaderStatusVisual console;
        HeaderStatusVisual validation;
        HeaderStatusVisual recorder;
        HeaderStatusVisual midi;
        HeaderStatusVisual console_tab;
        HeaderStatusVisual reaper_tab;
        HeaderStatusVisual wing_tab;
        HeaderStatusVisual control_tab;
        std::wstring pending_summary;
        COLORREF pending_color = RGB(110, 110, 110);
        std::wstring readiness_detail;
        COLORREF readiness_color = RGB(110, 110, 110);
        std::wstring footer;
        bool can_apply = false;
        bool can_discard = false;
        bool can_toggle = false;
        std::wstring apply_label;
        std::wstring toggle_label;
    };

    static WingConnectorWindowsDialog* instance_;

    static void RegisterClass() {
        static bool registered = false;
        if (registered) {
            return;
        }
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);

        WNDCLASSW wc{};
        wc.lpfnWndProc = &WingConnectorWindowsDialog::WndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = kDialogClassName;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(OCR_NORMAL));
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wc);

        WNDCLASSW page_wc{};
        page_wc.lpfnWndProc = &WingConnectorWindowsDialog::PageWndProc;
        page_wc.hInstance = g_hInst;
        page_wc.lpszClassName = kPageClassName;
        page_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(OCR_NORMAL));
        page_wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&page_wc);
        registered = true;
    }

    static LRESULT CALLBACK PageWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        HWND parent = GetParent(hwnd);
        switch (msg) {
            case WM_COMMAND:
            case WM_NOTIFY:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
                if (parent) {
                    return SendMessageW(parent, msg, wparam, lparam);
                }
                break;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<WingConnectorWindowsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = reinterpret_cast<WingConnectorWindowsDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (!self) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        switch (msg) {
            case WM_CREATE: return self->OnCreate();
            case WM_COMMAND: return self->OnCommand(LOWORD(wparam), HIWORD(wparam));
            case WM_NOTIFY: return self->OnNotify(reinterpret_cast<NMHDR*>(lparam));
            case WM_TIMER: return self->OnTimer(static_cast<UINT_PTR>(wparam));
            case WM_CTLCOLORSTATIC: return self->OnCtlColor(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam));
            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            case WM_DESTROY:
                self->hwnd_ = nullptr;
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    void ShowInternal() {
        RegisterClass();
        if (!hwnd_) {
            hwnd_ = CreateWindowExW(
                WS_EX_TOOLWINDOW,
                kDialogClassName,
                L"WINGuard",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
                g_hwndParent,
                nullptr,
                g_hInst,
                this);
        }
        if (!hwnd_) {
            ShowMessageBox("Failed to create the Windows WINGuard dialog.",
                           "WINGuard",
                           0);
            return;
        }
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        RefreshAll();
    }

    LRESULT OnCreate() {
        font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        bold_font_ = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        small_bold_font_ = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        CreateWindowW(L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                      12, 10, 820, 132, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBannerGroup)), g_hInst, nullptr);
        title_ = CreateWindowW(L"STATIC", L"WINGuard", WS_CHILD | WS_VISIBLE,
                               32, 34, 260, 28, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTitle)), g_hInst, nullptr);
        subtitle_ = CreateWindowW(L"STATIC", L"Guard every take. Faster setup, safer record(w)ing!",
                                  WS_CHILD | WS_VISIBLE,
                                  32, 66, 360, 20, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSubtitle)), g_hInst, nullptr);

        CreateWindowW(L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                      472, 26, 340, 96, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatusGroup)), g_hInst, nullptr);
        header_console_status_ = CreateWindowW(L"STATIC", L"Console: Not Connected", WS_CHILD | WS_VISIBLE,
                                               492, 42, 300, 18, hwnd_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderConsoleStatus)), g_hInst, nullptr);
        header_validation_status_ = CreateWindowW(L"STATIC", L"Reaper Recorder: Not Ready", WS_CHILD | WS_VISIBLE,
                                                  492, 62, 300, 18, hwnd_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderValidationStatus)), g_hInst, nullptr);
        header_recorder_status_ = CreateWindowW(L"STATIC", L"Wing Recorder: Disabled", WS_CHILD | WS_VISIBLE,
                                                492, 82, 300, 18, hwnd_,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderRecorderStatus)), g_hInst, nullptr);
        header_midi_status_ = CreateWindowW(L"STATIC", L"Wing control integration: Disabled", WS_CHILD | WS_VISIBLE,
                                            492, 102, 300, 18, hwnd_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderMidiStatus)), g_hInst, nullptr);

        tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                               12, 154, 820, 560, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab)), g_hInst, nullptr);
        InsertTab(0, L"Console");
        InsertTab(1, L"Reaper");
        InsertTab(2, L"Wing");
        InsertTab(3, L"Control Integration");
        CreatePages();
        CreateConsolePage();
        CreateReaperPage();
        CreateWingPlaceholderPage();
        CreateControlPlaceholderPage();

        footer_status_ = CreateWindowW(L"STATIC", L"Windows Phase 1: native connection and REAPER setup surfaces now match the macOS layout direction.",
                                       WS_CHILD | WS_VISIBLE,
                                       16, 724, 804, 20, hwnd_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFooterStatus)), g_hInst, nullptr);

        SetWindowFontRecursive(hwnd_, font_);
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(bold_font_), TRUE);
        SendMessageW(tab_status_console_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_reaper_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_wing_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_control_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);

        SelectOutputMode(ReaperExtension::Instance().GetConfig().soundcheck_output_mode);
        pending_output_mode_ = CurrentOutputMode();
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        ShowActivePage(0);
        RefreshDiscoveryControls(false);
        RefreshAll();
        return 0;
    }

    void InsertTab(int index, const wchar_t* label) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(label);
        TabCtrl_InsertItem(tab_, index, &item);
    }

    void CreatePages() {
        RECT tab_rect{};
        GetClientRect(tab_, &tab_rect);
        TabCtrl_AdjustRect(tab_, FALSE, &tab_rect);
        MapWindowPoints(tab_, hwnd_, reinterpret_cast<LPPOINT>(&tab_rect), 2);
        const int width = tab_rect.right - tab_rect.left;
        const int height = tab_rect.bottom - tab_rect.top;
        page_console_ = CreateWindowW(kPageClassName, L"", WS_CHILD | WS_VISIBLE,
                                      tab_rect.left, tab_rect.top, width, height,
                                      hwnd_, nullptr, g_hInst, nullptr);
        page_reaper_ = CreateWindowW(kPageClassName, L"", WS_CHILD,
                                     tab_rect.left, tab_rect.top, width, height,
                                     hwnd_, nullptr, g_hInst, nullptr);
        page_wing_ = CreateWindowW(kPageClassName, L"", WS_CHILD,
                                   tab_rect.left, tab_rect.top, width, height,
                                   hwnd_, nullptr, g_hInst, nullptr);
        page_control_ = CreateWindowW(kPageClassName, L"", WS_CHILD,
                                      tab_rect.left, tab_rect.top, width, height,
                                      hwnd_, nullptr, g_hInst, nullptr);
    }

    void CreateConsolePage() {
        CreateWindowW(L"STATIC",
                      L"Connect to a Wing, discover consoles on the network, or enter a manual IP when discovery comes back empty-handed.",
                      WS_CHILD | WS_VISIBLE,
                      20, 18, 740, 36, page_console_, nullptr, g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Connection", WS_CHILD | WS_VISIBLE,
                      20, 64, 160, 20, page_console_, nullptr, g_hInst, nullptr);
        tab_status_console_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                            640, 64, 120, 20, page_console_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdConsoleStatusChip)), g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Wing Console:", WS_CHILD | WS_VISIBLE,
                      20, 108, 110, 20, page_console_, nullptr, g_hInst, nullptr);
        wing_combo_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                    140, 104, 470, 240, page_console_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWingCombo)), g_hInst, nullptr);
        scan_button_ = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     630, 104, 120, 28, page_console_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdScanButton)), g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Pick a discovered Wing to fill the connection details automatically.",
                      WS_CHILD | WS_VISIBLE,
                      140, 136, 540, 18, page_console_, nullptr, g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Manual IP:", WS_CHILD | WS_VISIBLE,
                      20, 182, 110, 20, page_console_, nullptr, g_hInst, nullptr);
        manual_ip_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          140, 178, 260, 24, page_console_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdManualIpEdit)), g_hInst, nullptr);
        CreateWindowW(L"STATIC",
                      L"If you already know the console IP, skip the scan and connect directly.",
                      WS_CHILD | WS_VISIBLE,
                      140, 208, 540, 18, page_console_, nullptr, g_hInst, nullptr);

        connect_button_ = CreateWindowW(L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        630, 246, 120, 30, page_console_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdConnectButton)), g_hInst, nullptr);

        CreateWindowW(L"STATIC",
                      L"Console connection and recording-readiness status stay pinned in the header above, visible from every tab.",
                      WS_CHILD | WS_VISIBLE,
                      20, 248, 560, 24, page_console_, nullptr, g_hInst, nullptr);
    }

    void CreateReaperPage() {
        CreateWindowW(L"STATIC",
                      L"Prepare REAPER for live recording and virtual soundcheck here: choose USB or CARD routing, stage and apply the source layout, and switch prepared channels between live inputs and playback.",
                      WS_CHILD | WS_VISIBLE,
                      20, 18, 760, 40, page_reaper_, nullptr, g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Recording and Soundcheck", WS_CHILD | WS_VISIBLE,
                      20, 66, 240, 20, page_reaper_, nullptr, g_hInst, nullptr);
        tab_status_reaper_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                           640, 66, 120, 20, page_reaper_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperStatusChip)), g_hInst, nullptr);

        CreateWindowW(L"STATIC", L"Recording I/O Mode:", WS_CHILD | WS_VISIBLE,
                      20, 108, 130, 20, page_reaper_, nullptr, g_hInst, nullptr);
        output_usb_radio_ = CreateWindowW(L"BUTTON", L"USB", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                          200, 106, 80, 22, page_reaper_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperOutputUsb)), g_hInst, nullptr);
        output_card_radio_ = CreateWindowW(L"BUTTON", L"CARD", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           290, 106, 80, 22, page_reaper_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperOutputCard)), g_hInst, nullptr);
        CreateWindowW(L"STATIC",
                      L"Choose where the Wing sends the recording channels. USB is the usual direct-to-computer path; CARD uses the Wing audio card route.",
                      WS_CHILD | WS_VISIBLE,
                      200, 134, 520, 30, page_reaper_, nullptr, g_hInst, nullptr);

        pending_summary_ = CreateWindowW(L"STATIC",
                                         L"No pending setup changes. Choose sources for a new setup, or change recording mode to stage a rebuild of the current managed setup.",
                                         WS_CHILD | WS_VISIBLE,
                                         200, 184, 520, 54, page_reaper_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPendingSummary)), g_hInst, nullptr);
        readiness_detail_ = CreateWindowW(L"STATIC",
                                          L"Connect to a Wing to validate the current managed setup, then rebuild it only when routing or recording mode needs to change.",
                                          WS_CHILD | WS_VISIBLE,
                                          200, 250, 520, 70, page_reaper_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReadinessDetail)), g_hInst, nullptr);

        choose_sources_button_ = CreateWindowW(L"BUTTON", L"Choose Sources...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               200, 336, 160, 32, page_reaper_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdChooseSourcesButton)), g_hInst, nullptr);
        apply_setup_button_ = CreateWindowW(L"BUTTON", L"Apply Setup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            380, 336, 180, 32, page_reaper_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplySetupButton)), g_hInst, nullptr);
        discard_setup_button_ = CreateWindowW(L"BUTTON", L"Discard", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              570, 336, 120, 32, page_reaper_,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiscardSetupButton)), g_hInst, nullptr);
        toggle_soundcheck_button_ = CreateWindowW(L"BUTTON", L"Live Mode", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  200, 388, 220, 32, page_reaper_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdToggleSoundcheckButton)), g_hInst, nullptr);
        CreateWindowW(L"STATIC",
                      L"After setup is validated, this flips prepared channels between live inputs and REAPER playback. One button, less panic.",
                      WS_CHILD | WS_VISIBLE,
                      200, 428, 520, 30, page_reaper_, nullptr, g_hInst, nullptr);
    }

    void CreateWingPlaceholderPage() {
        CreateWindowW(L"STATIC",
                      L"Recorder coordination will live here on Windows, matching the macOS Wing tab layout. Phase 1 reserves the surface so the main UI now has the same frame and tab order.",
                      WS_CHILD | WS_VISIBLE,
                      24, 40, 740, 40, page_wing_, nullptr, g_hInst, nullptr);
        tab_status_wing_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                         640, 18, 120, 20, page_wing_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWingStatusChip)), g_hInst, nullptr);
    }

    void CreateControlPlaceholderPage() {
        CreateWindowW(L"STATIC",
                      L"Control Integration will move here on Windows to match the macOS tab for MIDI actions, layers, and debug activity. Phase 1 keeps the layout ready without shipping a half-ported surface.",
                      WS_CHILD | WS_VISIBLE,
                      24, 40, 740, 40, page_control_, nullptr, g_hInst, nullptr);
        tab_status_control_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                            640, 18, 120, 20, page_control_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdControlStatusChip)), g_hInst, nullptr);
    }

    void ShowActivePage(int tab_index) {
        ShowWindow(page_console_, tab_index == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_reaper_, tab_index == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_wing_, tab_index == 2 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_control_, tab_index == 3 ? SW_SHOW : SW_HIDE);
    }

    LRESULT OnNotify(NMHDR* hdr) {
        if (hdr && hdr->hwndFrom == tab_ && hdr->code == TCN_SELCHANGE) {
            ShowActivePage(TabCtrl_GetCurSel(tab_));
        }
        return 0;
    }

    LRESULT OnTimer(UINT_PTR timer_id) {
        if (timer_id == kRefreshTimerId) {
            RefreshAll();
        }
        return 0;
    }

    LRESULT OnCommand(WORD id, WORD notify_code) {
        switch (id) {
            case kIdScanButton:
                RunScan();
                return 0;
            case kIdWingCombo:
                if (notify_code == CBN_SELCHANGE) {
                    OnWingSelectionChanged();
                }
                return 0;
            case kIdManualIpEdit:
                if (notify_code == EN_CHANGE) {
                    OnManualIpChanged();
                }
                return 0;
            case kIdConnectButton:
                OnConnectClicked();
                return 0;
            case kIdReaperOutputUsb:
            case kIdReaperOutputCard:
                OnOutputModeChanged();
                return 0;
            case kIdChooseSourcesButton:
                OnChooseSources();
                return 0;
            case kIdApplySetupButton:
                OnApplySetup();
                return 0;
            case kIdDiscardSetupButton:
                OnDiscardSetup();
                return 0;
            case kIdToggleSoundcheckButton:
                OnToggleSoundcheck();
                return 0;
            default:
                return 0;
        }
    }

    LRESULT OnCtlColor(HDC hdc, HWND control) {
        SetBkMode(hdc, TRANSPARENT);
        const int id = GetDlgCtrlID(control);
        COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT);
        if (id == kIdHeaderConsoleStatus) {
            text_color = current_snapshot_.console.color;
        } else if (id == kIdHeaderValidationStatus) {
            text_color = current_snapshot_.validation.color;
        } else if (id == kIdHeaderRecorderStatus) {
            text_color = current_snapshot_.recorder.color;
        } else if (id == kIdHeaderMidiStatus) {
            text_color = current_snapshot_.midi.color;
        } else if (id == kIdConsoleStatusChip) {
            text_color = current_snapshot_.console_tab.color;
        } else if (id == kIdReaperStatusChip) {
            text_color = current_snapshot_.reaper_tab.color;
        } else if (id == kIdWingStatusChip) {
            text_color = current_snapshot_.wing_tab.color;
        } else if (id == kIdControlStatusChip) {
            text_color = current_snapshot_.control_tab.color;
        } else if (id == kIdPendingSummary) {
            text_color = current_snapshot_.pending_color;
        } else if (id == kIdReadinessDetail) {
            text_color = current_snapshot_.readiness_color;
        } else if (id == kIdFooterStatus) {
            text_color = RGB(90, 90, 90);
        }
        SetTextColor(hdc, text_color);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    void RefreshAll() {
        if (!hwnd_) {
            return;
        }
        auto snapshot = BuildSnapshot();
        current_snapshot_ = snapshot;
        SetWindowTextW(header_console_status_, snapshot.console.text.c_str());
        SetWindowTextW(header_validation_status_, snapshot.validation.text.c_str());
        SetWindowTextW(header_recorder_status_, snapshot.recorder.text.c_str());
        SetWindowTextW(header_midi_status_, snapshot.midi.text.c_str());
        SetWindowTextW(tab_status_console_, snapshot.console_tab.text.c_str());
        SetWindowTextW(tab_status_reaper_, snapshot.reaper_tab.text.c_str());
        SetWindowTextW(tab_status_wing_, snapshot.wing_tab.text.c_str());
        SetWindowTextW(tab_status_control_, snapshot.control_tab.text.c_str());
        SetWindowTextW(pending_summary_, snapshot.pending_summary.c_str());
        SetWindowTextW(readiness_detail_, snapshot.readiness_detail.c_str());
        SetWindowTextW(footer_status_, snapshot.footer.c_str());
        SetWindowTextW(apply_setup_button_, snapshot.apply_label.c_str());
        SetWindowTextW(toggle_soundcheck_button_, snapshot.toggle_label.c_str());
        EnableWindow(apply_setup_button_, snapshot.can_apply ? TRUE : FALSE);
        EnableWindow(discard_setup_button_, snapshot.can_discard ? TRUE : FALSE);
        EnableWindow(toggle_soundcheck_button_, snapshot.can_toggle ? TRUE : FALSE);
        SetWindowTextW(connect_button_,
                       ReaperExtension::Instance().IsConnected() ? L"Disconnect" : L"Connect");
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    HeaderStatusVisual MakeStatus(std::wstring text, COLORREF color) const {
        HeaderStatusVisual visual;
        visual.text = std::move(text);
        visual.color = color;
        return visual;
    }

    std::wstring CurrentOutputMode() const {
        return (IsDlgButtonChecked(page_reaper_, kIdReaperOutputCard) == BST_CHECKED) ? L"CARD" : L"USB";
    }

    void SelectOutputMode(const std::string& mode) {
        const bool card = mode == "CARD";
        CheckRadioButton(page_reaper_, kIdReaperOutputUsb, kIdReaperOutputCard,
                         card ? kIdReaperOutputCard : kIdReaperOutputUsb);
    }

    std::wstring SelectedOrManualIp() const {
        const int selection = static_cast<int>(SendMessageW(wing_combo_, CB_GETCURSEL, 0, 0));
        if (selection >= 0 && selection < static_cast<int>(discovered_wings_.size())) {
            return ToWide(discovered_wings_[static_cast<size_t>(selection)].console_ip);
        }
        const std::wstring typed = ReadWindowText(manual_ip_edit_);
        return typed;
    }

    bool EnsureConnected(const wchar_t* context) {
        auto& extension = ReaperExtension::Instance();
        if (extension.IsConnected()) {
            return true;
        }
        std::wstring ip = SelectedOrManualIp();
        if (ip.empty()) {
            wchar_t message[256];
            std::swprintf(message, sizeof(message) / sizeof(wchar_t),
                          L"No WING IP is selected. Scan or enter a manual IP before %ls.", context);
            ShowMessageBox(ToUtf8(message).c_str(), "WINGuard", 0);
            return false;
        }
        extension.GetConfig().wing_ip = ToUtf8(ip);
        SaveConfigIfPossible(extension);
        if (!extension.ConnectToWing()) {
            std::string detail = extension.GetLastConnectionFailureDetail();
            std::string message = "WINGuard could not connect to the configured WING.";
            if (!detail.empty()) {
                message += "\n\nFailure detail:\n" + detail;
            }
            ShowMessageBox(message.c_str(), "WINGuard", 0);
            return false;
        }
        footer_message_ = L"Connected to WING.";
        return true;
    }

    StatusSnapshot BuildSnapshot() {
        StatusSnapshot snapshot;
        auto& extension = ReaperExtension::Instance();
        auto& config = extension.GetConfig();
        const bool connected = extension.IsConnected();
        const std::wstring applied_output = ToWide(config.soundcheck_output_mode);
        const std::wstring staged_output = pending_output_mode_.empty() ? applied_output : pending_output_mode_;

        latest_validation_details_.clear();
        latest_validation_state_ = ValidationState::NotReady;
        if (connected && !has_pending_setup_draft_ && staged_output == applied_output) {
            latest_validation_state_ = extension.ValidateLiveRecordingSetup(latest_validation_details_);
        }

        snapshot.console = connected
            ? MakeStatus(L"Console: Connected", RGB(40, 140, 70))
            : MakeStatus(L"Console: Not Connected", RGB(110, 110, 110));

        if (has_pending_setup_draft_ || staged_output != applied_output) {
            snapshot.validation = MakeStatus(L"Reaper Recorder: Pending Apply", RGB(215, 135, 30));
        } else if (latest_validation_state_ == ValidationState::Ready) {
            if (config.auto_record_enabled) {
                snapshot.validation = MakeStatus(
                    config.auto_record_warning_only ? L"Reaper Recorder: Enabled + Warning Trigger"
                                                    : L"Reaper Recorder: Enabled + Record",
                    config.auto_record_warning_only ? RGB(215, 135, 30) : RGB(40, 140, 70));
            } else {
                snapshot.validation = MakeStatus(L"Reaper Recorder: Enabled", RGB(215, 135, 30));
            }
        } else if (connected) {
            snapshot.validation = MakeStatus(L"Reaper Recorder: Review / Rebuild", RGB(215, 135, 30));
        } else {
            snapshot.validation = MakeStatus(L"Reaper Recorder: Not Ready", RGB(110, 110, 110));
        }

        if (config.recorder_coordination_enabled && config.sd_auto_record_with_reaper && config.auto_record_enabled) {
            snapshot.recorder = MakeStatus(L"Wing Recorder: Enabled + Autostart", RGB(40, 140, 70));
        } else if (config.recorder_coordination_enabled) {
            snapshot.recorder = MakeStatus(L"Wing Recorder: Enabled", RGB(215, 135, 30));
        } else {
            snapshot.recorder = MakeStatus(L"Wing Recorder: Disabled", RGB(110, 110, 110));
        }

        if (extension.IsMidiActionsEnabled()) {
            snapshot.midi = MakeStatus(L"Wing control integration: Enabled", RGB(40, 140, 70));
        } else {
            snapshot.midi = MakeStatus(L"Wing control integration: Disabled", RGB(110, 110, 110));
        }

        snapshot.console_tab = connected
            ? MakeStatus(L"Connected", RGB(40, 140, 70))
            : MakeStatus(L"Inactive", RGB(110, 110, 110));

        if (has_pending_setup_draft_ || staged_output != applied_output) {
            snapshot.reaper_tab = MakeStatus(L"Pending", RGB(215, 135, 30));
        } else if (latest_validation_state_ == ValidationState::Ready) {
            snapshot.reaper_tab = MakeStatus(L"Ready", RGB(40, 140, 70));
        } else if (connected) {
            snapshot.reaper_tab = MakeStatus(L"Attention", RGB(215, 135, 30));
        } else {
            snapshot.reaper_tab = MakeStatus(L"Inactive", RGB(110, 110, 110));
        }

        if (config.recorder_coordination_enabled && config.sd_auto_record_with_reaper && config.auto_record_enabled) {
            snapshot.wing_tab = MakeStatus(L"Ready", RGB(40, 140, 70));
        } else if (config.recorder_coordination_enabled) {
            snapshot.wing_tab = MakeStatus(L"Enabled", RGB(215, 135, 30));
        } else {
            snapshot.wing_tab = MakeStatus(L"Inactive", RGB(110, 110, 110));
        }

        snapshot.control_tab = extension.IsMidiActionsEnabled()
            ? MakeStatus(L"Ready", RGB(40, 140, 70))
            : MakeStatus(L"Inactive", RGB(110, 110, 110));

        if (has_pending_setup_draft_ || staged_output != applied_output) {
            const size_t selected_count = SelectedPendingCount();
            if (!has_pending_setup_draft_ && staged_output != applied_output) {
                snapshot.pending_summary =
                    std::wstring(L"Recording I/O mode change staged. Click Rebuild Current Setup to reuse the current managed selection in ") +
                    staged_output + L" mode.";
            } else if (selected_count == 0) {
                snapshot.pending_summary =
                    std::wstring(L"Current managed setup staged for rebuild in ") + staged_output +
                    L" mode. Click Rebuild Current Setup to reuse the saved selection and rewrite routing.";
            } else {
                wchar_t buffer[256];
                std::swprintf(buffer, sizeof(buffer) / sizeof(wchar_t),
                              L"Changes staged for %zu sources in %ls mode. Review if needed, then click Apply Setup.",
                              selected_count,
                              staged_output.c_str());
                snapshot.pending_summary = buffer;
            }
            snapshot.pending_color = RGB(215, 135, 30);
        } else {
            snapshot.pending_summary =
                L"No pending setup changes. Choose sources for a new setup, or change recording mode to stage a rebuild of the current managed setup.";
            snapshot.pending_color = RGB(110, 110, 110);
        }

        if (has_pending_setup_draft_ || staged_output != applied_output) {
            snapshot.readiness_detail =
                L"Pending setup changes are staged. Applying them will update WING routing, REAPER tracks, and playback inputs for the selected sources.\r\n"
                L"Next step: review the staged draft, then click Apply Setup.";
            snapshot.readiness_color = RGB(215, 135, 30);
        } else if (!latest_validation_details_.empty()) {
            snapshot.readiness_detail = ToWide(latest_validation_details_);
            if (latest_validation_state_ == ValidationState::Ready) {
                snapshot.readiness_detail += L"\r\nSetup is ready. Use Live/Soundcheck to switch the validated setup now, or change recording mode to stage a rebuild.";
                snapshot.readiness_color = RGB(40, 140, 70);
            } else {
                snapshot.readiness_detail += L"\r\nNext step: review the validation warning. Rebuild the current managed setup if routing changed.";
                snapshot.readiness_color = RGB(215, 135, 30);
            }
        } else {
            snapshot.readiness_detail =
                L"Connect to a Wing to validate the current managed setup, then rebuild it only when routing or recording mode needs to change.\r\n"
                L"Next step: connect to a WING, validate the managed setup, then use Choose Sources only when you need a different selection.";
            snapshot.readiness_color = RGB(110, 110, 110);
        }

        const bool can_apply = (has_pending_setup_draft_ && SelectedPendingCount() > 0) ||
                               (!has_pending_setup_draft_ && staged_output != applied_output);
        snapshot.can_apply = can_apply;
        snapshot.can_discard = has_pending_setup_draft_ || staged_output != applied_output;
        snapshot.can_toggle = connected &&
                              !has_pending_setup_draft_ &&
                              staged_output == applied_output &&
                              latest_validation_state_ == ValidationState::Ready;
        snapshot.apply_label = (!has_pending_setup_draft_ && staged_output != applied_output)
            ? L"Rebuild Current Setup"
            : L"Apply Setup";
        snapshot.toggle_label = extension.IsSoundcheckModeEnabled() ? L"Soundcheck Mode" : L"Live Mode";
        snapshot.footer = footer_message_.empty()
            ? L"Windows Phase 1 mirrors the macOS shell: connection at the top, setup staging in Reaper, later tabs reserved for remaining parity work."
            : footer_message_;

        return snapshot;
    }

    size_t SelectedPendingCount() const {
        size_t count = 0;
        for (const auto& channel : pending_setup_channels_) {
            if (channel.selected) {
                ++count;
            }
        }
        return count;
    }

    void RefreshDiscoveryControls(bool keep_selection) {
        std::wstring previous_ip;
        if (keep_selection) {
            previous_ip = SelectedOrManualIp();
        }
        SendMessageW(wing_combo_, CB_RESETCONTENT, 0, 0);
        for (const auto& wing : discovered_wings_) {
            std::string title = wing.name.empty() ? wing.console_ip : wing.name + " (" + wing.console_ip + ")";
            const std::wstring wide = ToWide(title);
            SendMessageW(wing_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        }
        if (!discovered_wings_.empty()) {
            int selection_index = 0;
            if (!previous_ip.empty()) {
                const std::string prev_utf8 = ToUtf8(previous_ip);
                for (size_t i = 0; i < discovered_wings_.size(); ++i) {
                    if (discovered_wings_[i].console_ip == prev_utf8) {
                        selection_index = static_cast<int>(i);
                        break;
                    }
                }
            }
            SendMessageW(wing_combo_, CB_SETCURSEL, selection_index, 0);
            SetWindowTextW(manual_ip_edit_, ToWide(discovered_wings_[static_cast<size_t>(selection_index)].console_ip).c_str());
        }
    }

    void RunScan() {
        auto& extension = ReaperExtension::Instance();
        discovered_wings_ = extension.DiscoverWings(1500);
        RefreshDiscoveryControls(true);
        if (discovered_wings_.empty()) {
            footer_message_ = L"Scan finished. No WING consoles were discovered on the network.";
            ShowMessageBox("No WING consoles were discovered. Enter a manual IP if the console is on a reachable network path.",
                           "WINGuard",
                           0);
        } else {
            wchar_t buffer[160];
            std::swprintf(buffer, sizeof(buffer) / sizeof(wchar_t),
                          L"Scan finished. Found %zu WING console(s).", discovered_wings_.size());
            footer_message_ = buffer;
            if (discovered_wings_.size() == 1 && !extension.IsConnected()) {
                OnConnectClicked();
                return;
            }
        }
        RefreshAll();
    }

    void OnWingSelectionChanged() {
        const int index = static_cast<int>(SendMessageW(wing_combo_, CB_GETCURSEL, 0, 0));
        if (index >= 0 && index < static_cast<int>(discovered_wings_.size())) {
            const auto& wing = discovered_wings_[static_cast<size_t>(index)];
            SetWindowTextW(manual_ip_edit_, ToWide(wing.console_ip).c_str());
            auto& extension = ReaperExtension::Instance();
            extension.GetConfig().wing_ip = wing.console_ip;
            SaveConfigIfPossible(extension);
            footer_message_ = L"Selected discovered WING target.";
            RefreshAll();
        }
    }

    void OnManualIpChanged() {
        auto& extension = ReaperExtension::Instance();
        extension.GetConfig().wing_ip = ToUtf8(ReadWindowText(manual_ip_edit_));
        SaveConfigIfPossible(extension);
        footer_message_ = L"Manual WING IP updated.";
        RefreshAll();
    }

    void OnConnectClicked() {
        auto& extension = ReaperExtension::Instance();
        if (extension.IsConnected()) {
            extension.DisconnectFromWing();
            footer_message_ = L"Disconnected from WING.";
            RefreshAll();
            return;
        }
        if (EnsureConnected(L"connecting")) {
            RefreshAll();
        }
    }

    void OnOutputModeChanged() {
        const std::wstring current_mode = CurrentOutputMode();
        const std::wstring applied_mode = ToWide(ReaperExtension::Instance().GetConfig().soundcheck_output_mode);
        pending_output_mode_ = current_mode;
        if (!has_pending_setup_draft_ && pending_output_mode_ == applied_mode) {
            footer_message_ = L"Recording mode matches the applied setup.";
        } else if (!has_pending_setup_draft_) {
            footer_message_ = L"Recording mode change staged. Rebuild Current Setup will reuse the managed selection.";
        } else {
            footer_message_ = L"Recording mode updated for the staged setup draft.";
        }
        RefreshAll();
    }

    void ApplyPendingSelectionOverlay(std::vector<SourceSelectionInfo>& channels) {
        if (!has_pending_setup_draft_ || pending_setup_channels_.empty()) {
            return;
        }
        std::set<std::pair<int, int>> selected;
        for (const auto& channel : pending_setup_channels_) {
            if (channel.selected) {
                selected.insert({static_cast<int>(channel.kind), channel.source_number});
            }
        }
        for (auto& channel : channels) {
            channel.selected = selected.count({static_cast<int>(channel.kind), channel.source_number}) > 0;
        }
    }

    void OnChooseSources() {
        if (!EnsureConnected(L"loading sources")) {
            RefreshAll();
            return;
        }
        auto channels = ReaperExtension::Instance().GetAvailableSources();
        if (channels.empty()) {
            ShowMessageBox("Connected, but no selectable sources were discovered.", "WINGuard", 0);
            footer_message_ = L"No selectable sources were discovered.";
            RefreshAll();
            return;
        }
        ApplyPendingSelectionOverlay(channels);
        SourcePickerDialog picker(hwnd_,
                                  std::move(channels),
                                  pending_setup_soundcheck_,
                                  pending_replace_existing_);
        SourcePickerResult result = picker.Run();
        if (!result.confirmed) {
            footer_message_ = L"Source review cancelled.";
            RefreshAll();
            return;
        }
        size_t selected_count = 0;
        for (const auto& channel : result.channels) {
            if (channel.selected) {
                ++selected_count;
            }
        }
        if (selected_count == 0) {
            ShowMessageBox("No sources were selected for the next apply.", "WINGuard", 0);
            footer_message_ = L"No sources were staged.";
            RefreshAll();
            return;
        }
        has_pending_setup_draft_ = true;
        pending_setup_channels_ = std::move(result.channels);
        pending_setup_soundcheck_ = result.setup_soundcheck;
        pending_replace_existing_ = result.replace_existing;
        pending_output_mode_ = CurrentOutputMode();
        footer_message_ = L"Live setup draft staged. Review the summary and click Apply Setup when ready.";
        RefreshAll();
    }

    void OnApplySetup() {
        auto& extension = ReaperExtension::Instance();
        if (!EnsureConnected(L"applying setup")) {
            RefreshAll();
            return;
        }
        std::vector<SourceSelectionInfo> channels_to_apply;
        if (has_pending_setup_draft_) {
            channels_to_apply = pending_setup_channels_;
        } else {
            channels_to_apply = extension.GetAvailableSources();
        }
        size_t selected_count = 0;
        for (const auto& channel : channels_to_apply) {
            if (channel.selected) {
                ++selected_count;
            }
        }
        if (selected_count == 0) {
            ShowMessageBox("No sources are staged for apply.", "WINGuard", 0);
            footer_message_ = L"No staged sources are available to apply.";
            RefreshAll();
            return;
        }
        extension.PauseAutoRecordForSetup();
        extension.GetConfig().soundcheck_output_mode = ToUtf8(pending_output_mode_);
        if (extension.SetupSoundcheckFromSelection(channels_to_apply, pending_setup_soundcheck_, pending_replace_existing_)) {
            has_pending_setup_draft_ = false;
            pending_setup_channels_.clear();
            pending_output_mode_ = ToWide(extension.GetConfig().soundcheck_output_mode);
            SaveConfigIfPossible(extension);
            footer_message_ = L"Live recording setup applied.";
        } else {
            footer_message_ = L"Setup apply returned without success confirmation.";
        }
        RefreshAll();
    }

    void OnDiscardSetup() {
        has_pending_setup_draft_ = false;
        pending_setup_channels_.clear();
        pending_setup_soundcheck_ = true;
        pending_replace_existing_ = true;
        pending_output_mode_ = ToWide(ReaperExtension::Instance().GetConfig().soundcheck_output_mode);
        SelectOutputMode(ToUtf8(pending_output_mode_));
        footer_message_ = L"Staged setup changes discarded.";
        RefreshAll();
    }

    void OnToggleSoundcheck() {
        auto& extension = ReaperExtension::Instance();
        if (!EnsureConnected(L"toggling soundcheck mode")) {
            RefreshAll();
            return;
        }
        extension.ToggleSoundcheckMode();
        footer_message_ = extension.IsSoundcheckModeEnabled()
            ? L"Soundcheck mode enabled."
            : L"Live mode restored.";
        RefreshAll();
    }

    HWND hwnd_ = nullptr;
    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND tab_ = nullptr;
    HWND header_console_status_ = nullptr;
    HWND header_validation_status_ = nullptr;
    HWND header_recorder_status_ = nullptr;
    HWND header_midi_status_ = nullptr;
    HWND page_console_ = nullptr;
    HWND page_reaper_ = nullptr;
    HWND page_wing_ = nullptr;
    HWND page_control_ = nullptr;
    HWND tab_status_console_ = nullptr;
    HWND tab_status_reaper_ = nullptr;
    HWND tab_status_wing_ = nullptr;
    HWND tab_status_control_ = nullptr;
    HWND wing_combo_ = nullptr;
    HWND scan_button_ = nullptr;
    HWND manual_ip_edit_ = nullptr;
    HWND connect_button_ = nullptr;
    HWND output_usb_radio_ = nullptr;
    HWND output_card_radio_ = nullptr;
    HWND pending_summary_ = nullptr;
    HWND readiness_detail_ = nullptr;
    HWND choose_sources_button_ = nullptr;
    HWND apply_setup_button_ = nullptr;
    HWND discard_setup_button_ = nullptr;
    HWND toggle_soundcheck_button_ = nullptr;
    HWND footer_status_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_font_ = nullptr;
    HFONT small_bold_font_ = nullptr;
    std::vector<WingInfo> discovered_wings_;
    std::vector<SourceSelectionInfo> pending_setup_channels_;
    std::wstring pending_output_mode_;
    bool has_pending_setup_draft_ = false;
    bool pending_setup_soundcheck_ = true;
    bool pending_replace_existing_ = true;
    ValidationState latest_validation_state_ = ValidationState::NotReady;
    std::string latest_validation_details_;
    std::wstring footer_message_;
    StatusSnapshot current_snapshot_;
};

WingConnectorWindowsDialog* WingConnectorWindowsDialog::instance_ = nullptr;

}  // namespace

extern "C" void ShowWingConnectorDialogWindows() {
    WingConnectorWindowsDialog::Show();
}

#endif
