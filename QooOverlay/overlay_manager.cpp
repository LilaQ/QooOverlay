#include <windows.h>
#include <string>
#include <vector>
#include "overlay_manager.h"
#include "include/detours/detours.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>
#include <WebView2.h>
#include <shlobj.h>
#include <knownfolders.h>
#include "tray_manager.h"
#include <ShellScalingApi.h>

using namespace Microsoft::WRL;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "detours.lib")
#pragma comment(lib, "WebView2Loader.dll.lib")
#pragma comment(lib, "shell32.lib")

typedef HRESULT(WINAPI* PFN_Present)(IDXGISwapChain*, UINT, UINT);

struct Overlay {
    std::wstring name;
    std::wstring url;
    UINT hotkey_modifiers;
    UINT hotkey_vk;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    HWND hwnd = nullptr;
    bool visible = false;
    EventRegistrationToken navCompletedTokenBlank = { 0 };
    EventRegistrationToken navCompletedTokenContent = { 0 };
    std::wstring userDataFolder; // Store the user data folder for environment recreation
};

struct OverlayManager::Impl {
    std::vector<Overlay> overlays;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    PFN_Present TruePresent = nullptr;
    static OverlayManager::Impl* instance;
    TrayManager tray_manager;
    HHOOK keyboardHook = nullptr; // Low-level keyboard hook

    void AddOverlay(const std::wstring& name, const std::wstring& url, UINT modifiers, UINT key) {
        overlays.push_back({ name, url, modifiers, key });
        OutputDebugStringW((L"Added overlay: " + name + L"\n").c_str());
    }

    void CreateOverlayWindows() {
        OutputDebugStringW(L"Starting CreateOverlayWindows\n");
        wchar_t* appDataPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
            OutputDebugStringW(L"Failed to get AppData path\n");
            return;
        }
        std::wstring userDataFolder = std::wstring(appDataPath) + L"\\QooOverlay";
        CoTaskMemFree(appDataPath);
        CreateDirectoryW(userDataFolder.c_str(), nullptr);

        for (auto& overlay : overlays) {
            overlay.userDataFolder = userDataFolder; // Store the user data folder
            HWND hwnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"STATIC", overlay.name.c_str(), WS_POPUP,
                0, 0, 3840, 2160, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
            if (!hwnd) {
                OutputDebugStringW((L"Failed to create window for " + overlay.name + L"\n").c_str());
                continue;
            }
            overlay.hwnd = hwnd;
            OutputDebugStringW((L"Created window for " + overlay.name + L"\n").c_str());

            HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
                nullptr, userDataFolder.c_str(), nullptr,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [&overlay, hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                        if (FAILED(result) || !env) {
                            OutputDebugStringW((L"Failed to create WebView2 env for " + overlay.name + L": HRESULT " + std::to_wstring(result) + L"\n").c_str());
                            return result;
                        }
                        overlay.env = env;
                        OutputDebugStringW((L"Created WebView2 env for " + overlay.name + L"\n").c_str());
                        return env->CreateCoreWebView2Controller(
                            hwnd,
                            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [&overlay](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                    if (FAILED(result) || !controller) {
                                        OutputDebugStringW((L"Failed to create WebView2 controller for " + overlay.name + L": HRESULT " + std::to_wstring(result) + L"\n").c_str());
                                        return result;
                                    }
                                    overlay.controller = controller;
                                    OutputDebugStringW((L"Created WebView2 controller for " + overlay.name + L"\n").c_str());
                                    RECT bounds = { 0, 0, 3840, 2160 };
                                    controller->put_Bounds(bounds);
                                    controller->put_IsVisible(false);
                                    Microsoft::WRL::ComPtr<ICoreWebView2Controller4> controller4;
                                    HRESULT hr = controller->QueryInterface(IID_PPV_ARGS(&controller4));
                                    if (SUCCEEDED(hr) && controller4) {
                                        COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                        controller4->put_DefaultBackgroundColor(transparent);
                                        OutputDebugStringW((L"Set transparent background for " + overlay.name + L"\n").c_str());
                                    }
                                    else {
                                        OutputDebugStringW((L"Failed to get ICoreWebView2Controller4 for " + overlay.name + L"\n").c_str());
                                    }
                                    controller->get_CoreWebView2(&overlay.webview);
                                    if (!overlay.webview) {
                                        OutputDebugStringW((L"Failed to get WebView2 for " + overlay.name + L"\n").c_str());
                                        return E_FAIL;
                                    }
                                    SetLayeredWindowAttributes(overlay.hwnd, 0, 255, LWA_ALPHA);
                                    ShowWindow(overlay.hwnd, SW_HIDE);
                                    UpdateWindow(overlay.hwnd);
                                    return S_OK;
                                }).Get());
                    }).Get());
            if (FAILED(hr)) {
                OutputDebugStringW((L"CreateCoreWebView2EnvironmentWithOptions failed for " + overlay.name + L": HRESULT " + std::to_wstring(hr) + L"\n").c_str());
            }
        }
        OutputDebugStringW(L"Finished CreateOverlayWindows\n");
    }

    void ToggleOverlay(size_t index) {
        if (index >= overlays.size()) {
            OutputDebugStringW(L"ToggleOverlay: Invalid index\n");
            return;
        }
        auto& overlay = overlays[index];
        overlay.visible = !overlay.visible;
        OutputDebugStringW((L"Toggling " + overlay.name + L" to " + (overlay.visible ? L"visible" : L"hidden") + L"\n").c_str());
        if (overlay.controller) {
            // Close the existing controller and environment
            overlay.controller->Close();
            overlay.controller.Reset();
            overlay.webview.Reset();
            overlay.env.Reset();
            ShowWindow(overlay.hwnd, SW_HIDE);
            UpdateWindow(overlay.hwnd);
        }

        if (overlay.visible) {
            // Recreate the WebView2 environment and controller
            HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
                nullptr, overlay.userDataFolder.c_str(), nullptr,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [&overlay](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                        if (FAILED(result) || !env) {
                            OutputDebugStringW((L"Failed to recreate WebView2 env for " + overlay.name + L": HRESULT " + std::to_wstring(result) + L"\n").c_str());
                            return result;
                        }
                        overlay.env = env;
                        OutputDebugStringW((L"Recreated WebView2 env for " + overlay.name + L"\n").c_str());
                        return env->CreateCoreWebView2Controller(
                            overlay.hwnd,
                            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [&overlay](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                    if (FAILED(result) || !controller) {
                                        OutputDebugStringW((L"Failed to recreate WebView2 controller for " + overlay.name + L": HRESULT " + std::to_wstring(result) + L"\n").c_str());
                                        return result;
                                    }
                                    overlay.controller = controller;
                                    OutputDebugStringW((L"Recreated WebView2 controller for " + overlay.name + L"\n").c_str());
                                    RECT bounds = { 0, 0, 3840, 2160 };
                                    controller->put_Bounds(bounds);
                                    controller->put_IsVisible(true);
                                    Microsoft::WRL::ComPtr<ICoreWebView2Controller4> controller4;
                                    HRESULT hr = controller->QueryInterface(IID_PPV_ARGS(&controller4));
                                    if (SUCCEEDED(hr) && controller4) {
                                        COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                        controller4->put_DefaultBackgroundColor(transparent);
                                        OutputDebugStringW((L"Set transparent background for " + overlay.name + L"\n").c_str());
                                    }
                                    controller->get_CoreWebView2(&overlay.webview);
                                    if (!overlay.webview) {
                                        OutputDebugStringW((L"Failed to get WebView2 for " + overlay.name + L"\n").c_str());
                                        return E_FAIL;
                                    }
                                    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                                    overlay.webview->get_Settings(&settings);
                                    if (settings) {
                                        settings->put_AreDefaultContextMenusEnabled(false);
                                    }

                                    HRESULT navResult = overlay.webview->Navigate(overlay.url.c_str());
                                    if (SUCCEEDED(navResult)) {
                                        OutputDebugStringW((L"Navigated to " + overlay.url + L"\n").c_str());
                                    }
                                    else {
                                        OutputDebugStringW((L"Failed to navigate to " + overlay.url + L": HRESULT " + std::to_wstring(navResult) + L"\n").c_str());
                                    }
                                    ShowWindow(overlay.hwnd, SW_SHOWNA);
                                    UpdateWindow(overlay.hwnd);
                                    return S_OK;
                                }).Get());
                    }).Get());
            if (FAILED(hr)) {
                OutputDebugStringW((L"CreateCoreWebView2EnvironmentWithOptions failed for " + overlay.name + L": HRESULT " + std::to_wstring(hr) + L"\n").c_str());
            }
        }
        OutputDebugStringW((L"Updated window visibility for " + overlay.name + L"\n").c_str());
    }

    static HRESULT WINAPI DetouredPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        OutputDebugStringW(L"DetouredPresent called\n");
        if (!instance) return E_FAIL;
        if (!instance->TruePresent) {
            void** vtable = *(void***)swapChain;
            instance->TruePresent = (PFN_Present)vtable[8];
            HRESULT hr = DetourTransactionBegin();
            if (SUCCEEDED(hr)) {
                hr = DetourUpdateThread(GetCurrentThread());
                if (SUCCEEDED(hr)) {
                    hr = DetourAttach(&(PVOID&)instance->TruePresent, DetouredPresent);
                    if (SUCCEEDED(hr)) {
                        hr = DetourTransactionCommit();
                        if (SUCCEEDED(hr)) {
                            OutputDebugStringW(L"Hooked Present in DetouredPresent\n");
                        }
                        else {
                            OutputDebugStringW(L"DetourTransactionCommit failed\n");
                        }
                    }
                }
                if (FAILED(hr)) {
                    DetourTransactionAbort();
                }
            }
        }
        if (!instance->d3d_device) {
            swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&instance->d3d_device);
            instance->swap_chain = swapChain;
            OutputDebugStringW(L"Got device in DetouredPresent\n");
        }
        return instance->TruePresent ? instance->TruePresent(swapChain, syncInterval, flags) : E_FAIL;
    }

    void HookDirectX() {
        OutputDebugStringW(L"Starting HookDirectX\n");
        OutputDebugStringW(L"HookDirectX deferred to DetouredPresent\n");
    }

    // Low-level keyboard hook callback
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode < 0) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        if (wParam == WM_KEYDOWN) {
            KBDLLHOOKSTRUCT* kbStruct = (KBDLLHOOKSTRUCT*)lParam;
            UINT key = kbStruct->vkCode;

            // Check if any overlay is visible
            bool handled = false;
            for (auto& overlay : instance->overlays) {
                if (overlay.visible && overlay.webview) {
                    if (key == VK_LEFT || key == VK_RIGHT || key == VK_SPACE) {
                        // Map the virtual key to a JavaScript key name
                        std::wstring jsKey;
                        if (key == VK_LEFT) {
                            jsKey = L"ArrowLeft";
                        }
                        else if (key == VK_RIGHT) {
                            jsKey = L"ArrowRight";
                        }
                        else if (key == VK_SPACE) {
                            jsKey = L"Space";
                        }
                        else {
                            continue; // Ignore other keys for now
                        }

                        // Execute JavaScript to dispatch a keydown event
                        std::wstring script = L"(() => { "
                            L"const event = new KeyboardEvent('keydown', { key: '" + jsKey + L"', bubbles: true }); "
                            L"document.dispatchEvent(event); "
                            L"})();";
                        overlay.webview->ExecuteScript(script.c_str(), nullptr);
                        handled = true;
                        break; // Only handle the first visible overlay
                    }
                }
            }

            if (handled) {
                return 1; // Prevent the keypress from being processed further
            }
        }

        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    void Run() {
        OutputDebugStringW(L"Starting Run\n");

        // Set DPI awareness to ignore Windows scaling
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            OutputDebugStringW(L"Failed to set DPI awareness\n");
        }
        else {
            OutputDebugStringW(L"DPI awareness set to per-monitor V2\n");
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            OutputDebugStringW((L"CoInitializeEx failed: HRESULT " + std::to_wstring(hr) + L"\n").c_str());
            return;
        }
        OutputDebugStringW(L"COM initialized\n");
        instance = this;
        tray_manager.InitTrayIcon();
        HookDirectX();
        CreateOverlayWindows();

        // Set up the low-level keyboard hook
        keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
        if (keyboardHook == nullptr) {
            OutputDebugStringW(L"Failed to set keyboard hook\n");
        }
        else {
            OutputDebugStringW(L"Keyboard hook set successfully\n");
        }

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_HOTKEY) {
                OutputDebugStringW((L"WM_HOTKEY received: wParam = " + std::to_wstring(msg.wParam) + L"\n").c_str());
                for (size_t i = 0; i < overlays.size(); ++i) {
                    if (msg.wParam == static_cast<int>(i + 1)) {
                        ToggleOverlay(i);
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Clean up the keyboard hook
        if (keyboardHook) {
            UnhookWindowsHookEx(keyboardHook);
            keyboardHook = nullptr;
            OutputDebugStringW(L"Keyboard hook removed\n");
        }

        tray_manager.CleanupTrayIcon();
        for (auto& overlay : overlays) {
            if (overlay.navCompletedTokenBlank.value != 0) {
                overlay.webview->remove_NavigationCompleted(overlay.navCompletedTokenBlank);
            }
            if (overlay.navCompletedTokenContent.value != 0) {
                overlay.webview->remove_NavigationCompleted(overlay.navCompletedTokenContent);
            }
        }
        CoUninitialize();
        OutputDebugStringW(L"Exiting Run\n");
    }
};

OverlayManager::Impl* OverlayManager::Impl::instance = nullptr;

OverlayManager::OverlayManager() : impl(std::make_unique<Impl>()) {
    OutputDebugStringW(L"OverlayManager constructed\n");
}
OverlayManager::~OverlayManager() = default;
void OverlayManager::AddOverlay(const std::wstring& name, const std::wstring& htmlPath, UINT modifiers, UINT key) {
    impl->AddOverlay(name, htmlPath, modifiers, key);
    BOOL result = RegisterHotKey(nullptr, static_cast<int>(impl->overlays.size()), modifiers, key);
    OutputDebugStringW((L"RegisterHotKey for " + name + L": " + (result ? L"success" : L"failed") + L"\n").c_str());
}
void OverlayManager::Run() {
    impl->Run();
}