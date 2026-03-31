/*
 * Windows Native AUDIOLAB.wing.reaper.virtualsoundcheck Dialog Implementation
 */

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "internal/wing_connector_dialog_windows.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>
#include <mutex>
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
constexpr wchar_t kLogoClassName[] = L"WINGuardWindowsLogo";
constexpr int kMinWindowWidth = 1180;
constexpr int kMinWindowHeight = 860;
constexpr UINT_PTR kRefreshTimerId = 101;
constexpr UINT kRefreshTimerMs = 500;
constexpr int kScrollLineStep = 36;

enum ControlId {
    kIdTab = 100,
    kIdBannerGroup,
    kIdLogo,
    kIdTitle,
    kIdSubtitle,
    kIdStatusGroup,
    kIdHeaderConsoleIcon,
    kIdHeaderConsoleStatus,
    kIdHeaderValidationIcon,
    kIdHeaderValidationStatus,
    kIdHeaderRecorderIcon,
    kIdHeaderRecorderStatus,
    kIdHeaderMidiIcon,
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
    kIdPageFrame,
    kIdTabConsoleButton,
    kIdTabReaperButton,
    kIdTabWingButton,
    kIdTabControlButton,
    kIdAutoTriggerHeader,
    kIdAutoTriggerDetail,
    kIdAutoTriggerHint,
    kIdAutoTriggerEnableOff,
    kIdAutoTriggerEnableOn,
    kIdAutoTriggerModeWarning,
    kIdAutoTriggerModeRecord,
    kIdAutoTriggerThresholdEdit,
    kIdAutoTriggerHoldEdit,
    kIdAutoTriggerMonitorTrackCombo,
    kIdAutoTriggerMeterLabel,
    kIdApplyAutoTriggerButton,
    kIdDiscardAutoTriggerButton,
    kIdConsoleSectionIcon,
    kIdReaperSectionIcon,
    kIdAutoTriggerSectionIcon,
    kIdWingSectionIcon,
    kIdControlSectionIcon,
    kIdRecorderEnableOff,
    kIdRecorderEnableOn,
    kIdRecorderTargetWLive,
    kIdRecorderTargetUsb,
    kIdRecorderPair1,
    kIdRecorderPair3,
    kIdRecorderPair5,
    kIdRecorderPair7,
    kIdRecorderFollowOff,
    kIdRecorderFollowOn,
    kIdRecorderDetail,
    kIdApplyRecorderButton,
    kIdDiscardRecorderButton,
    kIdMidiActionsOff,
    kIdMidiActionsOn,
    kIdMidiSummary,
    kIdMidiDetail,
    kIdWarningLayerCombo,
    kIdApplyMidiButton,
    kIdDiscardMidiButton,
    kIdOpenDebugLogButton,
    kIdClearDebugLogButton,
    kIdDebugLogView,
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

ULONG_PTR EnsureGdiplusToken() {
    static ULONG_PTR token = 0;
    static bool started = false;
    if (started) {
        return token;
    }
    Gdiplus::GdiplusStartupInput startup_input;
    if (Gdiplus::GdiplusStartup(&token, &startup_input, nullptr) == Gdiplus::Ok) {
        started = true;
    }
    return token;
}

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(g_hInst), path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }
    std::wstring full_path(path, path + length);
    const size_t slash = full_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return full_path.substr(0, slash);
}

std::wstring ResolveLogoPath() {
    std::array<std::wstring, 4> candidates = {
        ModuleDirectory() + L"\\wingguard-logo.png",
        ModuleDirectory() + L"\\assets\\wingguard-logo.png",
        L"assets\\wingguard-logo.png",
        L"wingguard-logo.png"
    };
    for (const auto& path : candidates) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return path;
        }
    }
    return std::wstring();
}

RECT GetPreferredWindowRect() {
    RECT work_area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        work_area = RECT{0, 0, 1920, 1080};
    }
    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const int width = std::max(kMinWindowWidth, (work_width * 2) / 3);
    const int height = std::max(kMinWindowHeight, (work_height * 2) / 3);
    const int x = work_area.left + std::max(0, (work_width - width) / 2);
    const int y = work_area.top + std::max(0, (work_height - height) / 2);
    return RECT{x, y, x + width, y + height};
}

HFONT CreateUiFont(int height, int weight = FW_NORMAL, bool monospace = false) {
    return CreateFontW(height,
                       0,
                       0,
                       0,
                       weight,
                       FALSE,
                       FALSE,
                       FALSE,
                       DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | (monospace ? FF_MODERN : FF_DONTCARE),
                       monospace ? L"Consolas" : L"Segoe UI");
}

HFONT CreateSymbolFont(int height, int weight = FW_SEMIBOLD) {
    return CreateFontW(height,
                       0,
                       0,
                       0,
                       weight,
                       FALSE,
                       FALSE,
                       FALSE,
                       DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE,
                       L"Segoe UI Symbol");
}

struct HeaderStatusVisual {
    COLORREF color = RGB(110, 110, 110);
    std::wstring text;
};

void SaveConfigIfPossible(ReaperExtension& extension) {
    const std::string path = WingConfig::GetConfigPath();
    if (!extension.GetConfig().SaveToFile(path)) {
        Logger::Error("Failed to save WINGuard config to %s", path.c_str());
    }
}

int MeasureTextWidth(HWND control, const std::wstring& text) {
    if (!control) {
        return 0;
    }
    HDC hdc = GetDC(control);
    if (!hdc) {
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    RECT rect{0, 0, 0, 0};
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_SINGLELINE);
    if (old_font) {
        SelectObject(hdc, old_font);
    }
    ReleaseDC(control, hdc);
    return rect.right - rect.left;
}

int ButtonWidthForLabel(HWND control, int min_width, int padding = 34) {
    return std::max(min_width, MeasureTextWidth(control, ReadWindowText(control)) + padding);
}

int MultiLineTextHeight(HWND control, int width, const std::wstring& text) {
    if (!control) {
        return 0;
    }
    HDC hdc = GetDC(control);
    if (!hdc) {
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    RECT rect{0, 0, width, 0};
    DrawTextW(hdc,
              text.c_str(),
              static_cast<int>(text.size()),
              &rect,
              DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
    if (old_font) {
        SelectObject(hdc, old_font);
    }
    ReleaseDC(control, hdc);
    return rect.bottom - rect.top;
}

std::wstring CleanLogMessage(const std::string& message) {
    std::wstring cleaned = ToWide(message);
    const std::array<std::wstring, 4> prefixes = {
        L"AUDIOLAB.wing.reaper.virtualsoundcheck: ",
        L"AUDIOLAB.wing.reaper.virtualsoundcheck:",
        L"WINGuard: ",
        L"WINGuard:"
    };
    for (const auto& prefix : prefixes) {
        size_t pos = std::wstring::npos;
        while ((pos = cleaned.find(prefix)) != std::wstring::npos) {
            cleaned.erase(pos, prefix.size());
        }
    }
    return cleaned;
}

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
            CW_USEDEFAULT, CW_USEDEFAULT, 920, 760,
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
            case WM_CTLCOLORSTATIC: return self->OnCtlColor(reinterpret_cast<HDC>(wparam));
            case WM_CTLCOLOREDIT: return self->OnCtlColor(reinterpret_cast<HDC>(wparam));
            case WM_CTLCOLORLISTBOX: return self->OnCtlColor(reinterpret_cast<HDC>(wparam));
            case WM_CLOSE:
                self->done_ = true;
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    LRESULT OnCreate() {
        HFONT font = CreateUiFont(-22);
        HFONT title_font = CreateUiFont(-22, FW_SEMIBOLD);

        CreateWindowW(L"STATIC",
                      L"Choose which channels, buses, or matrices should be included in the next apply. No routing changes happen until you confirm.",
                      WS_CHILD | WS_VISIBLE,
                      24, 18, 860, 54,
                      hwnd_,
                      nullptr,
                      g_hInst,
                      nullptr);

        listbox_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                   L"LISTBOX",
                                   nullptr,
                                   WS_CHILD | WS_VISIBLE | LBS_EXTENDEDSEL | WS_VSCROLL | LBS_NOTIFY,
                                   24, 86, 860, 470,
                                   hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceList)),
                                   g_hInst,
                                   nullptr);

        HWND select_all = CreateWindowW(L"BUTTON", L"Select All", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      24, 576, 148, 38, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceSelectAll)), g_hInst, nullptr);
        HWND channels_only = CreateWindowW(L"BUTTON", L"Channels Only", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      186, 576, 170, 38, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceSelectChannels)), g_hInst, nullptr);
        HWND clear_button = CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      370, 576, 106, 38, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceClear)), g_hInst, nullptr);

        count_label_ = CreateWindowW(L"STATIC", L"0 sources selected", WS_CHILD | WS_VISIBLE,
                                     500, 582, 400, 32, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceCount)), g_hInst, nullptr);

        CreateWindowW(L"BUTTON", L"Soundcheck", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                      24, 628, 150, 28, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceModeSoundcheck)), g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Record", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                      188, 628, 100, 28, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceModeRecord)), g_hInst, nullptr);
        replace_checkbox_ = CreateWindowW(L"BUTTON", L"Replace managed REAPER tracks", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          24, 664, 420, 32, hwnd_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceReplace)), g_hInst, nullptr);

        HWND apply_draft = CreateWindowW(L"BUTTON", L"Apply Draft", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      624, 664, 180, 40, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceOk)), g_hInst, nullptr);
        HWND cancel_button = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      788, 664, 116, 40, hwnd_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSourceCancel)), g_hInst, nullptr);

        SetWindowFontRecursive(hwnd_, font);
        SendMessageW(count_label_, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        SendMessageW(listbox_, LB_SETITEMHEIGHT, 0, 30);
        MoveWindow(select_all, 24, 576, ButtonWidthForLabel(select_all, 148), 38, TRUE);
        MoveWindow(channels_only, 24 + ButtonWidthForLabel(select_all, 148) + 14, 576, ButtonWidthForLabel(channels_only, 170), 38, TRUE);
        MoveWindow(clear_button, 24 + ButtonWidthForLabel(select_all, 148) + ButtonWidthForLabel(channels_only, 170) + 28, 576, ButtonWidthForLabel(clear_button, 106), 38, TRUE);
        MoveWindow(apply_draft, 624, 664, ButtonWidthForLabel(apply_draft, 180), 40, TRUE);
        MoveWindow(cancel_button, 624 + ButtonWidthForLabel(apply_draft, 180) + 14, 664, ButtonWidthForLabel(cancel_button, 132), 40, TRUE);
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

    LRESULT OnCtlColor(HDC hdc) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(30, 30, 30));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
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
        wchar_t buffer[96];
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
    struct PageLayoutState {
        HWND hwnd = nullptr;
        int content_height = 0;
        int scroll_y = 0;
    };

    struct PageContext {
        WingConnectorWindowsDialog* owner = nullptr;
        PageLayoutState* state = nullptr;
    };

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
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wc);

        WNDCLASSW page_wc{};
        page_wc.lpfnWndProc = &WingConnectorWindowsDialog::PageWndProc;
        page_wc.hInstance = g_hInst;
        page_wc.lpszClassName = kPageClassName;
        page_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        page_wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&page_wc);

        WNDCLASSW logo_wc{};
        logo_wc.lpfnWndProc = &WingConnectorWindowsDialog::LogoWndProc;
        logo_wc.hInstance = g_hInst;
        logo_wc.lpszClassName = kLogoClassName;
        logo_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        logo_wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&logo_wc);
        registered = true;
    }

    static LRESULT CALLBACK PageWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* context = reinterpret_cast<PageContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            context = reinterpret_cast<PageContext*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        }
        HWND parent = GetParent(hwnd);
        switch (msg) {
            case WM_VSCROLL:
            case WM_MOUSEWHEEL:
            case WM_SIZE:
                if (context && context->owner && context->state) {
                    return context->owner->HandlePageMessage(context->state, msg, wparam, lparam);
                }
                break;
            case WM_COMMAND:
            case WM_NOTIFY:
            case WM_CTLCOLORSTATIC:
                if (parent) {
                    return SendMessageW(parent, msg, wparam, lparam);
                }
                break;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    static LRESULT CALLBACK LogoWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rect{};
                GetClientRect(hwnd, &rect);

                HBRUSH background = CreateSolidBrush(RGB(232, 234, 238));
                FillRect(hdc, &rect, background);
                DeleteObject(background);

                const std::wstring logo_path = ResolveLogoPath();
                bool image_drawn = false;
                if (!logo_path.empty() && EnsureGdiplusToken() != 0) {
                    Gdiplus::Graphics graphics(hdc);
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    Gdiplus::Image image(logo_path.c_str());
                    if (image.GetLastStatus() == Gdiplus::Ok) {
                        graphics.DrawImage(&image,
                                           0,
                                           0,
                                           rect.right - rect.left,
                                           rect.bottom - rect.top);
                        image_drawn = true;
                    }
                }

                if (!image_drawn) {
                    RECT inner = rect;
                    InflateRect(&inner, -8, -8);
                    HBRUSH accent = CreateSolidBrush(RGB(28, 114, 184));
                    FillRect(hdc, &inner, accent);
                    DeleteObject(accent);

                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    HFONT fallback_font = CreateUiFont(-32, FW_BOLD);
                    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, fallback_font));
                    DrawTextW(hdc, L"WG", -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, old_font);
                    DeleteObject(fallback_font);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }
            default:
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
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
            case WM_ERASEBKGND: return self->OnEraseBackground(reinterpret_cast<HDC>(wparam));
            case WM_SIZE: return self->OnSize(LOWORD(lparam), HIWORD(lparam));
            case WM_GETMINMAXINFO: return self->OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lparam));
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
            const RECT preferred = GetPreferredWindowRect();
            hwnd_ = CreateWindowExW(
                WS_EX_TOOLWINDOW,
                kDialogClassName,
                L"WINGuard",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                preferred.left, preferred.top,
                preferred.right - preferred.left,
                preferred.bottom - preferred.top,
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
        font_ = CreateUiFont(-24);
        bold_font_ = CreateUiFont(-28, FW_SEMIBOLD);
        small_bold_font_ = CreateUiFont(-22, FW_SEMIBOLD);
        section_font_ = CreateUiFont(-24, FW_SEMIBOLD);
        tab_font_ = CreateUiFont(-22, FW_SEMIBOLD);
        subtle_font_ = CreateUiFont(-20);
        mono_font_ = CreateUiFont(-22, FW_NORMAL, true);
        icon_font_ = CreateSymbolFont(-26, FW_SEMIBOLD);
        banner_brush_ = CreateSolidBrush(RGB(232, 234, 238));
        status_panel_brush_ = CreateSolidBrush(RGB(244, 245, 247));
        body_brush_ = CreateSolidBrush(RGB(255, 255, 255));
        border_brush_ = CreateSolidBrush(RGB(210, 214, 220));

        banner_group_ = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
                                      12, 10, 820, 156, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBannerGroup)), g_hInst, nullptr);
        logo_ = CreateWindowW(kLogoClassName, L"", WS_CHILD | WS_VISIBLE,
                              36, 34, 120, 120, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLogo)), g_hInst, nullptr);
        title_ = CreateWindowW(L"STATIC", L"WINGuard", WS_CHILD | WS_VISIBLE,
                               176, 48, 260, 32, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTitle)), g_hInst, nullptr);
        subtitle_ = CreateWindowW(L"STATIC", L"Guard every take. Faster setup, safer record(w)ing!",
                                  WS_CHILD | WS_VISIBLE,
                                  176, 90, 460, 28, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSubtitle)), g_hInst, nullptr);

        status_group_ = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
                                      472, 26, 340, 96, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatusGroup)), g_hInst, nullptr);
        header_console_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                             490, 42, 20, 18, hwnd_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderConsoleIcon)), g_hInst, nullptr);
        header_console_status_ = CreateWindowW(L"STATIC", L"Console: Not Connected", WS_CHILD | WS_VISIBLE,
                                               514, 42, 278, 18, hwnd_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderConsoleStatus)), g_hInst, nullptr);
        header_validation_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                                490, 62, 20, 18, hwnd_,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderValidationIcon)), g_hInst, nullptr);
        header_validation_status_ = CreateWindowW(L"STATIC", L"Reaper Recorder: Not Ready", WS_CHILD | WS_VISIBLE,
                                                  514, 62, 278, 18, hwnd_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderValidationStatus)), g_hInst, nullptr);
        header_recorder_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                              490, 82, 20, 18, hwnd_,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderRecorderIcon)), g_hInst, nullptr);
        header_recorder_status_ = CreateWindowW(L"STATIC", L"Wing Recorder: Disabled", WS_CHILD | WS_VISIBLE,
                                                514, 82, 278, 18, hwnd_,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderRecorderStatus)), g_hInst, nullptr);
        header_midi_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                          490, 102, 20, 18, hwnd_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderMidiIcon)), g_hInst, nullptr);
        header_midi_status_ = CreateWindowW(L"STATIC", L"Wing control integration: Disabled", WS_CHILD | WS_VISIBLE,
                                            514, 102, 278, 18, hwnd_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHeaderMidiStatus)), g_hInst, nullptr);

        tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_FOCUSNEVER,
                               12, 154, 820, 560, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab)), g_hInst, nullptr);
        ShowWindow(tab_, SW_HIDE);
        page_frame_ = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
                                    12, 206, 820, 560, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPageFrame)), g_hInst, nullptr);
        tab_button_console_ = CreateWindowW(L"BUTTON", L"Console", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_GROUP,
                                            12, 154, 180, 40, hwnd_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabConsoleButton)), g_hInst, nullptr);
        tab_button_reaper_ = CreateWindowW(L"BUTTON", L"Reaper", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           196, 154, 180, 40, hwnd_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabReaperButton)), g_hInst, nullptr);
        tab_button_wing_ = CreateWindowW(L"BUTTON", L"Wing", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         380, 154, 180, 40, hwnd_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabWingButton)), g_hInst, nullptr);
        tab_button_control_ = CreateWindowW(L"BUTTON", L"Control Integration", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE,
                                            564, 154, 240, 40, hwnd_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabControlButton)), g_hInst, nullptr);
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
        SendMessageW(subtitle_, WM_SETFONT, reinterpret_cast<WPARAM>(subtle_font_), TRUE);
        SendMessageW(header_console_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(header_validation_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(header_recorder_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(header_midi_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(tab_button_console_, WM_SETFONT, reinterpret_cast<WPARAM>(tab_font_), TRUE);
        SendMessageW(tab_button_reaper_, WM_SETFONT, reinterpret_cast<WPARAM>(tab_font_), TRUE);
        SendMessageW(tab_button_wing_, WM_SETFONT, reinterpret_cast<WPARAM>(tab_font_), TRUE);
        SendMessageW(tab_button_control_, WM_SETFONT, reinterpret_cast<WPARAM>(tab_font_), TRUE);
        SendMessageW(console_section_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(reaper_section_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(auto_trigger_section_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(wing_section_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(control_section_icon_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_), TRUE);
        SendMessageW(console_section_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(reaper_section_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(auto_trigger_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(wing_section_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(control_section_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(support_section_header_, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
        SendMessageW(tab_status_console_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_reaper_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_wing_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(tab_status_control_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(header_console_status_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(header_validation_status_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(header_recorder_status_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(header_midi_status_, WM_SETFONT, reinterpret_cast<WPARAM>(small_bold_font_), TRUE);
        SendMessageW(footer_status_, WM_SETFONT, reinterpret_cast<WPARAM>(subtle_font_), TRUE);
        SendMessageW(wing_placeholder_body_, WM_SETFONT, reinterpret_cast<WPARAM>(subtle_font_), TRUE);
        SendMessageW(control_placeholder_body_, WM_SETFONT, reinterpret_cast<WPARAM>(subtle_font_), TRUE);
        SendMessageW(support_detail_, WM_SETFONT, reinterpret_cast<WPARAM>(subtle_font_), TRUE);
        SendMessageW(debug_log_view_, WM_SETFONT, reinterpret_cast<WPARAM>(mono_font_), TRUE);
        SendMessageW(auto_trigger_meter_label_, WM_SETFONT, reinterpret_cast<WPARAM>(mono_font_), TRUE);
        SetWindowTextW(console_section_icon_, L"\x25CF");
        SetWindowTextW(reaper_section_icon_, L"\x25CF");
        SetWindowTextW(auto_trigger_section_icon_, L"\x25CF");
        SetWindowTextW(wing_section_icon_, L"\x25CF");
        SetWindowTextW(control_section_icon_, L"\x25CF");
        ShowWindow(banner_group_, SW_HIDE);
        ShowWindow(status_group_, SW_HIDE);

        SyncPendingSettingsFromConfig();
        SyncAutoTriggerFromConfig();
        SelectOutputMode(ReaperExtension::Instance().GetConfig().soundcheck_output_mode);
        SyncAutoTriggerControlsFromPending();
        SyncWingControlsFromPending();
        SyncControlTabFromPending();
        ReaperExtension::Instance().SetLogCallback([this](const std::string& message) {
            std::lock_guard<std::mutex> lock(log_buffer_mutex_);
            pending_log_buffer_ += CleanLogMessage(message);
            if (!pending_log_buffer_.empty() && pending_log_buffer_.back() != L'\n') {
                pending_log_buffer_ += L"\r\n";
            }
        });
        pending_output_mode_ = CurrentOutputMode();
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        RECT client_rect{};
        GetClientRect(hwnd_, &client_rect);
        LayoutControls(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
        SelectTab(0);
        RefreshDiscoveryControls(false);
        RefreshAll();
        return 0;
    }

    void CreatePages() {
        const int width = 100;
        const int height = 100;
        page_contexts_[0] = {this, &console_page_state_};
        page_contexts_[1] = {this, &reaper_page_state_};
        page_contexts_[2] = {this, &wing_page_state_};
        page_contexts_[3] = {this, &control_page_state_};
        page_console_ = CreateWindowW(kPageClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                      0, 0, width, height,
                                      hwnd_, nullptr, g_hInst, &page_contexts_[0]);
        page_reaper_ = CreateWindowW(kPageClassName, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
                                     0, 0, width, height,
                                     hwnd_, nullptr, g_hInst, &page_contexts_[1]);
        page_wing_ = CreateWindowW(kPageClassName, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
                                   0, 0, width, height,
                                   hwnd_, nullptr, g_hInst, &page_contexts_[2]);
        page_control_ = CreateWindowW(kPageClassName, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
                                      0, 0, width, height,
                                      hwnd_, nullptr, g_hInst, &page_contexts_[3]);
        console_page_state_.hwnd = page_console_;
        reaper_page_state_.hwnd = page_reaper_;
        wing_page_state_.hwnd = page_wing_;
        control_page_state_.hwnd = page_control_;
    }

    void CreateConsolePage() {
        console_intro_ = CreateWindowW(L"STATIC",
                                       L"Connect to a Wing, discover consoles on the network, or enter a manual IP when discovery comes back empty-handed.",
                                       WS_CHILD | WS_VISIBLE,
                                       20, 18, 740, 36, page_console_, nullptr, g_hInst, nullptr);
        console_section_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                              20, 64, 18, 18, page_console_,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdConsoleSectionIcon)), g_hInst, nullptr);

        console_section_header_ = CreateWindowW(L"STATIC", L"Connection", WS_CHILD | WS_VISIBLE,
                                                46, 64, 200, 20, page_console_, nullptr, g_hInst, nullptr);
        tab_status_console_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                            640, 64, 120, 20, page_console_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdConsoleStatusChip)), g_hInst, nullptr);

        console_label_ = CreateWindowW(L"STATIC", L"Wing Console:", WS_CHILD | WS_VISIBLE,
                                       20, 108, 110, 20, page_console_, nullptr, g_hInst, nullptr);
        wing_combo_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                    140, 104, 470, 240, page_console_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWingCombo)), g_hInst, nullptr);
        scan_button_ = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     630, 104, 120, 28, page_console_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdScanButton)), g_hInst, nullptr);

        console_help_discovery_ = CreateWindowW(L"STATIC", L"Pick a discovered Wing to fill the connection details automatically.",
                                                WS_CHILD | WS_VISIBLE,
                                                140, 136, 540, 18, page_console_, nullptr, g_hInst, nullptr);

        console_manual_ip_label_ = CreateWindowW(L"STATIC", L"Manual IP:", WS_CHILD | WS_VISIBLE,
                                                 20, 182, 110, 20, page_console_, nullptr, g_hInst, nullptr);
        manual_ip_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          140, 178, 260, 24, page_console_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdManualIpEdit)), g_hInst, nullptr);
        console_help_manual_ = CreateWindowW(L"STATIC",
                                             L"If you already know the console IP, skip the scan and connect directly.",
                                             WS_CHILD | WS_VISIBLE,
                                             140, 208, 540, 18, page_console_, nullptr, g_hInst, nullptr);

        connect_button_ = CreateWindowW(L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        630, 246, 120, 30, page_console_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdConnectButton)), g_hInst, nullptr);

        console_footer_ = CreateWindowW(L"STATIC",
                                        L"Console connection and recording-readiness status stay pinned in the header above, visible from every tab.",
                                        WS_CHILD | WS_VISIBLE,
                                        20, 248, 560, 24, page_console_, nullptr, g_hInst, nullptr);
    }

    void CreateReaperPage() {
        reaper_intro_ = CreateWindowW(L"STATIC",
                                      L"Prepare REAPER for live recording and virtual soundcheck here: choose USB or CARD routing, stage and apply the source layout, switch prepared channels between live inputs and playback, and use Auto Trigger when you want signal-driven starts.",
                                      WS_CHILD | WS_VISIBLE,
                                      20, 18, 760, 40, page_reaper_, nullptr, g_hInst, nullptr);
        reaper_section_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                             20, 66, 18, 18, page_reaper_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperSectionIcon)), g_hInst, nullptr);

        reaper_section_header_ = CreateWindowW(L"STATIC", L"Recording and Soundcheck", WS_CHILD | WS_VISIBLE,
                                               46, 66, 280, 20, page_reaper_, nullptr, g_hInst, nullptr);
        tab_status_reaper_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                           640, 66, 120, 20, page_reaper_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperStatusChip)), g_hInst, nullptr);

        reaper_output_label_ = CreateWindowW(L"STATIC", L"Recording I/O Mode:", WS_CHILD | WS_VISIBLE,
                                             20, 108, 130, 20, page_reaper_, nullptr, g_hInst, nullptr);
        output_usb_radio_ = CreateWindowW(L"BUTTON", L"USB", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                          200, 106, 80, 22, page_reaper_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperOutputUsb)), g_hInst, nullptr);
        output_card_radio_ = CreateWindowW(L"BUTTON", L"CARD", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           290, 106, 80, 22, page_reaper_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReaperOutputCard)), g_hInst, nullptr);
        reaper_output_help_ = CreateWindowW(L"STATIC",
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
        reaper_toggle_help_ = CreateWindowW(L"STATIC",
                                            L"After setup is validated, this flips prepared channels between live inputs and REAPER playback. One button, less panic.",
                                            WS_CHILD | WS_VISIBLE,
                                            200, 428, 520, 30, page_reaper_, nullptr, g_hInst, nullptr);

        auto_trigger_section_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                                   20, 500, 18, 18, page_reaper_,
                                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerSectionIcon)), g_hInst, nullptr);
        auto_trigger_header_ = CreateWindowW(L"STATIC", L"Auto Trigger", WS_CHILD | WS_VISIBLE,
                                             46, 500, 240, 24, page_reaper_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerHeader)), g_hInst, nullptr);
        auto_trigger_detail_ = CreateWindowW(L"STATIC",
                                             L"Trigger controls wake up after live setup validates, because they depend on the prepared recording path.",
                                             WS_CHILD | WS_VISIBLE,
                                             200, 540, 540, 34, page_reaper_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerDetail)), g_hInst, nullptr);
        auto_trigger_enable_label_ = CreateWindowW(L"STATIC", L"Enable Trigger:", WS_CHILD | WS_VISIBLE,
                                                   48, 596, 150, 24, page_reaper_, nullptr, g_hInst, nullptr);
        auto_trigger_enable_off_ = CreateWindowW(L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                                 200, 594, 84, 28, page_reaper_,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerEnableOff)), g_hInst, nullptr);
        auto_trigger_enable_on_ = CreateWindowW(L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                                296, 594, 84, 28, page_reaper_,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerEnableOn)), g_hInst, nullptr);
        auto_trigger_monitor_label_ = CreateWindowW(L"STATIC", L"Monitor Track:", WS_CHILD | WS_VISIBLE,
                                                    48, 644, 150, 24, page_reaper_, nullptr, g_hInst, nullptr);
        auto_trigger_monitor_combo_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                                    200, 640, 260, 240, page_reaper_,
                                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerMonitorTrackCombo)), g_hInst, nullptr);
        auto_trigger_mode_label_ = CreateWindowW(L"STATIC", L"Trigger Mode:", WS_CHILD | WS_VISIBLE,
                                                 48, 692, 150, 24, page_reaper_, nullptr, g_hInst, nullptr);
        auto_trigger_mode_warning_ = CreateWindowW(L"BUTTON", L"WARNING", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                                   200, 690, 120, 28, page_reaper_,
                                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerModeWarning)), g_hInst, nullptr);
        auto_trigger_mode_record_ = CreateWindowW(L"BUTTON", L"RECORD", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                                  332, 690, 110, 28, page_reaper_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerModeRecord)), g_hInst, nullptr);
        auto_trigger_threshold_label_ = CreateWindowW(L"STATIC", L"Threshold (dBFS):", WS_CHILD | WS_VISIBLE,
                                                      48, 740, 150, 24, page_reaper_, nullptr, g_hInst, nullptr);
        auto_trigger_threshold_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                                       200, 736, 100, 30, page_reaper_,
                                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerThresholdEdit)), g_hInst, nullptr);
        auto_trigger_hold_label_ = CreateWindowW(L"STATIC", L"Hold Time (s):", WS_CHILD | WS_VISIBLE,
                                                 332, 740, 120, 24, page_reaper_, nullptr, g_hInst, nullptr);
        auto_trigger_hold_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                                  456, 736, 100, 30, page_reaper_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerHoldEdit)), g_hInst, nullptr);
        auto_trigger_meter_label_ = CreateWindowW(L"STATIC", L"Trigger level: -- dBFS", WS_CHILD | WS_VISIBLE,
                                                  200, 776, 260, 24, page_reaper_,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerMeterLabel)), g_hInst, nullptr);
        apply_auto_trigger_button_ = CreateWindowW(L"BUTTON", L"Apply Auto Trigger Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   200, 816, 250, 40, page_reaper_,
                                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyAutoTriggerButton)), g_hInst, nullptr);
        discard_auto_trigger_button_ = CreateWindowW(L"BUTTON", L"Discard", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                     464, 816, 140, 40, page_reaper_,
                                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiscardAutoTriggerButton)), g_hInst, nullptr);
        auto_trigger_hint_ = CreateWindowW(L"STATIC",
                                           L"Pending Auto Trigger changes stay parked until you apply them.",
                                           WS_CHILD | WS_VISIBLE,
                                           200, 872, 540, 52, page_reaper_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutoTriggerHint)), g_hInst, nullptr);
    }

    void CreateWingPlaceholderPage() {
        wing_intro_ = CreateWindowW(L"STATIC",
                                    L"Manage the Wing-side recorder behavior here: target selection, source feed, and whether the recorder follows REAPER-triggered automation.",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 24, 760, 40, page_wing_, nullptr, g_hInst, nullptr);
        wing_section_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                           24, 88, 18, 18, page_wing_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWingSectionIcon)), g_hInst, nullptr);
        wing_section_header_ = CreateWindowW(L"STATIC", L"Recorder Coordination", WS_CHILD | WS_VISIBLE,
                                             48, 88, 300, 26, page_wing_, nullptr, g_hInst, nullptr);
        tab_status_wing_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                         640, 88, 120, 20, page_wing_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWingStatusChip)), g_hInst, nullptr);
        wing_enable_label_ = CreateWindowW(L"STATIC", L"Control:", WS_CHILD | WS_VISIBLE,
                                           48, 144, 160, 22, page_wing_, nullptr, g_hInst, nullptr);
        wing_enable_off_ = CreateWindowW(L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                         220, 142, 76, 26, page_wing_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderEnableOff)), g_hInst, nullptr);
        wing_enable_on_ = CreateWindowW(L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                        304, 142, 76, 26, page_wing_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderEnableOn)), g_hInst, nullptr);
        wing_target_label_ = CreateWindowW(L"STATIC", L"Target:", WS_CHILD | WS_VISIBLE,
                                           48, 198, 160, 22, page_wing_, nullptr, g_hInst, nullptr);
        wing_target_wlive_ = CreateWindowW(L"BUTTON", L"SD (WING-LIVE)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                           220, 196, 150, 26, page_wing_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderTargetWLive)), g_hInst, nullptr);
        wing_target_usb_ = CreateWindowW(L"BUTTON", L"USB Recorder", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                         384, 196, 150, 26, page_wing_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderTargetUsb)), g_hInst, nullptr);
        wing_pair_label_ = CreateWindowW(L"STATIC", L"MAIN Feed:", WS_CHILD | WS_VISIBLE,
                                         48, 252, 160, 22, page_wing_, nullptr, g_hInst, nullptr);
        wing_pair_1_ = CreateWindowW(L"BUTTON", L"MAIN 1/2", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                     220, 250, 110, 26, page_wing_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderPair1)), g_hInst, nullptr);
        wing_pair_3_ = CreateWindowW(L"BUTTON", L"MAIN 3/4", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     338, 250, 110, 26, page_wing_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderPair3)), g_hInst, nullptr);
        wing_pair_5_ = CreateWindowW(L"BUTTON", L"MAIN 5/6", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     456, 250, 110, 26, page_wing_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderPair5)), g_hInst, nullptr);
        wing_pair_7_ = CreateWindowW(L"BUTTON", L"MAIN 7/8", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     574, 250, 110, 26, page_wing_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderPair7)), g_hInst, nullptr);
        wing_follow_label_ = CreateWindowW(L"STATIC", L"Follow Trigger:", WS_CHILD | WS_VISIBLE,
                                           48, 306, 160, 22, page_wing_, nullptr, g_hInst, nullptr);
        wing_follow_off_ = CreateWindowW(L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                         220, 304, 76, 26, page_wing_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderFollowOff)), g_hInst, nullptr);
        wing_follow_on_ = CreateWindowW(L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                        304, 304, 76, 26, page_wing_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderFollowOn)), g_hInst, nullptr);
        wing_placeholder_body_ = CreateWindowW(L"STATIC",
                                               L"Recorder coordination is using the currently applied settings.",
                                               WS_CHILD | WS_VISIBLE,
                                               220, 356, 620, 52, page_wing_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRecorderDetail)), g_hInst, nullptr);
        apply_recorder_button_ = CreateWindowW(L"BUTTON", L"Apply Recorder Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               220, 430, 220, 38, page_wing_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyRecorderButton)), g_hInst, nullptr);
        discard_recorder_button_ = CreateWindowW(L"BUTTON", L"Discard", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 454, 430, 140, 38, page_wing_,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiscardRecorderButton)), g_hInst, nullptr);
    }

    void CreateControlPlaceholderPage() {
        control_intro_ = CreateWindowW(L"STATIC",
                                       L"Map Wing controls into REAPER here, and keep operator-facing integration settings close by when you need to verify how the plugin reacts on the console.",
                                       WS_CHILD | WS_VISIBLE,
                                       24, 24, 760, 44, page_control_, nullptr, g_hInst, nullptr);
        control_section_icon_ = CreateWindowW(L"STATIC", L"\x25CF", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                                              24, 88, 18, 18, page_control_,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdControlSectionIcon)), g_hInst, nullptr);
        control_section_header_ = CreateWindowW(L"STATIC", L"Wing Control Integration", WS_CHILD | WS_VISIBLE,
                                                48, 88, 340, 26, page_control_, nullptr, g_hInst, nullptr);
        tab_status_control_ = CreateWindowW(L"STATIC", L"Inactive", WS_CHILD | WS_VISIBLE,
                                            640, 88, 120, 20, page_control_,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdControlStatusChip)), g_hInst, nullptr);
        control_enable_label_ = CreateWindowW(L"STATIC", L"Wing Control Enabled:", WS_CHILD | WS_VISIBLE,
                                              48, 144, 170, 22, page_control_, nullptr, g_hInst, nullptr);
        midi_actions_off_ = CreateWindowW(L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                                          220, 142, 76, 26, page_control_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMidiActionsOff)), g_hInst, nullptr);
        midi_actions_on_ = CreateWindowW(L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                         304, 142, 76, 26, page_control_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMidiActionsOn)), g_hInst, nullptr);
        midi_summary_ = CreateWindowW(L"STATIC", L"No pending MIDI shortcut changes.", WS_CHILD | WS_VISIBLE,
                                      220, 198, 620, 42, page_control_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMidiSummary)), g_hInst, nullptr);
        midi_detail_ = CreateWindowW(L"STATIC", L"MIDI shortcuts are disabled.", WS_CHILD | WS_VISIBLE,
                                     220, 252, 620, 52, page_control_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMidiDetail)), g_hInst, nullptr);
        warning_layer_label_ = CreateWindowW(L"STATIC", L"Warning CC Layer:", WS_CHILD | WS_VISIBLE,
                                             48, 328, 170, 22, page_control_, nullptr, g_hInst, nullptr);
        warning_layer_combo_ = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             220, 324, 180, 260, page_control_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWarningLayerCombo)), g_hInst, nullptr);
        for (int i = 1; i <= 16; ++i) {
            std::wstring label = L"Layer " + std::to_wstring(i);
            SendMessageW(warning_layer_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        control_placeholder_body_ = CreateWindowW(L"STATIC",
                                                  L"Bridge routing still uses the applied config, MIDI output selection, and mappings from the existing workflow. This tab now exposes the main REAPER control integration state instead of a placeholder.",
                                                  WS_CHILD | WS_VISIBLE,
                                                  220, 370, 620, 64, page_control_, nullptr, g_hInst, nullptr);
        apply_midi_button_ = CreateWindowW(L"BUTTON", L"Apply MIDI Shortcuts", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           220, 454, 200, 38, page_control_,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyMidiButton)), g_hInst, nullptr);
        discard_midi_button_ = CreateWindowW(L"BUTTON", L"Discard", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             434, 454, 140, 38, page_control_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiscardMidiButton)), g_hInst, nullptr);
        support_section_header_ = CreateWindowW(L"STATIC", L"Support and Diagnostics", WS_CHILD | WS_VISIBLE,
                                                48, 526, 340, 26, page_control_, nullptr, g_hInst, nullptr);
        support_detail_ = CreateWindowW(L"STATIC",
                                        L"Use the debug log when things get weird, or when you want receipts for discovery, routing, validation, and recorder activity.",
                                        WS_CHILD | WS_VISIBLE,
                                        220, 564, 620, 42, page_control_, nullptr, g_hInst, nullptr);
        open_debug_log_button_ = CreateWindowW(L"BUTTON", L"Open Debug Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               220, 620, 170, 38, page_control_,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenDebugLogButton)), g_hInst, nullptr);
        clear_debug_log_button_ = CreateWindowW(L"BUTTON", L"Clear Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                404, 620, 130, 38, page_control_,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClearDebugLogButton)), g_hInst, nullptr);
        debug_log_view_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                          220, 676, 620, 220, page_control_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDebugLogView)), g_hInst, nullptr);
    }

    LRESULT OnGetMinMaxInfo(MINMAXINFO* info) {
        if (!info) {
            return 0;
        }
        info->ptMinTrackSize.x = kMinWindowWidth;
        info->ptMinTrackSize.y = kMinWindowHeight;
        return 0;
    }

    LRESULT OnSize(int width, int height) {
        if (!hwnd_ || width <= 0 || height <= 0) {
            return 0;
        }
        LayoutControls(width, height);
        return 0;
    }

    LRESULT OnEraseBackground(HDC hdc) {
        if (!hdc || !hwnd_) {
            return 0;
        }
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(hdc, &client, body_brush_);
        FillRect(hdc, &banner_rect_, banner_brush_);
        FillRect(hdc, &status_panel_rect_, status_panel_brush_);
        FrameRect(hdc, &banner_rect_, border_brush_);
        FrameRect(hdc, &status_panel_rect_, border_brush_);
        return 1;
    }

    LRESULT HandlePageMessage(PageLayoutState* page, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (!page || !page->hwnd) {
            return 0;
        }
        if (msg == WM_SIZE) {
            RECT client_rect{};
            GetClientRect(hwnd_, &client_rect);
            LayoutControls(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
            return 0;
        }

        int next_scroll = page->scroll_y;
        const int max_scroll = std::max(0, page->content_height - PageViewportHeight(*page));
        if (msg == WM_MOUSEWHEEL) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            next_scroll = std::clamp(page->scroll_y - ((delta / WHEEL_DELTA) * kScrollLineStep * 2), 0, max_scroll);
        } else if (msg == WM_VSCROLL) {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(page->hwnd, SB_VERT, &info);
            next_scroll = page->scroll_y;
            switch (LOWORD(wparam)) {
                case SB_LINEUP: next_scroll -= kScrollLineStep; break;
                case SB_LINEDOWN: next_scroll += kScrollLineStep; break;
                case SB_PAGEUP: next_scroll -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: next_scroll += static_cast<int>(info.nPage); break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: next_scroll = HIWORD(wparam); break;
                case SB_TOP: next_scroll = 0; break;
                case SB_BOTTOM: next_scroll = max_scroll; break;
                default: break;
            }
            next_scroll = std::clamp(next_scroll, 0, max_scroll);
        }

        if (next_scroll != page->scroll_y) {
            const int delta_y = page->scroll_y - next_scroll;
            page->scroll_y = next_scroll;
            UpdatePageScroll(*page, PageViewportHeight(*page));
            ScrollWindowEx(page->hwnd, 0, delta_y, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN);
            UpdateWindow(page->hwnd);
        }
        return 0;
    }

    int PageViewportHeight(const PageLayoutState& page) const {
        RECT rect{};
        GetClientRect(page.hwnd, &rect);
        const int height = static_cast<int>(rect.bottom - rect.top);
        return std::max(0, height);
    }

    void UpdatePageScroll(PageLayoutState& page, int viewport_height) {
        const int max_scroll = std::max(0, page.content_height - viewport_height);
        page.scroll_y = std::clamp(page.scroll_y, 0, max_scroll);
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, page.content_height - 1);
        info.nPage = static_cast<UINT>(std::max(1, viewport_height));
        info.nPos = page.scroll_y;
        SetScrollInfo(page.hwnd, SB_VERT, &info, TRUE);
    }

    int PageY(const PageLayoutState& page, int content_y) const {
        return content_y - page.scroll_y;
    }

    void LayoutControls(int client_width, int client_height) {
        const int outer_margin = 16;
        const int header_y = 16;
        const int header_height = 188;
        const int header_width = client_width - (outer_margin * 2);
        const int status_panel_width = std::min(430, std::max(360, header_width / 3));
        const int status_panel_height = 124;
        const int status_panel_x = outer_margin + header_width - status_panel_width - 20;
        const int status_panel_y = header_y + 24;
        banner_rect_ = RECT{outer_margin, header_y, outer_margin + header_width, header_y + header_height};
        status_panel_rect_ = RECT{status_panel_x, status_panel_y, status_panel_x + status_panel_width, status_panel_y + status_panel_height};

        MoveWindow(banner_group_, outer_margin, header_y, header_width, header_height, TRUE);
        MoveWindow(logo_, outer_margin + 22, header_y + 30, 124, 124, TRUE);
        MoveWindow(title_, outer_margin + 168, header_y + 54, 360, 34, TRUE);
        MoveWindow(subtitle_, outer_margin + 168, header_y + 98, std::max(420, header_width - status_panel_width - 220), 42, TRUE);
        MoveWindow(status_group_, status_panel_x, status_panel_y, status_panel_width, status_panel_height, TRUE);

        const int status_icon_x = status_panel_x + 16;
        const int status_text_x = status_panel_x + 40;
        const int status_text_w = status_panel_width - 52;
        MoveWindow(header_console_icon_, status_icon_x, status_panel_y + 14, 24, 24, TRUE);
        MoveWindow(header_console_status_, status_text_x + 4, status_panel_y + 14, status_text_w - 4, 24, TRUE);
        MoveWindow(header_validation_icon_, status_icon_x, status_panel_y + 42, 24, 24, TRUE);
        MoveWindow(header_validation_status_, status_text_x + 4, status_panel_y + 42, status_text_w - 4, 24, TRUE);
        MoveWindow(header_recorder_icon_, status_icon_x, status_panel_y + 70, 24, 24, TRUE);
        MoveWindow(header_recorder_status_, status_text_x + 4, status_panel_y + 70, status_text_w - 4, 24, TRUE);
        MoveWindow(header_midi_icon_, status_icon_x, status_panel_y + 98, 24, 24, TRUE);
        MoveWindow(header_midi_status_, status_text_x + 4, status_panel_y + 98, status_text_w - 4, 24, TRUE);

        const int footer_height = 30;
        const int footer_y = client_height - footer_height - 14;
        MoveWindow(footer_status_, outer_margin + 4, footer_y, client_width - (outer_margin * 2) - 8, footer_height, TRUE);

        const int tab_y = header_y + header_height + 14;
        const int tab_button_height = 42;
        const int tab_gap = 10;
        const int page_y = tab_y + tab_button_height + 8;
        const int page_height = std::max(520, footer_y - page_y - 10);
        const int console_w = std::max(180, header_width / 5);
        const int reaper_w = std::max(180, header_width / 5);
        const int wing_w = std::max(160, header_width / 6);
        const int control_w = std::max(320, header_width - console_w - reaper_w - wing_w - (tab_gap * 3));
        MoveWindow(tab_button_console_, outer_margin, tab_y, console_w, tab_button_height, TRUE);
        MoveWindow(tab_button_reaper_, outer_margin + console_w + tab_gap, tab_y, reaper_w, tab_button_height, TRUE);
        MoveWindow(tab_button_wing_, outer_margin + console_w + reaper_w + (tab_gap * 2), tab_y, wing_w, tab_button_height, TRUE);
        MoveWindow(tab_button_control_, outer_margin + console_w + reaper_w + wing_w + (tab_gap * 3), tab_y, control_w, tab_button_height, TRUE);
        MoveWindow(page_frame_, outer_margin, page_y, header_width, page_height, TRUE);

        const int page_x = outer_margin + 10;
        const int inner_page_y = page_y + 10;
        const int page_width = header_width - 20;
        const int inner_page_height = page_height - 20;

        MoveWindow(page_console_, page_x, inner_page_y, page_width, inner_page_height, TRUE);
        MoveWindow(page_reaper_, page_x, inner_page_y, page_width, inner_page_height, TRUE);
        MoveWindow(page_wing_, page_x, inner_page_y, page_width, inner_page_height, TRUE);
        MoveWindow(page_control_, page_x, inner_page_y, page_width, inner_page_height, TRUE);

        const int page_margin = 30;
        const int label_x = 34;
        const int control_x = 220;
        const int page_right = page_width - page_margin;
        const int status_chip_w = 180;
        const int status_chip_x = page_right - status_chip_w;
        const int content_w = page_width - (page_margin * 2);
        const int viewport_height = page_height;

        console_page_state_.content_height = 660;
        reaper_page_state_.content_height = 1520;
        wing_page_state_.content_height = 620;
        control_page_state_.content_height = 1040;
        UpdatePageScroll(console_page_state_, viewport_height);
        UpdatePageScroll(reaper_page_state_, viewport_height);
        UpdatePageScroll(wing_page_state_, viewport_height);
        UpdatePageScroll(control_page_state_, viewport_height);

        MoveWindow(console_intro_, page_margin, PageY(console_page_state_, 28), content_w - 10, 82, TRUE);
        MoveWindow(console_section_icon_, page_margin, PageY(console_page_state_, 132), 22, 22, TRUE);
        MoveWindow(console_section_header_, page_margin + 26, PageY(console_page_state_, 128), 330, 34, TRUE);
        MoveWindow(tab_status_console_, status_chip_x, PageY(console_page_state_, 128), status_chip_w, 28, TRUE);
        MoveWindow(console_label_, label_x, PageY(console_page_state_, 166), 160, 28, TRUE);
        MoveWindow(wing_combo_, control_x, PageY(console_page_state_, 160), std::max(360, page_width - control_x - 210), 360, TRUE);
        MoveWindow(scan_button_, page_width - ButtonWidthForLabel(scan_button_, 132), PageY(console_page_state_, 160), ButtonWidthForLabel(scan_button_, 132), 42, TRUE);
        MoveWindow(console_help_discovery_, control_x, PageY(console_page_state_, 206), page_width - control_x - 40, 34, TRUE);
        MoveWindow(console_manual_ip_label_, label_x, PageY(console_page_state_, 272), 160, 28, TRUE);
        MoveWindow(manual_ip_edit_, control_x, PageY(console_page_state_, 266), std::min(440, page_width - control_x - 250), 34, TRUE);
        MoveWindow(console_help_manual_, control_x, PageY(console_page_state_, 312), page_width - control_x - 40, 52, TRUE);
        MoveWindow(connect_button_, page_width - ButtonWidthForLabel(connect_button_, 148), PageY(console_page_state_, 388), ButtonWidthForLabel(connect_button_, 148), 42, TRUE);
        MoveWindow(console_footer_, page_margin, PageY(console_page_state_, 450), page_width - 220, 72, TRUE);

        MoveWindow(reaper_intro_, page_margin, PageY(reaper_page_state_, 28), content_w - 10, 96, TRUE);
        MoveWindow(reaper_section_icon_, page_margin, PageY(reaper_page_state_, 146), 22, 22, TRUE);
        MoveWindow(reaper_section_header_, page_margin + 26, PageY(reaper_page_state_, 140), 420, 34, TRUE);
        MoveWindow(tab_status_reaper_, status_chip_x, PageY(reaper_page_state_, 140), status_chip_w, 28, TRUE);
        MoveWindow(reaper_output_label_, label_x, PageY(reaper_page_state_, 182), 170, 28, TRUE);
        MoveWindow(output_usb_radio_, control_x, PageY(reaper_page_state_, 176), 96, 32, TRUE);
        MoveWindow(output_card_radio_, control_x + 112, PageY(reaper_page_state_, 176), 108, 32, TRUE);
        MoveWindow(reaper_output_help_, control_x, PageY(reaper_page_state_, 220), page_width - control_x - 40, 66, TRUE);
        MoveWindow(pending_summary_, control_x, PageY(reaper_page_state_, 304), page_width - control_x - 40, 112, TRUE);
        MoveWindow(readiness_detail_, control_x, PageY(reaper_page_state_, 432), page_width - control_x - 40, 170, TRUE);
        const int choose_width = ButtonWidthForLabel(choose_sources_button_, 230);
        const int apply_width = ButtonWidthForLabel(apply_setup_button_, 200);
        const int discard_width = ButtonWidthForLabel(discard_setup_button_, 150);
        const int toggle_width = ButtonWidthForLabel(toggle_soundcheck_button_, 244);
        MoveWindow(choose_sources_button_, control_x, PageY(reaper_page_state_, 630), choose_width, 44, TRUE);
        MoveWindow(apply_setup_button_, control_x + choose_width + 14, PageY(reaper_page_state_, 630), apply_width, 44, TRUE);
        MoveWindow(discard_setup_button_, control_x + choose_width + apply_width + 28, PageY(reaper_page_state_, 630), discard_width, 44, TRUE);
        MoveWindow(toggle_soundcheck_button_, control_x, PageY(reaper_page_state_, 692), toggle_width, 44, TRUE);
        MoveWindow(reaper_toggle_help_, control_x, PageY(reaper_page_state_, 748), page_width - control_x - 40, 58, TRUE);
        MoveWindow(auto_trigger_section_icon_, page_margin, PageY(reaper_page_state_, 858), 22, 22, TRUE);
        MoveWindow(auto_trigger_header_, page_margin + 26, PageY(reaper_page_state_, 852), 320, 34, TRUE);
        MoveWindow(auto_trigger_detail_, control_x, PageY(reaper_page_state_, 908), page_width - control_x - 40, 48, TRUE);
        MoveWindow(auto_trigger_enable_label_, label_x, PageY(reaper_page_state_, 972), 150, 28, TRUE);
        MoveWindow(auto_trigger_enable_off_, control_x, PageY(reaper_page_state_, 968), 90, 30, TRUE);
        MoveWindow(auto_trigger_enable_on_, control_x + 102, PageY(reaper_page_state_, 968), 90, 30, TRUE);
        MoveWindow(auto_trigger_monitor_label_, label_x, PageY(reaper_page_state_, 1020), 150, 28, TRUE);
        MoveWindow(auto_trigger_monitor_combo_, control_x, PageY(reaper_page_state_, 1016), 300, 260, TRUE);
        MoveWindow(auto_trigger_mode_label_, label_x, PageY(reaper_page_state_, 1068), 150, 28, TRUE);
        MoveWindow(auto_trigger_mode_warning_, control_x, PageY(reaper_page_state_, 1064), 126, 30, TRUE);
        MoveWindow(auto_trigger_mode_record_, control_x + 140, PageY(reaper_page_state_, 1064), 114, 30, TRUE);
        MoveWindow(auto_trigger_threshold_label_, label_x, PageY(reaper_page_state_, 1118), 150, 28, TRUE);
        MoveWindow(auto_trigger_threshold_edit_, control_x, PageY(reaper_page_state_, 1114), 110, 32, TRUE);
        MoveWindow(auto_trigger_hold_label_, control_x + 142, PageY(reaper_page_state_, 1118), 120, 28, TRUE);
        MoveWindow(auto_trigger_hold_edit_, control_x + 272, PageY(reaper_page_state_, 1114), 110, 32, TRUE);
        MoveWindow(auto_trigger_meter_label_, control_x, PageY(reaper_page_state_, 1160), 320, 28, TRUE);
        const int apply_auto_width = ButtonWidthForLabel(apply_auto_trigger_button_, 250);
        const int discard_auto_width = ButtonWidthForLabel(discard_auto_trigger_button_, 150);
        MoveWindow(apply_auto_trigger_button_, control_x, PageY(reaper_page_state_, 1200), apply_auto_width, 42, TRUE);
        MoveWindow(discard_auto_trigger_button_, control_x + apply_auto_width + 14, PageY(reaper_page_state_, 1200), discard_auto_width, 42, TRUE);
        MoveWindow(auto_trigger_hint_, control_x, PageY(reaper_page_state_, 1258), page_width - control_x - 40, 92, TRUE);

        MoveWindow(wing_intro_, page_margin, PageY(wing_page_state_, 28), content_w - 10, 74, TRUE);
        MoveWindow(wing_section_icon_, page_margin, PageY(wing_page_state_, 128), 22, 22, TRUE);
        MoveWindow(wing_section_header_, page_margin + 26, PageY(wing_page_state_, 122), 340, 34, TRUE);
        MoveWindow(tab_status_wing_, status_chip_x, PageY(wing_page_state_, 122), status_chip_w, 28, TRUE);
        MoveWindow(wing_enable_label_, label_x, PageY(wing_page_state_, 184), 148, 28, TRUE);
        MoveWindow(wing_enable_off_, control_x, PageY(wing_page_state_, 182), 84, 28, TRUE);
        MoveWindow(wing_enable_on_, control_x + 92, PageY(wing_page_state_, 182), 84, 28, TRUE);
        MoveWindow(wing_target_label_, label_x, PageY(wing_page_state_, 238), 148, 28, TRUE);
        MoveWindow(wing_target_wlive_, control_x, PageY(wing_page_state_, 236), 160, 28, TRUE);
        MoveWindow(wing_target_usb_, control_x + 174, PageY(wing_page_state_, 236), 150, 28, TRUE);
        MoveWindow(wing_pair_label_, label_x, PageY(wing_page_state_, 292), 148, 28, TRUE);
        MoveWindow(wing_pair_1_, control_x, PageY(wing_page_state_, 290), 120, 30, TRUE);
        MoveWindow(wing_pair_3_, control_x + 138, PageY(wing_page_state_, 290), 120, 30, TRUE);
        MoveWindow(wing_pair_5_, control_x + 276, PageY(wing_page_state_, 290), 120, 30, TRUE);
        MoveWindow(wing_pair_7_, control_x + 414, PageY(wing_page_state_, 290), 120, 30, TRUE);
        MoveWindow(wing_follow_label_, label_x, PageY(wing_page_state_, 346), 148, 28, TRUE);
        MoveWindow(wing_follow_off_, control_x, PageY(wing_page_state_, 344), 84, 28, TRUE);
        MoveWindow(wing_follow_on_, control_x + 92, PageY(wing_page_state_, 344), 84, 28, TRUE);
        MoveWindow(wing_placeholder_body_, control_x, PageY(wing_page_state_, 400), page_width - control_x - 40, 92, TRUE);
        const int apply_recorder_width = ButtonWidthForLabel(apply_recorder_button_, 248);
        const int discard_recorder_width = ButtonWidthForLabel(discard_recorder_button_, 150);
        MoveWindow(apply_recorder_button_, control_x, PageY(wing_page_state_, 514), apply_recorder_width, 42, TRUE);
        MoveWindow(discard_recorder_button_, control_x + apply_recorder_width + 14, PageY(wing_page_state_, 514), discard_recorder_width, 42, TRUE);

        MoveWindow(control_intro_, page_margin, PageY(control_page_state_, 28), content_w - 10, 74, TRUE);
        MoveWindow(control_section_icon_, page_margin, PageY(control_page_state_, 128), 22, 22, TRUE);
        MoveWindow(control_section_header_, page_margin + 26, PageY(control_page_state_, 122), 380, 34, TRUE);
        MoveWindow(tab_status_control_, status_chip_x, PageY(control_page_state_, 122), status_chip_w, 28, TRUE);
        MoveWindow(control_enable_label_, label_x, PageY(control_page_state_, 184), 180, 28, TRUE);
        MoveWindow(midi_actions_off_, control_x, PageY(control_page_state_, 182), 84, 28, TRUE);
        MoveWindow(midi_actions_on_, control_x + 92, PageY(control_page_state_, 182), 84, 28, TRUE);
        MoveWindow(midi_summary_, control_x, PageY(control_page_state_, 236), page_width - control_x - 40, 58, TRUE);
        MoveWindow(midi_detail_, control_x, PageY(control_page_state_, 308), page_width - control_x - 40, 74, TRUE);
        MoveWindow(warning_layer_label_, label_x, PageY(control_page_state_, 406), 180, 28, TRUE);
        MoveWindow(warning_layer_combo_, control_x, PageY(control_page_state_, 402), 190, 260, TRUE);
        MoveWindow(control_placeholder_body_, control_x, PageY(control_page_state_, 454), page_width - control_x - 40, 92, TRUE);
        const int apply_midi_width = ButtonWidthForLabel(apply_midi_button_, 226);
        const int discard_midi_width = ButtonWidthForLabel(discard_midi_button_, 150);
        MoveWindow(apply_midi_button_, control_x, PageY(control_page_state_, 568), apply_midi_width, 42, TRUE);
        MoveWindow(discard_midi_button_, control_x + apply_midi_width + 14, PageY(control_page_state_, 568), discard_midi_width, 42, TRUE);
        MoveWindow(support_section_header_, page_margin + 26, PageY(control_page_state_, 668), 360, 34, TRUE);
        MoveWindow(support_detail_, control_x, PageY(control_page_state_, 720), page_width - control_x - 40, 58, TRUE);
        const int open_log_width = ButtonWidthForLabel(open_debug_log_button_, 180);
        const int clear_log_width = ButtonWidthForLabel(clear_debug_log_button_, 136);
        MoveWindow(open_debug_log_button_, control_x, PageY(control_page_state_, 792), open_log_width, 42, TRUE);
        MoveWindow(clear_debug_log_button_, control_x + open_log_width + 14, PageY(control_page_state_, 792), clear_log_width, 42, TRUE);
        MoveWindow(debug_log_view_, control_x, PageY(control_page_state_, 854), page_width - control_x - 40, 150, TRUE);
    }

    void ShowActivePage(int tab_index) {
        ShowWindow(page_console_, tab_index == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_reaper_, tab_index == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_wing_, tab_index == 2 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_control_, tab_index == 3 ? SW_SHOW : SW_HIDE);
        current_tab_index_ = tab_index;
    }

    void SelectTab(int tab_index) {
        SendMessageW(tab_button_console_, BM_SETSTATE, tab_index == 0 ? TRUE : FALSE, 0);
        SendMessageW(tab_button_reaper_, BM_SETSTATE, tab_index == 1 ? TRUE : FALSE, 0);
        SendMessageW(tab_button_wing_, BM_SETSTATE, tab_index == 2 ? TRUE : FALSE, 0);
        SendMessageW(tab_button_control_, BM_SETSTATE, tab_index == 3 ? TRUE : FALSE, 0);
        ShowActivePage(tab_index);
    }

    LRESULT OnNotify(NMHDR* hdr) {
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
            case kIdTabConsoleButton:
                SelectTab(0);
                return 0;
            case kIdTabReaperButton:
                SelectTab(1);
                return 0;
            case kIdTabWingButton:
                SelectTab(2);
                return 0;
            case kIdTabControlButton:
                SelectTab(3);
                return 0;
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
            case kIdAutoTriggerEnableOff:
            case kIdAutoTriggerEnableOn:
            case kIdAutoTriggerModeWarning:
            case kIdAutoTriggerModeRecord:
                OnAutoTriggerSettingsChanged();
                return 0;
            case kIdAutoTriggerThresholdEdit:
            case kIdAutoTriggerHoldEdit:
                if (notify_code == EN_CHANGE) {
                    OnAutoTriggerSettingsChanged();
                }
                return 0;
            case kIdAutoTriggerMonitorTrackCombo:
                if (notify_code == CBN_SELCHANGE) {
                    OnAutoTriggerSettingsChanged();
                }
                return 0;
            case kIdApplyAutoTriggerButton:
                OnApplyAutoTriggerSettings();
                return 0;
            case kIdDiscardAutoTriggerButton:
                OnDiscardAutoTriggerSettings();
                return 0;
            case kIdRecorderEnableOff:
            case kIdRecorderEnableOn:
            case kIdRecorderTargetWLive:
            case kIdRecorderTargetUsb:
            case kIdRecorderPair1:
            case kIdRecorderPair3:
            case kIdRecorderPair5:
            case kIdRecorderPair7:
            case kIdRecorderFollowOff:
            case kIdRecorderFollowOn:
                OnRecorderSettingsChanged();
                return 0;
            case kIdApplyRecorderButton:
                OnApplyRecorderSettings();
                return 0;
            case kIdDiscardRecorderButton:
                OnDiscardRecorderSettings();
                return 0;
            case kIdMidiActionsOff:
            case kIdMidiActionsOn:
                OnMidiActionsChanged();
                return 0;
            case kIdWarningLayerCombo:
                if (notify_code == CBN_SELCHANGE) {
                    OnMidiActionsChanged();
                }
                return 0;
            case kIdApplyMidiButton:
                OnApplyMidiActions();
                return 0;
            case kIdDiscardMidiButton:
                OnDiscardMidiActions();
                return 0;
            case kIdOpenDebugLogButton:
                OnOpenDebugLog();
                return 0;
            case kIdClearDebugLogButton:
                OnClearDebugLog();
                return 0;
            default:
                return 0;
        }
    }

    LRESULT OnCtlColor(HDC hdc, HWND control) {
        SetBkMode(hdc, TRANSPARENT);
        const int id = GetDlgCtrlID(control);
        COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT);
        if (control == header_console_icon_) {
            text_color = current_snapshot_.console.color;
        } else if (control == header_validation_icon_) {
            text_color = current_snapshot_.validation.color;
        } else if (control == header_recorder_icon_) {
            text_color = current_snapshot_.recorder.color;
        } else if (control == header_midi_icon_) {
            text_color = current_snapshot_.midi.color;
        } else if (control == console_section_icon_) {
            text_color = RGB(28, 114, 184);
        } else if (control == reaper_section_icon_) {
            text_color = RGB(40, 140, 70);
        } else if (control == auto_trigger_section_icon_) {
            text_color = RGB(215, 135, 30);
        } else if (control == wing_section_icon_) {
            text_color = RGB(153, 84, 187);
        } else if (control == control_section_icon_) {
            text_color = RGB(80, 80, 80);
        } else if (id == kIdHeaderConsoleStatus) {
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
            text_color = RGB(80, 80, 80);
        } else if (control == subtitle_ ||
                   control == console_help_discovery_ ||
                   control == console_help_manual_ ||
                   control == console_footer_ ||
                   control == reaper_output_help_ ||
                   control == reaper_toggle_help_ ||
                   control == wing_placeholder_body_ ||
                   control == control_placeholder_body_) {
            text_color = RGB(92, 98, 104);
        } else if (control == console_intro_ ||
                   control == reaper_intro_) {
            text_color = RGB(40, 40, 40);
        } else if (control == auto_trigger_detail_) {
            text_color = RGB(92, 98, 104);
        } else if (control == auto_trigger_hint_) {
            text_color = RGB(28, 114, 184);
        } else if (control == auto_trigger_meter_label_) {
            text_color = RGB(80, 80, 80);
        } else if (control == support_detail_) {
            text_color = RGB(92, 98, 104);
        }
        SetTextColor(hdc, text_color);
        if (control == title_ ||
            control == subtitle_) {
            return reinterpret_cast<LRESULT>(banner_brush_);
        }
        if (control == header_console_icon_ ||
            control == header_console_status_ ||
            control == header_validation_icon_ ||
            control == header_validation_status_ ||
            control == header_recorder_icon_ ||
            control == header_recorder_status_ ||
            control == header_midi_icon_ ||
            control == header_midi_status_) {
            return reinterpret_cast<LRESULT>(status_panel_brush_);
        }
        return reinterpret_cast<LRESULT>(body_brush_);
    }

    void UpdateWingTabUI() {
        auto& extension = ReaperExtension::Instance();
        std::wstring detail;
        if (recorder_settings_dirty_) {
            detail = L"Recorder coordination changes are staged. Apply them to update the target recorder, the MAIN source pair, and follow behavior.";
        } else if (!extension.GetConfig().recorder_coordination_enabled) {
            detail = L"Recorder coordination is off. Turn Recorder Control on to prepare a Wing recorder and optionally follow auto-trigger recordings.";
        } else if (!extension.IsConnected()) {
            detail = L"Recorder coordination can be staged now, but a connected Wing is required before recorder routing can actually be pushed.";
        } else {
            detail = L"Recorder coordination is aligned with the current setup and ready to be used.";
        }
        SetWindowTextW(wing_placeholder_body_, detail.c_str());
        EnableWindow(apply_recorder_button_, recorder_settings_dirty_ ? TRUE : FALSE);
        EnableWindow(discard_recorder_button_, recorder_settings_dirty_ ? TRUE : FALSE);
    }

    void RefreshMonitorTrackDropdown() {
        if (!auto_trigger_monitor_combo_) {
            return;
        }
        auto& config = ReaperExtension::Instance().GetConfig();
        SendMessageW(auto_trigger_monitor_combo_, CB_RESETCONTENT, 0, 0);
        SendMessageW(auto_trigger_monitor_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (Armed+Monitored)"));
        const int track_count = ReaperExtension::Instance().GetProjectTrackCount();
        for (int i = 1; i <= track_count; ++i) {
            wchar_t label[64];
            std::swprintf(label, sizeof(label) / sizeof(wchar_t), L"Track %d", i);
            SendMessageW(auto_trigger_monitor_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        int wanted = std::max(0, pending_auto_record_monitor_track_);
        if (wanted > track_count) {
            wanted = 0;
            pending_auto_record_monitor_track_ = 0;
            if (config.auto_record_monitor_track > track_count) {
                config.auto_record_monitor_track = 0;
            }
        }
        SendMessageW(auto_trigger_monitor_combo_, CB_SETCURSEL, wanted, 0);
    }

    void SyncAutoTriggerFromConfig() {
        const auto& config = ReaperExtension::Instance().GetConfig();
        pending_auto_record_enabled_ = config.auto_record_enabled;
        pending_auto_record_warning_only_ = config.auto_record_warning_only;
        pending_auto_record_threshold_db_ = config.auto_record_threshold_db;
        pending_auto_record_hold_ms_ = config.auto_record_hold_ms;
        pending_auto_record_monitor_track_ = std::max(0, config.auto_record_monitor_track);
        auto_trigger_dirty_ = false;
    }

    void SyncAutoTriggerControlsFromPending() {
        CheckRadioButton(page_reaper_, kIdAutoTriggerEnableOff, kIdAutoTriggerEnableOn,
                         pending_auto_record_enabled_ ? kIdAutoTriggerEnableOn : kIdAutoTriggerEnableOff);
        CheckRadioButton(page_reaper_, kIdAutoTriggerModeWarning, kIdAutoTriggerModeRecord,
                         pending_auto_record_warning_only_ ? kIdAutoTriggerModeWarning : kIdAutoTriggerModeRecord);
        wchar_t threshold_text[32];
        std::swprintf(threshold_text, sizeof(threshold_text) / sizeof(wchar_t), L"%.1f", pending_auto_record_threshold_db_);
        SetWindowTextW(auto_trigger_threshold_edit_, threshold_text);
        wchar_t hold_text[32];
        std::swprintf(hold_text, sizeof(hold_text) / sizeof(wchar_t), L"%.1f", pending_auto_record_hold_ms_ / 1000.0);
        SetWindowTextW(auto_trigger_hold_edit_, hold_text);
        RefreshMonitorTrackDropdown();
    }

    void OnAutoTriggerSettingsChanged() {
        pending_auto_record_enabled_ = (IsDlgButtonChecked(page_reaper_, kIdAutoTriggerEnableOn) == BST_CHECKED);
        pending_auto_record_warning_only_ = (IsDlgButtonChecked(page_reaper_, kIdAutoTriggerModeWarning) == BST_CHECKED);
        pending_auto_record_threshold_db_ = std::wcstod(ReadWindowText(auto_trigger_threshold_edit_).c_str(), nullptr);
        if (!std::isfinite(pending_auto_record_threshold_db_)) {
            pending_auto_record_threshold_db_ = ReaperExtension::Instance().GetConfig().auto_record_threshold_db;
        }
        const double hold_seconds = std::wcstod(ReadWindowText(auto_trigger_hold_edit_).c_str(), nullptr);
        pending_auto_record_hold_ms_ = std::max(0, static_cast<int>(std::lround((std::isfinite(hold_seconds) ? hold_seconds : 0.0) * 1000.0)));
        const LRESULT selection = SendMessageW(auto_trigger_monitor_combo_, CB_GETCURSEL, 0, 0);
        pending_auto_record_monitor_track_ = (selection >= 0) ? static_cast<int>(selection) : 0;
        const auto& config = ReaperExtension::Instance().GetConfig();
        auto_trigger_dirty_ =
            pending_auto_record_enabled_ != config.auto_record_enabled ||
            pending_auto_record_warning_only_ != config.auto_record_warning_only ||
            std::fabs(pending_auto_record_threshold_db_ - config.auto_record_threshold_db) > 0.05 ||
            pending_auto_record_hold_ms_ != config.auto_record_hold_ms ||
            pending_auto_record_monitor_track_ != config.auto_record_monitor_track;
        footer_message_ = auto_trigger_dirty_
            ? L"Auto Trigger changes staged."
            : L"Auto Trigger matches the applied settings.";
        RefreshAll();
    }

    void OnApplyAutoTriggerSettings() {
        auto& extension = ReaperExtension::Instance();
        auto& config = extension.GetConfig();
        config.auto_record_enabled = pending_auto_record_enabled_;
        config.auto_record_warning_only = pending_auto_record_warning_only_;
        config.auto_record_threshold_db = pending_auto_record_threshold_db_;
        config.auto_record_hold_ms = pending_auto_record_hold_ms_;
        config.auto_record_monitor_track = pending_auto_record_monitor_track_;
        SaveConfigIfPossible(extension);
        extension.ApplyAutoRecordSettings();
        auto_trigger_dirty_ = false;
        footer_message_ = L"Auto Trigger settings applied.";
        RefreshAll();
    }

    void OnDiscardAutoTriggerSettings() {
        SyncAutoTriggerFromConfig();
        SyncAutoTriggerControlsFromPending();
        footer_message_ = L"Auto Trigger changes discarded.";
        RefreshAll();
    }

    void UpdateAutoTriggerUI() {
        auto& extension = ReaperExtension::Instance();
        const auto& config = extension.GetConfig();
        const bool pending_apply = has_pending_setup_draft_ || pending_output_mode_ != ToWide(config.soundcheck_output_mode);
        const bool live_setup_controls_enabled = (latest_validation_state_ == ValidationState::Ready) && !pending_apply;
        const bool auto_trigger_enabled = live_setup_controls_enabled && pending_auto_record_enabled_;

        std::wstring detail;
        if (auto_trigger_dirty_) {
            detail = L"Auto Trigger settings changed. Apply them to resume trigger monitoring with the staged mode, threshold, hold time, and monitor track.";
        } else if (pending_apply) {
            detail = L"Auto Trigger is blocked by pending setup changes. Apply the staged setup or rebuild the current managed setup first.";
        } else if (latest_validation_state_ != ValidationState::Ready) {
            detail = L"Auto Trigger is blocked until the live recording setup validates against the current WING and REAPER state.";
        } else if (extension.IsSoundcheckModeEnabled()) {
            detail = L"Auto Trigger is paused while Soundcheck Mode is active on the managed channels.";
        } else if (!config.auto_record_enabled) {
            detail = L"Auto Trigger is currently off. Turn it on and apply the change to start signal-based monitoring again.";
        } else {
            detail = L"Auto Trigger is clear to run with the current live recording setup.";
        }
        std::wstring hint = auto_trigger_dirty_
            ? L"Pending changes stay parked until you click Apply Auto Trigger Settings."
            : L"Warning mode flashes controls when triggered; Record mode starts and stops recording automatically.";
        SetWindowTextW(auto_trigger_detail_, detail.c_str());
        SetWindowTextW(auto_trigger_hint_, hint.c_str());

        EnableWindow(auto_trigger_enable_off_, live_setup_controls_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_enable_on_, live_setup_controls_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_monitor_combo_, auto_trigger_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_mode_warning_, auto_trigger_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_mode_record_, auto_trigger_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_threshold_edit_, auto_trigger_enabled ? TRUE : FALSE);
        EnableWindow(auto_trigger_hold_edit_, auto_trigger_enabled ? TRUE : FALSE);
        EnableWindow(apply_auto_trigger_button_, (live_setup_controls_enabled && auto_trigger_dirty_) ? TRUE : FALSE);
        EnableWindow(discard_auto_trigger_button_, auto_trigger_dirty_ ? TRUE : FALSE);
    }

    void UpdateControlTabUI() {
        auto& extension = ReaperExtension::Instance();
        std::wstring summary = midi_actions_dirty_
            ? (pending_midi_actions_enabled_ ? L"Pending control integration enable." : L"Pending control integration disable.")
            : L"No pending MIDI shortcut changes.";
        std::wstring detail;
        if (midi_actions_dirty_) {
            detail = L"These changes stay staged until you apply them. When enabled, Wing user controls can trigger REAPER transport and warning feedback.";
        } else if (extension.IsMidiActionsEnabled()) {
            detail = L"Wing control integration is active. Warning flash layer and transport mappings are using the applied settings.";
        } else {
            detail = L"MIDI shortcuts are disabled. Enable them after live setup is validated if you want hands-on transport control from the console.";
        }
        SetWindowTextW(midi_summary_, summary.c_str());
        SetWindowTextW(midi_detail_, detail.c_str());
        EnableWindow(apply_midi_button_, midi_actions_dirty_ ? TRUE : FALSE);
        EnableWindow(discard_midi_button_, midi_actions_dirty_ ? TRUE : FALSE);
    }

    void FlushPendingLogBuffer() {
        if (!debug_log_view_) {
            return;
        }
        std::wstring chunk;
        {
            std::lock_guard<std::mutex> lock(log_buffer_mutex_);
            if (pending_log_buffer_.empty()) {
                return;
            }
            chunk.swap(pending_log_buffer_);
        }

        const std::wstring current = ReadWindowText(debug_log_view_);
        std::wstring combined = current + chunk;
        constexpr size_t kMaxLogChars = 32000;
        if (combined.size() > kMaxLogChars) {
            combined = combined.substr(combined.size() - kMaxLogChars);
        }
        SetWindowTextW(debug_log_view_, combined.c_str());
        const int length = GetWindowTextLengthW(debug_log_view_);
        SendMessageW(debug_log_view_, EM_SETSEL, length, length);
        SendMessageW(debug_log_view_, EM_SCROLLCARET, 0, 0);
    }

    void UpdateAutoTriggerMeterPreview() {
        if (!auto_trigger_meter_label_) {
            return;
        }
        const double lin = ReaperExtension::Instance().ReadCurrentTriggerLevel();
        std::wstring text;
        if (lin <= 0.0000001 || !std::isfinite(lin)) {
            text = L"Trigger level: -inf dBFS";
        } else {
            const double db = 20.0 * std::log10(lin);
            wchar_t buffer[64];
            std::swprintf(buffer, sizeof(buffer) / sizeof(wchar_t), L"Trigger level: %.1f dBFS", db);
            text = buffer;
        }
        if (ReadWindowText(auto_trigger_meter_label_) != text) {
            SetWindowTextW(auto_trigger_meter_label_, text.c_str());
        }
    }

    void RefreshAll() {
        if (!hwnd_) {
            return;
        }
        const StatusSnapshot previous_snapshot = current_snapshot_;
        auto snapshot = BuildSnapshot();
        current_snapshot_ = snapshot;
        auto update_text = [](HWND control, const std::wstring& text) {
            if (control && ReadWindowText(control) != text) {
                SetWindowTextW(control, text.c_str());
            }
        };
        auto redraw_control = [](HWND control) {
            if (control) {
                RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
        };

        update_text(header_console_status_, snapshot.console.text);
        update_text(header_validation_status_, snapshot.validation.text);
        update_text(header_recorder_status_, snapshot.recorder.text);
        update_text(header_midi_status_, snapshot.midi.text);
        update_text(tab_status_console_, snapshot.console_tab.text);
        update_text(tab_status_reaper_, snapshot.reaper_tab.text);
        update_text(tab_status_wing_, snapshot.wing_tab.text);
        update_text(tab_status_control_, snapshot.control_tab.text);
        update_text(pending_summary_, snapshot.pending_summary);
        update_text(readiness_detail_, snapshot.readiness_detail);
        update_text(footer_status_, snapshot.footer);
        update_text(apply_setup_button_, snapshot.apply_label);
        update_text(toggle_soundcheck_button_, snapshot.toggle_label);
        UpdateAutoTriggerMeterPreview();
        FlushPendingLogBuffer();
        RefreshMonitorTrackDropdown();
        UpdateAutoTriggerUI();
        UpdateWingTabUI();
        UpdateControlTabUI();
        EnableWindow(apply_setup_button_, snapshot.can_apply ? TRUE : FALSE);
        EnableWindow(discard_setup_button_, snapshot.can_discard ? TRUE : FALSE);
        EnableWindow(toggle_soundcheck_button_, snapshot.can_toggle ? TRUE : FALSE);
        update_text(connect_button_, ReaperExtension::Instance().IsConnected() ? L"Disconnect" : L"Connect");

        if (previous_snapshot.console.color != snapshot.console.color || previous_snapshot.console.text != snapshot.console.text) {
            redraw_control(header_console_icon_);
            redraw_control(header_console_status_);
        }
        if (previous_snapshot.validation.color != snapshot.validation.color || previous_snapshot.validation.text != snapshot.validation.text) {
            redraw_control(header_validation_icon_);
            redraw_control(header_validation_status_);
        }
        if (previous_snapshot.recorder.color != snapshot.recorder.color || previous_snapshot.recorder.text != snapshot.recorder.text) {
            redraw_control(header_recorder_icon_);
            redraw_control(header_recorder_status_);
        }
        if (previous_snapshot.midi.color != snapshot.midi.color || previous_snapshot.midi.text != snapshot.midi.text) {
            redraw_control(header_midi_icon_);
            redraw_control(header_midi_status_);
        }
        if (previous_snapshot.console_tab.color != snapshot.console_tab.color || previous_snapshot.console_tab.text != snapshot.console_tab.text) {
            redraw_control(tab_status_console_);
        }
        if (previous_snapshot.reaper_tab.color != snapshot.reaper_tab.color || previous_snapshot.reaper_tab.text != snapshot.reaper_tab.text) {
            redraw_control(tab_status_reaper_);
        }
        if (previous_snapshot.wing_tab.color != snapshot.wing_tab.color || previous_snapshot.wing_tab.text != snapshot.wing_tab.text) {
            redraw_control(tab_status_wing_);
        }
        if (previous_snapshot.control_tab.color != snapshot.control_tab.color || previous_snapshot.control_tab.text != snapshot.control_tab.text) {
            redraw_control(tab_status_control_);
        }
        if (previous_snapshot.pending_color != snapshot.pending_color || previous_snapshot.pending_summary != snapshot.pending_summary) {
            redraw_control(pending_summary_);
        }
        if (previous_snapshot.readiness_color != snapshot.readiness_color || previous_snapshot.readiness_detail != snapshot.readiness_detail) {
            redraw_control(readiness_detail_);
        }
        if (previous_snapshot.footer != snapshot.footer) {
            redraw_control(footer_status_);
        }
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
            ? L"Windows WINGuard now mirrors the macOS shell more closely: connection in the header, setup in Reaper, and recorder/control sections in their own tabs."
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

    void SyncPendingSettingsFromConfig() {
        const auto& config = ReaperExtension::Instance().GetConfig();
        pending_recorder_enabled_ = config.recorder_coordination_enabled;
        pending_recorder_target_usb_ = (config.recorder_target == "USBREC");
        pending_recorder_follow_ = config.sd_auto_record_with_reaper;
        pending_recorder_pair_left_ = std::clamp(std::max(1, config.sd_lr_left_input), 1, 7);
        if ((pending_recorder_pair_left_ % 2) == 0) {
            --pending_recorder_pair_left_;
        }
        pending_midi_actions_enabled_ = ReaperExtension::Instance().IsMidiActionsEnabled();
        pending_warning_layer_ = std::clamp(config.warning_flash_cc_layer, 1, 16);
        recorder_settings_dirty_ = false;
        midi_actions_dirty_ = false;
    }

    void SyncWingControlsFromPending() {
        CheckRadioButton(page_wing_, kIdRecorderEnableOff, kIdRecorderEnableOn,
                         pending_recorder_enabled_ ? kIdRecorderEnableOn : kIdRecorderEnableOff);
        CheckRadioButton(page_wing_, kIdRecorderTargetWLive, kIdRecorderTargetUsb,
                         pending_recorder_target_usb_ ? kIdRecorderTargetUsb : kIdRecorderTargetWLive);
        const int pair_id = (pending_recorder_pair_left_ <= 1) ? kIdRecorderPair1 :
                            (pending_recorder_pair_left_ <= 3) ? kIdRecorderPair3 :
                            (pending_recorder_pair_left_ <= 5) ? kIdRecorderPair5 :
                                                                kIdRecorderPair7;
        CheckRadioButton(page_wing_, kIdRecorderPair1, kIdRecorderPair7, pair_id);
        CheckRadioButton(page_wing_, kIdRecorderFollowOff, kIdRecorderFollowOn,
                         pending_recorder_follow_ ? kIdRecorderFollowOn : kIdRecorderFollowOff);
    }

    void SyncControlTabFromPending() {
        CheckRadioButton(page_control_, kIdMidiActionsOff, kIdMidiActionsOn,
                         pending_midi_actions_enabled_ ? kIdMidiActionsOn : kIdMidiActionsOff);
        SendMessageW(warning_layer_combo_, CB_SETCURSEL, std::max(0, pending_warning_layer_ - 1), 0);
    }

    void OnRecorderSettingsChanged() {
        pending_recorder_enabled_ = (IsDlgButtonChecked(page_wing_, kIdRecorderEnableOn) == BST_CHECKED);
        pending_recorder_target_usb_ = (IsDlgButtonChecked(page_wing_, kIdRecorderTargetUsb) == BST_CHECKED);
        pending_recorder_follow_ = (IsDlgButtonChecked(page_wing_, kIdRecorderFollowOn) == BST_CHECKED);
        pending_recorder_pair_left_ = (IsDlgButtonChecked(page_wing_, kIdRecorderPair7) == BST_CHECKED) ? 7 :
                                      (IsDlgButtonChecked(page_wing_, kIdRecorderPair5) == BST_CHECKED) ? 5 :
                                      (IsDlgButtonChecked(page_wing_, kIdRecorderPair3) == BST_CHECKED) ? 3 : 1;
        const auto& config = ReaperExtension::Instance().GetConfig();
        recorder_settings_dirty_ =
            pending_recorder_enabled_ != config.recorder_coordination_enabled ||
            pending_recorder_target_usb_ != (config.recorder_target == "USBREC") ||
            pending_recorder_follow_ != config.sd_auto_record_with_reaper ||
            pending_recorder_pair_left_ != std::max(1, config.sd_lr_left_input);
        footer_message_ = recorder_settings_dirty_
            ? L"Recorder coordination changes staged."
            : L"Recorder coordination matches the applied settings.";
        RefreshAll();
    }

    void OnApplyRecorderSettings() {
        auto& extension = ReaperExtension::Instance();
        auto& config = extension.GetConfig();
        config.recorder_coordination_enabled = pending_recorder_enabled_;
        config.sd_lr_route_enabled = pending_recorder_enabled_;
        config.recorder_target = pending_recorder_target_usb_ ? "USBREC" : "WLIVE";
        config.sd_auto_record_with_reaper = pending_recorder_follow_;
        config.sd_lr_group = "MAIN";
        config.sd_lr_left_input = pending_recorder_pair_left_;
        config.sd_lr_right_input = pending_recorder_pair_left_ + 1;
        SaveConfigIfPossible(extension);
        extension.ApplyAutoRecordSettings();
        if (extension.IsConnected() && config.recorder_coordination_enabled) {
            extension.ApplyRecorderRoutingNoDialog();
        }
        recorder_settings_dirty_ = false;
        footer_message_ = L"Recorder coordination settings applied.";
        RefreshAll();
    }

    void OnDiscardRecorderSettings() {
        SyncPendingSettingsFromConfig();
        SyncWingControlsFromPending();
        footer_message_ = L"Recorder coordination changes discarded.";
        RefreshAll();
    }

    void OnMidiActionsChanged() {
        pending_midi_actions_enabled_ = (IsDlgButtonChecked(page_control_, kIdMidiActionsOn) == BST_CHECKED);
        const LRESULT selection = SendMessageW(warning_layer_combo_, CB_GETCURSEL, 0, 0);
        pending_warning_layer_ = (selection >= 0) ? static_cast<int>(selection) + 1 : 1;
        auto& extension = ReaperExtension::Instance();
        const auto& config = extension.GetConfig();
        midi_actions_dirty_ =
            pending_midi_actions_enabled_ != extension.IsMidiActionsEnabled() ||
            pending_warning_layer_ != config.warning_flash_cc_layer;
        footer_message_ = midi_actions_dirty_
            ? L"Control integration changes staged."
            : L"Control integration matches the applied settings.";
        RefreshAll();
    }

    void OnApplyMidiActions() {
        auto& extension = ReaperExtension::Instance();
        extension.GetConfig().warning_flash_cc_layer = pending_warning_layer_;
        SaveConfigIfPossible(extension);
        extension.EnableMidiActions(pending_midi_actions_enabled_);
        if (pending_midi_actions_enabled_ && extension.IsConnected()) {
            extension.SyncMidiActionsToWing();
        }
        midi_actions_dirty_ = false;
        footer_message_ = L"Control integration settings applied.";
        RefreshAll();
    }

    void OnDiscardMidiActions() {
        SyncPendingSettingsFromConfig();
        SyncControlTabFromPending();
        footer_message_ = L"Control integration changes discarded.";
        RefreshAll();
    }

    void OnOpenDebugLog() {
        if (!debug_log_view_) {
            return;
        }
        SetFocus(debug_log_view_);
        const int length = GetWindowTextLengthW(debug_log_view_);
        SendMessageW(debug_log_view_, EM_SETSEL, length, length);
        SendMessageW(debug_log_view_, EM_SCROLLCARET, 0, 0);
        footer_message_ = L"Focused the diagnostics log.";
        RefreshAll();
    }

    void OnClearDebugLog() {
        {
            std::lock_guard<std::mutex> lock(log_buffer_mutex_);
            pending_log_buffer_.clear();
        }
        if (debug_log_view_) {
            SetWindowTextW(debug_log_view_, L"");
        }
        footer_message_ = L"Diagnostics log cleared.";
        RefreshAll();
    }

    HWND hwnd_ = nullptr;
    HWND banner_group_ = nullptr;
    HWND status_group_ = nullptr;
    HWND logo_ = nullptr;
    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND tab_ = nullptr;
    HWND page_frame_ = nullptr;
    HWND tab_button_console_ = nullptr;
    HWND tab_button_reaper_ = nullptr;
    HWND tab_button_wing_ = nullptr;
    HWND tab_button_control_ = nullptr;
    HWND header_console_icon_ = nullptr;
    HWND header_console_status_ = nullptr;
    HWND header_validation_icon_ = nullptr;
    HWND header_validation_status_ = nullptr;
    HWND header_recorder_icon_ = nullptr;
    HWND header_recorder_status_ = nullptr;
    HWND header_midi_icon_ = nullptr;
    HWND header_midi_status_ = nullptr;
    HWND page_console_ = nullptr;
    HWND page_reaper_ = nullptr;
    HWND page_wing_ = nullptr;
    HWND page_control_ = nullptr;
    HWND tab_status_console_ = nullptr;
    HWND tab_status_reaper_ = nullptr;
    HWND tab_status_wing_ = nullptr;
    HWND tab_status_control_ = nullptr;
    HWND console_intro_ = nullptr;
    HWND console_section_icon_ = nullptr;
    HWND console_section_header_ = nullptr;
    HWND console_label_ = nullptr;
    HWND console_help_discovery_ = nullptr;
    HWND console_manual_ip_label_ = nullptr;
    HWND console_help_manual_ = nullptr;
    HWND console_footer_ = nullptr;
    HWND reaper_intro_ = nullptr;
    HWND reaper_section_icon_ = nullptr;
    HWND reaper_section_header_ = nullptr;
    HWND reaper_output_label_ = nullptr;
    HWND reaper_output_help_ = nullptr;
    HWND reaper_toggle_help_ = nullptr;
    HWND auto_trigger_header_ = nullptr;
    HWND auto_trigger_section_icon_ = nullptr;
    HWND auto_trigger_detail_ = nullptr;
    HWND auto_trigger_hint_ = nullptr;
    HWND auto_trigger_enable_label_ = nullptr;
    HWND auto_trigger_enable_off_ = nullptr;
    HWND auto_trigger_enable_on_ = nullptr;
    HWND auto_trigger_monitor_label_ = nullptr;
    HWND auto_trigger_monitor_combo_ = nullptr;
    HWND auto_trigger_mode_label_ = nullptr;
    HWND auto_trigger_mode_warning_ = nullptr;
    HWND auto_trigger_mode_record_ = nullptr;
    HWND auto_trigger_threshold_label_ = nullptr;
    HWND auto_trigger_threshold_edit_ = nullptr;
    HWND auto_trigger_hold_label_ = nullptr;
    HWND auto_trigger_hold_edit_ = nullptr;
    HWND auto_trigger_meter_label_ = nullptr;
    HWND apply_auto_trigger_button_ = nullptr;
    HWND discard_auto_trigger_button_ = nullptr;
    HWND wing_intro_ = nullptr;
    HWND wing_section_icon_ = nullptr;
    HWND wing_section_header_ = nullptr;
    HWND wing_enable_label_ = nullptr;
    HWND wing_enable_off_ = nullptr;
    HWND wing_enable_on_ = nullptr;
    HWND wing_target_label_ = nullptr;
    HWND wing_target_wlive_ = nullptr;
    HWND wing_target_usb_ = nullptr;
    HWND wing_pair_label_ = nullptr;
    HWND wing_pair_1_ = nullptr;
    HWND wing_pair_3_ = nullptr;
    HWND wing_pair_5_ = nullptr;
    HWND wing_pair_7_ = nullptr;
    HWND wing_follow_label_ = nullptr;
    HWND wing_follow_off_ = nullptr;
    HWND wing_follow_on_ = nullptr;
    HWND apply_recorder_button_ = nullptr;
    HWND discard_recorder_button_ = nullptr;
    HWND control_intro_ = nullptr;
    HWND control_section_icon_ = nullptr;
    HWND control_section_header_ = nullptr;
    HWND control_enable_label_ = nullptr;
    HWND midi_actions_off_ = nullptr;
    HWND midi_actions_on_ = nullptr;
    HWND midi_summary_ = nullptr;
    HWND midi_detail_ = nullptr;
    HWND warning_layer_label_ = nullptr;
    HWND warning_layer_combo_ = nullptr;
    HWND apply_midi_button_ = nullptr;
    HWND discard_midi_button_ = nullptr;
    HWND support_section_header_ = nullptr;
    HWND support_detail_ = nullptr;
    HWND open_debug_log_button_ = nullptr;
    HWND clear_debug_log_button_ = nullptr;
    HWND debug_log_view_ = nullptr;
    HWND wing_placeholder_body_ = nullptr;
    HWND control_placeholder_body_ = nullptr;
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
    HFONT section_font_ = nullptr;
    HFONT tab_font_ = nullptr;
    HFONT subtle_font_ = nullptr;
    HFONT mono_font_ = nullptr;
    HFONT icon_font_ = nullptr;
    HBRUSH banner_brush_ = nullptr;
    HBRUSH status_panel_brush_ = nullptr;
    HBRUSH body_brush_ = nullptr;
    HBRUSH border_brush_ = nullptr;
    RECT banner_rect_{};
    RECT status_panel_rect_{};
    PageLayoutState console_page_state_;
    PageLayoutState reaper_page_state_;
    PageLayoutState wing_page_state_;
    PageLayoutState control_page_state_;
    std::array<PageContext, 4> page_contexts_{};
    std::vector<WingInfo> discovered_wings_;
    std::vector<SourceSelectionInfo> pending_setup_channels_;
    std::wstring pending_output_mode_;
    bool has_pending_setup_draft_ = false;
    bool pending_setup_soundcheck_ = true;
    bool pending_replace_existing_ = true;
    bool pending_auto_record_enabled_ = false;
    bool pending_auto_record_warning_only_ = false;
    double pending_auto_record_threshold_db_ = -40.0;
    int pending_auto_record_hold_ms_ = 3000;
    int pending_auto_record_monitor_track_ = 0;
    bool auto_trigger_dirty_ = false;
    ValidationState latest_validation_state_ = ValidationState::NotReady;
    std::string latest_validation_details_;
    std::wstring footer_message_;
    StatusSnapshot current_snapshot_;
    int current_tab_index_ = 0;
    bool pending_recorder_enabled_ = false;
    bool pending_recorder_target_usb_ = false;
    bool pending_recorder_follow_ = false;
    int pending_recorder_pair_left_ = 1;
    bool recorder_settings_dirty_ = false;
    bool pending_midi_actions_enabled_ = false;
    int pending_warning_layer_ = 1;
    bool midi_actions_dirty_ = false;
    std::mutex log_buffer_mutex_;
    std::wstring pending_log_buffer_;
};

WingConnectorWindowsDialog* WingConnectorWindowsDialog::instance_ = nullptr;

}  // namespace

extern "C" void ShowWingConnectorDialogWindows() {
    WingConnectorWindowsDialog::Show();
}

#endif
