#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once
#include "CustomScrollBar.h"
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <unordered_set>

typedef std::wstring (*GetCellTextCallback)(int itemIndex, int subItemIndex, void* pParam);

struct CustomColumn {
    std::wstring title;
    int width;
    int align; // 0 = Left, 1 = Center, 2 = Right
};
#define NM_CELLCLICK (NM_FIRST - 50)
#define NM_RCELLCLICK (NM_FIRST - 51)

struct NMCELLCLICK {
    NMHDR hdr;
    int   itemIndex;
    int   subItemIndex;
};

class CustomListControl {
private:
    HWND m_hWnd;
    CustomScrollBar m_vScroll;
    CustomScrollBar m_hScroll;

    std::vector<CustomColumn> m_columns;
    
    // Standard Mode Storage
    std::vector<std::vector<std::wstring>> m_items;
    
    // Virtual Mode Configuration
    bool m_isVirtual;
    int m_itemCount;
    GetCellTextCallback m_callback;
    void* m_callbackParam;
    
    // Scroll state
    int m_scrollX; // pixel scroll offset
    int m_scrollY; // row scroll offset (items)
    int m_rowHeight;
    int m_headerHeight;
    static const int V_SCROLLBAR_WIDTH = 14; // Width of vertical scrollbar
    static const int H_SCROLLBAR_HEIGHT = 14; // Height of horizontal scrollbar area
    static const int FAYT_BAR_HEIGHT = 32;    // Height of bottom Find-As-You-Type search bar

    // Selection state
    int m_selectedIndex;
    bool m_isMultiSelect;
    std::unordered_set<int> m_selectedIndices;
    std::vector<int> m_selectedOrder;
    int m_anchorRow;

    // Integrated Selection Gutter state
    bool m_bShowSelectionGutter;
    std::unordered_set<int> m_checkedIndices;
    bool m_bHoverGutterHeader;
    bool m_bPressedGutterHeader;

    // Marquee / Rubber-band drag selection & Item Drag-Drop state
    bool m_isMarqueeSelecting;
    bool m_bPotentialDrag;
    bool m_bPotentialItemDrag;
    bool m_bIsItemDragging;
    int  m_dropTargetIndex;
    bool m_bAllowRearrange;
    bool m_bAllowTransferSource;
    bool m_bAllowMarquee;
    bool m_bDrawCardBorder;
    POINT m_ptDragStart;
    POINT m_ptMarqueeStart;
    RECT m_rcMarquee;
    int m_pendingClickedRow;
    bool m_bPendingCtrl;
    bool m_bPendingShift;
    int m_marqueeStartScrollY;
    int m_marqueeStartScrollX;
    int m_marqueeCurrentX;
    int m_marqueeCurrentY;
    std::unordered_set<int> m_marqueeInitialSelection;
    bool m_isAutoScrolling;
    static constexpr UINT_PTR TIMER_AUTOSCROLL_ID = 0x5C02;

    // Hover & Press states for buttons
    int m_hoveredRow;
    int m_hoveredCol;
    int m_pressedRow;
    int m_pressedCol;
    
    // Column Resizing state
    bool m_isResizing;
    int m_resizeColIndex;
    int m_resizeStartX;
    int m_resizeStartWidth;

    // Sorting state
    int m_sortColIndex;      // Index of currently sorted column (-1 if none)
    bool m_sortAscending;    // Sort direction (true = Ascending, false = Descending)
    int m_hoverHeaderColIndex;    // Index of header column currently hovered (-1 if none)
    int m_pressedHeaderColIndex;  // Index of header column currently pressed (-1 if none)
    bool m_hoverHeaderInDropdown;   // Cursor is in the right 20px of the hovered column
    bool m_pressedHeaderInDropdown; // Mouse down was initiated in the dropdown area
    std::vector<std::vector<std::wstring>> m_activeFilters; // Checked filters per column

    // Focus & Type-Ahead / FAYT state
    bool m_hasFocus;
    std::wstring m_typeAheadBuffer;
    DWORD m_lastTypeAheadTime;

    // Find-As-You-Type (FAYT) / DOpus-style Quick Search
    bool m_bFaytActive;
    std::wstring m_faytQuery;
    std::vector<int> m_faytMatches;
    int m_faytMatchIndex;
    RECT m_rcFaytPill;
    RECT m_rcFaytPrevBtn;
    RECT m_rcFaytNextBtn;
    RECT m_rcFaytCloseBtn;
    int m_faytHoverBtn; // 0=none, 1=prev, 2=next, 3=close
    HFONT m_hFontFaytIcon;
    HFONT m_hFontFaytText;
    static constexpr UINT_PTR TIMER_FAYT_TIMEOUT_ID = 0x5C03;

    // Inline Column Hover Copy Button & Feedback State
    int m_inlineCopyCol;
    int m_copiedRow;
    int m_copiedCol;
    ULONGLONG m_copiedTick;
    bool m_bHoverCopyBtn;
    HFONT m_hFontCopyIcon;
    static constexpr UINT_PTR TIMER_COPY_FEEDBACK_ID = 0x5C04;

    void StartFayt(wchar_t initialChar);
    void UpdateFaytMatches();
    void FaytNext();
    void FaytPrev();
    void CloseFayt();
    void ResetFaytTimer();

public:
    CustomListControl();
    ~CustomListControl();

    static bool Register(HINSTANCE hInstance);
    HWND Create(HWND hParent, int x, int y, int width, int height, UINT_PTR id);
    HWND GetHWND() const { return m_hWnd; }

    void AddColumn(const std::wstring& title, int width, int align = 0);
    void ClearColumns();
    void SetColumnTitle(int colIndex, const std::wstring& title);
    void SortByColumn(int colIndex, bool toggleDirection = true);
    int GetSortColumn() const { return m_sortColIndex; }
    bool IsSortAscending() const { return m_sortAscending; }
    void SetSortState(int colIndex, bool ascending) { m_sortColIndex = colIndex; m_sortAscending = ascending; }
    void SetColumnWidth(int colIndex, int width);
    int GetColumnWidth(int colIndex) const;

    // Filtering APIs
    const std::vector<std::wstring>& GetActiveFilters(int colIndex) const;
    void SetActiveFilters(int colIndex, const std::vector<std::wstring>& filters);
    void ClearAllFilters();
    
    // Standard Mode APIs
    void AddItem(const std::vector<std::wstring>& cells);
    void SetCellText(int itemIndex, int subItemIndex, const std::wstring& text);
    void Clear();
    int GetItemCount() const;
    int GetUsableWidth() const;

    // Virtual Mode APIs
    void SetVirtualMode(GetCellTextCallback callback, void* pParam);
    void SetItemCount(int count);

    // Multi-Selection APIs
    void SetMultiSelect(bool bEnable);
    bool IsMultiSelect() const { return m_isMultiSelect; }
    int GetSelectedIndex() const { return m_selectedIndex; }
    int GetFirstSelectedIndex() const {
        if (m_isMultiSelect) {
            return m_selectedOrder.empty() ? -1 : m_selectedOrder.front();
        }
        return m_selectedIndex;
    }
    void SetSelectedIndex(int index);
    std::vector<int> GetSelectedIndices() const;
    void SetSelectedIndices(const std::vector<int>& indices);
    void SelectAll();
    void ClearSelection();
    bool IsRowSelected(int row) const;
    void ToggleRowSelection(int row);

    // Integrated Selection Gutter APIs
    void SetShowSelectionGutter(bool bShow);
    bool IsShowSelectionGutter() const { return m_bShowSelectionGutter; }
    int GetGutterWidth() const { return m_bShowSelectionGutter ? 32 : 0; }
    void SetItemChecked(int itemIndex, bool bChecked);
    bool IsItemChecked(int itemIndex) const;
    void SetAllItemsChecked(bool bChecked);
    void ClearCheckedItems();
    std::vector<int> GetCheckedIndices() const;
    void SetCheckedIndices(const std::vector<int>& indices);
    int GetCheckedCount() const;

    // Consist Units Rearrangement / Reordering APIs
    void SetAllowRearrange(bool allow) { m_bAllowRearrange = allow; }
    bool IsAllowRearrange() const { return m_bAllowRearrange; }
    void SetAllowTransferSource(bool allow) { m_bAllowTransferSource = allow; }
    bool IsAllowTransferSource() const { return m_bAllowTransferSource; }
    void SetAllowMarquee(bool allow) { m_bAllowMarquee = allow; }
    bool IsAllowMarquee() const { return m_bAllowMarquee; }
    void SetDrawCardBorder(bool allow) { m_bDrawCardBorder = allow; Invalidate(); }
    bool IsDrawCardBorder() const { return m_bDrawCardBorder; }
    void SetInlineCopyColumn(int colIndex) { m_inlineCopyCol = colIndex; Invalidate(); }
    int  GetInlineCopyColumn() const { return m_inlineCopyCol; }
    void SetDropTargetIndex(int idx) { m_dropTargetIndex = idx; Invalidate(); }
    int  GetDropTargetIndex() const { return m_dropTargetIndex; }
    bool IsItemDragging() const { return m_bIsItemDragging; }
    void CancelItemDrag();

    // Drop Target & Drag Auto-Scroll Math Helpers
    int  GetDropIndexFromPoint(POINT ptClient) const;
    int  GetDropIndexFromScreenPoint(POINT ptScreen) const;
    void CheckDragAutoScroll(POINT ptClient);

    // Focus & Layout
    int GetScrollY() const { return m_scrollY; }
    void SetScrollY(int scrollY) { m_scrollY = scrollY; UpdateScrollbars(); Invalidate(); }
    void EnsureVisible(int index);

    void SetFocus();
    void Invalidate();
    void ApplyScrollbarTheme();

    // Size Helper
    void Resize(int x, int y, int width, int height);
    std::wstring GetCellText(int itemIndex, int subItemIndex) const;

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    void CreateScrollbars();
    void LayoutScrollbars();
    void UpdateScrollbars();

    void UpdateMarqueeSelection(int currentX, int currentY);
    void CheckAutoScroll(int currentX, int currentY);
    void StopAutoScroll();

    // Returns the usable client rect (excluding scrollbar strips)
    RECT GetListRect() const;
    int GetVisibleRows() const;
};
