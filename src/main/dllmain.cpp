#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnt.h>
#include <timeapi.h>
#include <cstring>
#include <cstdio>
#include <ctime>

extern "C" const void* GetDDrawSurfaceVtblForNullThis(void);
extern "C" const void* GetDDrawFreedSurfaceVtbl(void);
extern "C" struct IDirectDrawSurface7* GetDDrawDummyFreedSurface(void);
extern "C" volatile DWORD g_ddvk_lastReleasedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_prevReleasedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_prev2ReleasedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_prev3ReleasedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_lastCreatedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_prevCreatedSurfaceWrapper;
extern "C" volatile DWORD g_ddvk_allCreatedSurfaces[32];
extern "C" volatile DWORD g_ddvk_allCreatedSurfaceCount;
extern "C" volatile DWORD g_ddvk_freedSurfaceCallCount;

// Stubs for null-this vtable calls: return 0 (S_OK or refcount 0), stdcall pop N bytes.
#if defined(_WIN64)
#error "64-bit stubs not implemented"
#else
__attribute__((naked)) static void NullStub_Ret0(void)  { __asm__ volatile ("xorl %%eax, %%eax\n\tret"     ::: "eax"); }
__attribute__((naked)) static void NullStub_Ret4(void)  { __asm__ volatile ("xorl %%eax, %%eax\n\tret $4"  ::: "eax"); }
__attribute__((naked)) static void NullStub_Ret8(void)  { __asm__ volatile ("xorl %%eax, %%eax\n\tret $8"  ::: "eax"); }
__attribute__((naked)) static void NullStub_Ret12(void) { __asm__ volatile ("xorl %%eax, %%eax\n\tret $12" ::: "eax"); }
#endif

// Простой DllMain для ddraw.dll на базе DDVK.
// Здесь можно будет добавить инициализацию/деинициализацию глобального состояния,
// логирования и т.п., если это потребуется.

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Отключаем нотификации о создании/завершении потоков — они нам не нужны.
        DisableThreadLibraryCalls(hinstDLL);
        // Set 1ms timer resolution so sleep_for is accurate (default is 15.625ms on Windows).
        timeBeginPeriod(1);
        break;
    case DLL_PROCESS_DETACH:
        timeEndPeriod(1);
        // Здесь можно было бы вызвать ddvk::ReleaseVulkanRenderer(),
        // но в текущей архитектуре это уже делается через DirectDraw объекты.
        break;
    default:
        break;
    }

    return TRUE;
}

