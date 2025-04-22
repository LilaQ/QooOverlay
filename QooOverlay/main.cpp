#include <windows.h>
#include "overlay_manager.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    OverlayManager manager;
    manager.AddOverlay(L"Qoo PDF 1", L"file:///C:/Users/LilaQ/Desktop/Projects/QooOverlay/QooOverlay/assets/notification.html", MOD_CONTROL | MOD_SHIFT, 'P');
    manager.AddOverlay(L"Qoo PDF 2", L"file:///C:/Users/LilaQ/Desktop/Projects/QooOverlay/QooOverlay/assets/pdf_overlay2.html", MOD_CONTROL | MOD_SHIFT, 'O');
    manager.AddOverlay(L"Qoo PDF 3", L"file:///C:/Users/LilaQ/Desktop/Projects/QooOverlay/QooOverlay/assets/pdf_reader.html", MOD_CONTROL | MOD_SHIFT, 'I');
    manager.Run();
    return 0;
}