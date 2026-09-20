#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "CustomScrollBar.h"

struct CustomTreeNode
{
    int id = 0;
    std::wstring text;
    std::wstring tag;
    int level = 0;
    bool isFolder = false;
    bool isExpanded = false;
    CustomTreeNode* parent = nullptr;
    std::vector<CustomTreeNode*> children;

    ~CustomTreeNode()
    {
        for (auto* child : children)
        {
            delete child;
        }
        children.clear();
    }
};

class CustomTreeView
{
public:
    CustomTreeView();
    ~CustomTreeView();

    HWND Create(HWND hParent, int x, int y, int w, int h, int id);
    HWND GetHWND() const { return m_hWnd; }

    CustomTreeNode* AddRoot(const std::wstring& text, const std::wstring& tag = L"", int id = 0, bool isFolder = true);
    CustomTreeNode* AddChild(CustomTreeNode* parent, const std::wstring& text, const std::wstring& tag = L"", int id = 0, bool isFolder = false);
    void Clear();
    void DeleteAllChildren(CustomTreeNode* parent);

    void ExpandNode(CustomTreeNode* node, bool expand);
    void ToggleExpand(CustomTreeNode* node);
    void SelectNode(CustomTreeNode* node);
    CustomTreeNode* GetSelectedNode() const { return m_selectedNode; }
    CustomTreeNode* FindNodeByTag(const std::wstring& tag, CustomTreeNode* startFrom = nullptr);
    CustomTreeNode* FindNodeById(int id, CustomTreeNode* startFrom = nullptr);

    void SetBounds(int x, int y, int w, int h);
    void Show(bool show);
    void SetDarkMode(bool dark);
    void SetFont(HFONT hFont);
    void Invalidate();

    // Callbacks
    void SetSelectionCallback(std::function<void(CustomTreeNode*)> cb) { m_onSelectionChanged = cb; }
    void SetDoubleClickCallback(std::function<void(CustomTreeNode*)> cb) { m_onItemDoubleClicked = cb; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void FlattenVisibleNodes();
    void FlattenNode(CustomTreeNode* node);
    void UpdateScrollbars();
    void LayoutScrollbars();
    int GetRowHeight() const { return m_rowHeight; }

    CustomTreeNode* GetNodeAtPoint(POINT pt, bool* outIsChevron = nullptr);
    RECT GetRowRect(int visibleIndex) const;
    RECT GetChevronRect(int visibleIndex, int level) const;

    HWND m_hWnd;
    HWND m_hParent;
    int m_controlId;
    HFONT m_hFont;
    bool m_bDarkMode;
    int m_rowHeight;
    int m_scrollY;

    std::vector<CustomTreeNode*> m_roots;
    std::vector<CustomTreeNode*> m_visibleNodes;
    CustomTreeNode* m_selectedNode;
    CustomTreeNode* m_hoverNode;

    CustomScrollBar m_vScroll;
    bool m_isDraggingScroll;

    std::function<void(CustomTreeNode*)> m_onSelectionChanged;
    std::function<void(CustomTreeNode*)> m_onItemDoubleClicked;

    HWND m_hToolTip;
    std::wstring m_lastTooltipText;
};
