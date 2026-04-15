#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <functional>

namespace vk_window {

#ifdef _WIN32
    // Структура для оконных данных
    struct WindowData {
        HWND hwnd = nullptr;
        HINSTANCE hinstance = nullptr;
        int width = 800;
        int height = 600;
        bool framebufferResized = false;
        std::function<void(int, int)> onResize = nullptr;
    };

    // Создание окна
    bool createWindow(WindowData& data, const char* title);

    // Оконная процедура
    LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Обработка сообщений
    bool processMessages(WindowData& data);

    // Уничтожение окна
    void destroyWindow(WindowData& data);
#endif

} // namespace vk_window