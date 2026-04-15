// tests/test_blt_batch.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ============================================================================
// Структуры
// ============================================================================

struct TestResult {
    const char* name;
    DWORD time;
    HRESULT result;
    int operations;
    float opsPerSecond;
    
    TestResult() : name(nullptr), time(0), result(DD_OK), operations(0), opsPerSecond(0) {}
};

struct App {
    HWND hwnd;
    IDirectDraw7* dd;
    IDirectDrawSurface7* primary;
    IDirectDrawSurface7* backbuffer;
    IDirectDrawSurface7* offscreen[8];
    IDirectDrawClipper* clipper;
    bool running;
    int width;
    int height;
    std::vector<TestResult> results;
    int currentTest;
    bool testRunning;
    DWORD testStartTime;
    DWORD frameCount;
    DWORD lastFpsTime;
    int fps;
    
    // Диагностические флаги
    bool primaryInitialized;
    bool backbufferInitialized;
    bool blitWorking;
};

App g_app = {0};

// ============================================================================
// Предварительные объявления
// ============================================================================

void FillTestPattern(IDirectDrawSurface7* surface, int index);
void TestSimpleBatch();
void TestComplexBatch();
void TestMixedBatch();
void TestColorFillBatch();
void TestStretchBatch();
void TestKeyedBatch();
void TestErrorBatch();
void TestPerformanceComparison();
void DrawTestPattern(HDC hdc, int x, int y, int size);
void CheckAndRestoreSurfaces();
void TestSimpleColorFill();
void AddResult(const char* name, DWORD time, HRESULT result, int ops);
void DrawUI(HDC hdc);
void RenderFrame();
void Cleanup();
bool CreateTestWindow(HINSTANCE hInstance);
bool InitDirectDraw();
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// Window procedure
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            g_app.running = false;
            PostQuitMessage(0);
            return 0;
            
        case WM_ACTIVATEAPP:
            CheckAndRestoreSurfaces();
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (!g_app.primaryInitialized) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
                FillRect(hdc, &rect, brush);
                DeleteObject(brush);
                
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                TextOutA(hdc, 10, 10, "DirectDraw not initialized! ", 28);
                TextOutA(hdc, 10, 30, "Check console for details ", 25);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_app.running = false;
            }
            if (wParam >= '1' && wParam <= '9') {
                g_app.currentTest = wParam - '1';
                g_app.testRunning = true;
                g_app.testStartTime = GetTickCount();
                
                printf("\n=== Starting Test %d ===\n", g_app.currentTest + 1);
                
                switch (g_app.currentTest) {
                    case 0: TestSimpleBatch(); break;
                    case 1: TestComplexBatch(); break;
                    case 2: TestMixedBatch(); break;
                    case 3: TestColorFillBatch(); break;
                    case 4: TestStretchBatch(); break;
                    case 5: TestKeyedBatch(); break;
                    case 6: TestErrorBatch(); break;
                    case 7: TestPerformanceComparison(); break;
                    default: break;
                }
            }
            if (wParam == 'C' || wParam == 'c') {
                g_app.results.clear();
                printf("\n=== Results cleared ===\n");
            }
            if (wParam == 'T' || wParam == 't') {
                printf("\n=== Running simple color fill test ===\n");
                TestSimpleColorFill();
            }
            if (wParam == VK_SPACE && g_app.currentTest >= 0) {
                g_app.testRunning = true;
                g_app.testStartTime = GetTickCount();
                
                printf("\n=== Repeating Test %d ===\n", g_app.currentTest + 1);
                
                switch (g_app.currentTest) {
                    case 0: TestSimpleBatch(); break;
                    case 1: TestComplexBatch(); break;
                    case 2: TestMixedBatch(); break;
                    case 3: TestColorFillBatch(); break;
                    case 4: TestStretchBatch(); break;
                    case 5: TestKeyedBatch(); break;
                    case 6: TestErrorBatch(); break;
                    case 7: TestPerformanceComparison(); break;
                    default: break;
                }
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
    wc.lpszClassName = L"BltBatchTestWindow";
    
    if (!RegisterClassEx(&wc)) {
        printf("Failed to register window class\n");
        return false;
    }
    
    g_app.hwnd = CreateWindowEx(
        0, L"BltBatchTestWindow", L"DDVK - BltBatch Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768, NULL, NULL, hInstance, NULL);
    
    if (!g_app.hwnd) {
        printf("Failed to create window\n");
        return false;
    }
    
    ShowWindow(g_app.hwnd, SW_SHOW);
    UpdateWindow(g_app.hwnd);
    
    if (!IsWindowVisible(g_app.hwnd)) {
        printf("WARNING: Window is not visible!\n");
    }
    
    SetForegroundWindow(g_app.hwnd);
    SetFocus(g_app.hwnd);
    
    RECT rect;
    GetClientRect(g_app.hwnd, &rect);
    g_app.width = rect.right - rect.left;
    g_app.height = rect.bottom - rect.top;
    
    printf("Window created: %dx%d\n", g_app.width, g_app.height);
    
    g_app.currentTest = -1;
    g_app.testRunning = false;
    g_app.frameCount = 0;
    g_app.lastFpsTime = GetTickCount();
    g_app.fps = 0;
    g_app.primaryInitialized = false;
    g_app.backbufferInitialized = false;
    g_app.blitWorking = false;
    g_app.clipper = nullptr;
    
    return true;
}

// ============================================================================
// Заполнение тестового паттерна
// ============================================================================

void FillTestPattern(IDirectDrawSurface7* surface, int index) {
    if (!surface) {
        printf("FillTestPattern: surface is NULL!\n");
        return;
    }
    
    HDC hdc;
    HRESULT hr = surface->GetDC(&hdc);
    if (FAILED(hr)) {
        printf("FillTestPattern: GetDC failed with error 0x%08lX\n", hr);
        return;
    }
    
    RECT rect = {0, 0, 64, 64};
    
    COLORREF colors[] = {
        RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 0),
        RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 128, 0), RGB(128, 0, 255)
    };
    
    HBRUSH brush = CreateSolidBrush(colors[index % 8]);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    char text[16];
    sprintf(text, "%d", index);
    TextOutA(hdc, 28, 28, text, strlen(text));
    
    surface->ReleaseDC(hdc);
    printf("FillTestPattern: surface %d filled\n", index);
}

// ============================================================================
// Проверка и восстановление поверхностей
// ============================================================================

void CheckAndRestoreSurfaces() {
    HRESULT hr;
    
    if (g_app.primary) {
        hr = g_app.primary->IsLost();
        if (hr == DDERR_SURFACELOST) {
            printf("Primary surface lost, restoring...\n");
            g_app.primary->Restore();
        }
    }
    
    if (g_app.backbuffer) {
        hr = g_app.backbuffer->IsLost();
        if (hr == DDERR_SURFACELOST) {
            printf("Backbuffer lost, restoring...\n");
            g_app.backbuffer->Restore();
            
            HDC hdc;
            if (SUCCEEDED(g_app.backbuffer->GetDC(&hdc))) {
                RECT rect = {0, 0, g_app.width, g_app.height};
                HBRUSH brush = CreateSolidBrush(RGB(0, 100, 0));
                FillRect(hdc, &rect, brush);
                DeleteObject(brush);
                g_app.backbuffer->ReleaseDC(hdc);
            }
        }
    }
    
    for (int i = 0; i < 8; i++) {
        if (g_app.offscreen[i]) {
            hr = g_app.offscreen[i]->IsLost();
            if (hr == DDERR_SURFACELOST) {
                printf("Offscreen surface %d lost, restoring...\n", i);
                g_app.offscreen[i]->Restore();
                FillTestPattern(g_app.offscreen[i], i);
            }
        }
    }
}

// ============================================================================
// Инициализация DirectDraw
// ============================================================================

bool InitDirectDraw() {
    HRESULT hr;
    printf("\n=== Initializing DirectDraw ===\n");
    
    printf("Step 1: Creating DirectDraw object... ");
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
    
    printf("Step 3: Getting driver capabilities... ");
    DDCAPS driverCaps, helCaps;
    ZeroMemory(&driverCaps, sizeof(driverCaps));
    ZeroMemory(&helCaps, sizeof(helCaps));
    driverCaps.dwSize = sizeof(driverCaps);
    helCaps.dwSize = sizeof(helCaps);
    
    hr = g_app.dd->GetCaps(&driverCaps, &helCaps);
    if (SUCCEEDED(hr)) {
        printf("OK\n");
        printf("  Driver: %s\n", (driverCaps.dwCaps & DDCAPS_3D) ? "3D capable" : "2D only");
        printf("  Video memory: %lu KB total\n", driverCaps.dwVidMemTotal / 1024);
    } else {
        printf("FAILED (0x%08lX) - continuing anyway\n", hr);
    }
    
    printf("Step 4: Creating primary surface... ");
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
    g_app.primaryInitialized = true;
    
    printf("Step 5: Creating clipper... ");
    hr = g_app.dd->CreateClipper(0, &g_app.clipper, NULL);
    if (SUCCEEDED(hr)) {
        printf("OK\n");
        printf("  Setting clipper HWND... ");
        hr = g_app.clipper->SetHWnd(0, g_app.hwnd);
        if (SUCCEEDED(hr)) {
            printf("OK\n");
            printf("  Attaching clipper to primary surface... ");
            hr = g_app.primary->SetClipper(g_app.clipper);
            if (SUCCEEDED(hr)) {
                printf("OK\n");
            } else {
                printf("FAILED (0x%08lX)\n", hr);
            }
        } else {
            printf("FAILED (0x%08lX)\n", hr);
        }
    } else {
        printf("FAILED (0x%08lX) - continuing without clipper\n", hr);
        g_app.clipper = nullptr;
    }
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.width = client.right - client.left;
    g_app.height = client.bottom - client.top;
    printf("Step 6: Window size = %dx%d\n", g_app.width, g_app.height);
    
    printf("Step 7: Creating back buffer... ");
    desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    desc.dwWidth = g_app.width;
    desc.dwHeight = g_app.height;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    
    hr = g_app.dd->CreateSurface(&desc, &g_app.backbuffer, NULL);
    if (FAILED(hr)) {
        printf("Video memory failed (0x%08lX), trying system memory... ", hr);
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        hr = g_app.dd->CreateSurface(&desc, &g_app.backbuffer, NULL);
        if (FAILED(hr)) {
            printf("FAILED (0x%08lX)\n", hr);
            return false;
        }
        printf("OK (system memory)\n");
    } else {
        printf("OK (video memory)\n");
    }
    g_app.backbufferInitialized = true;
    
    printf("Step 8: Testing backbuffer fill... ");
    HDC hdc;
    hr = g_app.backbuffer->GetDC(&hdc);
    if (SUCCEEDED(hr)) {
        RECT rect = {0, 0, g_app.width, g_app.height};
        HBRUSH brush = CreateSolidBrush(RGB(0, 100, 0));
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        g_app.backbuffer->ReleaseDC(hdc);
        printf("OK (filled with dark green)\n");
    } else {
        printf("FAILED (0x%08lX)\n", hr);
    }
    
    printf("Step 9: Creating offscreen surfaces...\n");
    for (int i = 0; i < 8; i++) {
        desc.dwWidth = 64;
        desc.dwHeight = 64;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        
        hr = g_app.dd->CreateSurface(&desc, &g_app.offscreen[i], NULL);
        if (FAILED(hr)) {
            printf("  Surface %d: FAILED (0x%08lX)\n", i, hr);
            return false;
        }
        printf("  Surface %d: OK\n", i);
        FillTestPattern(g_app.offscreen[i], i);
    }
    
    printf("Step 10: Testing primary blit... ");
    RECT dstRect = {50, 50, 114, 114};
    hr = g_app.primary->Blt(&dstRect, g_app.offscreen[0], NULL, DDBLT_WAIT, NULL);
    if (SUCCEEDED(hr)) {
        printf("OK\n");
        g_app.blitWorking = true;
    } else {
        printf("FAILED (0x%08lX)\n", hr);
    }
    
    printf("=== DirectDraw initialization complete ===\n\n");
    return true;
}

// ============================================================================
// Простой тест цветовой заливки
// ============================================================================

void TestSimpleColorFill() {
    if (!g_app.backbuffer || !g_app.primary) {
        printf("Surfaces not initialized\n");
        return;
    }
    
    CheckAndRestoreSurfaces();
    
    HDC hdc;
    if (SUCCEEDED(g_app.backbuffer->GetDC(&hdc))) {
        RECT rect = {0, 0, g_app.width, g_app.height};
        HBRUSH brush = CreateSolidBrush(RGB(0, 255, 0));
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        g_app.backbuffer->ReleaseDC(hdc);
        
        RECT client;
        GetClientRect(g_app.hwnd, &client);
        HRESULT hr = g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
        if (SUCCEEDED(hr)) {
            printf("Simple color fill test completed - window should be green\n");
            g_app.blitWorking = true;
        } else {
            printf("Simple color fill test FAILED with error 0x%08lX\n", hr);
        }
    }
}

// ============================================================================
// Добавление результата теста
// ============================================================================

void AddResult(const char* name, DWORD time, HRESULT result, int ops) {
    TestResult tr;
    tr.name = name;
    tr.time = time;
    tr.result = result;
    tr.operations = ops;
    tr.opsPerSecond = ops * 1000.0f / (time > 0 ? time : 1);
    g_app.results.push_back(tr);
    
    printf("Test '%s' completed in %lu ms, %d ops, %.2f ops/sec, Result: 0x%08lX %s\n",
           name, time, ops, tr.opsPerSecond, result,
           SUCCEEDED(result) ? "(SUCCESS)" : "(FAILED)");
}

// ============================================================================
// Рисование тестового паттерна
// ============================================================================

void DrawTestPattern(HDC hdc, int x, int y, int size) {
    HBRUSH colors[] = {
        CreateSolidBrush(RGB(255, 0, 0)),
        CreateSolidBrush(RGB(0, 255, 0)),
        CreateSolidBrush(RGB(0, 0, 255)),
        CreateSolidBrush(RGB(255, 255, 0))
    };
    int quarter = size / 2;
    
    RECT rect = {x, y, x + quarter, y + quarter};
    FillRect(hdc, &rect, colors[0]);
    
    rect = {x + quarter, y, x + size, y + quarter};
    FillRect(hdc, &rect, colors[1]);
    
    rect = {x, y + quarter, x + quarter, y + size};
    FillRect(hdc, &rect, colors[2]);
    
    rect = {x + quarter, y + quarter, x + size, y + size};
    FillRect(hdc, &rect, colors[3]);
    
    for (int i = 0; i < 4; i++) {
        DeleteObject(colors[i]);
    }
}

// ============================================================================
// Рисование UI
// ============================================================================

void DrawUI(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    char text[256];
    int y = 10;
    
    sprintf(text, "DDVK - BltBatch Test Suite");
    TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
    
    sprintf(text, "Primary: %s, Backbuffer: %s, Blit: %s, Clipper: %s",
            g_app.primaryInitialized ? "OK" : "FAIL",
            g_app.backbufferInitialized ? "OK" : "FAIL",
            g_app.blitWorking ? "OK" : "FAIL",
            g_app.clipper ? "OK" : "NO");
    TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
    
    sprintf(text, "Window: %dx%d, FPS: %d", g_app.width, g_app.height, g_app.fps);
    TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
    
    y += 10;
    
    const char* instructions[] = {
        "1 - Simple Batch", "2 - Complex Batch", "3 - Mixed Batch",
        "4 - Color Fill Batch", "5 - Stretch Batch", "6 - Color Keyed Batch",
        "7 - Error Handling Batch", "8 - Performance Comparison",
        "T - Simple Color Fill Test", "C - Clear Results",
        "SPACE - Repeat Last Test", "ESC - Exit"
    };
    
    for (int i = 0; i < 12; i++) {
        sprintf(text, "  %s", instructions[i]);
        TextOutA(hdc, 20, y, text, strlen(text));
        y += 20;
    }
    
    y += 10;
    DrawTestPattern(hdc, g_app.width - 150, 10, 100);
    
    if (!g_app.results.empty()) {
        sprintf(text, "Test Results:");
        TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
        
        for (size_t i = 0; i < g_app.results.size(); i++) {
            const TestResult& tr = g_app.results[i];
            if (SUCCEEDED(tr.result)) {
                sprintf(text, "  %s: %lu ms, %d ops, %.0f ops/sec",
                        tr.name, tr.time, tr.operations, tr.opsPerSecond);
            } else {
                sprintf(text, "  %s: FAILED (0x%08lX)", tr.name, tr.result);
            }
            TextOutA(hdc, 20, y, text, strlen(text));
            y += 20;
        }
    }
}

// ============================================================================
// Рендеринг кадра
// ============================================================================

void RenderFrame() {
    if (!g_app.primary) {
        printf("RenderFrame: primary surface is NULL!\n");
        return;
    }
    if (!g_app.backbuffer) {
        printf("RenderFrame: backbuffer is NULL!\n");
        return;
    }
    
    CheckAndRestoreSurfaces();
    
    HDC hdc;
    HRESULT hr = g_app.backbuffer->GetDC(&hdc);
    if (FAILED(hr)) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            printf("RenderFrame: GetDC failed with error 0x%08lX\n", hr);
        }
        return;
    }
    
    RECT rect = {0, 0, g_app.width, g_app.height};
    HBRUSH bgBrush = g_app.blitWorking ? 
                     CreateSolidBrush(RGB(0, 70, 0)) : 
                     CreateSolidBrush(RGB(70, 0, 0));
    
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);
    
    DrawUI(hdc);
    
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    
    Rectangle(hdc, 5, 5, g_app.width - 5, g_app.height - 5);
    
    MoveToEx(hdc, 0, 0, NULL);
    LineTo(hdc, g_app.width, g_app.height);
    
    MoveToEx(hdc, g_app.width/2, 0, NULL);
    LineTo(hdc, g_app.width/2, g_app.height);
    MoveToEx(hdc, 0, g_app.height/2, NULL);
    LineTo(hdc, g_app.width, g_app.height/2);
    
    DeleteObject(pen);
    g_app.backbuffer->ReleaseDC(hdc);
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    
    hr = g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    if (FAILED(hr)) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            printf("RenderFrame: Blt failed with error 0x%08lX\n", hr);
        }
    } else {
        g_app.blitWorking = true;
    }
    
    g_app.frameCount++;
    DWORD currentTime = GetTickCount();
    if (currentTime - g_app.lastFpsTime >= 1000) {
        g_app.fps = g_app.frameCount;
        printf("FPS: %d, Blit: %s\n", g_app.fps, g_app.blitWorking ? "OK" : "FAIL");
        g_app.frameCount = 0;
        g_app.lastFpsTime = currentTime;
    }
}

// ============================================================================
// Очистка
// ============================================================================

void Cleanup() {
    printf("\n=== Cleaning up ===\n");
    
    if (g_app.clipper) {
        g_app.clipper->Release();
        g_app.clipper = nullptr;
        printf("Released clipper\n");
    }
    
    for (int i = 0; i < 8; i++) {
        if (g_app.offscreen[i]) {
            g_app.offscreen[i]->Release();
            g_app.offscreen[i] = nullptr;
            printf("Released offscreen surface %d\n", i);
        }
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
// ТЕСТ 1: Простой пакет
// ============================================================================

void TestSimpleBatch() {
    printf("\n=== Test 1: Simple Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 100;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        srcRects[i].left = 0;
        srcRects[i].top = 0;
        srcRects[i].right = 64;
        srcRects[i].bottom = 64;
        
        dstRects[i].left = (i % 16) * 70;
        dstRects[i].top = (i / 16) * 70;
        dstRects[i].right = dstRects[i].left + 64;
        dstRects[i].bottom = dstRects[i].top + 64;
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 4];
        batch[i].lprSrc = &srcRects[i];
        batch[i].dwFlags = DDBLT_WAIT;
        batch[i].lpDDBltFx = NULL;
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Simple Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 2: Сложный пакет
// ============================================================================

void TestComplexBatch() {
    printf("\n=== Test 2: Complex Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 50;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    std::vector<DDBLTFX> fx(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        srcRects[i].left = (i % 3) * 20;
        srcRects[i].top = (i % 3) * 20;
        srcRects[i].right = 64 - (i % 3) * 20;
        srcRects[i].bottom = 64 - (i % 3) * 20;
        
        dstRects[i].left = (i % 12) * 80 + (i % 5) * 10;
        dstRects[i].top = (i / 12) * 80 + (i % 5) * 10;
        dstRects[i].right = dstRects[i].left + (srcRects[i].right - srcRects[i].left);
        dstRects[i].bottom = dstRects[i].top + (srcRects[i].bottom - srcRects[i].top);
        
        int srcIdx = i % 8;
        
        if (i % 4 == 0) {
            fx[i].dwSize = sizeof(DDBLTFX);
            fx[i].dwDDFX = DDBLTFX_MIRRORLEFTRIGHT;
            batch[i].dwFlags = DDBLT_WAIT | DDBLT_DDFX;
            batch[i].lpDDBltFx = &fx[i];
        } else {
            batch[i].dwFlags = DDBLT_WAIT;
            batch[i].lpDDBltFx = NULL;
        }
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[srcIdx];
        batch[i].lprSrc = &srcRects[i];
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Complex Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 3: Смешанный пакет
// ============================================================================

void TestMixedBatch() {
    printf("\n=== Test 3: Mixed Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 40;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<DDBLTFX> fx(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        dstRects[i].left = (i % 10) * 90;
        dstRects[i].top = (i / 10) * 90;
        dstRects[i].right = dstRects[i].left + 64;
        dstRects[i].bottom = dstRects[i].top + 64;
        
        if (i % 3 == 0) {
            fx[i].dwSize = sizeof(DDBLTFX);
            fx[i].dwFillColor = RGB(128 + i * 10, 64, 192);
            
            batch[i].lprDest = &dstRects[i];
            batch[i].lpDDSSrc = NULL;
            batch[i].lprSrc = NULL;
            batch[i].dwFlags = DDBLT_WAIT | DDBLT_COLORFILL;
            batch[i].lpDDBltFx = &fx[i];
        } else {
            srcRects[i].left = 0;
            srcRects[i].top = 0;
            srcRects[i].right = 64;
            srcRects[i].bottom = 64;
            
            batch[i].lprDest = &dstRects[i];
            batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
            batch[i].lprSrc = &srcRects[i];
            batch[i].dwFlags = DDBLT_WAIT;
            batch[i].lpDDBltFx = NULL;
        }
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Mixed Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 4: Пакет с цветовой заливкой
// ============================================================================

void TestColorFillBatch() {
    printf("\n=== Test 4: Color Fill Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 60;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    std::vector<DDBLTFX> fx(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        dstRects[i].left = (i % 12) * 70;
        dstRects[i].top = (i / 12) * 70;
        dstRects[i].right = dstRects[i].left + 50;
        dstRects[i].bottom = dstRects[i].top + 50;
        
        fx[i].dwSize = sizeof(DDBLTFX);
        
        int r = (i * 50) % 256;
        int g = (i * 80) % 256;
        int b = (i * 110) % 256;
        fx[i].dwFillColor = RGB(r, g, b);
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = NULL;
        batch[i].lprSrc = NULL;
        batch[i].dwFlags = DDBLT_WAIT | DDBLT_COLORFILL;
        batch[i].lpDDBltFx = &fx[i];
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Color Fill Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 5: Пакет с растяжением
// ============================================================================

void TestStretchBatch() {
    printf("\n=== Test 5: Stretch Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 30;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        srcRects[i].left = 0;
        srcRects[i].top = 0;
        srcRects[i].right = 32 + (i % 3) * 16;
        srcRects[i].bottom = 32 + (i % 3) * 16;
        
        dstRects[i].left = (i % 8) * 120;
        dstRects[i].top = (i / 8) * 120;
        dstRects[i].right = dstRects[i].left + 64 + (i % 4) * 20;
        dstRects[i].bottom = dstRects[i].top + 64 + (i % 4) * 20;
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
        batch[i].lprSrc = &srcRects[i];
        batch[i].dwFlags = DDBLT_WAIT;
        batch[i].lpDDBltFx = NULL;
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Stretch Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 6: Пакет с цветовыми ключами
// ============================================================================

void TestKeyedBatch() {
    printf("\n=== Test 6: Color Keyed Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 40;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        srcRects[i].left = 0;
        srcRects[i].top = 0;
        srcRects[i].right = 64;
        srcRects[i].bottom = 64;
        
        dstRects[i].left = (i % 10) * 80;
        dstRects[i].top = (i / 10) * 80;
        dstRects[i].right = dstRects[i].left + 64;
        dstRects[i].bottom = dstRects[i].top + 64;
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
        batch[i].lprSrc = &srcRects[i];
        
        if (i % 3 == 0) {
            batch[i].dwFlags = DDBLT_WAIT | DDBLT_KEYSRC;
        } else if (i % 3 == 1) {
            batch[i].dwFlags = DDBLT_WAIT | DDBLT_KEYDEST;
        } else {
            batch[i].dwFlags = DDBLT_WAIT;
        }
        
        batch[i].lpDDBltFx = NULL;
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Color Keyed Batch", elapsed, hr, BATCH_SIZE);
}

// ============================================================================
// ТЕСТ 7: Пакет с ошибками
// ============================================================================

void TestErrorBatch() {
    printf("\n=== Test 7: Error Handling Batch ===\n");
    CheckAndRestoreSurfaces();
    
    const int BATCH_SIZE = 20;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> srcRects(BATCH_SIZE);
    std::vector<RECT> dstRects(BATCH_SIZE);
    std::vector<DDBLTFX> fx(BATCH_SIZE);
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        dstRects[i].left = (i % 8) * 90;
        dstRects[i].top = (i / 8) * 90;
        dstRects[i].right = dstRects[i].left + 64;
        dstRects[i].bottom = dstRects[i].top + 64;
        
        switch (i % 5) {
            case 0:
                srcRects[i].left = 0;
                srcRects[i].top = 0;
                srcRects[i].right = 64;
                srcRects[i].bottom = 64;
                
                batch[i].lprDest = &dstRects[i];
                batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
                batch[i].lprSrc = &srcRects[i];
                batch[i].dwFlags = DDBLT_WAIT;
                batch[i].lpDDBltFx = NULL;
                break;
                
            case 1:
                srcRects[i].left = -10;
                srcRects[i].top = -10;
                srcRects[i].right = 128;
                srcRects[i].bottom = 128;
                
                batch[i].lprDest = &dstRects[i];
                batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
                batch[i].lprSrc = &srcRects[i];
                batch[i].dwFlags = DDBLT_WAIT;
                batch[i].lpDDBltFx = NULL;
                break;
                
            case 2:
                dstRects[i].right = g_app.width + 100;
                dstRects[i].bottom = g_app.height + 100;
                
                srcRects[i].left = 0;
                srcRects[i].top = 0;
                srcRects[i].right = 64;
                srcRects[i].bottom = 64;
                
                batch[i].lprDest = &dstRects[i];
                batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
                batch[i].lprSrc = &srcRects[i];
                batch[i].dwFlags = DDBLT_WAIT;
                batch[i].lpDDBltFx = NULL;
                break;
                
            case 3:
                fx[i].dwSize = sizeof(DDBLTFX);
                fx[i].dwFillColor = RGB(255, 0, 0);
                
                batch[i].lprDest = &dstRects[i];
                batch[i].lpDDSSrc = NULL;
                batch[i].lprSrc = NULL;
                batch[i].dwFlags = DDBLT_WAIT;
                batch[i].lpDDBltFx = &fx[i];
                break;
                
            case 4:
                srcRects[i].left = 0;
                srcRects[i].top = 0;
                srcRects[i].right = 64;
                srcRects[i].bottom = 64;
                
                batch[i].lprDest = &dstRects[i];
                batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
                batch[i].lprSrc = &srcRects[i];
                batch[i].dwFlags = DDBLT_WAIT | 0x80000000;
                batch[i].lpDDBltFx = NULL;
                break;
        }
    }
    
    DWORD start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    DWORD elapsed = GetTickCount() - start;
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    AddResult("Error Handling Batch", elapsed, hr, BATCH_SIZE);
    printf("Note: This test intentionally contains errors\n");
}

// ============================================================================
// ТЕСТ 8: Сравнение производительности
// ============================================================================

void TestPerformanceComparison() {
    printf("\n=== Test 8: Performance Comparison ===\n");
    CheckAndRestoreSurfaces();
    
    const int NUM_OPS = 200;
    std::vector<RECT> dstRects(NUM_OPS);
    
    for (int i = 0; i < NUM_OPS; i++) {
        dstRects[i].left = (i % 20) * 40;
        dstRects[i].top = (i / 20) * 40;
        dstRects[i].right = dstRects[i].left + 32;
        dstRects[i].bottom = dstRects[i].top + 32;
    }
    
    // Тест 1: Последовательные Blt
    DWORD start = GetTickCount();
    for (int i = 0; i < NUM_OPS; i++) {
        RECT srcRect = {0, 0, 32, 32};
        g_app.backbuffer->Blt(&dstRects[i], g_app.offscreen[i % 8], &srcRect, DDBLT_WAIT, NULL);
    }
    DWORD sequentialTime = GetTickCount() - start;
    AddResult("Sequential Blits", sequentialTime, DD_OK, NUM_OPS);
    
    // Тест 2: Пакетный Blt (малыми группами)
    const int BATCH_SIZE = 20;
    int numBatches = NUM_OPS / BATCH_SIZE;
    std::vector<DDBLTBATCH> batch(BATCH_SIZE);
    std::vector<RECT> batchSrcRects(BATCH_SIZE);
    
    start = GetTickCount();
    for (int b = 0; b < numBatches; b++) {
        for (int i = 0; i < BATCH_SIZE; i++) {
            int idx = b * BATCH_SIZE + i;
            batchSrcRects[i].left = 0;
            batchSrcRects[i].top = 0;
            batchSrcRects[i].right = 32;
            batchSrcRects[i].bottom = 32;
            
            batch[i].lprDest = &dstRects[idx];
            batch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[idx % 8];
            batch[i].lprSrc = &batchSrcRects[i];
            batch[i].dwFlags = DDBLT_WAIT;
            batch[i].lpDDBltFx = NULL;
        }
        g_app.backbuffer->BltBatch(batch.data(), BATCH_SIZE, 0);
    }
    DWORD batchTime = GetTickCount() - start;
    AddResult("Batch Blits (20 ops/batch)", batchTime, DD_OK, NUM_OPS);
    
    // Тест 3: Пакетный Blt (одним большим пакетом)
    std::vector<DDBLTBATCH> bigBatch(NUM_OPS);
    std::vector<RECT> bigSrcRects(NUM_OPS);
    
    for (int i = 0; i < NUM_OPS; i++) {
        bigSrcRects[i].left = 0;
        bigSrcRects[i].top = 0;
        bigSrcRects[i].right = 32;
        bigSrcRects[i].bottom = 32;
        
        bigBatch[i].lprDest = &dstRects[i];
        bigBatch[i].lpDDSSrc = (LPDIRECTDRAWSURFACE)g_app.offscreen[i % 8];
        bigBatch[i].lprSrc = &bigSrcRects[i];
        bigBatch[i].dwFlags = DDBLT_WAIT;
        bigBatch[i].lpDDBltFx = NULL;
    }
    
    start = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(bigBatch.data(), NUM_OPS, 0);
    DWORD bigBatchTime = GetTickCount() - start;
    AddResult("Single Large Batch", bigBatchTime, hr, NUM_OPS);
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    
    printf("\nPerformance Summary:\n");
    printf("  Sequential: %lu ms (%.2f ops/sec)\n", sequentialTime, NUM_OPS * 1000.0f / sequentialTime);
    printf("  Batched (20): %lu ms (%.2f ops/sec)\n", batchTime, NUM_OPS * 1000.0f / batchTime);
    printf("  Batched (%d): %lu ms (%.2f ops/sec)\n", NUM_OPS, bigBatchTime, NUM_OPS * 1000.0f / bigBatchTime);
    
    if (sequentialTime > 0) {
        float speedup20 = (float)sequentialTime / batchTime;
        float speedupAll = (float)sequentialTime / bigBatchTime;
        printf("  Speedup (20): %.2fx\n", speedup20);
        printf("  Speedup (all): %.2fx\n", speedupAll);
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
    printf("DDVK - BltBatch Test Suite (FIXED VERSION)\n");
    printf("========================================\n\n");
    printf("Changes made:\n");
    printf("  1. Added clipper for windowed mode\n");
    printf("  2. Added surface loss detection and recovery\n");
    printf("  3. Fixed type casting in BltBatch\n");
    printf("  4. Added simple color fill test (press T)\n");
    printf("  5. Added diagnostic colors\n\n");
    
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
    
    printf("\nRunning automatic color fill test...\n");
    TestSimpleColorFill();
    
    g_app.running = true;
    
    printf("\n=== Entering main loop ===\n");
    printf("Window should now display content. If white, press T for test.\n");
    printf("Check console for error messages.\n\n");
    
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
    printf("\nTest suite completed.\n");
    
    FreeConsole();
    return 0;
}