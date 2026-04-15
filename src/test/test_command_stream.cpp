// tests/test_command_stream.cpp
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
    IDirectDrawSurface7* offscreen[4];
    bool running;
    int width;
    int height;
    float angle;
    DWORD lastTime;
    int frameCount;
    int asyncOperations;
    int completedOperations;
    
    // Статистика
    DWORD blitTime;
    DWORD asyncBlitTime;
    int blitCount;
    int asyncBlitCount;
};

App g_app = {0};

// ============================================================================
// Предварительные объявления функций
// ============================================================================

void FillTestPattern(IDirectDrawSurface7* surface, int pattern);
void TestSyncBlits();
void TestAsyncBlits();
void TestBatchBlits();
void TestMixedBlits();
void TestStress();
void ResetStats();
DWORD WINAPI SyncBlitThread(LPVOID lpParam);
DWORD WINAPI AsyncBlitThread(LPVOID lpParam);
DWORD WINAPI BatchBlitThread(LPVOID lpParam);

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
            if (wParam == '1') {
                printf("\n=== Running SYNC blit test ===\n");
                TestSyncBlits();
            }
            if (wParam == '2') {
                printf("\n=== Running ASYNC blit test ===\n");
                TestAsyncBlits();
            }
            if (wParam == '3') {
                printf("\n=== Running BATCH blit test ===\n");
                TestBatchBlits();
            }
            if (wParam == '4') {
                printf("\n=== Running MIXED blit test ===\n");
                TestMixedBlits();
            }
            if (wParam == '5') {
                printf("\n=== Running STRESS test ===\n");
                TestStress();
            }
            if (wParam == 'R' || wParam == 'r') {
                printf("\n=== Resetting statistics ===\n");
                ResetStats();
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
    wc.lpszClassName = L"CommandStreamTestWindow";
    
    if (!RegisterClassEx(&wc)) {
        printf("Failed to register window class\n");
        return false;
    }
    
    g_app.hwnd = CreateWindowEx(
        0, L"CommandStreamTestWindow", L"DDVK - Command Stream Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768, NULL, NULL, hInstance, NULL);
    
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
    g_app.angle = 0.0f;
    g_app.frameCount = 0;
    g_app.lastTime = GetTickCount();
    g_app.asyncOperations = 0;
    g_app.completedOperations = 0;
    g_app.blitTime = 0;
    g_app.asyncBlitTime = 0;
    g_app.blitCount = 0;
    g_app.asyncBlitCount = 0;
    
    return true;
}

// ============================================================================
// Заполнение тестового паттерна
// ============================================================================

void FillTestPattern(IDirectDrawSurface7* surface, int pattern) {
    HDC hdc;
    if (SUCCEEDED(surface->GetDC(&hdc))) {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = 128;
        rect.bottom = 128;
        
        COLORREF colors[] = {
            RGB(255, 0, 0),
            RGB(0, 255, 0),
            RGB(0, 0, 255),
            RGB(255, 255, 0)
        };
        
        HBRUSH brush = CreateSolidBrush(colors[pattern % 4]);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        char text[32];
        sprintf(text, "SURF %d", pattern);
        TextOutA(hdc, 50, 50, text, strlen(text));
        
        surface->ReleaseDC(hdc);
    }
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
    
    printf("Step 5: Creating offscreen surfaces...\n");
    for (int i = 0; i < 4; i++) {
        desc.dwWidth = 128;
        desc.dwHeight = 128;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        
        hr = g_app.dd->CreateSurface(&desc, &g_app.offscreen[i], NULL);
        if (FAILED(hr)) {
            printf("  Surface %d: FAILED (0x%08lX)\n", i, hr);
            return false;
        }
        printf("  Surface %d: OK\n", i);
        FillTestPattern(g_app.offscreen[i], i);
    }
    
    printf("=== DirectDraw initialization complete ===\n\n");
    return true;
}

// ============================================================================
// Сброс статистики
// ============================================================================

void ResetStats() {
    g_app.blitTime = 0;
    g_app.asyncBlitTime = 0;
    g_app.blitCount = 0;
    g_app.asyncBlitCount = 0;
    g_app.asyncOperations = 0;
    g_app.completedOperations = 0;
    printf("Statistics reset\n");
}

// ============================================================================
// ТЕСТ 1: Синхронные Blit операции
// ============================================================================

void TestSyncBlits() {
    printf("\n=== Test 1: Sync Blits (100 operations) ===\n");
    
    DWORD startTime = GetTickCount();
    int successCount = 0;
    
    for (int i = 0; i < 100; i++) {
        RECT destRect;
        destRect.left = (i % 10) * 100;
        destRect.top = (i / 10) * 100;
        destRect.right = destRect.left + 128;
        destRect.bottom = destRect.top + 128;
        
        HRESULT hr = g_app.backbuffer->Blt(
            &destRect,
            g_app.offscreen[i % 4],
            NULL,
            DDBLT_WAIT,
            NULL
        );
        
        if (SUCCEEDED(hr)) {
            successCount++;
        } else {
            printf("Sync blit %d failed: 0x%08lX\n", i, hr);
        }
    }
    
    DWORD endTime = GetTickCount();
    DWORD elapsed = endTime - startTime;
    
    g_app.blitTime += elapsed;
    g_app.blitCount += 100;
    
    printf("Sync blits completed: %d/100 success, %lu ms, %.2f ops/sec\n", 
           successCount, elapsed, 100 * 1000.0f / (elapsed > 0 ? elapsed : 1));
}

// ============================================================================
// ТЕСТ 2: Асинхронные Blit операции
// ============================================================================

void TestAsyncBlits() {
    printf("\n=== Test 2: Async Blits (100 operations) ===\n");
    
    DWORD startTime = GetTickCount();
    HANDLE events[100];
    int successCount = 0;
    
    for (int i = 0; i < 100; i++) {
        events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        RECT destRect;
        destRect.left = (i % 10) * 100;
        destRect.top = (i / 10) * 100;
        destRect.right = destRect.left + 128;
        destRect.bottom = destRect.top + 128;
        
        // DDBLT_ASYNC может не поддерживаться, эмулируем через обычный Blt
        HRESULT hr = g_app.backbuffer->Blt(
            &destRect,
            g_app.offscreen[i % 4],
            NULL,
            DDBLT_WAIT,
            NULL
        );
        
        if (SUCCEEDED(hr)) {
            successCount++;
            g_app.asyncOperations++;
        }
        
        SetEvent(events[i]);
    }
    
    WaitForMultipleObjects(100, events, TRUE, 5000);
    
    for (int i = 0; i < 100; i++) {
        CloseHandle(events[i]);
    }
    
    DWORD endTime = GetTickCount();
    DWORD elapsed = endTime - startTime;
    
    g_app.asyncBlitTime += elapsed;
    g_app.asyncBlitCount += 100;
    g_app.completedOperations += 100;
    
    printf("Async blits completed: %d/100 success, %lu ms, %.2f ops/sec\n", 
           successCount, elapsed, 100 * 1000.0f / (elapsed > 0 ? elapsed : 1));
}

// ============================================================================
// ТЕСТ 3: Пакетные Blit операции
// ============================================================================

void TestBatchBlits() {
    printf("\n=== Test 3: Batch Blits (50 operations) ===\n");
    
    std::vector<DDBLTBATCH> batch(50);
    std::vector<RECT> srcRects(50);
    std::vector<RECT> dstRects(50);
    
    for (int i = 0; i < 50; i++) {
        dstRects[i].left = (i % 10) * 100;
        dstRects[i].top = (i / 10) * 100;
        dstRects[i].right = dstRects[i].left + 128;
        dstRects[i].bottom = dstRects[i].top + 128;
        
        srcRects[i].left = 0;
        srcRects[i].top = 0;
        srcRects[i].right = 128;
        srcRects[i].bottom = 128;
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = reinterpret_cast<LPDIRECTDRAWSURFACE>(g_app.offscreen[i % 4]);
        batch[i].lprSrc = &srcRects[i];
        batch[i].dwFlags = DDBLT_WAIT;
        batch[i].lpDDBltFx = NULL;
    }
    
    DWORD startTime = GetTickCount();
    HRESULT hr = g_app.backbuffer->BltBatch(batch.data(), 50, 0);
    DWORD endTime = GetTickCount();
    DWORD elapsed = endTime - startTime;
    
    if (SUCCEEDED(hr)) {
        printf("Batch blits completed: 50 ops, %lu ms, %.2f ops/sec\n", 
               elapsed, 50 * 1000.0f / (elapsed > 0 ? elapsed : 1));
    } else {
        printf("BltBatch failed: 0x%08lX\n", hr);
        printf("Falling back to sequential blits...\n");
        
        DWORD fallbackStart = GetTickCount();
        for (int i = 0; i < 50; i++) {
            g_app.backbuffer->Blt(&dstRects[i], g_app.offscreen[i % 4], &srcRects[i], DDBLT_WAIT, NULL);
        }
        DWORD fallbackEnd = GetTickCount();
        printf("Fallback completed: %lu ms\n", fallbackEnd - fallbackStart);
    }
}

// ============================================================================
// ТЕСТ 4: Смешанные операции
// ============================================================================

void TestMixedBlits() {
    printf("\n=== Test 4: Mixed Operations ===\n");
    
    DWORD totalStart = GetTickCount();
    
    HANDLE threads[3];
    DWORD threadIds[3];
    
    threads[0] = CreateThread(NULL, 0, SyncBlitThread, NULL, 0, &threadIds[0]);
    threads[1] = CreateThread(NULL, 0, AsyncBlitThread, NULL, 0, &threadIds[1]);
    threads[2] = CreateThread(NULL, 0, BatchBlitThread, NULL, 0, &threadIds[2]);
    
    WaitForMultipleObjects(3, threads, TRUE, 10000);
    
    for (int i = 0; i < 3; i++) {
        CloseHandle(threads[i]);
    }
    
    DWORD totalEnd = GetTickCount();
    printf("Mixed operations completed in %lu ms\n", totalEnd - totalStart);
}

// ============================================================================
// Потоки для смешанных операций
// ============================================================================

DWORD WINAPI SyncBlitThread(LPVOID lpParam) {
    (void)lpParam;
    for (int i = 0; i < 30; i++) {
        RECT destRect;
        destRect.left = (i % 6) * 150;
        destRect.top = (i / 6) * 150;
        destRect.right = destRect.left + 128;
        destRect.bottom = destRect.top + 128;
        
        g_app.backbuffer->Blt(&destRect, g_app.offscreen[i % 4], NULL, DDBLT_WAIT, NULL);
        Sleep(10);
    }
    return 0;
}

DWORD WINAPI AsyncBlitThread(LPVOID lpParam) {
    (void)lpParam;
    HANDLE events[30];
    for (int i = 0; i < 30; i++) {
        events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        RECT destRect;
        destRect.left = (i % 6) * 150 + 50;
        destRect.top = (i / 6) * 150;
        destRect.right = destRect.left + 128;
        destRect.bottom = destRect.top + 128;
        
        g_app.backbuffer->Blt(&destRect, g_app.offscreen[(i + 1) % 4], NULL, DDBLT_WAIT, NULL);
        SetEvent(events[i]);
        Sleep(5);
    }
    
    WaitForMultipleObjects(30, events, TRUE, 5000);
    
    for (int i = 0; i < 30; i++) {
        CloseHandle(events[i]);
    }
    return 0;
}

DWORD WINAPI BatchBlitThread(LPVOID lpParam) {
    (void)lpParam;
    std::vector<DDBLTBATCH> batch(15);
    std::vector<RECT> srcRects(15);
    std::vector<RECT> dstRects(15);
    
    for (int i = 0; i < 15; i++) {
        dstRects[i].left = (i % 5) * 150 + 25;
        dstRects[i].top = (i / 5) * 150 + 100;
        dstRects[i].right = dstRects[i].left + 128;
        dstRects[i].bottom = dstRects[i].top + 128;
        
        srcRects[i].left = 0;
        srcRects[i].top = 0;
        srcRects[i].right = 128;
        srcRects[i].bottom = 128;
        
        batch[i].lprDest = &dstRects[i];
        batch[i].lpDDSSrc = reinterpret_cast<LPDIRECTDRAWSURFACE>(g_app.offscreen[(i + 2) % 4]);
        batch[i].lprSrc = &srcRects[i];
        batch[i].dwFlags = DDBLT_WAIT;
        batch[i].lpDDBltFx = NULL;
    }
    
    g_app.backbuffer->BltBatch(batch.data(), 15, 0);
    return 0;
}

// ============================================================================
// ТЕСТ 5: Стресс-тест
// ============================================================================

void TestStress() {
    printf("\n=== Test 5: Stress Test (1000 operations) ===\n");
    
    const int NUM_OPS = 1000;
    HANDLE* events = new HANDLE[NUM_OPS];
    HRESULT* results = new HRESULT[NUM_OPS];
    
    DWORD startTime = GetTickCount();
    int successCount = 0;
    
    for (int i = 0; i < NUM_OPS; i++) {
        events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        results[i] = DD_OK;
        
        RECT destRect;
        destRect.left = rand() % (g_app.width - 128);
        destRect.top = rand() % (g_app.height - 128);
        destRect.right = destRect.left + 128;
        destRect.bottom = destRect.top + 128;
        
        if (i % 3 == 0) {
            results[i] = g_app.backbuffer->Blt(&destRect, g_app.offscreen[rand() % 4], NULL, DDBLT_WAIT, NULL);
        } else if (i % 3 == 1) {
            results[i] = g_app.backbuffer->Blt(&destRect, g_app.offscreen[rand() % 4], NULL, DDBLT_WAIT, NULL);
        } else {
            DDBLTFX fx = {};
            fx.dwSize = sizeof(fx);
            fx.dwFillColor = RGB(rand() % 256, rand() % 256, rand() % 256);
            results[i] = g_app.backbuffer->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        }
        
        if (SUCCEEDED(results[i])) successCount++;
        SetEvent(events[i]);
    }
    
    WaitForMultipleObjects(NUM_OPS, events, TRUE, 10000);
    
    DWORD endTime = GetTickCount();
    DWORD elapsed = endTime - startTime;
    
    printf("Stress test completed: %d/%d success, %lu ms, %.2f ops/sec\n", 
           successCount, NUM_OPS, elapsed, NUM_OPS * 1000.0f / (elapsed > 0 ? elapsed : 1));
    
    for (int i = 0; i < NUM_OPS; i++) {
        CloseHandle(events[i]);
    }
    
    delete[] events;
    delete[] results;
}

// ============================================================================
// Отрисовка тестового паттерна
// ============================================================================

void DrawTestPattern() {
    if (!g_app.backbuffer) return;
    
    HDC hdc;
    if (SUCCEEDED(g_app.backbuffer->GetDC(&hdc))) {
        RECT rect = {0, 0, g_app.width, g_app.height};
        HBRUSH bgBrush = CreateSolidBrush(RGB(32, 32, 64));
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        
        char text[256];
        int y = 10;
        
        sprintf(text, "DDVK Command Stream Test - FPS: %d", g_app.frameCount);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
        
        sprintf(text, "Controls: 1-Sync, 2-Async, 3-Batch, 4-Mixed, 5-Stress, R-Reset");
        TextOutA(hdc, 10, y, text, strlen(text)); y += 25;
        
        sprintf(text, "Sync Blits: %d operations, %lu ms total", g_app.blitCount, g_app.blitTime);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        if (g_app.blitCount > 0) {
            sprintf(text, "Sync Avg: %.2f ms/op", (float)g_app.blitTime / g_app.blitCount);
            TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        }
        
        sprintf(text, "Async Blits: %d operations, %lu ms total", g_app.asyncBlitCount, g_app.asyncBlitTime);
        TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        
        if (g_app.asyncBlitCount > 0) {
            sprintf(text, "Async Avg: %.2f ms/op", (float)g_app.asyncBlitTime / g_app.asyncBlitCount);
            TextOutA(hdc, 10, y, text, strlen(text)); y += 20;
        }
        
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        SelectObject(hdc, pen);
        
        for (int x = 0; x < g_app.width; x += 100) {
            MoveToEx(hdc, x, 0, NULL);
            LineTo(hdc, x, g_app.height);
        }
        
        for (int y = 0; y < g_app.height; y += 100) {
            MoveToEx(hdc, 0, y, NULL);
            LineTo(hdc, g_app.width, y);
        }
        
        DeleteObject(pen);
        g_app.backbuffer->ReleaseDC(hdc);
    }
}

// ============================================================================
// Рендеринг кадра
// ============================================================================

void RenderFrame() {
    if (!g_app.primary || !g_app.backbuffer) return;
    
    DrawTestPattern();
    
    RECT client;
    GetClientRect(g_app.hwnd, &client);
    
    HRESULT hr = g_app.primary->Blt(&client, g_app.backbuffer, NULL, DDBLT_WAIT, NULL);
    if (FAILED(hr)) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            printf("RenderFrame: Blt failed 0x%08lX\n", hr);
        }
    }
    
    g_app.frameCount++;
    DWORD currentTime = GetTickCount();
    if (currentTime - g_app.lastTime >= 1000) {
        printf("FPS: %d, Async: %d/%d\n", g_app.frameCount, g_app.completedOperations, g_app.asyncOperations);
        g_app.frameCount = 0;
        g_app.lastTime = currentTime;
    }
}

// ============================================================================
// Очистка
// ============================================================================

void Cleanup() {
    printf("\n=== Cleaning up ===\n");
    
    for (int i = 0; i < 4; i++) {
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
    printf("DDVK - Command Stream Test\n");
    printf("========================================\n\n");
    printf("This test demonstrates command processing\n");
    printf("through multiple operation types.\n\n");
    printf("Controls:\n");
    printf("  1 - Run SYNC blit test (100 operations)\n");
    printf("  2 - Run ASYNC blit test (100 operations)\n");
    printf("  3 - Run BATCH blit test (50 operations)\n");
    printf("  4 - Run MIXED operations test\n");
    printf("  5 - Run STRESS test (1000 operations)\n");
    printf("  R - Reset statistics\n");
    printf("  ESC - Exit\n\n");
    
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
    
    srand(GetTickCount());
    g_app.running = true;
    
    printf("=== Entering main loop ===\n\n");
    
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