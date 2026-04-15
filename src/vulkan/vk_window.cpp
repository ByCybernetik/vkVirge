#include "vk_window.h"
#include <stdexcept>

namespace vk_window {

#ifdef _WIN32
    bool createWindow(WindowData& data, const char* title) {
        data.hinstance = GetModuleHandle(nullptr);
        
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = data.hinstance;
        wc.lpszClassName = "VulkanWindowClass";
        
        if (!RegisterClassExA(&wc)) {
            return false;
        }
        
        RECT rect = {0, 0, data.width, data.height};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        
        data.hwnd = CreateWindowExA(
            0,
            "VulkanWindowClass",
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            data.hinstance,
            &data
        );
        
        if (!data.hwnd) {
            return false;
        }
        
        ShowWindow(data.hwnd, SW_SHOW);
        UpdateWindow(data.hwnd);
        
        return true;
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        WindowData* data = nullptr;
        
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCTA* pCreate = reinterpret_cast<CREATESTRUCTA*>(lParam);
            data = reinterpret_cast<WindowData*>(pCreate->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        } else {
            data = reinterpret_cast<WindowData*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        }
        
        if (data) {
            switch (uMsg) {
                case WM_SIZE:
                    if (wParam != SIZE_MINIMIZED) {
                        data->framebufferResized = true;
                        data->width = LOWORD(lParam);
                        data->height = HIWORD(lParam);
                        if (data->onResize) {
                            data->onResize(data->width, data->height);
                        }
                    }
                    return 0;
                    
                case WM_CLOSE:
                    DestroyWindow(hwnd);
                    return 0;
                    
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
        }
        
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    bool processMessages(WindowData& data) {
        MSG msg = {};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }

    void destroyWindow(WindowData& data) {
        if (data.hwnd) {
            DestroyWindow(data.hwnd);
            data.hwnd = nullptr;
        }
    }
#endif

} // namespace vk_window