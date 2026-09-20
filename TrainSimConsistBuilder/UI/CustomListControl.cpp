#include "FluentDragGhost.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CustomListControl.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <cmath>
#include <algorithm>
#include <shlwapi.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")

extern HFONT hUIFont;

// Simple theme definition copies
namespace CustomUITheme {
    const COLORREF DarkBackground       = RGB(16, 16, 16);
    const COLORREF DarkHeaderBackground = RGB(33, 33, 33);
    const COLORREF TextPrimary          = RGB(240, 240, 240);
    const COLORREF TextSecondary        = RGB(160, 160, 160);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
CustomListControl::CustomListControl()
    : m_hWnd(NULL),
      m_vScroll(ScrollBarOrientation::Vertical),
      m_hScroll(ScrollBarOrientation::Horizontal),
      m_isVirtual(false), m_itemCount(0), m_callback(NULL), m_callbackParam(NULL),
      m_scrollX(0), m_scrollY(0), m_rowHeight(22), m_headerHeight(28), m_selectedIndex(-1),
      m_isMultiSelect(false), m_anchorRow(-1), m_isMarqueeSelecting(false),
      m_hoveredRow(-1), m_hoveredCol(-1), m_pressedRow(-1), m_pressedCol(-1),
      m_isResizing(false), m_resizeColIndex(-1), m_resizeStartX(0), m_resizeStartWidth(0),
      m_sortColIndex(-1), m_sortAscending(true),
      m_hoverHeaderColIndex(-1), m_pressedHeaderColIndex(-1),
      m_hoverHeaderInDropdown(false), m_pressedHeaderInDropdown(false),
      m_hasFocus(false),
    m_typeAheadBuffer(L""),
    m_lastTypeAheadTime(0),
    m_marqueeStartScrollY(0),
    m_marqueeStartScrollX(0),
    m_marqueeCurrentX(0),
    m_marqueeCurrentY(0),
    m_isAutoScrolling(false)
{
    m_vScroll.SetOrientation(ScrollBarOrientation::Vertical);
    m_hScroll.SetOrientation(ScrollBarOrientation::Horizontal);
    m_vScroll.SetGutterColor(CustomUITheme::DarkBackground);
    m_hScroll.SetGutterColor(CustomUITheme::DarkBackground);

    m_bPotentialDrag = false;
    m_bPotentialItemDrag = false;
    m_bIsItemDragging = false;
    m_dropTargetIndex = -1;
    m_bAllowRearrange = false;
    m_bDrawCardBorder = false;
    m_ptDragStart = { 0, 0 };
    m_ptMarqueeStart = { 0, 0 };
    m_rcMarquee = { 0, 0, 0, 0 };
    m_pendingClickedRow = -1;
    m_bPendingCtrl = false;
    m_bPendingShift = false;
}

CustomListControl::~CustomListControl()
{
}

// ---------------------------------------------------------------------------
// Registration & Creation
// ---------------------------------------------------------------------------
bool CustomListControl::Register(HINSTANCE hInstance)
{
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcx.lpfnWndProc   = CustomListControl::WndProc;
    wcx.cbWndExtra    = sizeof(CustomListControl*);
    wcx.hInstance     = hInstance;
    wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = NULL;
    wcx.lpszClassName = L"CustomListControl";

    return (RegisterClassExW(&wcx) != 0);
}

HWND CustomListControl::Create(HWND hParent, int x, int y, int width, int height, UINT_PTR id)
{
    m_hWnd = CreateWindowExW(
        0, L"CustomListControl", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, (HMENU)id, GetModuleHandle(NULL), this
    );
    return m_hWnd;
}

// ---------------------------------------------------------------------------
// Column APIs
// ---------------------------------------------------------------------------
void CustomListControl::AddColumn(const std::wstring& title, int width, int align)
{
    CustomColumn col = { title, width, align };
    m_columns.push_back(col);
    m_activeFilters.resize(m_columns.size());
    UpdateScrollbars();
    Invalidate();
}

void CustomListControl::ClearColumns()
{
    m_columns.clear();
    m_activeFilters.clear();
    m_sortColIndex = -1;
    m_hoverHeaderColIndex = -1;
    m_pressedHeaderColIndex = -1;
    UpdateScrollbars();
    Invalidate();
}

void CustomListControl::SetColumnTitle(int colIndex, const std::wstring& title)
{
    if (colIndex >= 0 && colIndex < (int)m_columns.size())
    {
        m_columns[colIndex].title = title;
        Invalidate();
    }
}

void CustomListControl::SetColumnWidth(int colIndex, int width)
{
    if (colIndex >= 0 && colIndex < (int)m_columns.size())
    {
        m_columns[colIndex].width = width;
        UpdateScrollbars();
        Invalidate();
    }
}

int CustomListControl::GetColumnWidth(int colIndex) const
{
    if (colIndex >= 0 && colIndex < (int)m_columns.size())
        return m_columns[colIndex].width;
    return 0;
}

const std::vector<std::wstring>& CustomListControl::GetActiveFilters(int colIndex) const
{
    static const std::vector<std::wstring> emptyFilters;
    if (colIndex >= 0 && colIndex < (int)m_activeFilters.size())
        return m_activeFilters[colIndex];
    return emptyFilters;
}

void CustomListControl::SetActiveFilters(int colIndex, const std::vector<std::wstring>& filters)
{
    if (colIndex >= 0 && colIndex < (int)m_activeFilters.size())
    {
        m_activeFilters[colIndex] = filters;
        Invalidate();
    }
}

void CustomListControl::ClearAllFilters()
{
    for (auto& filters : m_activeFilters)
    {
        filters.clear();
    }
    Invalidate();
}

// ---------------------------------------------------------------------------
// Standard Mode APIs
// ---------------------------------------------------------------------------
void CustomListControl::AddItem(const std::vector<std::wstring>& cells)
{
    m_items.push_back(cells);
    UpdateScrollbars();
    Invalidate();
}

void CustomListControl::SetCellText(int itemIndex, int subItemIndex, const std::wstring& text)
{
    if (itemIndex >= 0 && itemIndex < (int)m_items.size())
    {
        if (subItemIndex >= 0 && subItemIndex < (int)m_items[itemIndex].size())
        {
            m_items[itemIndex][subItemIndex] = text;
            Invalidate();
        }
    }
}

void CustomListControl::Clear()
{
    m_items.clear();
    m_selectedIndex = -1;
    m_scrollY = 0;
    m_scrollX = 0;
    UpdateScrollbars();
    Invalidate();
}

int CustomListControl::GetItemCount() const
{
    return m_isVirtual ? m_itemCount : (int)m_items.size();
}

// ---------------------------------------------------------------------------
// Virtual Mode APIs
// ---------------------------------------------------------------------------
void CustomListControl::SetVirtualMode(GetCellTextCallback callback, void* pParam)
{
    m_isVirtual     = true;
    m_callback      = callback;
    m_callbackParam = pParam;
}

void CustomListControl::SetItemCount(int count)
{
    m_itemCount = count;
    if (m_selectedIndex >= count) m_selectedIndex = -1;
    UpdateScrollbars();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
void CustomListControl::SetSelectedIndex(int index)
{
    int itemCount = GetItemCount();
    if (index < -1 || index >= itemCount) return;

    m_selectedIndex = index;
    if (m_isMultiSelect)
    {
        m_selectedIndices.clear();
        if (index >= 0) m_selectedIndices.insert(index);
    }

    if (index >= 0)
    {
        EnsureVisible(index);
    }
    Invalidate();
}

void CustomListControl::SetMultiSelect(bool bEnable)
{
    m_isMultiSelect = bEnable;
    if (!bEnable)
    {
        m_selectedIndices.clear();
        if (m_selectedIndex >= 0) m_selectedIndices.insert(m_selectedIndex);
    }
    Invalidate();
}

std::vector<int> CustomListControl::GetSelectedIndices() const
{
    if (!m_isMultiSelect)
    {
        if (m_selectedIndex >= 0) return { m_selectedIndex };
        return {};
    }
    std::vector<int> result(m_selectedIndices.begin(), m_selectedIndices.end());
    std::sort(result.begin(), result.end());
    return result;
}

void CustomListControl::SetSelectedIndices(const std::vector<int>& indices)
{
    m_selectedIndices.clear();
    int count = GetItemCount();
    for (int idx : indices)
    {
        if (idx >= 0 && idx < count)
        {
            m_selectedIndices.insert(idx);
        }
    }
    if (!indices.empty())
    {
        m_selectedIndex = indices.back();
        m_anchorRow = m_selectedIndex;
    }
    else
    {
        m_selectedIndex = -1;
        m_anchorRow = -1;
    }
    Invalidate();
}

void CustomListControl::SelectAll()
{
    m_selectedIndices.clear();
    int count = GetItemCount();
    for (int i = 0; i < count; ++i)
    {
        m_selectedIndices.insert(i);
    }
    if (count > 0)
    {
        m_selectedIndex = count - 1;
        m_anchorRow = 0;
    }
    Invalidate();
}

void CustomListControl::ClearSelection()
{
    m_selectedIndices.clear();
    m_selectedIndex = -1;
    m_anchorRow = -1;
    Invalidate();
}

bool CustomListControl::IsRowSelected(int row) const
{
    if (m_isMultiSelect)
    {
        return m_selectedIndices.find(row) != m_selectedIndices.end();
    }
    return row == m_selectedIndex;
}

void CustomListControl::ToggleRowSelection(int row)
{
    if (row < 0 || row >= GetItemCount()) return;
    if (m_selectedIndices.find(row) != m_selectedIndices.end())
    {
        m_selectedIndices.erase(row);
        if (m_selectedIndex == row)
        {
            m_selectedIndex = m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin();
        }
    }
    else
    {
        m_selectedIndices.insert(row);
        m_selectedIndex = row;
        m_anchorRow = row;
    }
    Invalidate();
}

// ---------------------------------------------------------------------------
// Focus & Layout
// ---------------------------------------------------------------------------
void CustomListControl::SetFocus()
{
    if (m_hWnd) ::SetFocus(m_hWnd);
}

void CustomListControl::Invalidate()
{
    if (m_hWnd) InvalidateRect(m_hWnd, NULL, FALSE);
}

void CustomListControl::Resize(int x, int y, int width, int height)
{
    if (m_hWnd) MoveWindow(m_hWnd, x, y, width, height, TRUE);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
RECT CustomListControl::GetListRect() const
{
    RECT rc = { 0, m_headerHeight, 0, 0 };
    if (m_hWnd)
    {
        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        bool hasV = m_vScroll.IsVisible();
        bool hasH = m_hScroll.IsVisible();
        rc.right  = rcClient.right  - (hasV ? V_SCROLLBAR_WIDTH : 0);
        rc.bottom = rcClient.bottom - (hasH ? H_SCROLLBAR_HEIGHT : 0);
    }
    return rc;
}

int CustomListControl::GetUsableWidth() const
{
    RECT rc = GetListRect();
    return rc.right - rc.left;
}

int CustomListControl::GetVisibleRows() const
{
    RECT rc = GetListRect();
    int h = rc.bottom - rc.top;
    if (h < 1) return 1;
    return h / m_rowHeight;
}

std::wstring CustomListControl::GetCellText(int itemIndex, int subItemIndex) const
{
    if (m_isVirtual)
    {
        if (m_callback)
            return m_callback(itemIndex, subItemIndex, m_callbackParam);
        return L"";
    }
    if (itemIndex >= 0 && itemIndex < (int)m_items.size())
    {
        if (subItemIndex >= 0 && subItemIndex < (int)m_items[itemIndex].size())
            return m_items[itemIndex][subItemIndex];
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Child Scrollbar: Create, Layout, Theme
// ---------------------------------------------------------------------------
void CustomListControl::CreateScrollbars()
{
    m_vScroll.SetOrientation(ScrollBarOrientation::Vertical);
    m_hScroll.SetOrientation(ScrollBarOrientation::Horizontal);
    m_vScroll.SetGutterColor(CustomUITheme::DarkBackground);
    m_hScroll.SetGutterColor(CustomUITheme::DarkBackground);
}

void CustomListControl::ApplyScrollbarTheme()
{
}

void CustomListControl::LayoutScrollbars()
{
    if (!m_hWnd) return;

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int cw = rcClient.right;
    int ch = rcClient.bottom;

    // Determine which scrollbars should be visible
    int totalColsWidth = 0;
    for (const auto& col : m_columns) totalColsWidth += col.width;

    int itemCount = GetItemCount();
    int innerH    = ch - H_SCROLLBAR_HEIGHT;  // height when hscroll visible
    int innerW    = cw - V_SCROLLBAR_WIDTH;  // width  when vscroll visible

    // Need vertical?
    int visRows = (ch - m_headerHeight) / m_rowHeight;
    bool needV  = (itemCount > visRows);
    // Need horizontal?
    bool needH  = (totalColsWidth > (needV ? innerW : cw));

    // Re-check vertical with hscroll taking space
    if (needH) visRows = (innerH - m_headerHeight) / m_rowHeight;
    if (!needV && needH) needV = (itemCount > visRows);

    m_vScroll.SetVisible(needV);
    m_hScroll.SetVisible(needH);

    int rightEdge  = cw - (needV ? V_SCROLLBAR_WIDTH : 0);
    int bottomEdge = ch - (needH ? H_SCROLLBAR_HEIGHT : 0);

    int vScrollBottom = needH ? bottomEdge : ch;
    RECT rcV = { rightEdge, m_headerHeight + 1, cw, vScrollBottom };
    m_vScroll.SetBounds(rcV);

    RECT rcH = { 0, bottomEdge, cw, ch };
    m_hScroll.SetBounds(rcH);
}

// ---------------------------------------------------------------------------
// UpdateScrollbars
// ---------------------------------------------------------------------------
void CustomListControl::UpdateScrollbars()
{
    if (!m_hWnd) return;

    LayoutScrollbars();

    RECT rc = GetListRect();
    int listW     = rc.right - rc.left;
    int listH     = rc.bottom - rc.top;
    int visRows   = listH / m_rowHeight;
    if (visRows < 1) visRows = 1;
    int itemCount = GetItemCount();

    int totalColsWidth = 0;
    for (const auto& col : m_columns) totalColsWidth += col.width;

    // --- Vertical scrollbar ---
    m_vScroll.SetRange(0, itemCount > 0 ? itemCount - 1 : 0, visRows);
    m_vScroll.SetPos(m_scrollY);
    m_scrollY = m_vScroll.GetPos();

    // --- Horizontal scrollbar ---
    m_hScroll.SetRange(0, totalColsWidth, listW);
    m_hScroll.SetPos(m_scrollX);
    m_scrollX = m_hScroll.GetPos();
}

// ---------------------------------------------------------------------------
// Window Procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK CustomListControl::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CustomListControl* pThis = (CustomListControl*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (uMsg == WM_NCCREATE)
    {
        LPCREATESTRUCTW pCreate = (LPCREATESTRUCTW)lParam;
        pThis = (CustomListControl*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
    }

    if (pThis)
        return pThis->HandleMessage(uMsg, wParam, lParam);

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT CustomListControl::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
        ApplyScrollbarTheme();
        Invalidate();
        return 0;
    }

    case WM_CREATE:
    {
        CreateScrollbars();
        return 0;
    }

    case WM_CHAR:
    {
        wchar_t ch = (wchar_t)wParam;
        if (ch < 32) break; // Ignore control codes (handled in WM_KEYDOWN)

        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bAlt  = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (bCtrl || bAlt) break;

        DWORD now = GetTickCount();
        if (now - m_lastTypeAheadTime > 1000)
        {
            m_typeAheadBuffer.clear();
        }
        m_lastTypeAheadTime = now;
        m_typeAheadBuffer += towlower(ch);

        int itemCount = GetItemCount();
        if (itemCount <= 0) return 0;

        int nameCol = 0;
        if (!m_columns.empty() && m_columns[0].title == L"No." && m_columns.size() > 1)
        {
            nameCol = 1;
        }

        bool isSingleCharRepeat = true;
        for (wchar_t c : m_typeAheadBuffer)
        {
            if (c != m_typeAheadBuffer[0]) { isSingleCharRepeat = false; break; }
        }

        int foundIdx = -1;

        if (isSingleCharRepeat)
        {
            wchar_t targetChar = m_typeAheadBuffer[0];
            int startIdx = (m_selectedIndex >= 0) ? (m_selectedIndex + 1) : 0;
            for (int step = 0; step < itemCount; ++step)
            {
                int i = (startIdx + step) % itemCount;
                std::wstring cellText = GetCellText(i, nameCol);
                if (!cellText.empty() && towlower(cellText[0]) == targetChar)
                {
                    foundIdx = i;
                    break;
                }
            }
        }
        else
        {
            int startIdx = (m_selectedIndex >= 0) ? m_selectedIndex : 0;
            for (int step = 0; step < itemCount; ++step)
            {
                int i = (startIdx + step) % itemCount;
                std::wstring cellText = GetCellText(i, nameCol);
                std::wstring cellLower = cellText;
                for (wchar_t& c : cellLower) c = towlower(c);
                if (cellLower.rfind(m_typeAheadBuffer, 0) == 0)
                {
                    foundIdx = i;
                    break;
                }
            }
        }

        if (foundIdx != -1)
        {
            if (m_isMultiSelect)
            {
                m_selectedIndices.clear();
                m_selectedIndices.insert(foundIdx);
                m_selectedIndex = foundIdx;
                m_anchorRow = foundIdx;
                EnsureVisible(foundIdx);
                Invalidate();
            }
            else
            {
                SetSelectedIndex(foundIdx);
            }

            NMHDR nmhdr = { 0 };
            nmhdr.hwndFrom = m_hWnd;
            nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmhdr.code     = NM_CLICK;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        int width  = rcClient.right  - rcClient.left;
        int height = rcClient.bottom - rcClient.top;

        if (width <= 0 || height <= 0)
        {
            EndPaint(m_hWnd, &ps);
            return 0;
        }

        // Double-buffered off-screen DC
        HDC hMemDC       = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp  = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP hOldBmp  = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        HFONT hOldFont = NULL;
        if (hUIFont) hOldFont = (HFONT)SelectObject(hMemDC, hUIFont);

        // ALWAYS set background mode to transparent immediately.
        // This fixes the white-box empty list header label glitch.
        SetBkMode(hMemDC, TRANSPARENT);

        // Theme colors
        COLORREF bgCol       = CustomUITheme::DarkBackground;
        COLORREF textCol     = CustomUITheme::TextPrimary;
        COLORREF headerBgCol = CustomUITheme::DarkHeaderBackground;
        COLORREF dividerCol  = RGB(65,  65,  65);

        // Background fill (only the list area, not over scrollbars)
        RECT rcList = GetListRect();
        RECT rcFill = rcList;
        rcFill.top  = 0; // include header
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hMemDC, &rcFill, hbrBg);
        DeleteObject(hbrBg);

        int listW = rcList.right - rcList.left;
        bool hasV = m_vScroll.IsVisible();
        bool hasH = m_hScroll.IsVisible();

        int itemCount  = GetItemCount();
        int visibleRows = (rcList.bottom - rcList.top) / m_rowHeight + 1;
        int startRow   = m_scrollY;
        int endRow     = startRow + visibleRows;
        if (endRow > itemCount) endRow = itemCount;

        // --- Draw row cells with strict GDI clipping to rcList ---
        int savedDCRows = SaveDC(hMemDC);
        IntersectClipRect(hMemDC, 0, m_headerHeight, listW, rcList.bottom);

        for (int r = startRow; r < endRow; ++r)
        {
            int yTop = m_headerHeight + (r - m_scrollY) * m_rowHeight;
            RECT rcRow = { 0, yTop, listW, yTop + m_rowHeight };

            bool isRowSel = IsRowSelected(r);
            if (isRowSel)
            {
                HBRUSH hbrSel = CreateSolidBrush(RGB(38, 79, 120));
                FillRect(hMemDC, &rcRow, hbrSel);
                DeleteObject(hbrSel);
            }

            COLORREF rowTextCol = textCol;
            for (size_t c_chk = 0; c_chk < m_columns.size(); ++c_chk)
            {
                std::wstring chkText = GetCellText(r, (int)c_chk);
                if (chkText == L"Broken")
                {
                    rowTextCol = RGB(255, 100, 100);
                    break;
                }
                else if (chkText == L"Fixed")
                {
                    rowTextCol = RGB(50, 205, 100);
                    break;
                }
            }
            SetTextColor(hMemDC, rowTextCol);

            int xAccum = -m_scrollX;
            for (size_t c = 0; c < m_columns.size(); ++c)
            {
                int colWidth = m_columns[c].width;
                RECT rcCell  = { xAccum, yTop, xAccum + colWidth, yTop + m_rowHeight };

                if (rcCell.right > 0 && rcCell.left < listW)
                {
                    std::wstring cellText = GetCellText(r, (int)c);
                    if (m_columns[c].title == L"Orientation")
                    {
                        RECT rcBtn = rcCell;
                        rcBtn.left   += 4;
                        rcBtn.top    += 3;
                        rcBtn.right  -= 4;
                        rcBtn.bottom -= 3;

                        if (rcBtn.right > listW - 4) rcBtn.right = listW - 4;
                        if (rcBtn.left < rcBtn.right)
                        {
                            bool isHovered = (r == m_hoveredRow && (int)c == m_hoveredCol);
                            bool isPressed = (r == m_pressedRow && (int)c == m_pressedCol);

                            COLORREF btnBg = RGB(45, 45, 45);
                            COLORREF btnBorder = RGB(70, 70, 70);
                            COLORREF btnTextCol = RGB(240, 240, 240);

                            if (isPressed)
                            {
                                btnBg = RGB(35, 35, 35);
                                btnBorder = RGB(50, 50, 50);
                            }
                            else if (isHovered)
                            {
                                btnBg = RGB(55, 55, 55);
                                btnBorder = RGB(90, 90, 90);
                            }

                            HBRUSH hbrBtn = CreateSolidBrush(btnBg);
                            HPEN hpenBtn = CreatePen(PS_SOLID, 1, btnBorder);
                            HGDIOBJ oldBrush = SelectObject(hMemDC, hbrBtn);
                            HGDIOBJ oldPen = SelectObject(hMemDC, hpenBtn);

                            RoundRect(hMemDC, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 4, 4);

                            SelectObject(hMemDC, oldBrush);
                            SelectObject(hMemDC, oldPen);
                            DeleteObject(hbrBtn);
                            DeleteObject(hpenBtn);

                            if (cellText == L"Flipped")
                            {
                                btnTextCol = RGB(100, 200, 255);
                            }

                            COLORREF oldTxt = SetTextColor(hMemDC, btnTextCol);
                            int oldBkMode = SetBkMode(hMemDC, TRANSPARENT);

                            UINT btnFmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
                            DrawTextW(hMemDC, cellText.c_str(), -1, &rcBtn, btnFmt);

                            SetTextColor(hMemDC, oldTxt);
                            SetBkMode(hMemDC, oldBkMode);

                            xAccum += colWidth;
                            continue;
                        }
                    }

                    RECT rcText = rcCell;
                    rcText.left  += 8;
                    rcText.right -= 8;
                    if (rcText.right > listW - 8) rcText.right = listW - 8;

                    UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
                    if (m_columns[c].align == 1) fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
                    else if (m_columns[c].align == 2) fmt = DT_RIGHT  | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

                    if (c == 0 && cellText.rfind(L"● ", 0) == 0)
                    {
                        int dotRadius = 4;
                        int dotX = rcText.left + dotRadius + 1;
                        int dotY = (rcText.top + rcText.bottom) / 2;
                        
                        COLORREF dotCol = RGB(0, 160, 255); // Crisp Bright Blue
                        HBRUSH hbrDot = CreateSolidBrush(dotCol);
                        HPEN hpenDot = CreatePen(PS_SOLID, 1, dotCol);
                        HGDIOBJ oldBr = SelectObject(hMemDC, hbrDot);
                        HGDIOBJ oldPn = SelectObject(hMemDC, hpenDot);
                        
                        Ellipse(hMemDC, dotX - dotRadius, dotY - dotRadius, dotX + dotRadius + 1, dotY + dotRadius + 1);
                        
                        SelectObject(hMemDC, oldBr);
                        SelectObject(hMemDC, oldPn);
                        DeleteObject(hbrDot);
                        DeleteObject(hpenDot);

                        rcText.left += (dotRadius * 2 + 6);
                        std::wstring cleanText = cellText.substr(2);
                        DrawTextW(hMemDC, cleanText.c_str(), -1, &rcText, fmt);
                    }
                    else
                    {
                        DrawTextW(hMemDC, cellText.c_str(), -1, &rcText, fmt);
                    }
                }

                xAccum += colWidth;
            }
        }

        // --- Draw unified vertical column separators top-to-bottom within rcList ---
        HPEN hPen    = CreatePen(PS_SOLID, 1, dividerCol);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPen);

        int xAccumDiv = -m_scrollX;
        for (size_t c = 0; c < m_columns.size() - 1; ++c)
        {
            xAccumDiv += m_columns[c].width;
            if (xAccumDiv > 0 && xAccumDiv < listW)
            {
                MoveToEx(hMemDC, xAccumDiv, m_headerHeight, NULL);
                LineTo(hMemDC, xAccumDiv, rcList.bottom);
            }
        }

        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPen);

        RestoreDC(hMemDC, savedDCRows);

        // Cleanly paint scrollbar gutter backgrounds after row cells
        if (hasV)
        {
            // Vertical Gutter background (strictly terminates above horizontal tray)
            RECT rcGutter = { listW, m_headerHeight, width, hasH ? rcList.bottom : height };
            HBRUSH hbrGutter = CreateSolidBrush(bgCol);
            FillRect(hMemDC, &rcGutter, hbrGutter);
            DeleteObject(hbrGutter);
        }

        if (hasH)
        {
            // Dedicated horizontal scrollbar tray with differentiated background
            COLORREF hTrayBg = RGB(28, 28, 28);
            RECT rcGutterH = { 0, rcList.bottom, width, height };
            HBRUSH hbrGutterH = CreateSolidBrush(hTrayBg);
            FillRect(hMemDC, &rcGutterH, hbrGutterH);
            DeleteObject(hbrGutterH);
        }

        // --- Draw header background ---
        RECT rcHeader = { 0, 0, width, m_headerHeight };
        HBRUSH hbrHeader = CreateSolidBrush(headerBgCol);
        FillRect(hMemDC, &rcHeader, hbrHeader);
        DeleteObject(hbrHeader);
 
        SetTextColor(hMemDC, CustomUITheme::TextPrimary);
        int xAccumH = -m_scrollX;
        for (size_t c = 0; c < m_columns.size(); ++c)
        {
            int colWidth = m_columns[c].width;
            if (c == m_columns.size() - 1)
            {
                colWidth = width - xAccumH;
            }
            RECT rcCell  = { xAccumH, 0, xAccumH + colWidth, m_headerHeight };
 
            if (rcCell.right > 0 && rcCell.left < width)
            {
                // Split the cell into Left (Sort) and Right (Dropdown) zones
                RECT rcLeftHighlight = rcCell;
                rcLeftHighlight.right -= 20;
                RECT rcRightHighlight = rcCell;
                rcRightHighlight.left = rcRightHighlight.right - 20;
 
                COLORREF leftBgColor = CLR_INVALID;
                COLORREF rightBgColor = CLR_INVALID;
 
                if ((int)c == m_pressedHeaderColIndex)
                {
                    if (m_pressedHeaderInDropdown)
                    {
                        leftBgColor = RGB(45, 45, 45);
                        rightBgColor = RGB(60, 60, 60);
                    }
                    else
                    {
                        leftBgColor = RGB(55, 55, 55);
                        rightBgColor = RGB(55, 55, 55);
                    }
                }
                else if ((int)c == m_hoverHeaderColIndex)
                {
                    leftBgColor = RGB(45, 45, 45);
                    if (m_hoverHeaderInDropdown)
                    {
                        rightBgColor = RGB(55, 55, 55);
                    }
                    else
                    {
                        rightBgColor = RGB(45, 45, 45);
                    }
                }
 
                if (leftBgColor != CLR_INVALID)
                {
                    HBRUSH hbrLeft = CreateSolidBrush(leftBgColor);
                    FillRect(hMemDC, &rcLeftHighlight, hbrLeft);
                    DeleteObject(hbrLeft);
                }
                if (rightBgColor != CLR_INVALID)
                {
                    HBRUSH hbrRight = CreateSolidBrush(rightBgColor);
                    FillRect(hMemDC, &rcRightHighlight, hbrRight);
                    DeleteObject(hbrRight);
                }
 
                RECT rcText = rcCell;
                rcText.left  += 8;
                rcText.right -= 22;
                if (rcText.right < rcText.left) rcText.right = rcText.left;
                if (rcText.right > width - 22) rcText.right = width - 22;
                DrawTextW(hMemDC, m_columns[c].title.c_str(), -1, &rcText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if ((int)c == m_hoverHeaderColIndex)
                {
                    SIZE size;
                    GetTextExtentPoint32W(hMemDC, m_columns[c].title.c_str(), (int)m_columns[c].title.length(), &size);
                    int arrowX = rcText.left + size.cx + 8;
                    int arrowY = m_headerHeight / 2;

                    if (arrowX + 4 < rcLeftHighlight.right)
                    {
                        COLORREF arrowColor = RGB(160, 160, 160);
                        HPEN hArrowPen = CreatePen(PS_SOLID, 1, arrowColor);
                        HPEN hOldArrowPen = (HPEN)SelectObject(hMemDC, hArrowPen);
                        HBRUSH hArrowBrush = CreateSolidBrush(arrowColor);
                        HBRUSH hOldArrowBrush = (HBRUSH)SelectObject(hMemDC, hArrowBrush);

                        POINT pts[3];
                        bool ascending = ((int)c == m_sortColIndex) ? m_sortAscending : true;
                        if (ascending)
                        {
                            pts[0] = { arrowX, arrowY - 2 };
                            pts[1] = { arrowX - 3, arrowY + 2 };
                            pts[2] = { arrowX + 3, arrowY + 2 };
                        }
                        else
                        {
                            pts[0] = { arrowX, arrowY + 2 };
                            pts[1] = { arrowX - 3, arrowY - 2 };
                            pts[2] = { arrowX + 3, arrowY - 2 };
                        }
                        Polygon(hMemDC, pts, 3);

                        SelectObject(hMemDC, hOldArrowPen);
                        DeleteObject(hArrowPen);
                        SelectObject(hMemDC, hOldArrowBrush);
                        DeleteObject(hArrowBrush);
                    }
                }

                if ((int)c == m_hoverHeaderColIndex)
                {
                    HPEN hDivPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
                    HPEN hOldDivPen = (HPEN)SelectObject(hMemDC, hDivPen);
                    MoveToEx(hMemDC, rcCell.right - 20, 0, NULL);
                    LineTo(hMemDC, rcCell.right - 20, m_headerHeight);
                    SelectObject(hMemDC, hOldDivPen);
                    DeleteObject(hDivPen);

                    int arrowX = rcCell.right - 10;
                    int arrowY = m_headerHeight / 2;

                    COLORREF arrowColor = RGB(160, 160, 160);
                    HPEN hArrowPen = CreatePen(PS_SOLID, 1, arrowColor);
                    HPEN hOldArrowPen = (HPEN)SelectObject(hMemDC, hArrowPen);
                    HBRUSH hArrowBrush = CreateSolidBrush(arrowColor);
                    HBRUSH hOldArrowBrush = (HBRUSH)SelectObject(hMemDC, hArrowBrush);

                    POINT pts[3];
                    pts[0] = { arrowX, arrowY + 2 };
                    pts[1] = { arrowX - 3, arrowY - 2 };
                    pts[2] = { arrowX + 3, arrowY - 2 };
                    Polygon(hMemDC, pts, 3);

                    SelectObject(hMemDC, hOldArrowPen);
                    DeleteObject(hArrowPen);
                    SelectObject(hMemDC, hOldArrowBrush);
                    DeleteObject(hArrowBrush);
                }
            }
            xAccumH += colWidth;
        }

        // Draw vertical column separators in header
        HPEN hPenHDiv = CreatePen(PS_SOLID, 1, dividerCol);
        HPEN hOldPenHDiv = (HPEN)SelectObject(hMemDC, hPenHDiv);
        int xAccumHDiv = -m_scrollX;
        for (size_t c = 0; c < m_columns.size() - 1; ++c)
        {
            xAccumHDiv += m_columns[c].width;
            if (xAccumHDiv > 0 && xAccumHDiv < width)
            {
                MoveToEx(hMemDC, xAccumHDiv, 0, NULL);
                LineTo(hMemDC, xAccumHDiv, m_headerHeight);
            }
        }
        SelectObject(hMemDC, hOldPenHDiv);
        DeleteObject(hPenHDiv);

        // Horizontal divider under header (extended to full width)
        HPEN hDivHeader = CreatePen(PS_SOLID, 1, dividerCol);
        HPEN hOldDivH = (HPEN)SelectObject(hMemDC, hDivHeader);
        MoveToEx(hMemDC, 0, m_headerHeight, NULL);
        LineTo(hMemDC, width, m_headerHeight);
        SelectObject(hMemDC, hOldDivH);
        DeleteObject(hDivHeader);

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        // Blit back buffer
        
        // Draw Drop Target Insertion Indicator Line if dragging
        if (m_dropTargetIndex >= 0)
        {
            int yLine = m_headerHeight + (m_dropTargetIndex - m_scrollY) * m_rowHeight;
            if (yLine >= m_headerHeight && yLine <= height)
            {
                // Vibrant Accent Blue line
                HPEN hPenLine = CreatePen(PS_SOLID, 3, RGB(0, 150, 255));
                HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenLine);
                MoveToEx(hMemDC, 4, yLine, NULL);
                LineTo(hMemDC, width - 4, yLine);
                SelectObject(hMemDC, hOldP);
                DeleteObject(hPenLine);

                // Left arrow indicator ◄
                POINT ptLeft[3] = { { 4, yLine - 6 }, { 4, yLine + 6 }, { 12, yLine } };
                HBRUSH hBrTri = CreateSolidBrush(RGB(0, 150, 255));
                HBRUSH hOldBrTri = (HBRUSH)SelectObject(hMemDC, hBrTri);
                HPEN hPenTri = CreatePen(PS_SOLID, 1, RGB(0, 150, 255));
                HPEN hOldPTri = (HPEN)SelectObject(hMemDC, hPenTri);
                Polygon(hMemDC, ptLeft, 3);

                // Right arrow indicator ►
                POINT ptRight[3] = { { width - 4, yLine - 6 }, { width - 4, yLine + 6 }, { width - 12, yLine } };
                Polygon(hMemDC, ptRight, 3);

                SelectObject(hMemDC, hOldBrTri);
                SelectObject(hMemDC, hOldPTri);
                DeleteObject(hBrTri);
                DeleteObject(hPenTri);
            }
        }

        // Draw Marquee / Rubber-band Selection Box
        if (m_isMarqueeSelecting)
        {
            RECT rcNorm;
            rcNorm.left   = (std::min)((LONG)m_rcMarquee.left, (LONG)m_rcMarquee.right);
            rcNorm.right  = (std::max)((LONG)m_rcMarquee.left, (LONG)m_rcMarquee.right);
            rcNorm.top    = (std::min)((LONG)m_rcMarquee.top, (LONG)m_rcMarquee.bottom);
            rcNorm.bottom = (std::max)((LONG)m_rcMarquee.top, (LONG)m_rcMarquee.bottom);

            // Clamp rcNorm to content list area [0, m_headerHeight, listW, rcList.bottom]
            if (rcNorm.top < m_headerHeight) rcNorm.top = m_headerHeight;
            if (rcNorm.bottom > rcList.bottom) rcNorm.bottom = rcList.bottom;
            if (rcNorm.left < 0) rcNorm.left = 0;
            if (rcNorm.right > listW) rcNorm.right = listW;

            int bw = rcNorm.right - rcNorm.left;
            int bh = rcNorm.bottom - rcNorm.top;
            if (bw > 2 && bh > 2)
            {
                HDC hBoxDC = CreateCompatibleDC(hMemDC);
                HBITMAP hBoxBmp = CreateCompatibleBitmap(hMemDC, bw, bh);
                HBITMAP hOldBB = (HBITMAP)SelectObject(hBoxDC, hBoxBmp);
                HBRUSH hbrBlue = CreateSolidBrush(RGB(0, 120, 215));
                RECT rcFullBox = { 0, 0, bw, bh };
                FillRect(hBoxDC, &rcFullBox, hbrBlue);
                DeleteObject(hbrBlue);

                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 60, 0 };
                AlphaBlend(hMemDC, rcNorm.left, rcNorm.top, bw, bh, hBoxDC, 0, 0, bw, bh, bf);

                SelectObject(hBoxDC, hOldBB);
                DeleteObject(hBoxBmp);
                DeleteDC(hBoxDC);

                HPEN hPenMarquee = CreatePen(PS_SOLID, 1, RGB(0, 150, 255));
                HGDIOBJ oldP = SelectObject(hMemDC, hPenMarquee);
                HGDIOBJ oldB = SelectObject(hMemDC, GetStockObject(NULL_BRUSH));
                Rectangle(hMemDC, rcNorm.left, rcNorm.top, rcNorm.right, rcNorm.bottom);
                SelectObject(hMemDC, oldB);
                SelectObject(hMemDC, oldP);
                DeleteObject(hPenMarquee);
            }
        }

        // Draw Custom Fluent Scrollbars
        m_vScroll.Paint(hMemDC, bgCol);
        m_hScroll.Paint(hMemDC, bgCol);

        // Draw 1px card frame (left, right, bottom) enclosing list and scrollbar tray
        if (m_bDrawCardBorder)
        {
            COLORREF borderCol = RGB(55, 55, 55);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
            HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenBorder);

            // Left border
            MoveToEx(hMemDC, 0, 0, NULL);
            LineTo(hMemDC, 0, height);

            // Right border
            MoveToEx(hMemDC, width - 1, 0, NULL);
            LineTo(hMemDC, width - 1, height);

            // Bottom border
            MoveToEx(hMemDC, 0, height - 1, NULL);
            LineTo(hMemDC, width, height - 1);

            SelectObject(hMemDC, hOldPen);
            DeleteObject(hPenBorder);
        }

        BitBlt(hdc, 0, 0, width, height, hMemDC, 0, 0, SRCCOPY);

        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == CustomScrollBar::TIMER_ANIM_ID)
        {
            bool vRedraw = m_vScroll.OnTimer(m_hWnd);
            bool hRedraw = m_hScroll.OnTimer(m_hWnd);
            if (vRedraw || hRedraw)
            {
                m_scrollY = m_vScroll.GetPos();
                m_scrollX = m_hScroll.GetPos();
                Invalidate();
            }
            return 0;
        }
        else if (wParam == TIMER_AUTOSCROLL_ID)
        {
            if (!m_isMarqueeSelecting && !m_bIsItemDragging && m_dropTargetIndex < 0)
            {
                StopAutoScroll();
                return 0;
            }

            POINT ptCursor;
            GetCursorPos(&ptCursor);
            ScreenToClient(m_hWnd, &ptCursor);

            m_marqueeCurrentX = ptCursor.x;
            m_marqueeCurrentY = ptCursor.y;

            RECT rcList = GetListRect();
            const int EDGE_ZONE = 24;
            int maxScrollY = m_vScroll.GetMaxScrollPos();
            int maxScrollX = m_hScroll.GetMaxScrollPos();
            int listW = rcList.right - rcList.left;

            int scrollDeltaY = 0;
            if (ptCursor.y < m_headerHeight + EDGE_ZONE)
            {
                int dist = (m_headerHeight + EDGE_ZONE) - ptCursor.y;
                scrollDeltaY = -(1 + dist / 20);
            }
            else if (ptCursor.y > rcList.bottom - EDGE_ZONE)
            {
                int dist = ptCursor.y - (rcList.bottom - EDGE_ZONE);
                scrollDeltaY = (1 + dist / 20);
            }

            int scrollDeltaX = 0;
            if (m_hScroll.IsVisible())
            {
                if (ptCursor.x < EDGE_ZONE)
                {
                    int dist = EDGE_ZONE - ptCursor.x;
                    scrollDeltaX = -(8 + dist * 2);
                }
                else if (ptCursor.x > listW - EDGE_ZONE)
                {
                    int dist = ptCursor.x - (listW - EDGE_ZONE);
                    scrollDeltaX = (8 + dist * 2);
                }
            }

            if (scrollDeltaY != 0)
            {
                int oldY = m_scrollY;
                m_scrollY += scrollDeltaY;
                if (m_scrollY < 0) m_scrollY = 0;
                if (m_scrollY > maxScrollY) m_scrollY = maxScrollY;
                if (m_scrollY != oldY)
                {
                    m_vScroll.SetPos(m_scrollY);
                }
            }

            if (scrollDeltaX != 0)
            {
                int oldX = m_scrollX;
                m_scrollX += scrollDeltaX;
                if (m_scrollX < 0) m_scrollX = 0;
                if (m_scrollX > maxScrollX) m_scrollX = maxScrollX;
                if (m_scrollX != oldX)
                {
                    m_hScroll.SetPos(m_scrollX);
                }
            }

            if (m_isMarqueeSelecting)
            {
                UpdateMarqueeSelection(m_marqueeCurrentX, m_marqueeCurrentY);
                CheckAutoScroll(m_marqueeCurrentX, m_marqueeCurrentY);
            }
            else if (m_bIsItemDragging && m_bAllowRearrange)
            {
                int newDropIdx = GetDropIndexFromPoint(ptCursor);
                m_dropTargetIndex = newDropIdx;
                POINT ptScreen = ptCursor;
                ClientToScreen(m_hWnd, &ptScreen);
                FluentDragGhost::Move(ptScreen, true, L"Reorder");
                CheckAutoScroll(ptCursor.x, ptCursor.y);
                Invalidate();
            }
            else if (m_dropTargetIndex >= 0)
            {
                int newDropIdx = GetDropIndexFromPoint(ptCursor);
                m_dropTargetIndex = newDropIdx;
                CheckDragAutoScroll(ptCursor);
                Invalidate();
            }
            else
            {
                StopAutoScroll();
            }

            return 0;
        }
        break;
    }

    case WM_SIZE:
    {
        UpdateScrollbars();
        Invalidate();
        return 0;
    }

    case WM_SETFONT:
    {
        Invalidate();
        return 0;
    }

    case WM_SETFOCUS:
    {
        m_hasFocus = true;
        Invalidate();

        NMHDR nmhdr     = { 0 };
        nmhdr.hwndFrom  = m_hWnd;
        nmhdr.idFrom    = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
        nmhdr.code      = NM_SETFOCUS;
        SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
        return 0;
    }

    case WM_KILLFOCUS:
    {
        if (m_isMarqueeSelecting)
        {
            m_isMarqueeSelecting = false;
            m_bPotentialDrag = false;
            StopAutoScroll();
            ReleaseCapture();
        }
        m_vScroll.OnLButtonUp({ 0, 0 }, m_hWnd);
        m_hScroll.OnLButtonUp({ 0, 0 }, m_hWnd);
        m_hasFocus = false;
        Invalidate();
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        short delta = (short)HIWORD(wParam);
        if (m_vScroll.OnMouseWheel(delta, 3, m_hWnd))
        {
            m_scrollY = m_vScroll.GetPos();
            Invalidate();
        }
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        ScreenToClient(m_hWnd, &pt);
        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);

        // Outer 6px boundary yields to parent splitters / resize borders
        if (pt.x >= rcClient.right - 6 || pt.y >= rcClient.bottom - 6 || pt.x <= 6)
        {
            return HTTRANSPARENT;
        }
        break;
    }

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hWnd, &pt);

        int xAccum = -m_scrollX;
        bool nearBoundary = false;
        if (m_columns.size() > 1)
        {
            for (size_t c = 0; c < m_columns.size() - 1; ++c)
            {
                xAccum += m_columns[c].width;
                if (abs(pt.x - xAccum) <= 2) { nearBoundary = true; break; }
            }
        }

        if (m_isResizing || (nearBoundary && pt.y <= m_headerHeight))
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        ::SetFocus(m_hWnd);

        if (m_vScroll.OnLButtonDown(pt, m_hWnd))
        {
            m_scrollY = m_vScroll.GetPos();
            Invalidate();
            return 0;
        }

        if (m_hScroll.OnLButtonDown(pt, m_hWnd))
        {
            m_scrollX = m_hScroll.GetPos();
            Invalidate();
            return 0;
        }

        RECT rcList = GetListRect();
        int listW = rcList.right - rcList.left;
        if (m_vScroll.IsVisible() && x >= listW && y > m_headerHeight)
        {
            return 0;
        }
        if (m_hScroll.IsVisible() && y >= rcList.bottom)
        {
            return 0;
        }

        if (y <= m_headerHeight)
        {
            // Column resize hit-test
            int xAccum = -m_scrollX;
            if (m_columns.size() > 1)
            {
                for (size_t c = 0; c < m_columns.size() - 1; ++c)
                {
                    xAccum += m_columns[c].width;
                    if (abs(x - xAccum) <= 2)
                    {
                        m_isResizing     = true;
                        m_resizeColIndex = (int)c;
                        m_resizeStartX   = x;
                        m_resizeStartWidth = m_columns[c].width;
                        SetCapture(m_hWnd);
                        return 0;
                    }
                }
            }

            // If not resizing, it's a column header press!
            int xPos = -m_scrollX;
            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            int cw = rcClient.right;

            for (size_t c = 0; c < m_columns.size(); ++c)
            {
                int colWidth = m_columns[c].width;
                if (c == m_columns.size() - 1)
                {
                    colWidth = cw - xPos;
                }
                int nextX = xPos + colWidth;
                if (x >= xPos && x < nextX)
                {
                    m_pressedHeaderColIndex = (int)c;
                    m_pressedHeaderInDropdown = (x >= nextX - 20);
                    SetCapture(m_hWnd);
                    RECT rcHeader = { 0, 0, cw, m_headerHeight };
                    InvalidateRect(m_hWnd, &rcHeader, TRUE);
                    break;
                }
                xPos = nextX;
            }
        }
        else
        {
            int clickedRow = m_scrollY + (y - m_headerHeight) / m_rowHeight;
            int itemCount = GetItemCount();
            bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            int xPos = -m_scrollX;
            int clickedCol = -1;
            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            int cw = rcClient.right;
            for (size_t c = 0; c < m_columns.size(); ++c)
            {
                int colWidth = m_columns[c].width;
                if (c == m_columns.size() - 1)
                {
                    colWidth = cw - xPos;
                }
                int nextX = xPos + colWidth;
                if (x >= xPos && x < nextX)
                {
                    clickedCol = (int)c;
                    break;
                }
                xPos = nextX;
            }

            if (clickedRow >= 0 && clickedRow < itemCount)
            {
                // Check if Orientation button was clicked
                if (clickedCol >= 0 && m_columns[clickedCol].title == L"Orientation")
                {
                    m_pressedRow = clickedRow;
                    m_pressedCol = clickedCol;
                    SetCapture(m_hWnd);
                    Invalidate();
                    return 0;
                }

                bool isAlreadySelected = (m_selectedIndices.count(clickedRow) > 0);

                if ((m_bAllowRearrange || m_bAllowTransferSource) && isAlreadySelected && !bCtrl && !bShift)
                {
                    // Clicked on ALREADY-SELECTED row in Rearrange or Transfer list -> Potential Item Drag
                    m_bPotentialItemDrag = true;
                    m_bPotentialDrag = false;
                    m_ptDragStart = { x, y };
                    m_pendingClickedRow = clickedRow;
                    m_bPendingCtrl = bCtrl;
                    m_bPendingShift = bShift;
                    SetCapture(m_hWnd);
                }
                else if (!m_bAllowMarquee)
                {
                    // Marquee disabled (Consists Manager) -> Direct single-click selection
                    m_selectedIndices.clear();
                    m_selectedIndices.insert(clickedRow);
                    m_selectedIndex = clickedRow;
                    m_anchorRow = clickedRow;
                    Invalidate();

                    m_bPotentialItemDrag = false;
                    m_bPotentialDrag = false;
                    m_pendingClickedRow = clickedRow;

                    NMHDR nmhdr = { 0 };
                    nmhdr.hwndFrom = m_hWnd;
                    nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                    nmhdr.code     = NM_CLICK;
                    SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
                    return 0;
                }
                else
                {
                    // Marquee allowed & clicked on UNSELECTED row -> Candidate for MARQUEE SELECTION
                    m_bPotentialItemDrag = false;
                    m_bPotentialDrag = true;
                    m_ptDragStart = { x, y };
                    m_pendingClickedRow = clickedRow;
                    m_bPendingCtrl = bCtrl;
                    m_bPendingShift = bShift;
                    SetCapture(m_hWnd);
                }
            }
            else
            {
                // Clicked on empty space below rows
                if (m_bAllowMarquee)
                {
                    m_bPotentialItemDrag = false;
                    m_bPotentialDrag = true;
                    m_ptDragStart = { x, y };
                    m_pendingClickedRow = -1;
                    m_bPendingCtrl = bCtrl;
                    m_bPendingShift = bShift;
                    SetCapture(m_hWnd);
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rcList = GetListRect();
        int listW = rcList.right - rcList.left;
        if (m_vScroll.IsVisible() && x >= listW) return 0;
        if (m_hScroll.IsVisible() && y >= rcList.bottom) return 0;

        if (y > m_headerHeight)
        {
            int clickedRow = m_scrollY + (y - m_headerHeight) / m_rowHeight;
            if (clickedRow >= 0 && clickedRow < GetItemCount())
            {
                SetSelectedIndex(clickedRow);
                NMHDR nmhdr = { 0 };
                nmhdr.hwndFrom = m_hWnd;
                nmhdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                nmhdr.code = NM_DBLCLK;
                SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (m_vScroll.OnMouseMove(pt, m_hWnd))
        {
            m_scrollY = m_vScroll.GetPos();
            Invalidate();
        }
        if (m_hScroll.OnMouseMove(pt, m_hWnd))
        {
            m_scrollX = m_hScroll.GetPos();
            Invalidate();
        }

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        int cw = rcClient.right;

        // 1. Check if CONSIST REARRANGEMENT has crossed the 4px threshold
        if (m_bAllowRearrange && m_bPotentialItemDrag && !m_bIsItemDragging)
        {
            if (abs(x - m_ptDragStart.x) > 4 || abs(y - m_ptDragStart.y) > 4)
            {
                m_bIsItemDragging = true;
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));

                std::vector<DragGhostItem> ghostItems;
                std::vector<int> selList = GetSelectedIndices();
                for (int selIdx : selList)
                {
                    DragGhostItem item;
                    item.name = GetCellText(selIdx, 1);
                    std::wstring typeStr = GetCellText(selIdx, 2);
                    item.isEngine = (typeStr == L"Engine" || typeStr == L"Diesel" || typeStr == L"Electric" || typeStr == L"Steam" || typeStr == L"Control");
                    ghostItems.push_back(item);
                }

                POINT ptScreen = { x, y };
                ClientToScreen(m_hWnd, &ptScreen);
                FluentDragGhost::Show(GetParent(m_hWnd), ptScreen, ghostItems);
            }
        }

        if (m_bAllowRearrange && m_bIsItemDragging)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            POINT ptScreen = { x, y };
            ClientToScreen(m_hWnd, &ptScreen);

            int newDropIdx = GetDropIndexFromPoint(pt);
            if (m_dropTargetIndex != newDropIdx)
            {
                m_dropTargetIndex = newDropIdx;
                InvalidateRect(m_hWnd, NULL, FALSE);
            }

            FluentDragGhost::Move(ptScreen, true, L"Reorder");
            CheckAutoScroll(x, y);

            // Forward drag hover notification to parent
            NMCELLCLICK nmcc = { 0 };
            nmcc.hdr.hwndFrom = m_hWnd;
            nmcc.hdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmcc.hdr.code = 3001; // Internal Drag Hover
            nmcc.itemIndex = ptScreen.x;
            nmcc.subItemIndex = ptScreen.y;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmcc.hdr.idFrom, (LPARAM)&nmcc);
            return 0;
        }

        // 2. Check if STOCK TRANSFER GHOST DRAG has crossed the 4px threshold
        if (m_bAllowTransferSource && m_bPotentialItemDrag && !m_bIsItemDragging)
        {
            if (abs(x - m_ptDragStart.x) > 4 || abs(y - m_ptDragStart.y) > 4)
            {
                m_bIsItemDragging = true;

                std::vector<DragGhostItem> ghostItems;
                std::vector<int> selList = GetSelectedIndices();
                for (int selIdx : selList)
                {
                    DragGhostItem item;
                    item.name = GetCellText(selIdx, 0);
                    std::wstring typeStr = GetCellText(selIdx, 1);
                    item.isEngine = (typeStr != L"Freight" && typeStr != L"Passenger" && typeStr != L"Tender");
                    ghostItems.push_back(item);
                }

                POINT ptScreen = { x, y };
                ClientToScreen(m_hWnd, &ptScreen);
                FluentDragGhost::Show(GetParent(m_hWnd), ptScreen, ghostItems);
            }
        }

        if (m_bAllowTransferSource && m_bIsItemDragging)
        {
            POINT ptScreen = { x, y };
            ClientToScreen(m_hWnd, &ptScreen);

            // Forward hover notification to parent to update drop line and detect drop target validity
            NMCELLCLICK nmcc = { 0 };
            nmcc.hdr.hwndFrom = m_hWnd;
            nmcc.hdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmcc.hdr.code = 3001; // Internal Drag Hover
            nmcc.itemIndex = ptScreen.x;
            nmcc.subItemIndex = ptScreen.y;
            LRESULT isOverValid = SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmcc.hdr.idFrom, (LPARAM)&nmcc);

            if (isOverValid)
            {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                FluentDragGhost::Move(ptScreen, true);
            }
            else
            {
                SetCursor(LoadCursor(NULL, IDC_NO));
                FluentDragGhost::Move(ptScreen, false);
            }
            return 0;
        }

        // 2. Check if MARQUEE DRAG has crossed the 4px threshold (STRICTLY when m_bAllowMarquee is true)
        if (m_bAllowMarquee && m_bPotentialDrag && !m_isMarqueeSelecting)
        {
            if (abs(x - m_ptDragStart.x) > 4 || abs(y - m_ptDragStart.y) > 4)
            {
                m_isMarqueeSelecting = true;
                m_ptMarqueeStart = m_ptDragStart;
                m_marqueeStartScrollY = m_scrollY;
                m_marqueeStartScrollX = m_scrollX;
                if (!m_bPendingCtrl && !m_bPendingShift)
                {
                    m_selectedIndices.clear();
                }
                UpdateMarqueeSelection(x, y);
                CheckAutoScroll(x, y);
                return 0;
            }
        }

        if (m_bAllowMarquee && m_isMarqueeSelecting)
        {
            UpdateMarqueeSelection(x, y);
            CheckAutoScroll(x, y);
            return 0;
        }

        if (m_isResizing)
        {
            int delta = x - m_resizeStartX;
            int newW  = m_resizeStartWidth + delta;
            if (newW < 20) newW = 20;
            SetColumnWidth(m_resizeColIndex, newW);
            RECT rcParent;
            GetClientRect(GetParent(m_hWnd), &rcParent);
            SendMessageW(GetParent(m_hWnd), WM_SIZE, 0, MAKELPARAM(rcParent.right - rcParent.left, rcParent.bottom - rcParent.top));
        }
        else
        {
            // Hover tracking on header & cells
            int oldHover = m_hoverHeaderColIndex;
            int newHover = -1;

            int oldHoveredRow = m_hoveredRow;
            int oldHoveredCol = m_hoveredCol;

            if (y <= m_headerHeight)
            {
                m_hoveredRow = -1;
                m_hoveredCol = -1;

                int xAccum = -m_scrollX;
                bool nearBoundary = false;
                if (m_columns.size() > 1)
                {
                    for (size_t c = 0; c < m_columns.size() - 1; ++c)
                    {
                        xAccum += m_columns[c].width;
                        if (abs(x - xAccum) <= 2) { nearBoundary = true; break; }
                    }
                }

                if (!nearBoundary)
                {
                    int xPos = -m_scrollX;
                    for (size_t c = 0; c < m_columns.size(); ++c)
                    {
                        int colWidth = m_columns[c].width;
                        if (c == m_columns.size() - 1)
                        {
                            colWidth = cw - xPos;
                        }
                        int nextX = xPos + colWidth;
                        if (x >= xPos && x < nextX)
                        {
                            newHover = (int)c;
                            break;
                        }
                        xPos = nextX;
                    }
                }
            }
            else
            {
                m_hoverHeaderColIndex = -1;
                m_hoveredRow = m_scrollY + (y - m_headerHeight) / m_rowHeight;
                if (m_hoveredRow >= 0 && m_hoveredRow < GetItemCount())
                {
                    int xPos = -m_scrollX;
                    m_hoveredCol = -1;
                    for (size_t c = 0; c < m_columns.size(); ++c)
                    {
                        int colWidth = m_columns[c].width;
                        if (c == m_columns.size() - 1)
                        {
                            colWidth = cw - xPos;
                        }
                        int nextX = xPos + colWidth;
                        if (x >= xPos && x < nextX)
                        {
                            m_hoveredCol = (int)c;
                            break;
                        }
                        xPos = nextX;
                    }
                }
                else
                {
                    m_hoveredRow = -1;
                    m_hoveredCol = -1;
                }
            }

            bool oldDropdownHover = m_hoverHeaderInDropdown;
            bool newDropdownHover = false;
            if (newHover != -1)
            {
                int xPos = -m_scrollX;
                for (int c = 0; c < newHover; ++c)
                {
                    xPos += m_columns[c].width;
                }
                int colWidth = m_columns[newHover].width;
                if (newHover == (int)m_columns.size() - 1)
                {
                    colWidth = cw - xPos;
                }
                int colRight = xPos + colWidth;
                newDropdownHover = (x >= colRight - 20);
            }

            if (newHover != oldHover || newDropdownHover != oldDropdownHover ||
                m_hoveredRow != oldHoveredRow || m_hoveredCol != oldHoveredCol)
            {
                m_hoverHeaderColIndex = newHover;
                m_hoverHeaderInDropdown = newDropdownHover;
                Invalidate();

                TRACKMOUSEEVENT tme = { sizeof(tme) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hWnd;
                TrackMouseEvent(&tme);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_vScroll.OnMouseLeave(m_hWnd);
        m_hScroll.OnMouseLeave(m_hWnd);
        m_hoverHeaderColIndex = -1;
        m_pressedHeaderColIndex = -1;
        m_hoveredRow = -1;
        m_hoveredCol = -1;
        Invalidate();
        return 0;
    }

    case WM_CAPTURECHANGED:
    {
        if ((HWND)lParam != m_hWnd)
        {
            if (m_isMarqueeSelecting)
            {
                m_isMarqueeSelecting = false;
                m_bPotentialDrag = false;
                StopAutoScroll();
                Invalidate();
            }
            if (m_bIsItemDragging)
            {
                m_bIsItemDragging = false;
                m_bPotentialItemDrag = false;
                m_dropTargetIndex = -1;
                StopAutoScroll();
                FluentDragGhost::Hide();
                Invalidate();
            }
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool vUp = m_vScroll.OnLButtonUp(pt, m_hWnd);
        bool hUp = m_hScroll.OnLButtonUp(pt, m_hWnd);
        if (vUp || hUp)
        {
            Invalidate();
            return 0;
        }

        // 1. Finalize Item Drag & Drop if was dragging items
        if (m_bIsItemDragging)
        {
            m_bIsItemDragging = false;
            m_bPotentialItemDrag = false;
            StopAutoScroll();
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            FluentDragGhost::Hide();

            POINT ptScreen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(m_hWnd, &ptScreen);

            NMCELLCLICK nmcc = { 0 };
            nmcc.hdr.hwndFrom = m_hWnd;
            nmcc.hdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmcc.hdr.code = 3002; // Internal Drag Drop Finalized
            nmcc.itemIndex = ptScreen.x;
            nmcc.subItemIndex = ptScreen.y;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmcc.hdr.idFrom, (LPARAM)&nmcc);
            Invalidate();
            return 0;
        }

        // If candidate for item drag but released without moving past 4px -> Treat as regular click
        if (m_bPotentialItemDrag)
        {
            m_bPotentialItemDrag = false;
            ReleaseCapture();

            if (m_pendingClickedRow >= 0 && m_pendingClickedRow < GetItemCount())
            {
                if (m_isMultiSelect)
                {
                    if (m_bPendingCtrl)
                    {
                        ToggleRowSelection(m_pendingClickedRow);
                    }
                    else if (m_bPendingShift)
                    {
                        int anchor = (m_anchorRow >= 0) ? m_anchorRow : 0;
                        int rStart = (std::min)(anchor, m_pendingClickedRow);
                        int rEnd   = (std::max)(anchor, m_pendingClickedRow);
                        m_selectedIndices.clear();
                        for (int r = rStart; r <= rEnd; ++r)
                        {
                            m_selectedIndices.insert(r);
                        }
                        m_selectedIndex = m_pendingClickedRow;
                        Invalidate();
                    }
                    else
                    {
                        m_selectedIndices.clear();
                        m_selectedIndices.insert(m_pendingClickedRow);
                        m_selectedIndex = m_pendingClickedRow;
                        m_anchorRow = m_pendingClickedRow;
                        Invalidate();
                    }
                }
                else
                {
                    SetSelectedIndex(m_pendingClickedRow);
                }

                NMHDR nmhdr = { 0 };
                nmhdr.hwndFrom = m_hWnd;
                nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                nmhdr.code     = NM_CLICK;
                SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            }
            return 0;
        }

        // 2. Finalize Marquee Selection if active
        if (m_isMarqueeSelecting)
        {
            m_isMarqueeSelecting = false;
            m_bPotentialDrag = false;
            StopAutoScroll();
            ReleaseCapture();
            Invalidate();
            NMHDR nmhdr = { 0 };
            nmhdr.hwndFrom = m_hWnd;
            nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmhdr.code     = NM_CLICK;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            return 0;
        }

        // If candidate for marquee drag but released without moving past 4px -> Treat as regular click
        if (m_bPotentialDrag)
        {
            m_bPotentialDrag = false;
            ReleaseCapture();

            if (m_pendingClickedRow >= 0 && m_pendingClickedRow < GetItemCount())
            {
                if (m_isMultiSelect)
                {
                    if (m_bPendingCtrl)
                    {
                        ToggleRowSelection(m_pendingClickedRow);
                    }
                    else if (m_bPendingShift)
                    {
                        int anchor = (m_anchorRow >= 0) ? m_anchorRow : 0;
                        int rStart = (std::min)(anchor, m_pendingClickedRow);
                        int rEnd   = (std::max)(anchor, m_pendingClickedRow);
                        m_selectedIndices.clear();
                        for (int r = rStart; r <= rEnd; ++r)
                        {
                            m_selectedIndices.insert(r);
                        }
                        m_selectedIndex = m_pendingClickedRow;
                        Invalidate();
                    }
                    else
                    {
                        m_selectedIndices.clear();
                        m_selectedIndices.insert(m_pendingClickedRow);
                        m_selectedIndex = m_pendingClickedRow;
                        m_anchorRow = m_pendingClickedRow;
                        Invalidate();
                    }
                }
                else
                {
                    SetSelectedIndex(m_pendingClickedRow);
                }

                NMHDR nmhdr = { 0 };
                nmhdr.hwndFrom = m_hWnd;
                nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                nmhdr.code     = NM_CLICK;
                SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            }
            else
            {
                // Clicked on empty space without dragging -> Clear selection
                if (m_isMultiSelect && !m_bPendingCtrl && !m_bPendingShift)
                {
                    ClearSelection();
                }
            }
            return 0;
        }

        if (m_isResizing)
        {
            m_isResizing = false;
            ReleaseCapture();
        }
        else if (m_pressedHeaderColIndex != -1)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            int cw = rcClient.right;

            int xPos = -m_scrollX;
            int targetCol = -1;
            bool targetInDropdown = false;
            if (y <= m_headerHeight)
            {
                for (size_t c = 0; c < m_columns.size(); ++c)
                {
                    int colWidth = m_columns[c].width;
                    if (c == m_columns.size() - 1)
                    {
                        colWidth = cw - xPos;
                    }
                    int nextX = xPos + colWidth;
                    if (x >= xPos && x < nextX)
                    {
                        targetCol = (int)c;
                        targetInDropdown = (x >= nextX - 20);
                        break;
                    }
                    xPos = nextX;
                }
            }

            if (targetCol == m_pressedHeaderColIndex)
            {
                if (m_pressedHeaderInDropdown && targetInDropdown)
                {
                    PostMessageW(GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(m_hWnd), 2000 + targetCol), (LPARAM)m_hWnd);
                }
                else if (!m_pressedHeaderInDropdown && !targetInDropdown)
                {
                    SortByColumn(m_pressedHeaderColIndex);
                }
            }

            m_pressedHeaderColIndex = -1;
            m_pressedHeaderInDropdown = false;
            ReleaseCapture();
            RECT rcHeader = { 0, 0, cw, m_headerHeight };
            InvalidateRect(m_hWnd, &rcHeader, TRUE);
        }
        else if (m_pressedRow != -1 && m_pressedCol != -1)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            int oldPressedRow = m_pressedRow;
            int oldPressedCol = m_pressedCol;
            m_pressedRow = -1;
            m_pressedCol = -1;
            ReleaseCapture();
            Invalidate();

            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            int cw = rcClient.right;

            if (y > m_headerHeight)
            {
                int clickedRow = m_scrollY + (y - m_headerHeight) / m_rowHeight;
                if (clickedRow == oldPressedRow)
                {
                    int xPos = -m_scrollX;
                    int clickedCol = -1;
                    for (size_t c = 0; c < m_columns.size(); ++c)
                    {
                        int colWidth = m_columns[c].width;
                        if (c == m_columns.size() - 1)
                        {
                            colWidth = cw - xPos;
                        }
                        int nextX = xPos + colWidth;
                        if (x >= xPos && x < nextX)
                        {
                            clickedCol = (int)c;
                            break;
                        }
                        xPos = nextX;
                    }

                    if (clickedCol == oldPressedCol)
                    {
                        NMCELLCLICK nmcc = { 0 };
                        nmcc.hdr.hwndFrom = m_hWnd;
                        nmcc.hdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                        nmcc.hdr.code = NM_CELLCLICK;
                        nmcc.itemIndex = clickedRow;
                        nmcc.subItemIndex = clickedCol;
                        SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmcc.hdr.idFrom, (LPARAM)&nmcc);
                    }
                }
            }
        }
        else
        {
            int y = GET_Y_LPARAM(lParam);
            if (y > m_headerHeight)
            {
                NMHDR nmhdr = { 0 };
                nmhdr.hwndFrom = m_hWnd;
                nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                nmhdr.code     = NM_CLICK;
                SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        ::SetFocus(m_hWnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (y > m_headerHeight)
        {
            int clickedRow = m_scrollY + (y - m_headerHeight) / m_rowHeight;
            if (clickedRow >= 0 && clickedRow < GetItemCount())
            {
                // If right-clicked row is not selected, select it
                if (m_selectedIndices.count(clickedRow) == 0)
                {
                    if (m_isMultiSelect)
                    {
                        m_selectedIndices.clear();
                        m_selectedIndices.insert(clickedRow);
                        m_selectedIndex = clickedRow;
                        m_anchorRow = clickedRow;
                        Invalidate();
                    }
                    else
                    {
                        SetSelectedIndex(clickedRow);
                    }
                }
            }
        }
        return 0;
    }

    case WM_RBUTTONUP:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (y > m_headerHeight)
        {
            POINT ptScreen = { x, y };
            ClientToScreen(m_hWnd, &ptScreen);

            NMCELLCLICK nmcc = { 0 };
            nmcc.hdr.hwndFrom = m_hWnd;
            nmcc.hdr.idFrom = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmcc.hdr.code = NM_RCELLCLICK;
            nmcc.itemIndex = ptScreen.x;
            nmcc.subItemIndex = ptScreen.y;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmcc.hdr.idFrom, (LPARAM)&nmcc);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SYSKEYDOWN:
    {
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (bAlt && (wParam == 'V' || wParam == 'v'))
        {
            SendMessageW(GetParent(m_hWnd), WM_SYSKEYDOWN, wParam, lParam);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (bAlt && (wParam == 'V' || wParam == 'v'))
        {
            SendMessageW(GetParent(m_hWnd), WM_SYSKEYDOWN, wParam, lParam);
            return 0;
        }
        int itemCount = GetItemCount();
        int visRows   = GetVisibleRows();
        int idx       = m_selectedIndex;
        bool bCtrl    = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (bCtrl && (wParam == 'A' || wParam == 'a'))
        {
            if (m_isMultiSelect)
            {
                SelectAll();
                NMHDR nmhdr = { 0 };
                nmhdr.hwndFrom = m_hWnd;
                nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
                nmhdr.code     = NM_CLICK;
                SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            }
            return 0;
        }

        // Forward shortcuts to parent
        if (wParam == VK_DELETE || (bCtrl && (wParam == 'Z' || wParam == 'z' || wParam == 'Y' || wParam == 'y' || wParam == 'C' || wParam == 'c' || wParam == 'X' || wParam == 'x' || wParam == 'V' || wParam == 'v' || wParam == 'R' || wParam == 'r')) || wParam == 'F' || wParam == 'f')
        {
            SendMessageW(GetParent(m_hWnd), WM_KEYDOWN, wParam, lParam);
            return 0;
        }

        switch (wParam)
        {
        case VK_RETURN:
        {
            NMHDR nmhdr = { 0 };
            nmhdr.hwndFrom = m_hWnd;
            nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(m_hWnd, GWLP_ID);
            nmhdr.code     = NM_RETURN;
            SendMessageW(GetParent(m_hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
            break;
        }
        case VK_UP:     if (idx > 0) SetSelectedIndex(idx - 1); break;
        case VK_DOWN:   if (idx < itemCount - 1) SetSelectedIndex(idx + 1); break;
        case VK_PRIOR:  SetSelectedIndex((std::max)(0, idx - visRows)); break;
        case VK_NEXT:   SetSelectedIndex((std::min)(itemCount - 1, idx + visRows)); break;
        case VK_HOME:   SetSelectedIndex(0); break;
        case VK_END:    SetSelectedIndex(itemCount - 1); break;
        }
        return 0;
    }

    } // end switch

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void CustomListControl::SortByColumn(int colIndex, bool toggleDirection)
{
    if (colIndex < 0 || colIndex >= (int)m_columns.size())
        return;

    if (m_isVirtual)
    {
        if (colIndex == m_sortColIndex)
        {
            if (toggleDirection) m_sortAscending = !m_sortAscending;
        }
        else
        {
            m_sortColIndex = colIndex;
            m_sortAscending = true;
        }
        RECT rcHeader = { 0, 0, GetListRect().right, m_headerHeight };
        InvalidateRect(m_hWnd, &rcHeader, TRUE);

        PostMessageW(GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(m_hWnd), 1000 + colIndex), (LPARAM)m_hWnd);
        return;
    }

    if (colIndex == m_sortColIndex)
    {
        if (toggleDirection) m_sortAscending = !m_sortAscending;
    }
    else
    {
        m_sortColIndex = colIndex;
        m_sortAscending = true;
    }

    // Perform sort
    std::stable_sort(m_items.begin(), m_items.end(), [colIndex, this](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
        if (colIndex >= (int)a.size() || colIndex >= (int)b.size())
            return false;

        const std::wstring& valAStr = a[colIndex];
        const std::wstring& valBStr = b[colIndex];

        if (m_columns[colIndex].title == L"Units")
        {
            int valA = 0;
            int valB = 0;
            try { valA = std::stoi(valAStr); } catch (...) {}
            try { valB = std::stoi(valBStr); } catch (...) {}
            
            if (valA == valB) return false;
            return m_sortAscending ? (valA < valB) : (valA > valB);
        }
        else
        {
            int cmp = StrCmpLogicalW(valAStr.c_str(), valBStr.c_str());
            if (cmp == 0) return false;
            return m_sortAscending ? (cmp < 0) : (cmp > 0);
        }
    });

    Invalidate();
}

void CustomListControl::EnsureVisible(int index)
{
    int count = GetItemCount();
    if (index < 0 || index >= count) return;
    int visRows = GetVisibleRows();
    if (visRows <= 0) return;

    // Only scroll if the index is strictly OUTSIDE the visible viewport
    if (index < m_scrollY)
    {
        m_scrollY = index;
        UpdateScrollbars();
        Invalidate();
    }
    else if (index >= m_scrollY + visRows)
    {
        m_scrollY = index - visRows + 1;
        int maxPos = count - visRows;
        if (maxPos < 0) maxPos = 0;
        if (m_scrollY > maxPos) m_scrollY = maxPos;
        UpdateScrollbars();
        Invalidate();
    }
}

void CustomListControl::UpdateMarqueeSelection(int currentX, int currentY)
{
    m_marqueeCurrentX = currentX;
    m_marqueeCurrentY = currentY;

    RECT rcList = GetListRect();
    int listW = rcList.right - rcList.left;

    // Content-space coordinates
    int startWorldY = m_marqueeStartScrollY * m_rowHeight + (m_ptMarqueeStart.y - m_headerHeight);
    int currentWorldY = m_scrollY * m_rowHeight + (currentY - m_headerHeight);

    int topWorldY = (std::min)(startWorldY, currentWorldY);
    int bottomWorldY = (std::max)(startWorldY, currentWorldY);

    // Current screen coordinates for drawing marquee box
    int startScreenY = m_headerHeight + (startWorldY - m_scrollY * m_rowHeight);
    int startScreenX = m_ptMarqueeStart.x + (m_marqueeStartScrollX - m_scrollX);

    m_rcMarquee.left = startScreenX;
    m_rcMarquee.top = startScreenY;
    m_rcMarquee.right = currentX;
    m_rcMarquee.bottom = currentY;

    int itemCount = GetItemCount();
    bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!bCtrl && !m_bPendingCtrl)
    {
        m_selectedIndices.clear();
    }

    for (int r = 0; r < itemCount; ++r)
    {
        int rowTopWorld = r * m_rowHeight;
        int rowBottomWorld = rowTopWorld + m_rowHeight;
        if (rowBottomWorld >= topWorldY && rowTopWorld <= bottomWorldY)
        {
            m_selectedIndices.insert(r);
            m_selectedIndex = r;
        }
    }

    Invalidate();
}

void CustomListControl::CancelItemDrag()
{
    m_bIsItemDragging = false;
    m_bPotentialItemDrag = false;
    m_dropTargetIndex = -1;
    StopAutoScroll();
    FluentDragGhost::Hide();
    Invalidate();
}

int CustomListControl::GetDropIndexFromPoint(POINT ptClient) const
{
    int dropIdx = m_scrollY + (ptClient.y - m_headerHeight + m_rowHeight / 2) / m_rowHeight;
    if (dropIdx < 0) dropIdx = 0;
    int total = GetItemCount();
    if (dropIdx > total) dropIdx = total;
    return dropIdx;
}

int CustomListControl::GetDropIndexFromScreenPoint(POINT ptScreen) const
{
    POINT pt = ptScreen;
    ScreenToClient(m_hWnd, &pt);
    return GetDropIndexFromPoint(pt);
}

void CustomListControl::CheckDragAutoScroll(POINT ptClient)
{
    if (ptClient.x == -1 && ptClient.y == -1)
    {
        StopAutoScroll();
        return;
    }

    RECT rcList = GetListRect();
    const int EDGE_ZONE = 24;

    bool needScrollV = false;
    int maxScrollY = m_vScroll.GetMaxScrollPos();
    if (ptClient.y < m_headerHeight + EDGE_ZONE && m_scrollY > 0)
    {
        needScrollV = true;
    }
    else if (ptClient.y > rcList.bottom - EDGE_ZONE && m_scrollY < maxScrollY)
    {
        needScrollV = true;
    }

    if (needScrollV)
    {
        if (!m_isAutoScrolling)
        {
            m_isAutoScrolling = true;
            SetTimer(m_hWnd, TIMER_AUTOSCROLL_ID, 25, NULL);
        }
    }
    else
    {
        StopAutoScroll();
    }
}

void CustomListControl::CheckAutoScroll(int currentX, int currentY)
{
    if (!m_isMarqueeSelecting && !m_bIsItemDragging)
    {
        StopAutoScroll();
        return;
    }

    RECT rcList = GetListRect();
    const int EDGE_ZONE = 24;

    bool needScrollV = false;
    bool needScrollH = false;

    int maxScrollY = m_vScroll.GetMaxScrollPos();
    if (currentY < m_headerHeight + EDGE_ZONE && m_scrollY > 0)
    {
        needScrollV = true;
    }
    else if (currentY > rcList.bottom - EDGE_ZONE && m_scrollY < maxScrollY)
    {
        needScrollV = true;
    }

    int maxScrollX = m_hScroll.GetMaxScrollPos();
    int listW = rcList.right - rcList.left;
    if (m_hScroll.IsVisible())
    {
        if (currentX < EDGE_ZONE && m_scrollX > 0)
        {
            needScrollH = true;
        }
        else if (currentX > listW - EDGE_ZONE && m_scrollX < maxScrollX)
        {
            needScrollH = true;
        }
    }

    if (needScrollV || needScrollH)
    {
        if (!m_isAutoScrolling)
        {
            m_isAutoScrolling = true;
            SetTimer(m_hWnd, TIMER_AUTOSCROLL_ID, 25, NULL);
        }
    }
    else
    {
        StopAutoScroll();
    }
}

void CustomListControl::StopAutoScroll()
{
    if (m_isAutoScrolling)
    {
        m_isAutoScrolling = false;
        KillTimer(m_hWnd, TIMER_AUTOSCROLL_ID);
    }
}


