#include "CustomScrollBar.h"
#include <cmath>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

static ULONG_PTR s_sbGdiplusToken = 0;
static int s_sbGdiplusRefCount = 0;

static void EnsureGdiPlus()
{
    if (s_sbGdiplusRefCount == 0)
    {
        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&s_sbGdiplusToken, &gsi, NULL);
    }
    s_sbGdiplusRefCount++;
}

static void ReleaseGdiPlus()
{
    s_sbGdiplusRefCount--;
    if (s_sbGdiplusRefCount <= 0)
    {
        s_sbGdiplusRefCount = 0;
        if (s_sbGdiplusToken)
        {
            Gdiplus::GdiplusShutdown(s_sbGdiplusToken);
            s_sbGdiplusToken = 0;
        }
    }
}

CustomScrollBar::CustomScrollBar(ScrollBarOrientation orientation)
    : m_orientation(orientation),
      m_min(0),
      m_max(0),
      m_page(0),
      m_pos(0),
      m_isVisible(false),
      m_isHovered(false),
      m_isDragging(false),
      m_dragStartPos(0),
      m_isHoverArrow1(false),
      m_isHoverArrow2(false),
      m_isPressedArrow1(false),
      m_isPressedArrow2(false),
      m_repeatAction(ScrollRepeatAction::None),
      m_pressStartTime(0),
      m_lastRepeatTime(0),
      m_autoHide(false),
      m_hideDelayMs(2000),
      m_lastActivityTime(0),
      m_animExpand(0.0f),
      m_targetExpand(0.0f),
      m_expandStartTime(0),
      m_expandStartVal(0.0f),
      m_animAlpha(1.0f),
      m_targetAlpha(1.0f),
      m_alphaStartTime(0),
      m_alphaStartVal(1.0f),
      m_isAnimating(false),
      m_clrThumbNormal(RGB(145, 145, 145)),
      m_clrThumbHover(RGB(200, 200, 200)),
      m_clrThumbActive(RGB(235, 235, 235)),
      m_clrGutter(CLR_INVALID)
{
    EnsureGdiPlus();
    SetRectEmpty(&m_rcBounds);
    m_dragStartPt = { 0, 0 };
    m_pressPt = { 0, 0 };
    m_lastActivityTime = GetTickCount64();
}

CustomScrollBar::~CustomScrollBar()
{
    ReleaseGdiPlus();
}

void CustomScrollBar::SetOrientation(ScrollBarOrientation orientation)
{
    m_orientation = orientation;
}

void CustomScrollBar::SetBounds(const RECT& rc)
{
    m_rcBounds = rc;
}

void CustomScrollBar::SetRange(int minVal, int maxVal, int pageSize)
{
    m_min = minVal;
    m_max = (std::max)(minVal, maxVal);
    m_page = (std::max)(1, pageSize);

    int maxPos = GetMaxScrollPos();
    if (m_pos > maxPos) m_pos = maxPos;
    if (m_pos < m_min)  m_pos = m_min;

    bool oldVis = m_isVisible;
    m_isVisible = (m_max - m_min + 1 > m_page);
    if (m_isVisible != oldVis)
    {
        m_lastActivityTime = GetTickCount64();
    }
}

void CustomScrollBar::SetPos(int pos)
{
    int maxPos = GetMaxScrollPos();
    int newPos = (std::max)(m_min, (std::min)(pos, maxPos));
    if (newPos != m_pos)
    {
        m_pos = newPos;
        TriggerActivity();
    }
}

int CustomScrollBar::GetMaxScrollPos() const
{
    int maxPos = m_max - m_page + 1;
    if (maxPos < m_min) maxPos = m_min;
    return maxPos;
}

void CustomScrollBar::SetThumbColor(COLORREF normal, COLORREF hover, COLORREF active)
{
    m_clrThumbNormal = normal;
    m_clrThumbHover  = hover;
    m_clrThumbActive = active;
}

void CustomScrollBar::SetGutterColor(COLORREF gutter)
{
    m_clrGutter = gutter;
}

float CustomScrollBar::EaseFluent(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float f = 1.0f - t;
    return 1.0f - (f * f * f * f); // Quartic ease-out: characteristic deceleration of Fluent UI
}

void CustomScrollBar::StartAnimation(HWND hWnd)
{
    if (hWnd && IsWindow(hWnd))
    {
        SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL);
        m_isAnimating = true;
    }
}

void CustomScrollBar::TriggerActivity(HWND hWnd)
{
    m_lastActivityTime = GetTickCount64();
}

bool CustomScrollBar::UpdateAnimation(ULONGLONG now, HWND hWnd)
{
    if (!m_isVisible) return false;

    bool expanded = (m_isHovered || m_isDragging || m_isPressedArrow1 || m_isPressedArrow2 || m_repeatAction != ScrollRepeatAction::None);
    float desiredExpand = expanded ? 1.0f : 0.0f;

    // Check if target expansion changed
    if (std::fabs(desiredExpand - m_targetExpand) > 0.001f)
    {
        m_targetExpand = desiredExpand;
        m_expandStartTime = now;
        m_expandStartVal = m_animExpand;
        StartAnimation(hWnd);
    }

    bool stateChanged = false;

    // 1. Morph Expand Animation (Fluent Spec: 250ms expansion / 280ms collapse)
    if (std::fabs(m_animExpand - m_targetExpand) > 0.001f)
    {
        float duration = (m_targetExpand > m_expandStartVal) ? 250.0f : 280.0f;
        ULONGLONG elapsed = (now >= m_expandStartTime) ? (now - m_expandStartTime) : 0;
        float t = (float)elapsed / duration;
        if (t >= 1.0f)
        {
            m_animExpand = m_targetExpand;
        }
        else
        {
            m_animExpand = m_expandStartVal + (m_targetExpand - m_expandStartVal) * EaseFluent(t);
        }
        stateChanged = true;
    }

    // 2. Handle Auto-Repeat Scrolling using exact Windows Native Timing Formulas
    // Windows Native Specs:
    // - Initial Delay = 4/5 * GetDoubleClickTime() (Default 500ms -> 400ms)
    // - Repeat Interval = 1/10 * GetDoubleClickTime() (Default 500ms -> 50ms, i.e. 20 repeats/sec)
    // - Holding Acceleration: ramps to 2x and 4x speed when held
    UINT dt = GetDoubleClickTime();
    if (dt < 100) dt = 500;
    DWORD initialDelay = (dt * 4) / 5;
    DWORD baseInterval = (dt / 10 >= 10) ? (dt / 10) : 10;

    if (m_repeatAction == ScrollRepeatAction::Arrow1 && m_isPressedArrow1)
    {
        ULONGLONG elapsed = (now >= m_pressStartTime) ? (now - m_pressStartTime) : 0;
        if (elapsed >= initialDelay)
        {
            DWORD interval = baseInterval;
            if (elapsed >= 3000)
                interval = (baseInterval / 4 >= 10) ? (baseInterval / 4) : 10; // ~12ms (~83 repeats/sec)
            else if (elapsed >= 1500)
                interval = (baseInterval / 2 >= 10) ? (baseInterval / 2) : 10; // ~25ms (~40 repeats/sec)

            if (now - m_lastRepeatTime >= interval)
            {
                m_lastRepeatTime = now;
                int step = (m_orientation == ScrollBarOrientation::Vertical) ? 1 : 16;
                int oldPos = m_pos;
                SetPos(m_pos - step);
                if (m_pos != oldPos)
                {
                    stateChanged = true;
                }
            }
        }
    }
    else if (m_repeatAction == ScrollRepeatAction::Arrow2 && m_isPressedArrow2)
    {
        ULONGLONG elapsed = (now >= m_pressStartTime) ? (now - m_pressStartTime) : 0;
        if (elapsed >= initialDelay)
        {
            DWORD interval = baseInterval;
            if (elapsed >= 3000)
                interval = (baseInterval / 4 >= 10) ? (baseInterval / 4) : 10; // ~12ms (~83 repeats/sec)
            else if (elapsed >= 1500)
                interval = (baseInterval / 2 >= 10) ? (baseInterval / 2) : 10; // ~25ms (~40 repeats/sec)

            if (now - m_lastRepeatTime >= interval)
            {
                m_lastRepeatTime = now;
                int step = (m_orientation == ScrollBarOrientation::Vertical) ? 1 : 16;
                int oldPos = m_pos;
                SetPos(m_pos + step);
                if (m_pos != oldPos)
                {
                    stateChanged = true;
                }
            }
        }
    }
    else if (m_repeatAction == ScrollRepeatAction::PageUp || m_repeatAction == ScrollRepeatAction::PageDown)
    {
        ULONGLONG elapsed = (now >= m_pressStartTime) ? (now - m_pressStartTime) : 0;
        if (elapsed >= initialDelay)
        {
            DWORD pageInterval = ((baseInterval * 5) / 2 >= 50) ? ((baseInterval * 5) / 2) : 50; // ~125ms
            if (now - m_lastRepeatTime >= pageInterval)
            {
                m_lastRepeatTime = now;
                RECT rcThumb = CalculateThumbRect(1.0f);
                int oldPos = m_pos;
                if (m_repeatAction == ScrollRepeatAction::PageUp)
                {
                    bool shouldScroll = (m_orientation == ScrollBarOrientation::Vertical) 
                        ? (m_pressPt.y < rcThumb.top) : (m_pressPt.x < rcThumb.left);
                    if (shouldScroll)
                    {
                        SetPos(m_pos - m_page);
                    }
                }
                else // PageDown
                {
                    bool shouldScroll = (m_orientation == ScrollBarOrientation::Vertical) 
                        ? (m_pressPt.y > rcThumb.bottom) : (m_pressPt.x > rcThumb.right);
                    if (shouldScroll)
                    {
                        SetPos(m_pos + m_page);
                    }
                }
                if (m_pos != oldPos)
                {
                    stateChanged = true;
                }
            }
        }
    }

    // If fully settled AND no repeat active, evaluate whether we can stop the 16ms animation timer
    if (std::fabs(m_animExpand - m_targetExpand) <= 0.001f && m_repeatAction == ScrollRepeatAction::None)
    {
        m_animExpand = m_targetExpand;

        if (m_isAnimating)
        {
            if (hWnd && IsWindow(hWnd))
            {
                KillTimer(hWnd, TIMER_ANIM_ID);
            }
            m_isAnimating = false;
        }
    }

    return stateChanged;
}

bool CustomScrollBar::OnTimer(HWND hWnd)
{
    ULONGLONG now = GetTickCount64();
    bool changed = UpdateAnimation(now, hWnd);
    if (changed && hWnd && IsWindow(hWnd))
    {
        InvalidateRect(hWnd, &m_rcBounds, FALSE);
    }
    return changed;
}

RECT CustomScrollBar::GetArrow1Rect(float expandFraction) const
{
    RECT rc = { 0, 0, 0, 0 };
    if (!m_isVisible || expandFraction <= 0.01f) return rc;

    int arrowSize = 14;
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        rc = { m_rcBounds.left, m_rcBounds.top, m_rcBounds.right, m_rcBounds.top + arrowSize };
    }
    else
    {
        rc = { m_rcBounds.left, m_rcBounds.top, m_rcBounds.left + arrowSize, m_rcBounds.bottom };
    }
    return rc;
}

RECT CustomScrollBar::GetArrow2Rect(float expandFraction) const
{
    RECT rc = { 0, 0, 0, 0 };
    if (!m_isVisible || expandFraction <= 0.01f) return rc;

    int arrowSize = 14;
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        rc = { m_rcBounds.left, m_rcBounds.bottom - arrowSize, m_rcBounds.right, m_rcBounds.bottom };
    }
    else
    {
        rc = { m_rcBounds.right - arrowSize, m_rcBounds.top, m_rcBounds.right, m_rcBounds.bottom };
    }
    return rc;
}

RECT CustomScrollBar::CalculateThumbRect(float expandFraction) const
{
    RECT rcThumb = { 0, 0, 0, 0 };
    if (!m_isVisible) return rcThumb;

    int boundsW = m_rcBounds.right - m_rcBounds.left;
    int boundsH = m_rcBounds.bottom - m_rcBounds.top;
    if (boundsW <= 0 || boundsH <= 0) return rcThumb;

    int totalRange = m_max - m_min + 1;
    if (totalRange <= 0) return rcThumb;

    int maxPos = GetMaxScrollPos();
    int travelRange = maxPos - m_min;

    const float arrowPad = 16.0f;

    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        float trackLen = (float)boundsH - (arrowPad * 2.0f);
        if (trackLen < 10.0f) trackLen = (float)boundsH;
        float thumbLen = (float)m_page * trackLen / (float)totalRange;
        if (thumbLen < 24.0f) thumbLen = 24.0f;
        if (thumbLen > trackLen) thumbLen = trackLen;

        float travelLen = trackLen - thumbLen;
        float thumbOffset = (travelRange > 0 && travelLen > 0) ? ((float)(m_pos - m_min) * travelLen / (float)travelRange) : 0.0f;

        // Collapsed: right aligned (width 2.5px ~ 3px)
        float colRight = (float)m_rcBounds.right - 2.0f;
        float colLeft  = colRight - 2.5f;

        // Expanded: centered (width 6px)
        float cx = (float)(m_rcBounds.left + m_rcBounds.right) / 2.0f;
        float expLeft  = cx - 3.0f;
        float expRight = cx + 3.0f;

        float left  = colLeft + (expLeft - colLeft) * expandFraction;
        float right = colRight + (expRight - colRight) * expandFraction;
        float top   = (float)m_rcBounds.top + arrowPad + thumbOffset;
        float bottom = top + thumbLen;

        rcThumb.left   = (int)std::round(left);
        rcThumb.right  = (int)std::round(right);
        rcThumb.top    = (int)std::round(top);
        rcThumb.bottom = (int)std::round(bottom);
    }
    else
    {
        float trackLen = (float)boundsW - (arrowPad * 2.0f);
        if (trackLen < 10.0f) trackLen = (float)boundsW;
        float thumbLen = (float)m_page * trackLen / (float)totalRange;
        if (thumbLen < 24.0f) thumbLen = 24.0f;
        if (thumbLen > trackLen) thumbLen = trackLen;

        float travelLen = trackLen - thumbLen;
        float thumbOffset = (travelRange > 0 && travelLen > 0) ? ((float)(m_pos - m_min) * travelLen / (float)travelRange) : 0.0f;

        float cy = (float)(m_rcBounds.top + m_rcBounds.bottom) / 2.0f;
        float thumbH = 2.5f + 3.5f * expandFraction;
        float top    = cy - (thumbH / 2.0f);
        float bottom = top + thumbH;
        float left   = (float)m_rcBounds.left + arrowPad + thumbOffset;
        float right  = left + thumbLen;

        rcThumb.left   = (int)std::round(left);
        rcThumb.right  = (int)std::round(right);
        rcThumb.top    = (int)std::round(top);
        rcThumb.bottom = (int)std::round(bottom);
    }

    return rcThumb;
}

bool CustomScrollBar::OnLButtonDown(POINT pt, HWND hWnd)
{
    if (!m_isVisible) return false;
    if (!PtInRect(&m_rcBounds, pt)) return false;

    TriggerActivity(hWnd);
    m_isHovered = true;
    StartAnimation(hWnd);

    RECT rcArrow1 = GetArrow1Rect(1.0f);
    RECT rcArrow2 = GetArrow2Rect(1.0f);

    ULONGLONG now = GetTickCount64();

    if (PtInRect(&rcArrow1, pt))
    {
        m_isPressedArrow1 = true;
        m_isHoverArrow1 = true;
        m_repeatAction = ScrollRepeatAction::Arrow1;
        m_pressStartTime = now;
        m_lastRepeatTime = now;
        m_pressPt = pt;
        int step = (m_orientation == ScrollBarOrientation::Vertical) ? 1 : 16;
        SetPos(m_pos - step);
        if (hWnd) SetCapture(hWnd);
        return true;
    }

    if (PtInRect(&rcArrow2, pt))
    {
        m_isPressedArrow2 = true;
        m_isHoverArrow2 = true;
        m_repeatAction = ScrollRepeatAction::Arrow2;
        m_pressStartTime = now;
        m_lastRepeatTime = now;
        m_pressPt = pt;
        int step = (m_orientation == ScrollBarOrientation::Vertical) ? 1 : 16;
        SetPos(m_pos + step);
        if (hWnd) SetCapture(hWnd);
        return true;
    }

    RECT rcThumb = CalculateThumbRect(1.0f);
    RECT rcHit = rcThumb;
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        rcHit.left = m_rcBounds.left;
        rcHit.right = m_rcBounds.right;
    }
    else
    {
        rcHit.top = m_rcBounds.top;
        rcHit.bottom = m_rcBounds.bottom;
    }

    if (PtInRect(&rcHit, pt))
    {
        m_isDragging = true;
        m_dragStartPt = pt;
        m_dragStartPos = m_pos;
        m_repeatAction = ScrollRepeatAction::None;
        if (hWnd) SetCapture(hWnd);
        return true;
    }

    // Clicked on track above or below / left or right of thumb (Page Jump)
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        if (pt.y < rcThumb.top)
        {
            m_repeatAction = ScrollRepeatAction::PageUp;
            m_pressStartTime = now;
            m_lastRepeatTime = now;
            m_pressPt = pt;
            SetPos(m_pos - m_page);
            if (hWnd) SetCapture(hWnd);
            return true;
        }
        else if (pt.y > rcThumb.bottom)
        {
            m_repeatAction = ScrollRepeatAction::PageDown;
            m_pressStartTime = now;
            m_lastRepeatTime = now;
            m_pressPt = pt;
            SetPos(m_pos + m_page);
            if (hWnd) SetCapture(hWnd);
            return true;
        }
    }
    else
    {
        if (pt.x < rcThumb.left)
        {
            m_repeatAction = ScrollRepeatAction::PageUp;
            m_pressStartTime = now;
            m_lastRepeatTime = now;
            m_pressPt = pt;
            SetPos(m_pos - m_page);
            if (hWnd) SetCapture(hWnd);
            return true;
        }
        else if (pt.x > rcThumb.right)
        {
            m_repeatAction = ScrollRepeatAction::PageDown;
            m_pressStartTime = now;
            m_lastRepeatTime = now;
            m_pressPt = pt;
            SetPos(m_pos + m_page);
            if (hWnd) SetCapture(hWnd);
            return true;
        }
    }

    return false;
}

bool CustomScrollBar::OnMouseMove(POINT pt, HWND hWnd)
{
    if (!m_isVisible) return false;

    if (m_isDragging)
    {
        TriggerActivity(hWnd);
        int boundsW = m_rcBounds.right - m_rcBounds.left;
        int boundsH = m_rcBounds.bottom - m_rcBounds.top;
        int totalRange = m_max - m_min + 1;
        int maxPos = GetMaxScrollPos();
        int travelRange = maxPos - m_min;
        int arrowPad = 16;

        if (m_orientation == ScrollBarOrientation::Vertical)
        {
            int trackLen = boundsH - (arrowPad * 2);
            if (trackLen < 10) trackLen = boundsH;
            int thumbLen = MulDiv(m_page, trackLen, totalRange);
            if (thumbLen < 24) thumbLen = 24;
            if (thumbLen > trackLen) thumbLen = trackLen;
            int travelLen = trackLen - thumbLen;

            if (travelLen > 0 && travelRange > 0)
            {
                int deltaPx = pt.y - m_dragStartPt.y;
                int deltaPos = MulDiv(deltaPx, travelRange, travelLen);
                int oldPos = m_pos;
                SetPos(m_dragStartPos + deltaPos);
                return (m_pos != oldPos);
            }
        }
        else
        {
            int trackLen = boundsW - (arrowPad * 2);
            if (trackLen < 10) trackLen = boundsW;
            int thumbLen = MulDiv(m_page, trackLen, totalRange);
            if (thumbLen < 24) thumbLen = 24;
            if (thumbLen > trackLen) thumbLen = trackLen;
            int travelLen = trackLen - thumbLen;

            if (travelLen > 0 && travelRange > 0)
            {
                int deltaPx = pt.x - m_dragStartPt.x;
                int deltaPos = MulDiv(deltaPx, travelRange, travelLen);
                int oldPos = m_pos;
                SetPos(m_dragStartPos + deltaPos);
                return (m_pos != oldPos);
            }
        }
        return false;
    }

    if (m_repeatAction != ScrollRepeatAction::None)
    {
        TriggerActivity(hWnd);
        if (m_repeatAction == ScrollRepeatAction::Arrow1)
        {
            RECT rcA1 = GetArrow1Rect(1.0f);
            m_isPressedArrow1 = (PtInRect(&rcA1, pt) != FALSE);
            m_isHoverArrow1 = m_isPressedArrow1;
        }
        else if (m_repeatAction == ScrollRepeatAction::Arrow2)
        {
            RECT rcA2 = GetArrow2Rect(1.0f);
            m_isPressedArrow2 = (PtInRect(&rcA2, pt) != FALSE);
            m_isHoverArrow2 = m_isPressedArrow2;
        }
        else if (m_repeatAction == ScrollRepeatAction::PageUp || m_repeatAction == ScrollRepeatAction::PageDown)
        {
            m_pressPt = pt;
        }
        return true;
    }

    bool wasHovered = m_isHovered;
    bool wasH1 = m_isHoverArrow1;
    bool wasH2 = m_isHoverArrow2;

    m_isHovered = (PtInRect(&m_rcBounds, pt) != FALSE);

    if (m_isHovered)
    {
        TriggerActivity(hWnd);
        StartAnimation(hWnd);
        RECT rcA1 = GetArrow1Rect(1.0f);
        RECT rcA2 = GetArrow2Rect(1.0f);
        m_isHoverArrow1 = (PtInRect(&rcA1, pt) != FALSE);
        m_isHoverArrow2 = (PtInRect(&rcA2, pt) != FALSE);
    }
    else
    {
        m_isHoverArrow1 = false;
        m_isHoverArrow2 = false;
        if (wasHovered)
        {
            StartAnimation(hWnd);
        }
    }

    return (m_isHovered != wasHovered || m_isHoverArrow1 != wasH1 || m_isHoverArrow2 != wasH2);
}

bool CustomScrollBar::OnLButtonUp(POINT /*pt*/, HWND hWnd)
{
    bool changed = false;
    if (m_isDragging)
    {
        m_isDragging = false;
        TriggerActivity(hWnd);
        StartAnimation(hWnd);
        changed = true;
    }
    if (m_isPressedArrow1 || m_isPressedArrow2 || m_repeatAction != ScrollRepeatAction::None)
    {
        m_isPressedArrow1 = false;
        m_isPressedArrow2 = false;
        m_repeatAction = ScrollRepeatAction::None;
        m_pressStartTime = 0;
        m_lastRepeatTime = 0;
        StartAnimation(hWnd);
        changed = true;
    }

    if (changed)
    {
        if (hWnd && GetCapture() == hWnd)
        {
            ReleaseCapture();
        }
        return true;
    }
    return false;
}

bool CustomScrollBar::OnMouseWheel(short delta, int scrollStep, HWND hWnd)
{
    if (!m_isVisible) return false;
    TriggerActivity(hWnd);
    StartAnimation(hWnd);
    int stepCount = -delta / WHEEL_DELTA * scrollStep;
    int oldPos = m_pos;
    SetPos(m_pos + stepCount);
    return (m_pos != oldPos);
}

bool CustomScrollBar::OnMouseLeave(HWND hWnd)
{
    bool changed = false;
    if (m_isHovered && !m_isDragging && m_repeatAction == ScrollRepeatAction::None)
    {
        m_isHovered = false;
        m_isHoverArrow1 = false;
        m_isHoverArrow2 = false;
        TriggerActivity(hWnd);
        StartAnimation(hWnd);
        changed = true;
    }
    return changed;
}

bool CustomScrollBar::CheckAutoScroll(POINT pt, int edgeThreshold, int scrollStep, int& outDelta)
{
    outDelta = 0;
    if (!m_isVisible) return false;

    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        if (pt.y < m_rcBounds.top + edgeThreshold)
        {
            outDelta = -scrollStep;
            return true;
        }
        else if (pt.y > m_rcBounds.bottom - edgeThreshold)
        {
            outDelta = scrollStep;
            return true;
        }
    }
    else
    {
        if (pt.x < m_rcBounds.left + edgeThreshold)
        {
            outDelta = -scrollStep;
            return true;
        }
        else if (pt.x > m_rcBounds.right - edgeThreshold)
        {
            outDelta = scrollStep;
            return true;
        }
    }
    return false;
}

static void AddRoundedRectPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF rc, Gdiplus::REAL radius)
{
    Gdiplus::REAL d = radius * 2.0f;
    if (d > rc.Width) d = rc.Width;
    if (d > rc.Height) d = rc.Height;

    path.AddArc(rc.X, rc.Y, d, d, 180, 90);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270, 90);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0, 90);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void CustomScrollBar::Paint(HDC hdc, COLORREF gutterColor, COLORREF parentBg)
{
    if (!m_isVisible) return;
    if (m_orientation == ScrollBarOrientation::Vertical && m_animAlpha <= 0.005f) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    float boundsW = (float)(m_rcBounds.right - m_rcBounds.left);
    float boundsH = (float)(m_rcBounds.bottom - m_rcBounds.top);
    if (boundsW <= 0.0f || boundsH <= 0.0f) return;

    // 1. Paint Gutter background for Vertical scrollbar only
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        COLORREF gutter = (gutterColor != CLR_INVALID) ? gutterColor : m_clrGutter;
        if (gutter != CLR_INVALID && m_animExpand > 0.01f)
        {
            BYTE gutterAlpha = (BYTE)(40.0f * m_animExpand * m_animAlpha);
            Gdiplus::SolidBrush gutterBr(Gdiplus::Color(gutterAlpha, GetRValue(gutter), GetGValue(gutter), GetBValue(gutter)));
            g.FillRectangle(&gutterBr, (Gdiplus::REAL)m_rcBounds.left, (Gdiplus::REAL)m_rcBounds.top, (Gdiplus::REAL)boundsW, (Gdiplus::REAL)boundsH);
        }
    }

    // 2. Draw Anti-Aliased Solid Arrow Glyphs (Matching Windows 11 Fluent spec)
    if (m_animExpand > 0.05f)
    {
        float arrowAlpha = m_animExpand * m_animAlpha;
        RECT rcA1 = GetArrow1Rect(m_animExpand);
        RECT rcA2 = GetArrow2Rect(m_animExpand);

        // --- Arrow 1 (Top / Left) ---
        BYTE arrowLum1 = m_isPressedArrow1 ? 255 : (m_isHoverArrow1 ? 245 : 145);
        BYTE arrowAlpha1 = (BYTE)(255 * arrowAlpha);
        Gdiplus::SolidBrush arrowBr1(Gdiplus::Color(arrowAlpha1, arrowLum1, arrowLum1, arrowLum1));

        float cx1 = (float)(rcA1.left + rcA1.right) / 2.0f;
        float cy1 = (float)(rcA1.top + rcA1.bottom) / 2.0f + (m_isPressedArrow1 ? 1.0f : 0.0f);

        if (m_orientation == ScrollBarOrientation::Vertical)
        {
            // Upward solid triangle ▲ (anti-aliased vector)
            Gdiplus::PointF pts[3] = {
                { cx1,         cy1 - 2.5f },
                { cx1 - 4.25f, cy1 + 2.5f },
                { cx1 + 4.25f, cy1 + 2.5f }
            };
            g.FillPolygon(&arrowBr1, pts, 3);
        }
        else
        {
            // Leftward solid triangle ◄ (anti-aliased vector)
            Gdiplus::PointF pts[3] = {
                { cx1 - 2.5f, cy1 },
                { cx1 + 2.5f, cy1 - 4.25f },
                { cx1 + 2.5f, cy1 + 4.25f }
            };
            g.FillPolygon(&arrowBr1, pts, 3);
        }

        // --- Arrow 2 (Bottom / Right) ---
        BYTE arrowLum2 = m_isPressedArrow2 ? 255 : (m_isHoverArrow2 ? 245 : 145);
        BYTE arrowAlpha2 = (BYTE)(255 * arrowAlpha);
        Gdiplus::SolidBrush arrowBr2(Gdiplus::Color(arrowAlpha2, arrowLum2, arrowLum2, arrowLum2));

        float cx2 = (float)(rcA2.left + rcA2.right) / 2.0f;
        float cy2 = (float)(rcA2.top + rcA2.bottom) / 2.0f + (m_isPressedArrow2 ? 1.0f : 0.0f);

        if (m_orientation == ScrollBarOrientation::Vertical)
        {
            // Downward solid triangle ▼ (anti-aliased vector)
            Gdiplus::PointF pts[3] = {
                { cx2,         cy2 + 2.5f },
                { cx2 - 4.25f, cy2 - 2.5f },
                { cx2 + 4.25f, cy2 - 2.5f }
            };
            g.FillPolygon(&arrowBr2, pts, 3);
        }
        else
        {
            // Rightward solid triangle ► (anti-aliased vector)
            Gdiplus::PointF pts[3] = {
                { cx2 + 2.5f, cy2 },
                { cx2 - 2.5f, cy2 - 4.25f },
                { cx2 - 2.5f, cy2 + 4.25f }
            };
            g.FillPolygon(&arrowBr2, pts, 3);
        }
    }

    // 3. Calculate Thumb Geometry with continuous sub-pixel interpolation
    int totalRange = m_max - m_min + 1;
    if (totalRange <= 0) return;
    int maxPos = GetMaxScrollPos();
    int travelRange = maxPos - m_min;

    const float arrowPad = 16.0f;

    float thumbX = 0, thumbY = 0, thumbW = 0, thumbH = 0;

    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        float trackLen = boundsH - (arrowPad * 2.0f);
        if (trackLen < 10.0f) trackLen = boundsH;
        float thumbLen = (float)m_page * trackLen / (float)totalRange;
        if (thumbLen < 24.0f) thumbLen = 24.0f;
        if (thumbLen > trackLen) thumbLen = trackLen;

        float travelLen = trackLen - thumbLen;
        float thumbOffset = (travelRange > 0 && travelLen > 0) ? ((float)(m_pos - m_min) * travelLen / (float)travelRange) : 0.0f;

        float colW = 2.5f;
        float expW = 6.0f;
        thumbW = colW + (expW - colW) * m_animExpand;

        float colRight = (float)m_rcBounds.right - 2.0f;
        float expCenterX = (float)(m_rcBounds.left + m_rcBounds.right) / 2.0f;

        float colLeft = colRight - colW;
        float expLeft = expCenterX - (expW / 2.0f);

        thumbX = colLeft + (expLeft - colLeft) * m_animExpand;
        thumbY = (float)m_rcBounds.top + arrowPad + thumbOffset;
        thumbH = thumbLen;
    }
    else
    {
        float trackLen = boundsW - (arrowPad * 2.0f);
        if (trackLen < 10.0f) trackLen = boundsW;
        float thumbLen = (float)m_page * trackLen / (float)totalRange;
        if (thumbLen < 24.0f) thumbLen = 24.0f;
        if (thumbLen > trackLen) thumbLen = trackLen;

        float travelLen = trackLen - thumbLen;
        float thumbOffset = (travelRange > 0 && travelLen > 0) ? ((float)(m_pos - m_min) * travelLen / (float)travelRange) : 0.0f;

        float cy = (float)(m_rcBounds.top + m_rcBounds.bottom) / 2.0f;
        float colH = 2.5f;
        float expH = 6.0f;
        thumbH = colH + (expH - colH) * m_animExpand;
        thumbY = cy - (thumbH / 2.0f);
        thumbX = (float)m_rcBounds.left + arrowPad + thumbOffset;
        thumbW = thumbLen;
    }

    if (thumbW <= 0.0f || thumbH <= 0.0f) return;

    // 4. Determine Thumb Color & Alpha
    COLORREF clrBase = m_clrThumbNormal;
    if (m_isDragging)
    {
        clrBase = m_clrThumbActive;
    }
    else if (m_isHovered)
    {
        clrBase = m_clrThumbHover;
    }

    BYTE thumbAlpha = (m_orientation == ScrollBarOrientation::Horizontal)
        ? (BYTE)(130.0f + 125.0f * (std::max)(m_animAlpha, m_animExpand))
        : (BYTE)(255.0f * m_animAlpha);

    Gdiplus::SolidBrush thumbBr(Gdiplus::Color(thumbAlpha, GetRValue(clrBase), GetGValue(clrBase), GetBValue(clrBase)));

    // 5. Draw Perfectly Rounded Anti-Aliased Fluent Pill Capsule
    Gdiplus::RectF rcThumbF(thumbX, thumbY, thumbW, thumbH);
    float radius = (m_orientation == ScrollBarOrientation::Vertical) ? (thumbW / 2.0f) : (thumbH / 2.0f);

    Gdiplus::GraphicsPath thumbPath;
    AddRoundedRectPath(thumbPath, rcThumbF, radius);
    g.FillPath(&thumbBr, &thumbPath);
}



