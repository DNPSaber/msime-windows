// PagerArrows: two small stroke-drawn chevron buttons ("previous" / "next") for paging a list.
#include "msimeui/Controls.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Window.h"

#include "ControlsInternal.h"

#include <algorithm>
#include <utility>

namespace msimeui
{
using namespace controls_detail;

void PagerArrows::SetAppearance(Appearance appearance)
{
    appearance_ = std::move(appearance);
    InvalidateMeasure();
    InvalidateVisual();
}

void PagerArrows::SetEnabled(bool previous, bool next)
{
    if (previousEnabled_ == previous && nextEnabled_ == next)
    {
        return;
    }
    previousEnabled_ = previous;
    nextEnabled_ = next;
    if (!IsPartEnabled(pressed_))
    {
        pressed_ = Part::None;
    }
    InvalidateVisual();
}

bool PagerArrows::IsPartEnabled(Part part) const
{
    return (part == Part::Previous && previousEnabled_) || (part == Part::Next && nextEnabled_);
}

void PagerArrows::SetOnClick(ClickHandler handler)
{
    onClick_ = std::move(handler);
}

void PagerArrows::SetHoverEnabled(bool enabled)
{
    if (hoverEnabled_ == enabled)
    {
        return;
    }
    hoverEnabled_ = enabled;
    if (!enabled && hovered_ != Part::None)
    {
        hovered_ = Part::None;
        InvalidateVisual();
    }
}

RectF PagerArrows::GetPartBounds(Part part) const
{
    // The buttons sit at the trailing bottom of the arranged box, so an arranged
    // box taller or wider than the two buttons keeps them in that corner.
    const float width = std::min(appearance_.buttonWidth, bounds_.width);
    const float height = std::min(appearance_.buttonHeight, bounds_.height);
    const float y = bounds_.y + bounds_.height - height;
    const float nextX = bounds_.x + bounds_.width - width;
    if (part == Part::Next)
    {
        return {nextX, y, width, height};
    }
    if (part == Part::Previous)
    {
        return {std::max(bounds_.x, nextX - appearance_.gap - width), y, width, height};
    }
    return {};
}

PagerArrows::Part PagerArrows::HitTestPart(const PointF &point) const
{
    if (PointInRect(GetPartBounds(Part::Next), point))
    {
        return Part::Next;
    }
    if (PointInRect(GetPartBounds(Part::Previous), point))
    {
        return Part::Previous;
    }
    return Part::None;
}

SizeF PagerArrows::Measure(const SizeF &availableSize)
{
    (void)availableSize;
    return {appearance_.buttonWidth * 2.0f + appearance_.gap, appearance_.buttonHeight};
}

void PagerArrows::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void PagerArrows::Render(DeviceResources &deviceResources)
{
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!target || bounds_.width <= 0.0f || bounds_.height <= 0.0f)
    {
        return;
    }

    for (const Part part : {Part::Previous, Part::Next})
    {
        const RectF box = GetPartBounds(part);
        const bool enabled = IsPartEnabled(part);
        if (enabled && (pressed_ == part || hovered_ == part))
        {
            const D2D1_COLOR_F fill = pressed_ == part ? appearance_.pressedFill : appearance_.hoverFill;
            FillRoundedRect(deviceResources, box, appearance_.cornerRadius, fill, D2D1::ColorF(0, 0.0f), 0.0f);
        }

        ID2D1SolidColorBrush *brush =
            deviceResources.GetSolidColorBrush(enabled ? appearance_.glyphColor : appearance_.disabledGlyphColor);
        if (!brush)
        {
            continue;
        }
        // A chevron twice as tall as it is wide: "<" for previous, ">" for next.
        const float half = std::min(appearance_.glyphSize, box.height) * 0.5f;
        const float depth = std::min(appearance_.GlyphWidth(), half * 0.55f);
        const float cx = box.x + box.width * 0.5f;
        const float cy = box.y + box.height * 0.5f;
        const float tipX = part == Part::Previous ? cx - depth * 0.5f : cx + depth * 0.5f;
        const float tailX = part == Part::Previous ? cx + depth * 0.5f : cx - depth * 0.5f;
        target->DrawLine(D2D1::Point2F(tailX, cy - half), D2D1::Point2F(tipX, cy), brush, appearance_.strokeWidth);
        target->DrawLine(D2D1::Point2F(tipX, cy), D2D1::Point2F(tailX, cy + half), brush, appearance_.strokeWidth);
    }
}

bool PagerArrows::HitTest(const PointF &point) const
{
    return HitTestPart(point) != Part::None;
}

bool PagerArrows::OnMouseDown(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_)
    {
        return false;
    }
    const Part part = HitTestPart(window_->ClientPixelsToDips(point));
    pressed_ = IsPartEnabled(part) ? part : Part::None;
    InvalidateVisual();
    // Swallow clicks on a disabled button too, so they do not fall through to what lies below.
    return part != Part::None;
}

bool PagerArrows::OnMouseUp(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_ || pressed_ == Part::None)
    {
        return false;
    }
    const Part pressed = pressed_;
    pressed_ = Part::None;
    InvalidateVisual();
    if (HitTestPart(window_->ClientPixelsToDips(point)) == pressed && IsPartEnabled(pressed) && onClick_)
    {
        // The handler may rebuild the scene that owns this control; keep it alive for the call.
        ClickHandler handler = onClick_;
        handler(pressed);
    }
    return true;
}

bool PagerArrows::OnMouseMove(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_ || !hoverEnabled_)
    {
        return false;
    }
    const Part part = HitTestPart(window_->ClientPixelsToDips(point));
    if (hovered_ != part)
    {
        hovered_ = part;
        InvalidateVisual();
    }
    return part != Part::None;
}

void PagerArrows::OnMouseLeave()
{
    // A press survives leaving: OnMouseUp only clicks when the release lands on the same button.
    if (hovered_ == Part::None)
    {
        return;
    }
    hovered_ = Part::None;
    InvalidateVisual();
}

HCURSOR PagerArrows::GetCursor() const
{
    return LoadCursor(nullptr, IDC_ARROW);
}
} // namespace msimeui
