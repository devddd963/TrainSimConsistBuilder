#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ContextMenuItem {
    int id = 0;
    std::wstring icon = L"";     // Segoe Fluent Icons glyph, e.g. L"\xE74D" (Delete)
    std::wstring text = L"";     // Label text, e.g. L"Delete Selected"
    std::wstring shortcut = L""; // e.g. L"Delete", L"Ctrl+Z", L"Ctrl+R"
    bool isSeparator = false;
    bool isEnabled = true;
    std::vector<ContextMenuItem> subItems; // Child items for cascading submenu

    static ContextMenuItem Action(int id, const std::wstring& icon, const std::wstring& text, const std::wstring& shortcut = L"", bool enabled = true)
    {
        ContextMenuItem item;
        item.id = id;
        item.icon = icon;
        item.text = text;
        item.shortcut = shortcut;
        item.isSeparator = false;
        item.isEnabled = enabled;
        return item;
    }

    static ContextMenuItem SubMenu(const std::wstring& icon, const std::wstring& text, const std::vector<ContextMenuItem>& subItems, bool enabled = true)
    {
        ContextMenuItem item;
        item.id = -1;
        item.icon = icon;
        item.text = text;
        item.shortcut = L"";
        item.isSeparator = false;
        item.isEnabled = enabled;
        item.subItems = subItems;
        return item;
    }

    static ContextMenuItem Separator()
    {
        ContextMenuItem item;
        item.id = 0;
        item.isSeparator = true;
        item.isEnabled = false;
        return item;
    }
};


class ModernContextMenu {
public:
    // Shows the context menu modally at screen coordinates (x, y) and returns the chosen command ID (0 if dismissed)
    static int Show(HWND hParent, int x, int y, const std::vector<ContextMenuItem>& items, BOOL bDarkMode = TRUE, int minWidth = 0);
};
