// tests/test_gamma.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <math.h>
#include <vector>

// ============================================================================
// Структура приложения
// ============================================================================

struct App {
    HWND hwnd;
    IDirectDraw7* dd;
    IDirectDrawSurface7* primary;
    IDirectDrawSurface7* backbuffer;
    IDirectDrawGammaControl* gamma;
    bool running;
    int width;
    int height;
    float gammaValue;
    float brightness;
    float contrast;
    DWORD lastTime;
    int frameCount;
    bool gammaSupported;
};

App g_app = {0};

// ============================================================================
// Предварительные объявления
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CreateTestWindow(HINSTANCE hInstance);
bool InitDirectDraw();
void UpdateGamma();
void DrawTestPattern();
void RenderFrame();
void Cleanup();

// ============================================================================
// Window procedure
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            g_app.running = false;
            PostQuitMessage(0);
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_app.running = false;
            }
            // Управление гаммой стрелками
            if (wParam == VK_UP) {
                g_app.gammaValue += 0.1f;
                if (g_app.gammaValue > 3.0f) g_app.gammaValue = 3.0f;
                printf("Gamma: %.1f\n", g_app.gammaValue);
                if (g_app.gammaSupported) UpdateGamma();
            }
            if (wParam == VK_DOWN) {
                g_app.gammaValue -= 0.1f;
                if (g_app.gammaValue < 0.5f) g_app.gammaValue = 0.5f;
                printf("Gamma: %.1f\n", g_app.gammaValue);
                if (g_app.gammaSupported) UpdateGamma();
            }
            // Управление яркостью +/-
            if (wParam == VK_ADD || wParam == VK_OEM_PLUS) {
                g_app.brightness += 0.1f;
                if (g_app.brightness > 1.0f) g_app.brightness = 1.0f;
                printf("Brightness: %.1f\n", g_app.brightness);
                if (g_app.gammaSupported) UpdateGamma();
            }
            if (wParam == VK_SUBTRACT || wParam == VK_OEM_MINUS) {
                g_app.brightness -= 0.1f;
                if (g_app.brightness < -1.0f) g_app.brightness = -1.0f;
                printf("Brightness: %.1f\n", g_app.brightness);
                if (g_app.gammaSupported) UpdateGamma();
            }
            // Управление контрастом [ и ]
            if (wParam == VK_OEM_4) { // [
                g_app.contrast -= 0.1f;
                if (g_app.contrast < 0.0f) g_app.contrast = 0.0f;
                printf("Contrast: %.1f\n", g_app.contrast);
                if (g_app.gammaSupported) UpdateGamma();
            }
            if (wParam == VK_OEM_6) { // ]
                g_app.contrast += 0.1f;
                if (g_app.contrast > 2.0f) g_app.contrast = 2.0f;
                printf("Contrast: %.1f\n", g_app.contrast);
                if (g_app.gammaSupported) UpdateGamma();
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Создание окна
// ============================================================================

bool CreateTestWindow(HINSTANCE hInstance) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GammaTestWindow";
    
    if (!RegisterClassEx(&wc)) {
        printf("Failed to register window class\n");
        return false;
    }
    
    g_app.hwnd = CreateWindowEx(
        0, L"GammaTestWindow", L"DDVK - Gamma Control Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, NULL, NULL, hInstance, NULL);
    
    if (!g_app.hwnd) {
        printf("Failed to create window\n");
        return false;
    }
    
    ShowWindow(g_app.hwnd, SW_SHOW);
    UpdateWindow(g_app.hwnd);
    
    RECT rect;
    GetClientRect(g_app.hwnd, &rect);
    g_app.width = rect.right - rect.left;
    g_app.height = rect.bottom - rect.top;
    
    g_app.gammaValue = 1.0f;
    g_app.brightness = 0.0f;
    g_app.contrast = 1.0f;
    g_app.frameCount = 0;
    g_app.lastTime = GetTickCount();
    g_app.gammaSupported = false;
    g_app.gamma = nullptr;
    
    return true;
}

// ============================================================================
// Инициализация DirectDraw
// ============================================================================

bool InitDirectDraw() {
    HRESULT hr;
    
    printf("Step 1: Creating DirectDraw7... ");
    hr = DirectDrawCreateEx(NULL, (void**)&g_app.dd, IID_IDirectDraw7, NULL);
    if (FAILED(hr)) {
        printf("FAILED (0x%08lX)\n", hr);
        return false;
    }
    printf("OK\n");
    
    printf("Step 2: Setting cooperative level... ");
    hr = g_app.dd->SetCooperativeLevel(g_app.hwnd, DDSCL_NORMAL);
    if (FAILED(hr)) {
        printf("FAILED (0x%08lX)\n", hr);
        return false;
    }
    printf("OK\n");
    
    printf("Step 3: Creating primary surface... ");
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    
    hr = g_app.dd->CreateSurface(&desc, &g_app.primary, NULL);
    if (FAILED(hr)) {
        printf("FAILED (0x%08lX)\n", hr);
        return false;
    }
    printf("OK\n");
    
    printf("Step 4: Creating back buffer... ");
    desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    desc.dwWidth = g_app.width;
    desc.dwHeight = g_app.height;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    
    hr = g_app.dd->CreateSurface(&desc, &g_app.backbuffer, NULL);
    if (FAILED(hr)) {
        printf("FAILED (0x%08lX)\n", hr);
        return false;
    }
    printf("OK\n");
    
    printf("Step 5: Getting Gamma Control interface... ");
    hr = g_app.primary->QueryInterface(IID_IDirectDrawGammaControl, (void**)&g_app.gamma);
    if (FAILED(hr)) {
        printf("NOT SUPPORTED (0x%08lX)\n", hr);
        printf("  Continuing without gamma control\n");
        g_app.gamma = nullptr;
        g_app.gammaSupported = false;
    } else {
        printf("OK\n");
        g_app.gammaSupported = true;
        
        // Получаем текущую гамма-рампу
        DDGAMMARAMP ramp;
        hr = g_app.gamma->GetGammaRamp(0, &ramp);
        if (SUCCEEDED(hr)) {
            printf("  Gamma ramp acquired successfully\n");
        }
    }
    
    return true;
}

// ============================================================================
// Обновление гаммы
// ============================================================================

void UpdateGamma() {
    if (!g_app.gamma || !g_app.gammaSupported) return;
    
    DDGAMMARAMP ramp;
    
    // Создаём гамма-рамп с учётом gamma, brightness, contrast
    for (int i = 0; i < 256; i++) {
        float normalized = i / 255.0f;
        
        // Применяем контраст
        normalized = (normalized - 0.5f) * g_app.contrast + 0.5f;
        
        // Применяем яркость
        normalized += g_app.brightness;
        
        // Клиппинг
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        
        // Применяем гамму
        float corrected = powf(normalized, 1.0f / g_app.gammaValue);
        
        WORD value = (WORD)(corrected * 65535.0f);
        ramp.red[i] = value;
        ramp.green[i] = value;
        ramp.blue[i] = value;
    }
    
    HRESULT hr = g_app.gamma->SetGammaRamp(0, &ramp);
    if (FAILED(hr)) {
        printf("SetGammaRamp failed: 0x%08lX\n", hr);
    }
}

// ============================================================================
// Отрисовка тестового паттерна
// ============================================================================

void DrawTestPattern() {
    if (!g_app.backbuffer) return;
    
    HDC hdc;
    if (SUCCEEDED(g_app.backbuffer->GetDC(&hdc))) {
        // Рисуем градиент
        for (int y = 0; y < g_app.height; y++) {
            int red = (y * 255) / g_app.height;
            int green = 255 - red;
            int blue = (y * 128) / g_app.height;
            
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(red, green, blue));
            SelectObject(hdc, pen);
            MoveToEx(hdc, 0, y, NULL);
            LineTo(hdc, g_app.width, y);
            DeleteObject(pen);
        }
        
        // Рисуем цветные полосы
        HBRUSH colors[6] = {
            CreateSolidBrush(RGB(255, 0, 0)),
            CreateSolidBrush(RGB(0, 255, 0)),
            CreateSolidBrush(RGB(0, 0, 255)),
            CreateSolidBrush(RGB(255, 255, 0)),
            CreateSolidBrush(RGB(0, 255, 255)),
            CreateSolidBrush(RGB(255, 0, 255))
        };
        
        int barWidth = g_app.width / 6;
        for (int i = 0; i < 6; i++) {
            RECT rect = {i * barWidth, g_app.height/2,
                        (i + 1) * barWidth, g_app.height/2 + 50};
            FillRect(hdc, &rect, colors[i]);
            DeleteObject(colors[i]);
        }
        
        // Текст с информацией
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        
        char text[256];
        int y = 10;
        
        sprintf(text, "Gamma: %.2f (Up/Down)", g_app.gammaValue);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        sprintf(text, "Brightness: %.2f (+/-)", g_app.brightness);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        sprintf(text, "Contrast: %.2f ([ / ])", g_app.contrast);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        sprintf(text, "FPS: %d", g_app.frameCount);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        sprintf(text, "Gamma Control: %s", g_app.gammaSupported ? "YES" : "NO");
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        g_app.backbuffer->ReleaseDC(hdc);
    }
}

// ============================================================================
// Рендеринг кадра
// ============================================================================

void RenderFrame() {
    if (!g_app.primary || !g_app.backbuffer) return;
    
    // Рисуем тестовый паттерн
    DrawTestPattern();
    
    // Получаем размеры клиентской области
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    
    // Блит на primary поверхность
    HRESULT hr = g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    if (FAILED(hr)) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            printf("RenderFrame: Blt failed 0x%08lX\n", hr);
        }
    }
    
    // Считаем FPS
    g_app.frameCount++;
    DWORD currentTime = GetTickCount();
    if (currentTime - g_app.lastTime >= 1000) {
        printf("FPS: %d, Gamma: %.2f, Brightness: %.2f, Contrast: %.2f\n",
               g_app.frameCount, g_app.gammaValue, g_app.brightness, g_app.contrast);
        g_app.frameCount = 0;
        g_app.lastTime = currentTime;
    }
}

// ============================================================================
// Очистка
// ============================================================================

void Cleanup() {
    printf("\n=== Cleaning up ===\n");
    
    if (g_app.gamma) {
        // Сбрасываем гамма-рампу к исходной
        DDGAMMARAMP defaultRamp;
        for (int i = 0; i < 256; i++) {
            WORD value = (WORD)(i * 257);
            defaultRamp.red[i] = value;
            defaultRamp.green[i] = value;
            defaultRamp.blue[i] = value;
        }
        g_app.gamma->SetGammaRamp(0, &defaultRamp);
        
        g_app.gamma->Release();
        g_app.gamma = nullptr;
        printf("Released gamma control\n");
    }
    
    if (g_app.backbuffer) {
        g_app.backbuffer->Release();
        g_app.backbuffer = nullptr;
        printf("Released backbuffer\n");
    }
    
    if (g_app.primary) {
        g_app.primary->Release();
        g_app.primary = nullptr;
        printf("Released primary surface\n");
    }
    
    if (g_app.dd) {
        g_app.dd->Release();
        g_app.dd = nullptr;
        printf("Released DirectDraw object\n");
    }
}

// ============================================================================
// Главная функция
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    
    printf("========================================\n");
    printf("DDVK - Gamma Control Test\n");
    printf("========================================\n\n");
    printf("Controls:\n");
    printf("  Up/Down    - Change gamma (0.5 - 3.0)\n");
    printf("  +/-        - Change brightness (-1.0 - 1.0)\n");
    printf("  [ / ]      - Change contrast (0.0 - 2.0)\n");
    printf("  ESC        - Exit\n\n");
    
    if (!CreateTestWindow(hInstance)) {
        printf("Failed to create window\n");
        FreeConsole();
        return 1;
    }
    
    if (!InitDirectDraw()) {
        printf("Failed to initialize DirectDraw\n");
        Cleanup();
        FreeConsole();
        return 1;
    }
    
    g_app.running = true;
    
    printf("\n=== Entering main loop ===\n\n");
    
    MSG msg;
    while (g_app.running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_app.running = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        RenderFrame();
        Sleep(10);
    }
    
    Cleanup();
    printf("\nTest completed successfully.\n");
    
    FreeConsole();
    return 0;
}