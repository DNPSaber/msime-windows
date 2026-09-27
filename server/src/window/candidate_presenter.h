#pragma once

#include "utils/window_utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <windows.h>

class CandidatePresenter
{
  public:
    static CandidatePresenter &Instance();

    bool Bind(HWND hwnd);
    bool IsBound() const;
    void ShowFromGlobalState();
    void ShowFromGlobalState(POINT caret);
    void Hide();
    void Present();
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  private:
    CandidatePresenter();
    ~CandidatePresenter();
    CandidatePresenter(const CandidatePresenter &) = delete;
    CandidatePresenter &operator=(const CandidatePresenter &) = delete;

    void RebuildScene();
    void ApplySkin();
    // Returns the generation of the snapshot that was actually rendered, so the caller can echo it
    // back to Global::rendered_candidate_page_generation once the paint is on screen.
    std::uint64_t FillItemsFromUi();
    // `scale` carries the scale resolved once per show in ShowFromGlobalState
    // so measure, clamping, sizing and rendering all share one source; an
    // unset (scale == 0) value makes PlaceAndShow resolve it itself.
    void PlaceAndShow(POINT caret, float widthDip, float heightDip, float cardLeftDip, float cardTopDip,
                      const ResolvedCandidateScale &scale = {});
    void ArmHoverIfPointerMoved();
    void CommitItem(size_t pageIndex);
    void ShowItemContextMenu(size_t pageIndex, POINT clientPoint);
    void CloseContextMenu(bool restoreHost);
    void OpenFixSubmenu();
    void CloseFixSubmenu();
    void ExpandHostForMenu(POINT clientPoint);
    void RestoreHostAfterMenu();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    HWND hwnd_ = nullptr;
    bool bound_ = false;
    bool hoverArmed_ = false;
    bool ignoreSelectionCallback_ = false;
    POINT hoverBaseline_{};
    float decorationTopDip_ = 0.0f;
    float decorationWidthDip_ = 0.0f;
    std::string lastSkinFingerprint_;
    int lastHostWidthPx_ = 0;
    int lastHostHeightPx_ = 0;
    float lastLayoutWidthDip_ = 0.0f;
    float lastLayoutHeightDip_ = 0.0f;
    // Largest card size kept for the current composition; reset by Hide().
    float stickyCardWidthDip_ = 0.0f;
    float stickyCardHeightDip_ = 0.0f;
    int wheelDeltaAccumulator_ = 0;
};
