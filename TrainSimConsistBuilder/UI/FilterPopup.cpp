#include "FilterPopup.h"
#include "CustomDropDownMenu.h"
#include <algorithm>

FilterPopup::FilterPopup()
    : m_hWnd(NULL), m_hParent(NULL), m_colIndex(-1), m_callback(NULL), m_callbackParam(NULL),
      m_hoverIndex(-1), m_bTrackingMouse(false), m_hFont(NULL), m_itemHeight(32)
{
}

FilterPopup::~FilterPopup()
{
}

bool FilterPopup::Register(HINSTANCE hInstance)
{
    return true;
}

void FilterPopup::Show(HWND hParent, int colIndex, int x, int y, const std::vector<std::wstring>& allOptions, const std::vector<std::wstring>& checkedOptions, FilterPopupCallback callback, void* pParam)
{
    if (allOptions.empty()) return;

    std::vector<DropDownItem> items;
    for (size_t i = 0; i < allOptions.size(); ++i)
    {
        bool checked = (std::find(checkedOptions.begin(), checkedOptions.end(), allOptions[i]) != checkedOptions.end());
        items.push_back(DropDownItem::Action((int)i + 1, L"", allOptions[i], L"", checked, true));
    }

    POINT ptAnchor = { x, y };
    ScreenToClient(hParent, &ptAnchor);
    RECT rcAnchor = { ptAnchor.x, ptAnchor.y, ptAnchor.x + 20, ptAnchor.y + 20 };

    CustomDropDownMenu::ShowMultiSelect(hParent, rcAnchor, items, [colIndex, callback, pParam](const std::vector<DropDownItem>& updatedItems) {
        if (callback)
        {
            std::vector<std::wstring> newChecked;
            for (const auto& item : updatedItems)
            {
                if (item.isChecked)
                {
                    newChecked.push_back(item.text);
                }
            }
            callback(colIndex, newChecked, pParam);
        }
    });
}

LRESULT CALLBACK FilterPopup::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT FilterPopup::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}
