#pragma once
#include <windows.h>
#include <vector>
#include <string>

enum class GhostItemType
{
    VehicleUnit,
    PoolCard
};

struct DragGhostItem
{
    std::wstring name;
    bool isEngine = false;
    GhostItemType type = GhostItemType::VehicleUnit;
    std::wstring subtitle = L"";
};

class FluentDragGhost
{
public:
    static void Initialize(HINSTANCE hInstance);
    static void Show(HWND hParent, POINT ptScreen, const std::vector<DragGhostItem>& items);
    static void Move(POINT ptScreen, bool isValidDropTarget, const std::wstring& actionText = L"");
    static void Hide();
    static bool IsActive();

private:
    static HWND s_hWnd;
    static HINSTANCE s_hInstance;
    static std::vector<DragGhostItem> s_items;
    static bool s_isValidTarget;
    static std::wstring s_actionText;
    static HFONT s_hFontText;
    static HFONT s_hFontBadge;
    static HFONT s_hFontIcon;

    static void RenderGhost();
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
