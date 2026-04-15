#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>

// IID_IDirectDraw7 уже определён в нашем враппере (ddraw.cpp)

// ============================================================================
// Макросы (как в ddraw7_surface.cpp)
// ============================================================================
#ifndef MY_MIN
#define MY_MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MY_MAX
#define MY_MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

// ============================================================================
// Константы
// ============================================================================
#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE  L"DirectDraw Triangle - DDVK Test"

// ============================================================================
// Глобальные переменные
// ============================================================================
LPDIRECTDRAW7        g_pDD = NULL;
LPDIRECTDRAWSURFACE7 g_pPrimarySurface = NULL;
LPDIRECTDRAWSURFACE7 g_pBackBuffer = NULL;
LPDIRECTDRAWCLIPPER  g_pClipper = NULL;
HWND                 g_hWnd = NULL;
BOOL                 g_bRunning = TRUE;

// ============================================================================
// Структура вершины треугольника
// ============================================================================
typedef struct {
    float x, y;      // Позиция
    DWORD color;     // Цвет (0xAARRGGBB)
} Vertex;

// ============================================================================
// Инициализация DirectDraw
// ============================================================================
HRESULT InitDDraw(HWND hWnd)
{
    HRESULT hr;
    DDSURFACEDESC2 ddsd;

    // Создаем IDirectDraw7 напрямую
    hr = DirectDrawCreateEx(NULL, (void**)&g_pDD, IID_IDirectDraw7, NULL);
    if(FAILED(hr))
        return hr;

    // Устанавливаем cooperative level (windowed mode)
    hr = g_pDD->SetCooperativeLevel(hWnd, DDSCL_NORMAL);
    if(FAILED(hr))
        return hr;

    // Создаем первичную поверхность
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hr = g_pDD->CreateSurface(&ddsd, &g_pPrimarySurface, NULL);
    if(FAILED(hr))
        return hr;

    // Создаем задний буфер
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    ddsd.dwWidth = WINDOW_WIDTH;
    ddsd.dwHeight = WINDOW_HEIGHT;

    hr = g_pDD->CreateSurface(&ddsd, &g_pBackBuffer, NULL);
    if(FAILED(hr))
        return hr;

    // Создаем клиппер
    hr = g_pDD->CreateClipper(0, &g_pClipper, NULL);
    if(FAILED(hr))
        return hr;

    hr = g_pClipper->SetHWnd(0, hWnd);
    if(FAILED(hr))
        return hr;

    hr = g_pPrimarySurface->SetClipper(g_pClipper);
    if(FAILED(hr))
        return hr;

    return S_OK;
}

// ============================================================================
// Очистка ресурсов DirectDraw
// ============================================================================
void CleanupDDraw()
{
    if(g_pClipper) {
        g_pClipper->Release();
        g_pClipper = NULL;
    }
    if(g_pBackBuffer) {
        g_pBackBuffer->Release();
        g_pBackBuffer = NULL;
    }
    if(g_pPrimarySurface) {
        g_pPrimarySurface->Release();
        g_pPrimarySurface = NULL;
    }
    if(g_pDD) {
        g_pDD->RestoreDisplayMode();
        g_pDD->Release();
        g_pDD = NULL;
    }
}

// ============================================================================
// Рисование линии (алгоритм Брезенхема)
// ============================================================================
void DrawLine(LPDIRECTDRAWSURFACE7 pSurface, int x1, int y1, int x2, int y2, DWORD color)
{
    DDSURFACEDESC2 ddsd;
    HRESULT hr;

    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = pSurface->Lock(NULL, &ddsd, DDLOCK_WAIT, NULL);
    if(FAILED(hr))
        return;

    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while(TRUE) {
        if(x1 >= 0 && x1 < WINDOW_WIDTH && y1 >= 0 && y1 < WINDOW_HEIGHT) {
            DWORD* pixel = (DWORD*)((BYTE*)ddsd.lpSurface + y1 * ddsd.lPitch) + x1;
            *pixel = color;
        }

        if(x1 == x2 && y1 == y2)
            break;

        int e2 = 2 * err;
        if(e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if(e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }

    pSurface->Unlock(NULL);
}

// ============================================================================
// Заливка треугольника (scanline algorithm)
// ============================================================================
void FillTriangle(LPDIRECTDRAWSURFACE7 pSurface, Vertex v1, Vertex v2, Vertex v3, DWORD color)
{
    DDSURFACEDESC2 ddsd;
    HRESULT hr;

    // Сортируем вершины по Y
    Vertex temp;
    if(v1.y > v2.y) { temp = v1; v1 = v2; v2 = temp; }
    if(v1.y > v3.y) { temp = v1; v1 = v3; v3 = temp; }
    if(v2.y > v3.y) { temp = v2; v2 = v3; v3 = temp; }

    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = pSurface->Lock(NULL, &ddsd, DDLOCK_WAIT, NULL);
    if(FAILED(hr))
        return;

    float dy1 = v2.y - v1.y;
    float dy2 = v3.y - v2.y;
    float dy3 = v3.y - v1.y;

    for(int y = (int)v1.y; y <= (int)v3.y && y < WINDOW_HEIGHT; y++) {
        if(y < 0) continue;

        float left, right;

        if(y < v2.y && dy1 != 0) {
            float t = (y - v1.y) / dy1;
            float x1_start = v1.x + t * (v2.x - v1.x);
            float x1_end = v1.x + t * (v3.x - v1.x);
            left = (x1_start < x1_end) ? x1_start : x1_end;
            right = (x1_start > x1_end) ? x1_start : x1_end;
        } else if(dy2 != 0) {
            float t = (y - v2.y) / dy2;
            float x2_start = v2.x + t * (v3.x - v2.x);
            float x2_end = v1.x + ((y - v1.y) / dy3) * (v3.x - v1.x);
            left = (x2_start < x2_end) ? x2_start : x2_end;
            right = (x2_start > x2_end) ? x2_start : x2_end;
        } else {
            continue;
        }

        // ✅ ИСПРАВЛЕНО: Используем MY_MIN/MY_MAX вместо min/max
        int startX = (int)MY_MAX(0.0f, left);
        int endX = (int)MY_MIN((float)(WINDOW_WIDTH - 1), right);

        DWORD* line = (DWORD*)((BYTE*)ddsd.lpSurface + y * ddsd.lPitch);
        for(int x = startX; x <= endX; x++) {
            line[x] = color;
        }
    }

    pSurface->Unlock(NULL);
}

// ============================================================================
// Рисование треугольника
// ============================================================================
void DrawTriangle(LPDIRECTDRAWSURFACE7 pSurface, Vertex v1, Vertex v2, Vertex v3,
                  DWORD fillColor, DWORD lineColor)
{
    FillTriangle(pSurface, v1, v2, v3, fillColor);
    DrawLine(pSurface, (int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, lineColor);
    DrawLine(pSurface, (int)v2.x, (int)v2.y, (int)v3.x, (int)v3.y, lineColor);
    DrawLine(pSurface, (int)v3.x, (int)v3.y, (int)v1.x, (int)v1.y, lineColor);
}

// ============================================================================
// Очистка поверхности
// ============================================================================
void ClearSurface(LPDIRECTDRAWSURFACE7 pSurface, DWORD color)
{
    DDSURFACEDESC2 ddsd;
    HRESULT hr;

    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = pSurface->Lock(NULL, &ddsd, DDLOCK_WAIT, NULL);
    if(FAILED(hr))
        return;

    for(DWORD y = 0; y < WINDOW_HEIGHT; y++) {
        DWORD* line = (DWORD*)((BYTE*)ddsd.lpSurface + y * ddsd.lPitch);
        for(DWORD x = 0; x < WINDOW_WIDTH; x++) {
            line[x] = color;
        }
    }

    pSurface->Unlock(NULL);
}

// ============================================================================
// Рендеринг
// ============================================================================
void Render()
{
    HRESULT hr;
    RECT rect;
    POINT point;

    // Очищаем задний буфер (темно-синий)
    ClearSurface(g_pBackBuffer, 0xFF000080);

    // Анимация
    static float angle = 0.0f;
    angle += 0.02f;

    Vertex v1, v2, v3;
    float centerX = WINDOW_WIDTH / 2.0f;
    float centerY = WINDOW_HEIGHT / 2.0f;
    float radius = 150.0f;

    v1.x = centerX + radius * cosf(angle);
    v1.y = centerY + radius * sinf(angle);
    v1.color = 0xFFFF0000;

    v2.x = centerX + radius * cosf(angle + 2.094f);
    v2.y = centerY + radius * sinf(angle + 2.094f);
    v2.color = 0xFF00FF00;

    v3.x = centerX + radius * cosf(angle + 4.188f);
    v3.y = centerY + radius * sinf(angle + 4.188f);
    v3.color = 0xFF0000FF;

    // Рисуем треугольник (желтая заливка, белая обводка)
    DrawTriangle(g_pBackBuffer, v1, v2, v3, 0xFFFFFF00, 0xFFFFFFFF);

    // Получаем позицию окна для блита
    point.x = 0;
    point.y = 0;
    ClientToScreen(g_hWnd, &point);
    GetClientRect(g_hWnd, &rect);
    OffsetRect(&rect, point.x, point.y);

    // Блит на первичную поверхность
    hr = g_pPrimarySurface->Blt(&rect, g_pBackBuffer, NULL, DDBLT_WAIT, NULL);
    if(FAILED(hr)) {
        // Если не удалось, пробуем без rect
        hr = g_pPrimarySurface->Blt(NULL, g_pBackBuffer, NULL, DDBLT_WAIT, NULL);
    }
}

// ============================================================================
// Оконная процедура
// ============================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message) {
        case WM_KEYDOWN:
            if(wParam == VK_ESCAPE) {
                g_bRunning = FALSE;
                PostQuitMessage(0);
            }
            break;
        case WM_CLOSE:
        case WM_DESTROY:
            g_bRunning = FALSE;
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ============================================================================
// Главная функция
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wc = {0};
    MSG msg;
    HRESULT hr;

    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // Регистрируем класс окна
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DDrawTriangleClass";

    if(!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Failed to register window class!", L"Error", MB_OK);
        return 1;
    }

    // Создаем окно
    g_hWnd = CreateWindowEx(
        0,
        L"DDrawTriangleClass",
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if(!g_hWnd) {
        MessageBox(NULL, L"Failed to create window!", L"Error", MB_OK);
        return 1;
    }

    // Инициализируем DirectDraw
    hr = InitDDraw(g_hWnd);
    if(FAILED(hr)) {
        wchar_t msgBuf[256];
        swprintf(msgBuf, 256, L"Failed to initialize DirectDraw!\nHRESULT: 0x%08lX", hr);
        MessageBox(NULL, msgBuf, L"Error", MB_OK);
        CleanupDDraw();
        return 1;
    }

    // Главный цикл
    while(g_bRunning) {
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Render();
        Sleep(10);  // ~100 FPS
    }

    CleanupDDraw();
    DestroyWindow(g_hWnd);
    UnregisterClass(L"DDrawTriangleClass", hInstance);

    return 0;
}
