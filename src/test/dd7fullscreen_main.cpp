#include <windows.h>
#include <ddraw.h>
#include <stdio.h>

// Simple fullscreen DirectDraw7 test used with the wrapper

static LPDIRECTDRAW7        g_pDD              = nullptr;
static LPDIRECTDRAWSURFACE7 g_pPrimarySurface  = nullptr;
static LPDIRECTDRAWSURFACE7 g_pBackSurface     = nullptr;
static BOOL                 g_bActive          = TRUE;
static HWND                 g_hWnd             = nullptr;
static HINSTANCE            g_hInst            = nullptr;

static DWORD g_dwScreenWidth  = 800;
static DWORD g_dwScreenHeight = 600;
static DWORD g_dwScreenBPP    = 32;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HRESULT InitDirectDraw(HWND hwnd);
static HRESULT CreateSurfaces();
static void    DrawTestPattern(LPDIRECTDRAWSURFACE7 pSurface, DWORD color);
static void    CleanupDirectDraw();

static HRESULT InitDirectDraw(HWND hwnd) {
    HRESULT hr = DirectDrawCreateEx(nullptr, (void**)&g_pDD, IID_IDirectDraw7, nullptr);
    if (FAILED(hr)) return hr;

    hr = IDirectDraw7_SetCooperativeLevel(g_pDD, hwnd,
        DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT);
    if (FAILED(hr)) return hr;

    hr = IDirectDraw7_SetDisplayMode(g_pDD, g_dwScreenWidth, g_dwScreenHeight,
                                     g_dwScreenBPP, 0, 0);
    if (FAILED(hr)) return hr;

    return CreateSurfaces();
}

static HRESULT CreateSurfaces() {
    if (!g_pDD) return DDERR_INVALIDOBJECT;

    DDSURFACEDESC2 ddsd = {};
    ddsd.dwSize  = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE |
                          DDSCAPS_FLIP |
                          DDSCAPS_COMPLEX;
    ddsd.dwBackBufferCount = 1;

    LPDIRECTDRAWSURFACE7 primary = nullptr;
    HRESULT hr = IDirectDraw7_CreateSurface(g_pDD, &ddsd, &primary, nullptr);
    if (FAILED(hr)) return hr;

    g_pPrimarySurface = primary;

    DDSCAPS2 caps = {};
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    hr = IDirectDrawSurface7_GetAttachedSurface(g_pPrimarySurface, &caps, &g_pBackSurface);
    return hr;
}

static void DrawTestPattern(LPDIRECTDRAWSURFACE7 pSurface, DWORD color) {
    if (!pSurface) return;

    DDBLTFX fx = {};
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;

    RECT rc;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = (LONG)g_dwScreenWidth;
    rc.bottom = (LONG)g_dwScreenHeight;

    IDirectDrawSurface7_Blt(pSurface, &rc, nullptr, nullptr,
                            DDBLT_COLORFILL | DDBLT_WAIT, &fx);
}

static void CleanupDirectDraw() {
    if (g_pBackSurface) {
        IDirectDrawSurface7_Release(g_pBackSurface);
        g_pBackSurface = nullptr;
    }
    if (g_pPrimarySurface) {
        IDirectDrawSurface7_Release(g_pPrimarySurface);
        g_pPrimarySurface = nullptr;
    }
    if (g_pDD) {
        IDirectDraw7_Release(g_pDD);
        g_pDD = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATEAPP:
        g_bActive = (wParam != 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    g_hInst = hInstance;

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DDVKFullscreenMain";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"RegisterClassEx failed", L"Error", MB_OK);
        return 0;
    }

    HWND hwnd = CreateWindowEx(
        0,
        L"DDVKFullscreenMain",
        L"DDVK Fullscreen Main",
        WS_POPUP,
        0, 0,
        (int)g_dwScreenWidth, (int)g_dwScreenHeight,
        nullptr, nullptr,
        hInstance, nullptr);

    if (!hwnd) {
        MessageBox(nullptr, L"CreateWindowEx failed", L"Error", MB_OK);
        return 0;
    }

    g_hWnd = hwnd;

    HRESULT hr = InitDirectDraw(hwnd);
    if (FAILED(hr)) {
        wchar_t buf[256];
        swprintf(buf, 256, L"InitDirectDraw failed: 0x%08lx", hr);
        MessageBox(nullptr, buf, L"Error", MB_OK);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double targetDelta = 1.0 / 60.0;

    LARGE_INTEGER lastCounter;
    QueryPerformanceCounter(&lastCounter);

    BOOL running = TRUE;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = FALSE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        if (g_bActive && g_pPrimarySurface && g_pBackSurface) {
            DrawTestPattern(g_pBackSurface, RGB(0, 0, 128));
            IDirectDrawSurface7_Flip(g_pPrimarySurface, nullptr, DDFLIP_WAIT);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double delta = double(now.QuadPart - lastCounter.QuadPart) /
                       double(freq.QuadPart);
        if (delta < targetDelta) {
            DWORD sleepMs = (DWORD)((targetDelta - delta) * 1000.0);
            if (sleepMs > 0) Sleep(sleepMs);
        }
        QueryPerformanceCounter(&lastCounter);
    }

    CleanupDirectDraw();
    return 0;
}
