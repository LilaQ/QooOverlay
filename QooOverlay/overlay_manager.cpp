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
    HWND hwnd = nullptr;
    bool visible = false;
};

struct OverlayManager::Impl {
    std::vector<Overlay> overlays;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    PFN_Present TruePresent = nullptr;
    static OverlayManager::Impl* instance;

    void AddOverlay(const std::wstring& name, const std::wstring& url, UINT modifiers, UINT key) {
        overlays.push_back({ name, url, modifiers, key });
        OutputDebugStringW((L"Added overlay: " + name + L"\n").c_str());
    }

    void CreateOverlayWindows() {
        OutputDebugStringW(L"Starting CreateOverlayWindows\n");
        // Get user data folder (%APPDATA%\QooOverlay)
        wchar_t* appDataPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
            OutputDebugStringW(L"Failed to get AppData path\n");
            return;
        }
        std::wstring userDataFolder = std::wstring(appDataPath) + L"\\QooOverlay";
        CoTaskMemFree(appDataPath);
        CreateDirectoryW(userDataFolder.c_str(), nullptr);

        for (auto& overlay : overlays) {
            HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP,
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
                                    // Set bounds for 4K
                                    RECT bounds = { 0, 0, 3840, 2160 };
                                    controller->put_Bounds(bounds);
                                    controller->put_IsVisible(false); // Start hidden
                                    // Set transparent background
                                    Microsoft::WRL::ComPtr<ICoreWebView2Controller4> controller4;
                                    HRESULT hr = controller->QueryInterface(IID_PPV_ARGS(&controller4));
                                    if (SUCCEEDED(hr) && controller4) {
                                        COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 }; // ARGB: fully transparent
                                        controller4->put_DefaultBackgroundColor(transparent);
                                        OutputDebugStringW((L"Set transparent background for " + overlay.name + L"\n").c_str());
                                    }
                                    else {
                                        OutputDebugStringW((L"Failed to get ICoreWebView2Controller4 for " + overlay.name + L"\n").c_str());
                                    }
                                    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
                                    controller->get_CoreWebView2(&webview);
                                    if (webview) {
                                        HRESULT navResult = webview->Navigate(overlay.url.c_str());
                                        if (SUCCEEDED(navResult)) {
                                            OutputDebugStringW((L"Navigated to " + overlay.url + L"\n").c_str());
                                        }
                                        else {
                                            OutputDebugStringW((L"Failed to navigate to " + overlay.url + L": HRESULT " + std::to_wstring(navResult) + L"\n").c_str());
                                        }
                                    }
                                    // Set colorkey for transparency
                                    SetLayeredWindowAttributes(overlay.hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
                                    // Show window
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
            overlay.controller->put_IsVisible(overlay.visible);
            SetLayeredWindowAttributes(overlay.hwnd, RGB(0, 0, 0), overlay.visible ? 255 : 0, LWA_ALPHA);
            ShowWindow(overlay.hwnd, overlay.visible ? SW_SHOW : SW_HIDE);
            UpdateWindow(overlay.hwnd);
            OutputDebugStringW((L"Updated window visibility for " + overlay.name + L"\n").c_str());
        }
    }

    static HRESULT WINAPI DetouredPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        OutputDebugStringW(L"DetouredPresent called\n");
        if (!instance) return E_FAIL;
        if (!instance->TruePresent) {
            // Hook the swap chain's Present
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
        // Initialize without creating a device; hooking happens in DetouredPresent
        OutputDebugStringW(L"HookDirectX deferred to DetouredPresent\n");
    }

    void Run() {
        OutputDebugStringW(L"Starting Run\n");
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            OutputDebugStringW((L"CoInitializeEx failed: HRESULT " + std::to_wstring(hr) + L"\n").c_str());
            return;
        }
        OutputDebugStringW(L"COM initialized\n");
        instance = this;
        HookDirectX();
        CreateOverlayWindows();

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