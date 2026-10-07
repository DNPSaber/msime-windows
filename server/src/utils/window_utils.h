#pragma once

#include "defines/base_structures.h"
#include <memory>
#include <utility>
#include <windows.h>

FLOAT GetWindowScale(HWND);
FLOAT GetForegroundWindowScale();
// Prefer the monitor that contains the caret / composition anchor. Foreground
// HWND can disagree with the caret on Office extended-display setups.
FLOAT GetScaleForPoint(POINT pt);

// Where the candidate scale came from — diagnostic logging must be able to
// tell the RDP foreground path apart from the plain monitor path.
enum class CandidateScaleSource
{
    Monitor,       // caret's monitor DPI (local sessions, and RDP fallback)
    RdpForeground, // foreground window DPI inside an RDP session
};

struct ResolvedCandidateScale
{
    FLOAT scale = 0.0f;
    CandidateScaleSource source = CandidateScaleSource::Monitor;
};

// Candidate window scale authority. RDP syncs the client's display scaling
// into the session's monitor DPI metadata (150% client -> 144), while the
// focused application in the session usually still renders at 96 DPI, so the
// monitor metadata is unreliable there and GetDpiForWindow(foreground) is the
// best proxy for what the user actually sees:
//   - DPI-unaware / System Aware host -> virtualized 96, matches the app;
//   - PMv2 and correctly following -> current monitor DPI, matches the app;
//   - PMv2 but not following (pathological) -> mismatch, cannot be detected
//     via public APIs; documented as a known residue, not probed.
// Local sessions keep the caret-monitor convention (mixed-DPI multi-monitor
// setups depend on it). Falls back to the monitor path when the foreground
// window is gone (lock screen, focus switch) so behavior matches pre-fix.
ResolvedCandidateScale ResolveCandidateScaleForCaret(POINT caret);
// Candidate placement uses that monitor's work area to avoid taskbars/app bars.
MonitorCoordinates GetMonitorCoordinatesFromPoint(POINT pt);

MonitorCoordinates GetMonitorCoordinates();
MonitorCoordinates GetMainMonitorCoordinates();
int GetTaskbarHeight();

// Half of the target monitor in CSS DIPs (physical/2 / dpiScale). Single source
// of truth for FTB / menu / candidate max content size.
struct HalfScreenDipLimits
{
    FLOAT scale = 1.0f;
    double maxWidthDip = 0.0;
    double maxHeightDip = 0.0;
    MonitorCoordinates monitor{};
};
HalfScreenDipLimits QueryHalfScreenDipLimitsForHwnd(HWND hwnd);
HalfScreenDipLimits QueryHalfScreenDipLimitsForPoint(POINT pt);
double ClampWidthDipToHalfScreen(double widthDip, const HalfScreenDipLimits &limits);
double ClampHeightDipToHalfScreen(double heightDip, const HalfScreenDipLimits &limits);

// Host placement on mixed-DPI multi-monitor setups (floating toolbar, menus).
// MonitorFromWindow follows the monitor the window currently sits on, so
// clamping against it pins the host to the screen it came from and makes a
// caption drag across a seam bounce back. These helpers treat the union of
// every monitor's work area as the visible region: the host may straddle a
// seam, and is only pulled back when it would leave that region entirely.
bool IsRectInsideVisibleMonitorWorkAreas(const RECT &rect);
// Fits `rect` (size preserved) into the work area of the monitor nearest to the
// rect's center, so a host that fell past every screen comes back on the side
// the user was heading for. Returns true when the rect was moved; `monitor`
// optionally receives the monitor that was used.
bool ClampRectIntoNearestMonitorWorkArea(RECT &rect, HMONITOR *monitor = nullptr);

int AdjustCandidateWindowPosition(        //
    const POINT *point,                   //
    const std::pair<double, double> &,    //
    std::shared_ptr<std::pair<int, int>>, //
    FLOAT layoutScale = 0.0f,             //
    double minWidthDip = 0.0              //
);

// Drop the "tallest list so far" flip memory. A bogus oversized measure would
// otherwise keep parking later cards at the top/left of the monitor.
void ResetCandidatePlacementMemory();

int AdjustWndPosition( //
    HWND hwnd,         //
    int crateX,        //
    int crateY,        //
    int width,         //
    int height,        //
    int properPos[2]   //
);
