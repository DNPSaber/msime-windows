// 悬浮工具栏宿主：按配置计算尺寸、拖动后收回可见屏幕、随 DPI 重新布局与放置、
// 显隐决策与首帧绘制宽限，以及 WndProcFtbWindow。
#include "window/ime_windows_internal.h"
#include "global/globals.h"
#include "config/ime_config.h"
#include "ipc/ipc.h"
#include "ime_windows.h"
#include "window/floating_toolbar_presenter.h"
#include "defines/defines.h"
#include "defines/globals.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <fmt/xchar.h>
#include "webview2/windows_webview2.h"
#include "utils/webview_utils.h"
#include "utils/window_utils.h"
#include "window/floating_toolbar_visibility_policy.h"
#include "log/ftb_diag_log.h"
#include <windowsx.h>

using namespace ime_windows_detail;

namespace ime_windows_detail
{
int ConfiguredFloatingToolbarWidth()
{
    const FloatingToolbarItemsConfig &items = GetConfiguredFloatingToolbarItems();
    const int optional_item_count = static_cast<int>(items.fullwidth) + static_cast<int>(items.punctuation) +
                                    static_cast<int>(items.character_set) + static_cast<int>(items.emoji) +
                                    static_cast<int>(items.screen_keyboard) + static_cast<int>(items.settings);
    // 59 DIPs covers the drag handle, divider, borders, mandatory CN/EN button and
    // container padding. Each optional button adds 24 DIPs plus the 6-DIP gap.
    // User scale/font_size multiply the design size independently of system DPI.
    const double userScale = GetConfiguredFloatingToolbarScale();
    const double fontFactor = static_cast<double>(GetConfiguredFloatingToolbarFontSize()) / 24.0;
    return static_cast<int>(std::lround((59 + optional_item_count * 30) * userScale * fontFactor));
}

int ConfiguredFloatingToolbarHeight()
{
    const double userScale = GetConfiguredFloatingToolbarScale();
    const double fontFactor = static_cast<double>(GetConfiguredFloatingToolbarFontSize()) / 24.0;
    return static_cast<int>(std::lround(35.0 * userScale * fontFactor));
}

bool FloatingToolbarItemsEqual(const FloatingToolbarItemsConfig &left, const FloatingToolbarItemsConfig &right)
{
    return left.fullwidth == right.fullwidth && left.punctuation == right.punctuation &&
           left.character_set == right.character_set && left.emoji == right.emoji &&
           left.screen_keyboard == right.screen_keyboard && left.settings == right.settings;
}
} // namespace ime_windows_detail

namespace
{
BOOL CALLBACK AddMonitorWorkAreaToRegion(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto visibleRegion = reinterpret_cast<HRGN>(data);
    if (!visibleRegion)
    {
        return FALSE;
    }

    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfo(monitor, &info))
    {
        return TRUE;
    }

    HRGN workAreaRegion = CreateRectRgnIndirect(&info.rcWork);
    if (workAreaRegion)
    {
        CombineRgn(visibleRegion, visibleRegion, workAreaRegion, RGN_OR);
        DeleteObject(workAreaRegion);
    }
    return TRUE;
}

bool IsRectInsideVisibleMonitorWorkAreas(const RECT &rect)
{
    HRGN visibleRegion = CreateRectRgn(0, 0, 0, 0);
    HRGN windowRegion = CreateRectRgnIndirect(&rect);
    HRGN outsideRegion = CreateRectRgn(0, 0, 0, 0);
    if (!visibleRegion || !windowRegion || !outsideRegion)
    {
        if (visibleRegion)
            DeleteObject(visibleRegion);
        if (windowRegion)
            DeleteObject(windowRegion);
        if (outsideRegion)
            DeleteObject(outsideRegion);
        return true;
    }

    EnumDisplayMonitors(nullptr, nullptr, AddMonitorWorkAreaToRegion, reinterpret_cast<LPARAM>(visibleRegion));
    const int outsideType = CombineRgn(outsideRegion, windowRegion, visibleRegion, RGN_DIFF);
    DeleteObject(outsideRegion);
    DeleteObject(windowRegion);
    DeleteObject(visibleRegion);
    return outsideType == NULLREGION;
}

void KeepFloatingToolbarInsideVisibleScreens(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) || IsRectInsideVisibleMonitorWorkAreas(rect))
    {
        return;
    }

    // MonitorFromRect chooses the monitor with the largest intersection, or the
    // nearest monitor when the toolbar was dragged completely beyond all screens.
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!monitor || !GetMonitorInfo(monitor, &info))
    {
        return;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int workLeft = static_cast<int>(info.rcWork.left);
    const int workTop = static_cast<int>(info.rcWork.top);
    const int workRight = static_cast<int>(info.rcWork.right);
    const int workBottom = static_cast<int>(info.rcWork.bottom);
    const int oldX = static_cast<int>(rect.left);
    const int oldY = static_cast<int>(rect.top);
    const int maxX = (std::max)(workLeft, workRight - width);
    const int maxY = (std::max)(workTop, workBottom - height);
    const int x = (std::max)(workLeft, (std::min)(oldX, maxX));
    const int y = (std::max)(workTop, (std::min)(oldY, maxY));
    if (x == rect.left && y == rect.top)
    {
        return;
    }

    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SyncHostWebViewBounds(::webviewControllerFtbWnd.Get(), hwnd);
    FTB_DIAG_LOGF(L"ftb drag clamped from ({},{}) to ({},{}) work=({},{})-({},{})", rect.left, rect.top, x, y,
                  info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom);
}

// Recompute FTB outer HWND from design DIPs * current DPI. Placement used to be
// SWP_NOSIZE-only, so a live display-scale change left the host stuck at the
// create-time physical size while WebView content grew.
//
// reset_to_default_corner=true snaps to the primary-monitor bottom-right slot
// (startup / first layout). false keeps the current top-left so IME activate,
// fullscreen exit, and config toggles do not undo a user drag.
//
// scaleOverride>0: use that factor (WM_DPICHANGED's wParam) instead of
// GetWindowScale, which can briefly lag the message's new DPI.
void LayoutFloatingToolbar(HWND hwnd, bool reset_to_default_corner, FLOAT scaleOverride = 0.0f)
{
    if (!hwnd)
    {
        return;
    }
    const bool d2d = FloatingToolbarPresenter::Instance().IsBound();
    if (::FTB_CONTENT_WIDTH_DIP > 1.0 && ::FTB_CONTENT_HEIGHT_DIP > 1.0)
    {
        // ceil + 1 DIP pad: fractional CSS sizes rounded down in physical px make
        // the WebView viewport slightly smaller than .status-bar and Chromium
        // adds both scrollbars (seen after live DPI changes).
        const int pad = d2d ? 0 : 1;
        ::FTB_WND_WIDTH = static_cast<int>(std::ceil(::FTB_CONTENT_WIDTH_DIP)) + pad;
        ::FTB_WND_HEIGHT = static_cast<int>(std::ceil(::FTB_CONTENT_HEIGHT_DIP)) + pad;
    }
    else
    {
        ::FTB_WND_WIDTH = ConfiguredFloatingToolbarWidth();
        ::FTB_WND_HEIGHT = ConfiguredFloatingToolbarHeight();
    }
    const FLOAT nativeScale = GetWindowScale(hwnd);
    const FLOAT rasterScale = d2d ? nativeScale : GetWebViewRasterizationScale(hwnd);
    const FLOAT textScale = !d2d && nativeScale > 0.0f ? rasterScale / nativeScale : 1.0f;
    FLOAT scale = scaleOverride > 0.0f ? scaleOverride * textScale : rasterScale;
    if (scale <= 0.0f)
    {
        scale = 1.0f;
    }
    // Main-path half-screen cap so HTML wrap/scroll and HWND agree before show.
    HalfScreenDipLimits limits = QueryWebViewHalfScreenDipLimitsForHwnd(hwnd);
    if (scaleOverride > 0.0f)
    {
        const double monitorWidthPx = static_cast<double>((std::max)(1, limits.monitor.right - limits.monitor.left));
        const double monitorHeightPx = static_cast<double>((std::max)(1, limits.monitor.bottom - limits.monitor.top));
        limits.scale = scale;
        limits.maxWidthDip = (monitorWidthPx * 0.5) / static_cast<double>(scale);
        limits.maxHeightDip = (monitorHeightPx * 0.5) / static_cast<double>(scale);
    }
    ::FTB_WND_WIDTH =
        static_cast<int>(std::ceil(ClampWidthDipToHalfScreen(static_cast<double>(::FTB_WND_WIDTH), limits)));
    ::FTB_WND_HEIGHT =
        static_cast<int>(std::ceil(ClampHeightDipToHalfScreen(static_cast<double>(::FTB_WND_HEIGHT), limits)));
    const int shadowWidth = d2d ? 0 : ::FTB_WND_SHADOW_WIDTH;
    const int width = static_cast<int>(std::ceil((::FTB_WND_WIDTH + shadowWidth) * static_cast<double>(scale)));
    const int height = static_cast<int>(std::ceil((::FTB_WND_HEIGHT + shadowWidth) * static_cast<double>(scale)));
    const int cornerInset = static_cast<int>(std::lround(10.0 * static_cast<double>(scale)));
    int posX = 0;
    int posY = 0;
    if (reset_to_default_corner)
    {
        MonitorCoordinates coordinates = GetMainMonitorCoordinates();
        const int taskbarHeight = GetTaskbarHeight();
        posX = coordinates.right - width - cornerInset;
        posY = coordinates.bottom - height - taskbarHeight - cornerInset;
    }
    else
    {
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        posX = rc.left;
        posY = rc.top;

        // When optional buttons are added, the toolbar grows to the right from
        // the existing top-left. Clamp it back onto the current monitor so the
        // resized host stays fully reachable.
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(monitorInfo)};
        if (monitor && GetMonitorInfo(monitor, &monitorInfo))
        {
            const int monitorLeft = static_cast<int>(monitorInfo.rcMonitor.left);
            const int monitorTop = static_cast<int>(monitorInfo.rcMonitor.top);
            const int maxX = static_cast<int>(monitorInfo.rcMonitor.right) - width;
            const int maxY = static_cast<int>(monitorInfo.rcMonitor.bottom) - height;
            posX = (std::max)(monitorLeft, (std::min)(posX, maxX));
            posY = (std::max)(monitorTop, (std::min)(posY, maxY));
        }
    }
    // Never touch Z-order here: HWND_TOP would cover an open tray menu. Topmost
    // for the toolbar is owned by EnsureSmallWindowsTopmost / lazy pin order.
    SetLastError(0);
    const BOOL ok = SetWindowPos(hwnd, nullptr, posX, posY, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!d2d)
    {
        SyncHostWebViewBounds(::webviewControllerFtbWnd.Get(), hwnd);
        InjectSurfaceViewportLimits(::webviewFtbWnd.Get(), hwnd);
    }
    else
    {
        FloatingToolbarPresenter::Instance().Present();
    }
    (void)ok;
}

void ScheduleFloatingToolbarDpiRemeasure(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }
    KillTimer(hwnd, TIMER_ID_FTB_DPI_REMEASURE);
    SetTimer(hwnd, TIMER_ID_FTB_DPI_REMEASURE, 1, nullptr);
}

void PlaceFloatingToolbarOnScreen(HWND hwnd)
{
    LayoutFloatingToolbar(hwnd, true);
}
} // namespace

void ApplyConfiguredFloatingToolbarVisibility(const wchar_t *reason)
{
    if (!::global_hwnd_ftb)
    {
        FTB_DIAG_LOGF(L"apply reason={} skipped: toolbar window not created yet", reason);
        return;
    }
    // IPC ownership and this flag can only disagree in one direction. Terminal
    // deactivation clears the active client, so a live client alongside an
    // inactive flag is never legitimate: it means a WM_IMEACTIVATE edge was
    // lost, most often because the activation raced candidate-window creation.
    // Nothing else re-evaluates visibility afterwards, so the toolbar would stay
    // hidden until the user changed focus again.
    const uint64_t active_client = GetActivePipeClient().client_id;
    if (active_client != 0 && !g_is_ime_active)
    {
        FTB_DIAG_LOGF(L"reconcile reason={} active_client={} repairs ime_active false -> true", reason, active_client);
        g_is_ime_active = true;
    }

    const HWND foreground = GetForegroundWindow();
    const bool fullscreen = foreground && CheckFullscreen(foreground);
    const bool configured = GetConfiguredFloatingToolbarEnabled();
    const bool should_show = FanyImeUi::ShouldShowFloatingToolbar(configured, fullscreen, g_is_ime_active);
    const bool is_visible = IsWindowVisible(::global_hwnd_ftb) != FALSE;
    const bool paint_grace = IsFloatingToolbarPaintGraceActive();
    // First successful navigation: show briefly so cold WebView2 can paint, then
    // reconcile. Do not jump straight to SW_HIDE when IME is not active yet.
    const bool start_paint_grace =
        reason && wcscmp(reason, L"ftb-navigation-completed") == 0 && IsFloatingToolbarWebviewReady();

    // Each host-state snapshot crosses into DWM and walks the child windows, and
    // a burst of queued activations can drive hundreds of applies through here on
    // the UI thread. Collapsing identical consecutive records keeps the trace
    // complete without letting the diagnostics starve WebView2 initialisation.
    bool trace = false;
    if (::DiagnosticLog::IsEnabled())
    {
        // Every input of the decision, so a blank toolbar can be attributed to a
        // specific term rather than guessed at. Cloak and webview readiness
        // matter more than IsWindowVisible once the host is warmed up.
        std::wstring decision =
            fmt::format(L"apply reason={} active_client={} ime_active={} configured={} fullscreen={} "
                        L"should_show={} was_visible={} was_cloaked={} webview_ready={} paint_grace={}",
                        reason, active_client, g_is_ime_active, configured, fullscreen, should_show, is_visible,
                        IsHostWindowCloaked(::global_hwnd_ftb), IsFloatingToolbarWebviewReady(), paint_grace);

        // Applies are confined to the UI thread, so plain statics suffice.
        static std::wstring last_decision;
        static unsigned long long repeats = 0;
        trace = decision != last_decision;
        if (!trace)
        {
            ++repeats;
        }
        else
        {
            if (repeats != 0)
            {
                FTB_DIAG_LOGF(L"  (preceding apply record repeated {} more times)", repeats);
                repeats = 0;
            }
            last_decision = decision;
            ::DiagnosticLog::Write(decision);
            // The decision above only explains a hidden toolbar. A blank one
            // needs the host and controller state, which nothing else reports.
            FTB_DIAG_LOGF(L"  state before reason={} {}", reason, DescribeFloatingToolbarHostState());
        }
    }
    // Whatever the decision was, record what it actually produced. A show that
    // leaves the state unchanged is the failure being hunted here.
    struct StateAfter
    {
        const wchar_t *reason;
        bool trace;
        ~StateAfter()
        {
            if (trace)
            {
                FTB_DIAG_LOGF(L"  state after  reason={} {}", reason, DescribeFloatingToolbarHostState());
            }
        }
    } state_after{reason, trace};

    const bool force_show = should_show || start_paint_grace || paint_grace;
    if (force_show)
    {
        // Keep the dragged position across IME activate / config / fullscreen
        // exit. Only resize for the current DPI.
        LayoutFloatingToolbar(::global_hwnd_ftb, false);
        EnsureSmallWindowsTopmost(L"show-floating-toolbar");
        // Ensure may pin FTB last while the tray menu is open; put the menu
        // back on top without a visible flash. IsWindowVisible() cannot express
        // "open" here: the menu host spends all of startup visible-but-cloaked
        // for warmup, and raising it in that state is what leaves it blank.
        if (IsTrayMenuOpenToUser())
        {
            RaiseTrayMenuAboveSmallWindows(L"after-show-floating-toolbar");
        }
        if (!is_visible)
        {
            ShowWindow(::global_hwnd_ftb, SW_SHOWNA);
        }
        UpdateSmallWindowWebviewVisibility(::global_hwnd_ftb, true);
        // Reveal only once there is something to reveal. Before the first
        // navigation completes the host is deliberately visible-but-cloaked for
        // warmup, and uncloaking it there would just park an empty transparent
        // rectangle on screen. The navigation-completed apply repeats this call.
        if (IsFloatingToolbarWebviewReady())
        {
            SetHostWindowCloaked(::global_hwnd_ftb, false);
        }
        if (start_paint_grace)
        {
            BeginFloatingToolbarPaintGrace();
            KillTimer(::global_hwnd_ftb, TIMER_ID_FTB_VISIBILITY_RECONCILE);
            // Fallback only: page posts type=ready to reconcile earlier.
            SetTimer(::global_hwnd_ftb, TIMER_ID_FTB_VISIBILITY_RECONCILE, kFloatingToolbarPaintGraceMs, nullptr);
            FTB_DIAG_LOGF(L"ftb paint grace started; waiting for page ready (fallback {}ms)",
                          kFloatingToolbarPaintGraceMs);
        }
    }
    else if (is_visible)
    {
        HideFloatingToolbarHost();
    }
}

void ReconcileFloatingToolbarVisibilityAfterReady(const wchar_t *reason)
{
    if (!::global_hwnd_ftb)
    {
        return;
    }
    KillTimer(::global_hwnd_ftb, TIMER_ID_FTB_VISIBILITY_RECONCILE);
    if (IsFloatingToolbarPaintGraceActive())
    {
        EndFloatingToolbarPaintGrace();
        FTB_DIAG_LOGF(L"ftb paint grace ended reason={}", reason ? reason : L"unspecified");
    }
    ApplyConfiguredFloatingToolbarVisibility(reason ? reason : L"ftb-ready");
}

// Hiding the host before its WebView2 has painted once is what leaves the
// toolbar permanently blank, so during warmup the hide is expressed as a cloak:
// invisible to the user, still on-monitor and "visible" to WebView2. Until the
// page reports ready (or the fallback timer fires), skip hide so first paint can
// finish and the real visibility decision can be applied afterwards.
void HideFloatingToolbarHost()
{
    if (!::global_hwnd_ftb)
    {
        return;
    }
    // The fullscreen hook calls this directly, so without a line here the trace
    // shows a toolbar that became hidden with no apply to account for it.
    if (FanyImeUi::ShouldDeferFloatingToolbarHide(IsFloatingToolbarPaintGraceActive()))
    {
        FTB_DIAG_LOGF(L"hide host skipped: paint grace active");
        return;
    }
    FTB_DIAG_LOGF(L"hide host webview_ready={} (cloak-only while warming up)", IsFloatingToolbarWebviewReady());
    if (!IsFloatingToolbarWebviewReady())
    {
        SetHostWindowCloaked(::global_hwnd_ftb, true);
        return;
    }
    ShowWindow(::global_hwnd_ftb, SW_HIDE);
    UpdateSmallWindowWebviewVisibility(::global_hwnd_ftb, false);
}

void ApplyConfiguredFloatingToolbarSize()
{
    if (FloatingToolbarPresenter::Instance().IsBound())
    {
        FloatingToolbarPresenter::Instance().ApplyAppearance();
        return;
    }
    ::FTB_WND_WIDTH = ConfiguredFloatingToolbarWidth();
    ::FTB_WND_HEIGHT = ConfiguredFloatingToolbarHeight();
    LayoutFloatingToolbar(::global_hwnd_ftb, false);

    if (!::webviewFtbWnd || !::global_hwnd_ftb)
    {
        return;
    }
    // Wait for CSS vars to land, then measure .status-bar so HWND matches content.
    ApplyConfiguredFloatingToolbarAppearance([]() {
        GetContainerSizeFtb(::webviewFtbWnd, [](std::pair<double, double> size) {
            if (size.first > 1.0 && size.second > 1.0)
            {
                ::FTB_CONTENT_WIDTH_DIP = size.first;
                ::FTB_CONTENT_HEIGHT_DIP = size.second;
                ::FTB_WND_WIDTH = static_cast<int>(std::ceil(size.first));
                ::FTB_WND_HEIGHT = static_cast<int>(std::ceil(size.second));
            }
            LayoutFloatingToolbar(::global_hwnd_ftb, false);
        });
    });
}

LRESULT CALLBACK WndProcFtbWindow(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // The floating toolbar is persistent, so it cannot rely on being re-themed
    // on the next show (unlike the tray menu, which re-themes on every open).
    if (IsSystemLightDarkToggle(message, lParam) && FloatingToolbarPresenter::Instance().IsBound())
    {
        FloatingToolbarPresenter::Instance().ApplyTheme();
    }

    if (message == WM_NCHITTEST && FloatingToolbarPresenter::Instance().IsBound())
    {
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &client);
        if (FloatingToolbarPresenter::Instance().HitCaptionDrag(client))
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    if (FloatingToolbarPresenter::Instance().HandleMessage(message, wParam, lParam))
    {
        return message == WM_ERASEBKGND ? 1 : 0;
    }

    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case UPDATE_FTB_STATUS: {
        int capsLockState = (wParam >> 3) & 0x1;
        int cnEnState = (wParam >> 2) & 0x1;
        int doubleSingleByteState = (wParam >> 1) & 0x1;
        int puncState = wParam & 0x1;
        UpdateFtbCnEnAndDoubleSingleAndPuncState(::webviewFtbWnd, cnEnState, doubleSingleByteState, puncState,
                                                 capsLockState);
        break;
    }

    case UPDATE_FTB_ENGLISH_INPUT_MODE:
        UpdateFtbEnglishInputModeState(::webviewFtbWnd, wParam != 0 ? 1 : 0);
        break;

    case UPDATE_FTB_CAPS_LOCK:
        UpdateFtbCapsLockState(::webviewFtbWnd, wParam != 0 ? 1 : 0);
        BroadcastToTsfWorkerThreadViaNamedpipe(Global::DataFromServerMsgTypeToTsfWorkerThread::CapsLockChanged,
                                               wParam != 0 ? L"1" : L"0");
        break;

    case WM_EXITSIZEMOVE:
        // Native caption dragging runs a modal move loop. Clamp only after that
        // loop ends so movement remains smooth and crossing to another monitor
        // is never blocked.
        KeepFloatingToolbarInsideVisibleScreens(hwnd);
        return 0;

    case WM_DPICHANGED: {
        const FLOAT scale = HIWORD(wParam) / 96.0f;
        if (FloatingToolbarPresenter::Instance().IsBound())
        {
            FloatingToolbarPresenter::Instance().RelayoutHost(scale);
            ScheduleFloatingToolbarDpiRemeasure(hwnd);
            return 0;
        }
        // Same contract as the tray menu: size from DIPs * the DPI in wParam
        // (GetWindowScale can lag), then remeasure once WebView2 settles so a
        // fractional undersize cannot leave Chromium scrollbars over 中/简.
        ::FTB_CONTENT_WIDTH_DIP = 0.0;
        ::FTB_CONTENT_HEIGHT_DIP = 0.0;
        LayoutFloatingToolbar(hwnd, false, scale > 0.0f ? scale : 0.0f);
        ScheduleFloatingToolbarDpiRemeasure(hwnd);
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        // Resolution-only switches may not send WM_DPICHANGED. Reclamp + remasure.
        ScheduleFloatingToolbarDpiRemeasure(hwnd);
        return 0;
    }

    case WM_SIZE: {
        if (::webviewControllerFtbWnd && wParam != SIZE_MINIMIZED)
        {
            SyncHostWebViewBounds(::webviewControllerFtbWnd.Get(), hwnd);
        }
        break;
    }

    case WM_TIMER: {
        if (wParam == TIMER_ID_MOVE_WEBVIEW_FTB)
        {
            KillTimer(hwnd, TIMER_ID_MOVE_WEBVIEW_FTB);
            // Host HWND placement must not wait for WebView2. After reboot /
            // uiAccess, WebView can lag for a long time while the toolbar would
            // otherwise stay off-screen or never become discoverable.
            PlaceFloatingToolbarOnScreen(hwnd);
            break;
        }
        if (wParam == TIMER_ID_FTB_VISIBILITY_RECONCILE)
        {
            KillTimer(hwnd, TIMER_ID_FTB_VISIBILITY_RECONCILE);
            ReconcileFloatingToolbarVisibilityAfterReady(L"ftb-ready-fallback-timeout");
            break;
        }
        if (wParam == TIMER_ID_FTB_DPI_REMEASURE)
        {
            KillTimer(hwnd, TIMER_ID_FTB_DPI_REMEASURE);
            if (FloatingToolbarPresenter::Instance().IsBound())
            {
                FloatingToolbarPresenter::Instance().RelayoutHost();
            }
            else if (::webviewFtbWnd)
            {
                ApplyConfiguredFloatingToolbarSize();
            }
            else
            {
                SetTimer(hwnd, TIMER_ID_FTB_DPI_REMEASURE, 100, nullptr);
            }
            break;
        }
        break;
    }
    default: {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    }
    return 0;
}
