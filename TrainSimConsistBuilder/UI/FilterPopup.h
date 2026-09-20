#pragma once
#include <windows.h>
#include <string>
#include <vector>

typedef void (*FilterPopupCallback)(int colIndex, const std::vector<std::wstring>& checkedOptions, void* pParam);

struct FilterItem {
    std::wstring label;
    bool checked;
};

class FilterPopup {
private:
    HWND m_hWnd;
    HWND m_hParent;
    int m_colIndex;
    std::vector<FilterItem> m_items;
    FilterPopupCallback m_callback;
    void* m_callbackParam;
    int m_hoverIndex;
    bool m_bTrackingMouse;
    HFONT m_hFont;
    int m_itemHeight;

public:
    FilterPopup();
    ~FilterPopup();

    static bool Register(HINSTANCE hInstance);
    
    void Show(HWND hParent, int colIndex, int x, int y, const std::vector<std::wstring>& allOptions, const std::vector<std::wstring>& checkedOptions, FilterPopupCallback callback, void* pParam);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
