#include <windows.h>
#include <ddraw.h>
#include <stdio.h>

// Глобальные переменные
LPDIRECTDRAW7 lpDD = NULL;
LPDIRECTDRAWSURFACE7 lpPrimarySurface = NULL;
LPDIRECTDRAWSURFACE7 lpBackSurface = NULL;
BOOL isActive = TRUE;
BOOL isFullScreen = FALSE;

// Прототипы функций
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CleanupDirectDraw(void);
void DrawTestPattern(void);

// Точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HWND hwnd;
    MSG msg;
    WNDCLASSEX wc = {0};
    HRESULT hr;

    // Создаем окно
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"DDrawTestClass";

    if(!RegisterClassEx(&wc))
        return 0;

    hwnd = CreateWindowEx(
        0,
        L"DDrawTestClass",
        L"DirectDraw Fullscreen Test",
        WS_POPUP | WS_VISIBLE,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL);

    if(!hwnd)
        return 0;

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    // Инициализируем DirectDraw
    hr = DirectDrawCreateEx(NULL, (void**)&lpDD, IID_IDirectDraw7, NULL);
    if(FAILED(hr))
    {
        wchar_t msgBuf[256];
        swprintf(msgBuf, 256, L"Failed to create DirectDraw object. Error code: 0x%08lX", hr);
        MessageBox(hwnd, msgBuf, L"Error", MB_OK);
        return 0;
    }

    // Устанавливаем кооперативный уровень для полноэкранного режима
    hr = IDirectDraw7_SetCooperativeLevel(lpDD, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if(FAILED(hr))
    {
        MessageBox(hwnd, L"Failed to set cooperative level", L"Error", MB_OK);
        CleanupDirectDraw();
        return 0;
    }

    // Устанавливаем режим дисплея
    // Пробуем разные разрешения
    hr = IDirectDraw7_SetDisplayMode(lpDD, 1024, 768, 32, 0, 0);
    if(FAILED(hr))
    {
        hr = IDirectDraw7_SetDisplayMode(lpDD, 800, 600, 32, 0, 0);
        if(FAILED(hr))
        {
            hr = IDirectDraw7_SetDisplayMode(lpDD, 640, 480, 32, 0, 0);
            if(FAILED(hr))
            {
                hr = IDirectDraw7_SetDisplayMode(lpDD, 640, 480, 16, 0, 0);
                if(FAILED(hr))
                {
                    MessageBox(hwnd, L"Failed to set any display mode", L"Error", MB_OK);
                    CleanupDirectDraw();
                    return 0;
                }
            }
        }
    }

    isFullScreen = TRUE;

    // Получаем текущий режим дисплея для определения размера поверхности
    DDSURFACEDESC2 ddsd = {0};
    ddsd.dwSize = sizeof(ddsd);
    hr = IDirectDraw7_GetDisplayMode(lpDD, &ddsd);

    DWORD width = 640, height = 480;
    if(SUCCEEDED(hr))
    {
        width = ddsd.dwWidth;
        height = ddsd.dwHeight;
    }

    // Создаем первичную поверхность с back buffer'ом
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    ddsd.dwBackBufferCount = 1;

    hr = IDirectDraw7_CreateSurface(lpDD, &ddsd, &lpPrimarySurface, NULL);
    if(FAILED(hr))
    {
        // Пробуем без флипа (простой primary surface)
        ZeroMemory(&ddsd, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

        hr = IDirectDraw7_CreateSurface(lpDD, &ddsd, &lpPrimarySurface, NULL);
        if(FAILED(hr))
        {
                MessageBox(hwnd, L"Failed to create primary surface", L"Error", MB_OK);
            CleanupDirectDraw();
            return 0;
        }
    }
    else
    {
        // Получаем указатель на back buffer если он есть
        DDSCAPS2 ddscaps = {0};
        ddscaps.dwCaps = DDSCAPS_BACKBUFFER;
        hr = IDirectDrawSurface7_GetAttachedSurface(lpPrimarySurface, &ddscaps, &lpBackSurface);
        if(FAILED(hr))
        {
            lpBackSurface = NULL;
        }
    }

    // Главный цикл сообщений
    while(GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Рисуем тестовый паттерн
        if(isActive)
        {
            if(lpBackSurface)
            {
                DrawTestPattern();

                // Переключаем страницы (flip)
                hr = IDirectDrawSurface7_Flip(lpPrimarySurface, NULL, DDFLIP_WAIT);
                if(FAILED(hr))
                {
                    // Если флип не удался, игнорируем
                }
            }
            else if(lpPrimarySurface)
            {
                // Рисуем прямо на primary surface
                DDSURFACEDESC2 ddsd = {0};
                ddsd.dwSize = sizeof(ddsd);

                hr = IDirectDrawSurface7_Lock(lpPrimarySurface, NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);
                if(SUCCEEDED(hr))
                {
                    // Рисуем тестовый паттерн
                    BYTE* buffer = (BYTE*)ddsd.lpSurface;
                    int pitch = ddsd.lPitch;

                    for(UINT y = 0; y < height; y++)
                    {
                        for(UINT x = 0; x < width; x++)
                        {
                            // Простой цветной паттерн
                            buffer[y * pitch + x * 4 + 0] = (BYTE)(x * 255 / width);     // B
                            buffer[y * pitch + x * 4 + 1] = (BYTE)(y * 255 / height);    // G
                            buffer[y * pitch + x * 4 + 2] = (BYTE)(255 - x * 255 / width); // R
                            buffer[y * pitch + x * 4 + 3] = 0;                            // A
                        }
                    }

                    IDirectDrawSurface7_Unlock(lpPrimarySurface, NULL);
                }
            }
        }
    }

    CleanupDirectDraw();
    return msg.wParam;
}

// Оконная процедура
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_ACTIVATEAPP:
            isActive = (BOOL)wParam;
            break;

        case WM_KEYDOWN:
            if(wParam == VK_ESCAPE)
            {
                DestroyWindow(hwnd);
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Рисование тестового паттерна
void DrawTestPattern(void)
{
    if(!lpBackSurface) return;

    DDSURFACEDESC2 ddsd = {0};
    ddsd.dwSize = sizeof(ddsd);
    HRESULT hr;

    // Получаем размеры поверхности
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    hr = IDirectDrawSurface7_GetSurfaceDesc(lpBackSurface, &ddsd);

    DWORD width = ddsd.dwWidth;
    DWORD height = ddsd.dwHeight;

    // Блокируем back buffer для рисования
    hr = IDirectDrawSurface7_Lock(lpBackSurface, NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);
    if(SUCCEEDED(hr))
    {
        // Рисуем тестовый паттерн
        DWORD* buffer = (DWORD*)ddsd.lpSurface;
        int pitch = ddsd.lPitch / sizeof(DWORD);

        for(DWORD y = 0; y < height; y++)
        {
            for(DWORD x = 0; x < width; x++)
            {
                // Создаем градиент
                BYTE r = (BYTE)((x * 255) / width);      // Красный градиент по X
                BYTE g = (BYTE)((y * 255) / height);     // Зеленый градиент по Y
                BYTE b = (BYTE)(255 - (x * 255) / width); // Синий обратный градиент

                // Рисуем рамку
                if(x < 10 || x > width-10 || y < 10 || y > height-10)
                {
                    buffer[y * pitch + x] = 0xFFFFFFFF; // Белая рамка
                }
                else
                {
                    buffer[y * pitch + x] = (r << 16) | (g << 8) | b;
                }
            }
        }

        // Разблокируем поверхность
        IDirectDrawSurface7_Unlock(lpBackSurface, NULL);

        // Рисуем текст с помощью GDI
        HDC hdc;
        if(SUCCEEDED(IDirectDrawSurface7_GetDC(lpBackSurface, &hdc)))
        {
            SetBkMode(hdc, TRANSPARENT);

            // Большой заголовок
            HFONT hFont = CreateFontA(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Arial");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            SetTextColor(hdc, RGB(255, 255, 0));
            TextOutA(hdc, width/2 - 200, height/2 - 100, "DIRECTDRAW TEST", 15);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);

            // Инструкция
            hFont = CreateFontA(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = (HFONT)SelectObject(hdc, hFont);

            SetTextColor(hdc, RGB(255, 255, 255));
            TextOutA(hdc, width/2 - 100, height/2, "Press ESC to exit", 17);

            // Информация о разрешении
            char resText[64];
            sprintf(resText, "Resolution: %ld x %ld", width, height);
            TextOutA(hdc, width/2 - 100, height/2 + 40, resText, strlen(resText));

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);

            IDirectDrawSurface7_ReleaseDC(lpBackSurface, hdc);
        }
    }
}

// Очистка DirectDraw
void CleanupDirectDraw(void)
{
    if(lpBackSurface)
    {
        IDirectDrawSurface7_Release(lpBackSurface);
        lpBackSurface = NULL;
    }

    if(lpPrimarySurface)
    {
        IDirectDrawSurface7_Release(lpPrimarySurface);
        lpPrimarySurface = NULL;
    }

    if(lpDD)
    {
        if(isFullScreen)
        {
            IDirectDraw7_RestoreDisplayMode(lpDD);
            IDirectDraw7_SetCooperativeLevel(lpDD, NULL, DDSCL_NORMAL);
        }
        IDirectDraw7_Release(lpDD);
        lpDD = NULL;
    }
}
