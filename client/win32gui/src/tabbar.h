// tabbar.h -- Owner-drawn tab bar for world connections.
#ifndef TABBAR_H
#define TABBAR_H

#include "window.h"
#include <string>
#include <vector>

struct TabInfo {
    std::string name;
    bool active = false;    // has unread background activity
    bool ssl = false;
    bool connected = true;
};

class CTabBar : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy);

    int AddTab(const std::string& name);
    void RemoveTab(int index);
    void SetCurrentTab(int index);
    int GetCurrentTab() const { return current_; }
    int TabCount() const { return (int)tabs_.size(); }
    void UpdateTab(int index, const TabInfo& info);
    const TabInfo* GetTab(int index) const;

    void SetFont(HFONT font);

    static constexpr int TAB_HEIGHT = 28;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    int HitTest(int x, int y) const;
    int TabWidth(int index) const;

    std::vector<TabInfo> tabs_;
    int current_ = -1;
    HFONT font_ = nullptr;
    int char_width_ = 8;
};

#endif // TABBAR_H
