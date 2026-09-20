#include "CustomTreeView.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>

static const wchar_t* CUSTOM_TREEVIEW_CLASS = L"CustomTreeViewControl";
static bool s_isClassRegistered = false;

CustomTreeView::CustomTreeView()
    : m_hWnd(NULL),
      m_hParent(NULL),
      m_controlId(0),
      m_hFont(NULL),
      m_bDarkMode(true),
      m_rowHeight(24),
      m_scrollY(0),
      m_selectedNode(nullptr),
      m_hoverNode(nullptr),
      m_vScroll(ScrollBarOrientation::Vertical),
      m_isDraggingScroll(false),
      m_hToolTip(NULL)
{
    m_vScroll.SetAutoHide(true);
    m_vScroll.SetHideDelay(1500);
    m_vScroll.SetThumbColor(RGB(140, 140, 140), RGB(190, 190, 190), RGB(230, 230, 230));
}

CustomTreeView::~CustomTreeView()
{
    Clear();
    if (m_hWnd && IsWindow(m_hWnd))
    {
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
}

HWND CustomTreeView::Create(HWND hParent, int x, int y, int w, int h, int id)
{
    m_hParent = hParent;
    m_controlId = id;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);

    if (!s_isClassRegistered)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = CustomTreeView::WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = CUSTOM_TREEVIEW_CLASS;
        RegisterClassExW(&wc);
        s_isClassRegistered = true;
    }

    m_hWnd = CreateWindowExW(
        0,
        CUSTOM_TREEVIEW_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, w, h,
        hParent,
        (HMENU)(INT_PTR)id,
        hInst,
        this
    );

    if (m_hWnd)
    {
        m_hToolTip = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASS,
            NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            m_hWnd,
            NULL,
            hInst,
            NULL
        );

        if (m_hToolTip)
        {
            TOOLINFOW ti = { 0 };
            ti.cbSize = sizeof(TOOLINFOW);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = m_hWnd;
            ti.uId = (UINT_PTR)m_hWnd;
            ti.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(m_hToolTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            SendMessageW(m_hToolTip, TTM_SETDELAYTIME, TTDT_INITIAL, (LPARAM)800);
            SendMessageW(m_hToolTip, TTM_SETDELAYTIME, TTDT_RESHOW, (LPARAM)400);
            SendMessageW(m_hToolTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, (LPARAM)10000);
        }

        UpdateScrollbars();
    }

    return m_hWnd;
}

CustomTreeNode* CustomTreeView::AddRoot(const std::wstring& text, const std::wstring& tag, int id, bool isFolder)
{
    CustomTreeNode* node = new CustomTreeNode();
    node->id = id;
    node->text = text;
    node->tag = tag;
    node->level = 0;
    node->isFolder = isFolder;
    node->isExpanded = false;
    node->parent = nullptr;
    m_roots.push_back(node);

    FlattenVisibleNodes();
    Invalidate();
    return node;
}

CustomTreeNode* CustomTreeView::AddChild(CustomTreeNode* parent, const std::wstring& text, const std::wstring& tag, int id, bool isFolder)
{
    if (!parent) return nullptr;

    CustomTreeNode* node = new CustomTreeNode();
    node->id = id;
    node->text = text;
    node->tag = tag;
    node->level = parent->level + 1;
    node->isFolder = isFolder;
    node->isExpanded = false;
    node->parent = parent;
    parent->children.push_back(node);
    parent->isFolder = true;

    FlattenVisibleNodes();
    Invalidate();
    return node;
}

void CustomTreeView::Clear()
{
    for (auto* root : m_roots)
    {
        delete root;
    }
    m_roots.clear();
    m_visibleNodes.clear();
    m_selectedNode = nullptr;
    m_hoverNode = nullptr;
    m_scrollY = 0;

    UpdateScrollbars();
    Invalidate();
}

void CustomTreeView::DeleteAllChildren(CustomTreeNode* parent)
{
    if (!parent) return;

    for (auto* child : parent->children)
    {
        if (child == m_selectedNode) m_selectedNode = nullptr;
        if (child == m_hoverNode) m_hoverNode = nullptr;
        delete child;
    }
    parent->children.clear();

    FlattenVisibleNodes();
    Invalidate();
}

void CustomTreeView::ExpandNode(CustomTreeNode* node, bool expand)
{
    if (!node || !node->isFolder || node->isExpanded == expand) return;
    node->isExpanded = expand;
    FlattenVisibleNodes();
    Invalidate();
}

void CustomTreeView::ToggleExpand(CustomTreeNode* node)
{
    if (!node || !node->isFolder) return;
    ExpandNode(node, !node->isExpanded);
}

void CustomTreeView::SelectNode(CustomTreeNode* node)
{
    if (m_selectedNode == node) return;
    m_selectedNode = node;
    Invalidate();

    if (m_onSelectionChanged)
    {
        m_onSelectionChanged(m_selectedNode);
    }
}

CustomTreeNode* CustomTreeView::FindNodeByTag(const std::wstring& tag, CustomTreeNode* startFrom)
{
    if (startFrom)
    {
        if (_wcsicmp(startFrom->tag.c_str(), tag.c_str()) == 0) return startFrom;
        for (auto* child : startFrom->children)
        {
            CustomTreeNode* found = FindNodeByTag(tag, child);
            if (found) return found;
        }
        return nullptr;
    }

    for (auto* root : m_roots)
    {
        CustomTreeNode* found = FindNodeByTag(tag, root);
        if (found) return found;
    }
    return nullptr;
}

CustomTreeNode* CustomTreeView::FindNodeById(int id, CustomTreeNode* startFrom)
{
    if (startFrom)
    {
        if (startFrom->id == id) return startFrom;
        for (auto* child : startFrom->children)
        {
            CustomTreeNode* found = FindNodeById(id, child);
            if (found) return found;
        }
        return nullptr;
    }

    for (auto* root : m_roots)
    {
        CustomTreeNode* found = FindNodeById(id, root);
        if (found) return found;
    }
    return nullptr;
}

void CustomTreeView::SetBounds(int x, int y, int w, int h)
{
    if (m_hWnd)
    {
        SetWindowPos(m_hWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateScrollbars();
        Invalidate();
    }
}

void CustomTreeView::Show(bool show)
{
    if (m_hWnd)
    {
        ShowWindow(m_hWnd, show ? SW_SHOW : SW_HIDE);
    }
}

void CustomTreeView::SetDarkMode(bool dark)
{
    m_bDarkMode = dark;
    Invalidate();
}

void CustomTreeView::SetFont(HFONT hFont)
{
    m_hFont = hFont;
    Invalidate();
}

void CustomTreeView::Invalidate()
{
    if (m_hWnd && IsWindow(m_hWnd))
    {
        InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CustomTreeView::FlattenVisibleNodes()
{
    m_visibleNodes.clear();
    for (auto* root : m_roots)
    {
        FlattenNode(root);
    }
    UpdateScrollbars();
}

void CustomTreeView::FlattenNode(CustomTreeNode* node)
{
    if (!node) return;
    m_visibleNodes.push_back(node);
    if (node->isFolder && node->isExpanded)
    {
        for (auto* child : node->children)
        {
            FlattenNode(child);
        }
    }
}

void CustomTreeView::LayoutScrollbars()
{
    if (!m_hWnd) return;

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int cw = rcClient.right - rcClient.left;
    int ch = rcClient.bottom - rcClient.top;
    if (cw <= 0 || ch <= 0) return;

    int visRows = ch / m_rowHeight;
    if (visRows < 1) visRows = 1;

    bool needV = ((int)m_visibleNodes.size() > visRows);
    m_vScroll.SetVisible(needV);

    const int SCROLL_WIDTH = 12;
    RECT rcV = { cw - (needV ? SCROLL_WIDTH : 0), 0, cw, ch };
    m_vScroll.SetBounds(rcV);
}

void CustomTreeView::UpdateScrollbars()
{
    if (!m_hWnd) return;

    LayoutScrollbars();

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int ch = rcClient.bottom - rcClient.top;
    int visRows = ch / m_rowHeight;
    if (visRows < 1) visRows = 1;

    int totalCount = (int)m_visibleNodes.size();
    m_vScroll.SetRange(0, totalCount > 0 ? totalCount - 1 : 0, visRows);
    m_vScroll.SetPos(m_scrollY);
    m_scrollY = m_vScroll.GetPos();
}

CustomTreeNode* CustomTreeView::GetNodeAtPoint(POINT pt, bool* outIsChevron)
{
    if (outIsChevron) *outIsChevron = false;
    if (pt.y < 0) return nullptr;

    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int cw = rcClient.right - rcClient.left;
    int rightEdge = cw - (m_vScroll.IsVisible() ? 12 : 0);
    if (pt.x > rightEdge) return nullptr;

    int index = m_scrollY + (pt.y / m_rowHeight);
    if (index >= 0 && index < (int)m_visibleNodes.size())
    {
        CustomTreeNode* node = m_visibleNodes[index];
        if (node && node->isFolder && outIsChevron)
        {
            int indent = 8 + node->level * 16;
            RECT rcChev = { indent - 2, (index - m_scrollY) * m_rowHeight, indent + 18, (index - m_scrollY + 1) * m_rowHeight };
            if (PtInRect(&rcChev, pt))
            {
                *outIsChevron = true;
            }
        }
        return node;
    }
    return nullptr;
}

RECT CustomTreeView::GetRowRect(int visibleIndex) const
{
    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int cw = rcClient.right - rcClient.left;
    int rightEdge = cw - (m_vScroll.IsVisible() ? 12 : 0);

    int y = (visibleIndex - m_scrollY) * m_rowHeight;
    RECT rc = { 2, y + 1, rightEdge - 2, y + m_rowHeight - 1 };
    return rc;
}

LRESULT CALLBACK CustomTreeView::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CustomTreeView* pThis = (CustomTreeView*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (uMsg == WM_NCCREATE)
    {
        LPCREATESTRUCTW pCreate = (LPCREATESTRUCTW)lParam;
        pThis = (CustomTreeView*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
    }

    if (!pThis) return DefWindowProcW(hWnd, uMsg, wParam, lParam);

    switch (uMsg)
    {
    case WM_SIZE:
    {
        pThis->UpdateScrollbars();
        pThis->Invalidate();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right - rcClient.left;
        int h = rcClient.bottom - rcClient.top;

        if (w > 0 && h > 0)
        {
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);

            COLORREF bgCol = pThis->m_bDarkMode ? RGB(22, 22, 22) : RGB(255, 255, 255);
            COLORREF textCol = pThis->m_bDarkMode ? RGB(225, 225, 225) : RGB(20, 20, 20);
            COLORREF hoverPillCol = pThis->m_bDarkMode ? RGB(40, 40, 40) : RGB(230, 230, 230);
            COLORREF selPillCol = pThis->m_bDarkMode ? RGB(35, 65, 105) : RGB(200, 225, 250);
            COLORREF selTextCol = pThis->m_bDarkMode ? RGB(255, 255, 255) : RGB(10, 10, 10);
            COLORREF arrowNormalCol = pThis->m_bDarkMode ? RGB(160, 160, 160) : RGB(100, 100, 100);
            COLORREF arrowHoverCol = pThis->m_bDarkMode ? RGB(235, 235, 235) : RGB(20, 20, 20);

            HBRUSH hBgBr = CreateSolidBrush(bgCol);
            FillRect(hMemDC, &rcClient, hBgBr);
            DeleteObject(hBgBr);

            HFONT hOldFont = NULL;
            if (pThis->m_hFont)
            {
                hOldFont = (HFONT)SelectObject(hMemDC, pThis->m_hFont);
            }
            SetBkMode(hMemDC, TRANSPARENT);

            int rightLimit = w - (pThis->m_vScroll.IsVisible() ? 12 : 0);
            int startRow = pThis->m_scrollY;
            int endRow = startRow + (h / pThis->m_rowHeight) + 1;
            if (endRow > (int)pThis->m_visibleNodes.size()) endRow = (int)pThis->m_visibleNodes.size();

            for (int i = startRow; i < endRow; ++i)
            {
                CustomTreeNode* node = pThis->m_visibleNodes[i];
                if (!node) continue;

                int rowY = (i - pThis->m_scrollY) * pThis->m_rowHeight;
                RECT rcRow = { 4, rowY + 1, rightLimit - 4, rowY + pThis->m_rowHeight - 1 };

                bool isSelected = (node == pThis->m_selectedNode);
                bool isHovered = (node == pThis->m_hoverNode);

                // Draw Row Pill Highlight
                if (isSelected || isHovered)
                {
                    COLORREF pillCol = isSelected ? selPillCol : hoverPillCol;
                    HBRUSH hPillBr = CreateSolidBrush(pillCol);
                    HPEN hPillPen = CreatePen(PS_SOLID, 1, pillCol);
                    HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hPillBr);
                    HPEN hOldP = (HPEN)SelectObject(hMemDC, hPillPen);

                    RoundRect(hMemDC, rcRow.left, rcRow.top, rcRow.right, rcRow.bottom, 6, 6);

                    // If selected, draw vibrant vertical indicator bar on left
                    if (isSelected)
                    {
                        HBRUSH hAccentBr = CreateSolidBrush(RGB(0, 150, 255));
                        RECT rcBar = { rcRow.left, rcRow.top + 3, rcRow.left + 3, rcRow.bottom - 3 };
                        FillRect(hMemDC, &rcBar, hAccentBr);
                        DeleteObject(hAccentBr);
                    }

                    SelectObject(hMemDC, hOldBr);
                    SelectObject(hMemDC, hOldP);
                    DeleteObject(hPillBr);
                    DeleteObject(hPillPen);
                }

                // Calculate indentation
                int indent = 8 + node->level * 16;

                // Draw Solid Filled Triangle Arrow for Folders
                if (node->isFolder)
                {
                    int triCX = indent + 4;
                    int triCY = rowY + (pThis->m_rowHeight / 2);
                    COLORREF curArrCol = (isHovered || isSelected) ? arrowHoverCol : arrowNormalCol;

                    HBRUSH hArrBr = CreateSolidBrush(curArrCol);
                    HPEN hArrPen = CreatePen(PS_SOLID, 1, curArrCol);
                    HBRUSH hOldArrBr = (HBRUSH)SelectObject(hMemDC, hArrBr);
                    HPEN hOldArrPen = (HPEN)SelectObject(hMemDC, hArrPen);

                    POINT pts[3];
                    if (node->isExpanded)
                    {
                        // Expanded (Pointing Down ▼)
                        pts[0] = { triCX - 4, triCY - 2 };
                        pts[1] = { triCX + 4, triCY - 2 };
                        pts[2] = { triCX,     triCY + 3 };
                    }
                    else
                    {
                        // Collapsed (Pointing Right ►)
                        pts[0] = { triCX - 2, triCY - 4 };
                        pts[1] = { triCX - 2, triCY + 4 };
                        pts[2] = { triCX + 3, triCY     };
                    }
                    Polygon(hMemDC, pts, 3);

                    SelectObject(hMemDC, hOldArrBr);
                    SelectObject(hMemDC, hOldArrPen);
                    DeleteObject(hArrBr);
                    DeleteObject(hArrPen);
                }

                // Draw Node Label Text
                int textLeft = indent + (node->isFolder ? 16 : 14);
                RECT rcText = { textLeft, rowY, rightLimit - 6, rowY + pThis->m_rowHeight };
                SetTextColor(hMemDC, isSelected ? selTextCol : textCol);
                DrawTextW(hMemDC, node->text.c_str(), (int)node->text.length(), &rcText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            if (hOldFont)
            {
                SelectObject(hMemDC, hOldFont);
            }

            // Paint Custom ScrollBar in dedicated gutter
            pThis->m_vScroll.Paint(hMemDC, bgCol);

            BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
            SelectObject(hMemDC, hOldBmp);
            DeleteObject(hBmp);
            DeleteDC(hMemDC);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Scrollbar interaction
        if (pThis->m_vScroll.IsVisible())
        {
            bool vHover = PtInRect(&pThis->m_vScroll.GetBounds(), pt);
            if (pThis->m_isDraggingScroll || vHover)
            {
                int oldPos = pThis->m_vScroll.GetPos();
                if (pThis->m_vScroll.OnMouseMove(pt, hWnd))
                {
                    int newPos = pThis->m_vScroll.GetPos();
                    if (newPos != oldPos)
                    {
                        pThis->m_scrollY = newPos;
                        pThis->Invalidate();
                    }
                }
                if (pThis->m_isDraggingScroll) return 0;
            }
            else if (pThis->m_vScroll.IsHovered())
            {
                pThis->m_vScroll.OnMouseLeave(hWnd);
                pThis->Invalidate();
            }
        }

        // Row hover interaction
        bool isChev = false;
        CustomTreeNode* hit = pThis->GetNodeAtPoint(pt, &isChev);
        if (hit != pThis->m_hoverNode)
        {
            pThis->m_hoverNode = hit;
            pThis->Invalidate();

            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        SetFocus(hWnd);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Scrollbar check
        if (pThis->m_vScroll.IsVisible() && PtInRect(&pThis->m_vScroll.GetBounds(), pt))
        {
            SetCapture(hWnd);
            pThis->m_isDraggingScroll = true;
            int oldPos = pThis->m_vScroll.GetPos();
            if (pThis->m_vScroll.OnLButtonDown(pt, hWnd))
            {
                int newPos = pThis->m_vScroll.GetPos();
                if (newPos != oldPos)
                {
                    pThis->m_scrollY = newPos;
                    pThis->Invalidate();
                }
            }
            return 0;
        }

        // Tree node check
        bool isChevron = false;
        CustomTreeNode* hit = pThis->GetNodeAtPoint(pt, &isChevron);
        if (hit)
        {
            if (isChevron && hit->isFolder)
            {
                pThis->ToggleExpand(hit);
            }
            else
            {
                pThis->SelectNode(hit);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (pThis->m_isDraggingScroll)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pThis->m_vScroll.OnLButtonUp(pt, hWnd);
            pThis->m_isDraggingScroll = false;
            ReleaseCapture();
            pThis->Invalidate();
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pThis->m_vScroll.IsVisible() && PtInRect(&pThis->m_vScroll.GetBounds(), pt)) return 0;

        bool isChev = false;
        CustomTreeNode* hit = pThis->GetNodeAtPoint(pt, &isChev);
        if (hit)
        {
            if (hit->isFolder)
            {
                pThis->ToggleExpand(hit);
            }
            if (pThis->m_onItemDoubleClicked)
            {
                pThis->m_onItemDoubleClicked(hit);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (pThis->m_vScroll.IsVisible())
        {
            int oldPos = pThis->m_vScroll.GetPos();
            pThis->m_vScroll.OnMouseWheel(zDelta, 3, hWnd);
            int newPos = pThis->m_vScroll.GetPos();
            if (newPos != oldPos)
            {
                pThis->m_scrollY = newPos;
                pThis->Invalidate();
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (pThis->m_vScroll.IsHovered())
        {
            pThis->m_vScroll.OnMouseLeave(hWnd);
        }
        if (pThis->m_hoverNode)
        {
            pThis->m_hoverNode = nullptr;
            pThis->Invalidate();
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == CustomScrollBar::TIMER_ANIM_ID)
        {
            if (pThis->m_vScroll.OnTimer(hWnd))
            {
                pThis->Invalidate();
            }
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh && pnmh->code == TTN_GETDISPINFOW)
        {
            LPNMTTDISPINFOW pDispInfo = (LPNMTTDISPINFOW)lParam;
            if (pThis->m_hoverNode)
            {
                pThis->m_lastTooltipText = pThis->m_hoverNode->text;
                pDispInfo->lpszText = (LPWSTR)pThis->m_lastTooltipText.c_str();
            }
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pThis->m_hToolTip && IsWindow(pThis->m_hToolTip))
        {
            DestroyWindow(pThis->m_hToolTip);
            pThis->m_hToolTip = NULL;
        }
        pThis->m_hWnd = NULL;
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}
