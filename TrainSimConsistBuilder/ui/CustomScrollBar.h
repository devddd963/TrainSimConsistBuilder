#pragma once
#include <windows.h>
#include <algorithm>

enum class ScrollBarOrientation
{
    Vertical,
    Horizontal
};

enum class ScrollRepeatAction
{
    None,
    Arrow1,
    Arrow2,
    PageUp,
    PageDown
};

class CustomScrollBar
{
public:
    static constexpr UINT_PTR TIMER_ANIM_ID = 0x5C01;

    CustomScrollBar(ScrollBarOrientation orientation = ScrollBarOrientation::Vertical);
    ~CustomScrollBar();

    void SetOrientation(ScrollBarOrientation orientation);
    ScrollBarOrientation GetOrientation() const { return m_orientation; }
    void SetBounds(const RECT& rc);
    const RECT& GetBounds() const { return m_rcBounds; }

    void SetRange(int minVal, int maxVal, int pageSize);
    void SetPos(int pos);
    int  GetPos() const { return m_pos; }
    int  GetMin() const { return m_min; }
    int  GetMax() const { return m_max; }
    int  GetPage() const { return m_page; }
    int  GetMaxScrollPos() const;

    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }

    bool IsDragging() const { return m_isDragging; }
    bool IsHovered() const { return m_isHovered; }
    bool IsExpanded() const { return m_animExpand > 0.05f; }

    // Auto-hide & Expand/Collapse settings
    void SetAutoHide(bool autoHide) { m_autoHide = autoHide; }
    bool GetAutoHide() const { return m_autoHide; }
    void SetHideDelay(DWORD ms) { m_hideDelayMs = ms; }
    void TriggerActivity(HWND hWnd = NULL);

    // Hit testing and mouse handling (returns true if position or visual state changed)
    bool OnLButtonDown(POINT pt, HWND hWnd);
    bool OnMouseMove(POINT pt, HWND hWnd);
    bool OnLButtonUp(POINT pt, HWND hWnd);
    bool OnMouseWheel(short delta, int scrollStep = 3, HWND hWnd = NULL);
    bool OnMouseLeave(HWND hWnd = NULL);

    // Animation timer callback (call from WM_TIMER or host loop)
    // Returns true if an animation frame updated and host should redraw
    bool OnTimer(HWND hWnd);
    bool UpdateAnimation(ULONGLONG now, HWND hWnd = NULL);

    // Auto-scroll helper during drag & drop operations (returns true and outDelta if pt is near bounds edge)
    bool CheckAutoScroll(POINT pt, int edgeThreshold, int scrollStep, int& outDelta);

    // Painting
    void Paint(HDC hdc, COLORREF gutterColor = CLR_INVALID, COLORREF parentBg = RGB(26, 26, 26));

    // Custom colors
    void SetThumbColor(COLORREF normal, COLORREF hover, COLORREF active);
    void SetGutterColor(COLORREF gutter);

private:
    void StartAnimation(HWND hWnd);
    RECT CalculateThumbRect(float expandFraction) const;
    RECT GetArrow1Rect(float expandFraction) const;
    RECT GetArrow2Rect(float expandFraction) const;
    static float EaseFluent(float t);

    ScrollBarOrientation m_orientation;
    RECT m_rcBounds;
    int m_min;
    int m_max;
    int m_page;
    int m_pos;
    bool m_isVisible;

    bool m_isHovered;
    bool m_isDragging;
    POINT m_dragStartPt;
    int m_dragStartPos;

    bool m_isHoverArrow1;
    bool m_isHoverArrow2;
    bool m_isPressedArrow1;
    bool m_isPressedArrow2;

    ScrollRepeatAction m_repeatAction;
    ULONGLONG m_pressStartTime;
    ULONGLONG m_lastRepeatTime;
    POINT m_pressPt;

    bool m_autoHide;
    DWORD m_hideDelayMs;
    ULONGLONG m_lastActivityTime;

    // Fluent Animation state
    float m_animExpand;      // 0.0f (collapsed 2.5px) -> 1.0f (expanded 6.0px)
    float m_targetExpand;
    ULONGLONG m_expandStartTime;
    float m_expandStartVal;

    float m_animAlpha;       // 0.0f (invisible) -> 1.0f (fully visible)
    float m_targetAlpha;
    ULONGLONG m_alphaStartTime;
    float m_alphaStartVal;

    bool m_isAnimating;

    COLORREF m_clrThumbNormal;
    COLORREF m_clrThumbHover;
    COLORREF m_clrThumbActive;
    COLORREF m_clrGutter;
};
