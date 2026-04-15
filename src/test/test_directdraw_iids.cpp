#define CINTERFACE
#define COBJMACROS
#define INITGUID

#include <windows.h>
#include <ddraw.h>

// Тест: вызывает DirectDrawCreateEx с разными IID и проверяет,
// что возвращается DD_OK и ненулевой указатель.

static void ShowFail(const wchar_t* title, HRESULT hr) {
    wchar_t buf[256];
    wsprintfW(buf, L"%s: 0x%08lx", title, hr);
    MessageBoxW(nullptr, buf, L"DDRAW IID TEST", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    struct Case {
        const IID* iid;
        const wchar_t* name;
    } cases[] = {
        { &IID_IDirectDraw,  L"IID_IDirectDraw" },
        { &IID_IDirectDraw2, L"IID_IDirectDraw2" },
        { &IID_IDirectDraw4, L"IID_IDirectDraw4" },
        { &IID_IDirectDraw7, L"IID_IDirectDraw7" },
    };

    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
        void* p = nullptr;
        HRESULT hr = DirectDrawCreateEx(nullptr, &p, *cases[i].iid, nullptr);
        if (FAILED(hr) || !p) {
            ShowFail(cases[i].name, hr);
            return 0;
        }
        // Освобождаем через IUnknown, чтобы не зависеть от конкретной версии
        IUnknown* unk = (IUnknown*)p;
        IUnknown_Release(unk);
    }

    MessageBoxW(nullptr,
                L"DirectDrawCreateEx успешно отработал для IID_IDirectDraw/2/4/7.",
                L"DDRAW IID TEST",
                MB_OK | MB_ICONINFORMATION);
    return 0;
}

