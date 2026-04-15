#define CINTERFACE
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>

// Минимальное приложение, которое проверяет, что ddraw.dll
// действительно экспортирует и выполняет DirectDrawCreateEx.

static void ShowError(const wchar_t* title, HRESULT hr) {
    wchar_t buf[256];
    wsprintfW(buf, L"%s: 0x%08lx", title, hr);
    MessageBoxW(nullptr, buf, L"DDRAW DLL TEST", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DDrawDllTestWindow";

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassExW failed", L"DDRAW DLL TEST", MB_OK | MB_ICONERROR);
        return 0;
    }

    HWND hwnd = CreateWindowExW(
        0,
        L"DDrawDllTestWindow",
        L"DDraw DLL Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        320, 240,
        nullptr, nullptr,
        hInstance, nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindowExW failed", L"DDRAW DLL TEST", MB_OK | MB_ICONERROR);
        return 0;
    }

    LPDIRECTDRAW7 dd = nullptr;
    HRESULT hr = DirectDrawCreateEx(nullptr, (void**)&dd, IID_IDirectDraw7, nullptr);
    if (FAILED(hr)) {
        ShowError(L"DirectDrawCreateEx failed", hr);
        return 0;
    }

    hr = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    if (FAILED(hr)) {
        ShowError(L"SetCooperativeLevel failed", hr);
        IDirectDraw7_Release(dd);
        return 0;
    }

    MessageBoxW(hwnd, L"DirectDrawCreateEx + SetCooperativeLevel succeeded.\n"
                      L"ddraw.dll работает корректно.",
                L"DDRAW DLL TEST", MB_OK | MB_ICONINFORMATION);

    IDirectDraw7_Release(dd);
    return 0;
}

