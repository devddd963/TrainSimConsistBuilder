#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

struct DropDownItem
{
    int id = 0;
    std::wstring text;
    std::wstring secondaryText;
    std::wstring icon; // Fluent glyph, e.g. L"\xE73E"
    bool isChecked = false;
    bool isHeader = false;
    bool isIndeterminate = false;
    bool isEnabled = true;

    DropDownItem() = default;

    static DropDownItem Action(int id, const std::wstring& icon, const std::wstring& text, const std::wstring& secondaryText = L"", bool isChecked = false, bool isEnabled = true)
    {
        DropDownItem item;
        item.id = id;
        item.icon = icon;
        item.text = text;
        item.secondaryText = secondaryText;
        item.isChecked = isChecked;
        item.isHeader = false;
        item.isEnabled = isEnabled;
        return item;
    }

    static DropDownItem Header(int id, const std::wstring& icon, const std::wstring& text, const std::wstring& secondaryText = L"", bool isChecked = false, bool isIndeterminate = false)
    {
        DropDownItem item;
        item.id = id;
        item.icon = icon;
        item.text = text;
        item.secondaryText = secondaryText;
        item.isChecked = isChecked;
        item.isHeader = true;
        item.isIndeterminate = isIndeterminate;
        item.isEnabled = true;
        return item;
    }
};

class CustomDropDownMenu
{
public:
    // Shows a single-select dropdown attached to rcAnchor (in parent client coordinates).
    // Returns the selected item ID (id > 0), or 0 if cancelled / dismissed.
    static int ShowSingleSelect(
        HWND hParent,
        const RECT& rcAnchor,
        const std::vector<DropDownItem>& items,
        int selectedId = -1,
        int minWidth = 0
    );

    // Shows an interactive multi-select checkbox dropdown attached to rcAnchor (in parent client coordinates).
    // The items list will be updated with user selections.
    // An optional callback is triggered whenever an item is toggled.
    // Returns true if modified/confirmed, or false if dismissed without changes.
    static bool ShowMultiSelect(
        HWND hParent,
        const RECT& rcAnchor,
        std::vector<DropDownItem>& items,
        std::function<void(const std::vector<DropDownItem>&)> onItemToggledCallback = nullptr,
        int minWidth = 0
    );

    // Programmatically closes any active dropdown window.
    static void CloseActive();

    // Checks if any dropdown menu is currently active/open.
    static bool IsActive();
};
