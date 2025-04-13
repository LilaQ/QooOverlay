#pragma once
#include <string>
#include <memory>

class OverlayManager {
private:
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    OverlayManager();
    ~OverlayManager();
    void AddOverlay(const std::wstring& name, const std::wstring& htmlPath, UINT modifiers, UINT key);
    void Run();
};