#include "tray_manager.h"
#include "resource.h"
#include <string>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_COPYRIGHT 1002

TrayManager::TrayManager() : message_hwnd(nullptr) {
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
}

TrayManager::~TrayManager() {
    CleanupTrayIcon();
}

void TrayManager::InitTrayIcon() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"QooOverlayTrayClass";
    RegisterClassW(&wc);

    message_hwnd = CreateWindowW(L"QooOverlayTrayClass", L"QooOverlay", 0, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    SetWindowLongPtr(message_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = message_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    // Load the icon directly from file
    nid.hIcon = (HICON)LoadImage(nullptr, L"C:\\Users\\LilaQ\\Desktop\\Projects\\QooOverlay\\QooOverlay\\assets\\qoo_icon.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (nid.hIcon == nullptr) {
        wchar_t buffer[32];
        _itow_s(GetLastError(), buffer, 10);
        OutputDebugStringW((L"Failed to load tray icon from file: Error " + std::wstring(buffer) + L"\n").c_str());
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        OutputDebugStringW(L"Using fallback system icon\n");
    }
    else {
        OutputDebugStringW(L"Successfully loaded tray icon from file\n");
    }

    wcscpy_s(nid.szTip, L"QooOverlay - Running");
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        wchar_t buffer[32];
        _itow_s(GetLastError(), buffer, 10);
        OutputDebugStringW((L"Failed to add tray icon: Error " + std::wstring(buffer) + L"\n").c_str());
    }
    else {
        OutputDebugStringW(L"Initialized tray icon\n");
    }
}

void TrayManager::CleanupTrayIcon() {
    if (!Shell_NotifyIcon(NIM_DELETE, &nid)) {
        wchar_t buffer[32];
        _itow_s(GetLastError(), buffer, 10);
        OutputDebugStringW((L"Failed to remove tray icon: Error " + std::wstring(buffer) + L"\n").c_str());
    }
    if (message_hwnd) {
        DestroyWindow(message_hwnd);
        message_hwnd = nullptr;
    }
    OutputDebugStringW(L"Cleaned up tray icon\n");
}

LRESULT CALLBACK TrayManager::TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrayManager* tray = reinterpret_cast<TrayManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, ID_TRAY_COPYRIGHT, L"QooOverlay - Copyright 2025 by Jan Sallads");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
    }
    else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIcon(NIM_DELETE, &tray->nid);
            PostQuitMessage(0);
        }
    }
    else if (msg == WM_DESTROY) {
        PostQuitMessage(0);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}