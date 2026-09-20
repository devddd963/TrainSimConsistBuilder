#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ContextMenuItem {
    int id;
    std::wstring icon;     // Segoe Fluent Icons glyph, e.g. L"\xE74D" (Delete)
    std::wstring text;     // Label text, e.g. L"Delete Selected"
    std::wstring shortcut; // e.g. L"Delete", L"Ctrl+Z", L"Ctrl+R"
    bool isSeparator;
    bool isEnabled;

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
