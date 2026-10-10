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
    std::wstring group; // Collapsible group / category identifier
    std::wstring parentGroup; // Parent group identifier for nested collapsible groups
    int indentLevel = 0; // 0 = root, 1 = category child or preset pool, 2 = blueprint pool
    bool isChecked = false;
    bool isHeader = false;
    bool isGroupHeader = false;
    bool isCollapsed = false;
    int groupCount = 0;
    bool isSeparator = false;
    bool isIndeterminate = false;
    bool isEnabled = true;

    DropDownItem() = default;

    static DropDownItem Action(int id, const std::wstring& icon, const std::wstring& text, const std::wstring& secondaryText = L"", bool isChecked = false, bool isEnabled = true, const std::wstring& group = L"", const std::wstring& parentGroup = L"", int indentLevel = 0)
    {
        DropDownItem item;
        item.id = id;
        item.icon = icon;
        item.text = text;
        item.secondaryText = secondaryText;
        item.group = group;
        item.parentGroup = parentGroup;
        item.indentLevel = indentLevel;
        item.isChecked = isChecked;
        item.isHeader = false;
        item.isGroupHeader = false;
        item.isSeparator = false;
        item.isEnabled = isEnabled;
        return item;
    }

    static DropDownItem GroupHeader(const std::wstring& groupName, const std::wstring& icon = L"\xE8B7", int count = 0, bool isCollapsed = false, int id = 0, bool isChecked = false, bool isIndeterminate = false, const std::wstring& secondaryText = L"", const std::wstring& parentGroup = L"", int indentLevel = 0, const std::wstring& groupKey = L"")
    {
        DropDownItem item;
        item.id = id;
        item.icon = icon;
        item.text = groupName;
        item.group = groupKey.empty() ? groupName : groupKey;
        item.parentGroup = parentGroup;
        item.indentLevel = indentLevel;
        item.secondaryText = secondaryText;
        item.isChecked = isChecked;
        item.isHeader = true;
        item.isGroupHeader = true;
        item.isCollapsed = isCollapsed;
        item.groupCount = count;
        item.isSeparator = false;
        item.isIndeterminate = isIndeterminate;
        item.isEnabled = true;
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
        item.isGroupHeader = false;
        item.isSeparator = false;
        item.isIndeterminate = isIndeterminate;
        item.isEnabled = true;
        return item;
    }

    static DropDownItem Header(const std::wstring& text)
    {
        DropDownItem item;
        item.id = 0;
        item.icon = L"";
        item.text = text;
        item.isHeader = true;
        item.isGroupHeader = false;
        item.isSeparator = false;
        item.isEnabled = false;
        return item;
    }

    static DropDownItem Separator()
    {
        DropDownItem item;
        item.id = -1;
        item.text = L"";
        item.isHeader = false;
        item.isGroupHeader = false;
        item.isSeparator = true;
        item.isEnabled = false;
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
