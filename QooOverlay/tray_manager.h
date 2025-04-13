#pragma once
#include <windows.h>

class TrayManager {
public:
    TrayManager();
    ~TrayManager();
    void InitTrayIcon();
    void CleanupTrayIcon();

private:
    NOTIFYICONDATA nid;
    HWND message_hwnd;

    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};