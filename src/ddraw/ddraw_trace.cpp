// Lightweight tracing-only DirectDraw DLL.
// Purpose: log which DirectDraw/IDirectDraw7 calls a game uses,
// without implementing any real graphics functionality.

#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

// Simple file logger local to this DLL.
static FILE* g_log = nullptr;

static void TraceLog(const char* fmt, ...)
{
    if (!g_log) {
        g_log = std::fopen("ddraw_trace.log", "a");
        if (!g_log) {
            return;
        }
        std::fprintf(g_log, "==== ddraw_trace session start ====\n");
    }

    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log, fmt, args);
    std::fprintf(g_log, "\n");
    std::fflush(g_log);
    va_end(args);
}

//==============================================================================
// Trivial IDirectDraw7 implementation that only logs calls.
//==============================================================================

struct TraceDirectDraw7 {
    IDirectDraw7Vtbl* lpVtbl;
    ULONG             refCount;
};

static HRESULT WINAPI TraceDD7_QueryInterface(
    IDirectDraw7* This,
    REFIID /*riid*/,
    LPVOID* ppvObj)
{
    TraceLog("IDirectDraw7::QueryInterface");
    if (!ppvObj) return E_POINTER;
    *ppvObj = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG WINAPI TraceDD7_AddRef(IDirectDraw7* This)
{
    auto self = reinterpret_cast<TraceDirectDraw7*>(This);
    return ++self->refCount;
}

static ULONG WINAPI TraceDD7_Release(IDirectDraw7* This)
{
    auto self = reinterpret_cast<TraceDirectDraw7*>(This);
    ULONG ref = --self->refCount;
    if (ref == 0) {
        TraceLog("IDirectDraw7::Release -> delete");
        delete self;
    }
    return ref;
}

// --- IDirectDraw core methods (all stubs that only log) ----------------------

static HRESULT WINAPI TraceDD7_Compact(IDirectDraw7* /*This*/)
{
    TraceLog("IDirectDraw7::Compact");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_CreateClipper(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    LPDIRECTDRAWCLIPPER* lplpDDClipper,
    IUnknown* /*pUnkOuter*/)
{
    TraceLog("IDirectDraw7::CreateClipper");
    if (lplpDDClipper) *lplpDDClipper = nullptr;
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_CreatePalette(
    IDirectDraw7* /*This*/,
    DWORD dwFlags,
    LPPALETTEENTRY /*lpColorTable*/,
    LPDIRECTDRAWPALETTE* lplpDDPalette,
    IUnknown* /*pUnkOuter*/)
{
    TraceLog("IDirectDraw7::CreatePalette flags=0x%08lX", (unsigned long)dwFlags);
    if (lplpDDPalette) *lplpDDPalette = nullptr;
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_CreateSurface(
    IDirectDraw7* /*This*/,
    LPDDSURFACEDESC2 lpDDSurfaceDesc2,
    LPDIRECTDRAWSURFACE7* lplpDDSurface,
    IUnknown* /*pUnkOuter*/)
{
    if (!lpDDSurfaceDesc2 || !lplpDDSurface) return DDERR_INVALIDPARAMS;
    TraceLog("IDirectDraw7::CreateSurface caps=0x%08lX flags=0x%08lX %lux%lu bpp=%lu",
             (unsigned long)lpDDSurfaceDesc2->ddsCaps.dwCaps,
             (unsigned long)lpDDSurfaceDesc2->dwFlags,
             (unsigned long)lpDDSurfaceDesc2->dwWidth,
             (unsigned long)lpDDSurfaceDesc2->dwHeight,
             (unsigned long)lpDDSurfaceDesc2->ddpfPixelFormat.dwRGBBitCount);
    *lplpDDSurface = nullptr;
    // Возвращаем ошибку, чтобы игра явно показала, как реагирует.
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_DuplicateSurface(
    IDirectDraw7* /*This*/,
    LPDIRECTDRAWSURFACE7 /*lpDDSrc*/,
    LPDIRECTDRAWSURFACE7* /*lplpDDDst*/)
{
    TraceLog("IDirectDraw7::DuplicateSurface");
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_EnumDisplayModes(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    LPDDSURFACEDESC2 /*lpDDSurfaceDesc2*/,
    LPVOID /*lpContext*/,
    LPDDENUMMODESCALLBACK2 /*lpEnumModesCallback*/)
{
    TraceLog("IDirectDraw7::EnumDisplayModes");
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_EnumSurfaces(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    LPDDSURFACEDESC2 /*lpDDSD2*/,
    LPVOID /*lpContext*/,
    LPDDENUMSURFACESCALLBACK7 /*lpEnumSurfacesCallback*/)
{
    TraceLog("IDirectDraw7::EnumSurfaces");
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_FlipToGDISurface(IDirectDraw7* /*This*/)
{
    TraceLog("IDirectDraw7::FlipToGDISurface");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetCaps(
    IDirectDraw7* /*This*/,
    LPDDCAPS lpDDDriverCaps,
    LPDDCAPS lpDDHELCaps)
{
    TraceLog("IDirectDraw7::GetCaps");
    if (lpDDDriverCaps) std::memset(lpDDDriverCaps, 0, sizeof(DDCAPS));
    if (lpDDHELCaps)    std::memset(lpDDHELCaps, 0, sizeof(DDCAPS));
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetDisplayMode(
    IDirectDraw7* /*This*/,
    LPDDSURFACEDESC2 lpDDSurfaceDesc2)
{
    TraceLog("IDirectDraw7::GetDisplayMode");
    if (lpDDSurfaceDesc2) std::memset(lpDDSurfaceDesc2, 0, sizeof(DDSURFACEDESC2));
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetFourCCCodes(
    IDirectDraw7* /*This*/,
    LPDWORD lpNumCodes,
    LPDWORD /*lpCodes*/)
{
    TraceLog("IDirectDraw7::GetFourCCCodes");
    if (lpNumCodes) *lpNumCodes = 0;
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetGDISurface(
    IDirectDraw7* /*This*/,
    LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface)
{
    TraceLog("IDirectDraw7::GetGDISurface");
    if (lplpGDIDDSSurface) *lplpGDIDDSSurface = nullptr;
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI TraceDD7_GetMonitorFrequency(
    IDirectDraw7* /*This*/,
    LPDWORD lpdwFreq)
{
    TraceLog("IDirectDraw7::GetMonitorFrequency");
    if (lpdwFreq) *lpdwFreq = 60;
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetScanLine(
    IDirectDraw7* /*This*/,
    LPDWORD lpdwScanLine)
{
    TraceLog("IDirectDraw7::GetScanLine");
    if (!lpdwScanLine) return DDERR_INVALIDPARAMS;
    *lpdwScanLine = 0;
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetVerticalBlankStatus(
    IDirectDraw7* /*This*/,
    LPBOOL lpInVB)
{
    TraceLog("IDirectDraw7::GetVerticalBlankStatus");
    if (!lpInVB) return DDERR_INVALIDPARAMS;
    *lpInVB = FALSE;
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_Initialize(
    IDirectDraw7* /*This*/,
    GUID* /*lpGUID*/)
{
    TraceLog("IDirectDraw7::Initialize");
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI TraceDD7_RestoreDisplayMode(IDirectDraw7* /*This*/)
{
    TraceLog("IDirectDraw7::RestoreDisplayMode");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_SetCooperativeLevel(
    IDirectDraw7* /*This*/,
    HWND hWnd,
    DWORD dwFlags)
{
    TraceLog("IDirectDraw7::SetCooperativeLevel hwnd=%p flags=0x%08lX",
             (void*)hWnd, (unsigned long)dwFlags);
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_SetDisplayMode(
    IDirectDraw7* /*This*/,
    DWORD dwWidth,
    DWORD dwHeight,
    DWORD dwBPP,
    DWORD dwRefreshRate,
    DWORD dwFlags)
{
    TraceLog("IDirectDraw7::SetDisplayMode %lux%lu bpp=%lu rr=%lu flags=0x%08lX",
             (unsigned long)dwWidth,
             (unsigned long)dwHeight,
             (unsigned long)dwBPP,
             (unsigned long)dwRefreshRate,
             (unsigned long)dwFlags);
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_WaitForVerticalBlank(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    HANDLE /*hEvent*/)
{
    TraceLog("IDirectDraw7::WaitForVerticalBlank");
    return DD_OK;
}

// v2/v4/7 additions

static HRESULT WINAPI TraceDD7_GetAvailableVidMem(
    IDirectDraw7* /*This*/,
    LPDDSCAPS2 /*lpDDSCaps2*/,
    LPDWORD lpdwTotal,
    LPDWORD lpdwFree)
{
    TraceLog("IDirectDraw7::GetAvailableVidMem");
    if (lpdwTotal) *lpdwTotal = 256 * 1024 * 1024;
    if (lpdwFree)  *lpdwFree  = 128 * 1024 * 1024;
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetSurfaceFromDC(
    IDirectDraw7* /*This*/,
    HDC /*hdc*/,
    LPDIRECTDRAWSURFACE7* /*lpDDS*/)
{
    TraceLog("IDirectDraw7::GetSurfaceFromDC");
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI TraceDD7_RestoreAllSurfaces(IDirectDraw7* /*This*/)
{
    TraceLog("IDirectDraw7::RestoreAllSurfaces");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_TestCooperativeLevel(IDirectDraw7* /*This*/)
{
    TraceLog("IDirectDraw7::TestCooperativeLevel");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_GetDeviceIdentifier(
    IDirectDraw7* /*This*/,
    LPDDDEVICEIDENTIFIER2 lpDDDI,
    DWORD /*dwFlags*/)
{
    TraceLog("IDirectDraw7::GetDeviceIdentifier");
    if (!lpDDDI) return DDERR_INVALIDPARAMS;
    std::memset(lpDDDI, 0, sizeof(DDDEVICEIDENTIFIER2));
    std::snprintf(lpDDDI->szDriver, sizeof(lpDDDI->szDriver), "ddraw_trace");
    std::snprintf(lpDDDI->szDescription, sizeof(lpDDDI->szDescription), "DDRAW Trace Logger");
    return DD_OK;
}

static HRESULT WINAPI TraceDD7_StartModeTest(
    IDirectDraw7* /*This*/,
    LPSIZE /*lpModesToTest*/,
    DWORD dwNumEntries,
    DWORD dwFlags)
{
    TraceLog("IDirectDraw7::StartModeTest entries=%lu flags=0x%08lX",
             (unsigned long)dwNumEntries, (unsigned long)dwFlags);
    return DDERR_TESTFINISHED;
}

static HRESULT WINAPI TraceDD7_EvaluateMode(
    IDirectDraw7* /*This*/,
    DWORD dwFlags,
    DWORD* /*pdwTimeout*/)
{
    TraceLog("IDirectDraw7::EvaluateMode flags=0x%08lX", (unsigned long)dwFlags);
    return DDERR_TESTFINISHED;
}

// Global vtable for our trace object.
static IDirectDraw7Vtbl g_traceDD7Vtbl = {
    // IUnknown
    TraceDD7_QueryInterface,
    TraceDD7_AddRef,
    TraceDD7_Release,
    // IDirectDraw
    TraceDD7_Compact,
    TraceDD7_CreateClipper,
    TraceDD7_CreatePalette,
    TraceDD7_CreateSurface,
    TraceDD7_DuplicateSurface,
    TraceDD7_EnumDisplayModes,
    TraceDD7_EnumSurfaces,
    TraceDD7_FlipToGDISurface,
    TraceDD7_GetCaps,
    TraceDD7_GetDisplayMode,
    TraceDD7_GetFourCCCodes,
    TraceDD7_GetGDISurface,
    TraceDD7_GetMonitorFrequency,
    TraceDD7_GetScanLine,
    TraceDD7_GetVerticalBlankStatus,
    TraceDD7_Initialize,
    TraceDD7_RestoreDisplayMode,
    TraceDD7_SetCooperativeLevel,
    TraceDD7_SetDisplayMode,
    TraceDD7_WaitForVerticalBlank,
    // v2/v4/v7 additions
    TraceDD7_GetAvailableVidMem,
    TraceDD7_GetSurfaceFromDC,
    TraceDD7_RestoreAllSurfaces,
    TraceDD7_TestCooperativeLevel,
    TraceDD7_GetDeviceIdentifier,
    TraceDD7_StartModeTest,
    TraceDD7_EvaluateMode
};

//==============================================================================
// Exported DirectDraw entry points
//==============================================================================

extern "C" HRESULT WINAPI DirectDrawCreateEx(
    GUID* /*lpGUID*/,
    LPVOID* lplpDD,
    REFIID /*iid*/,
    IUnknown* /*pUnkOuter*/)
{
    if (!lplpDD) return DDERR_INVALIDPARAMS;

    TraceLog("DirectDrawCreateEx called");

    auto* obj = new TraceDirectDraw7();
    if (!obj) return DDERR_OUTOFMEMORY;

    obj->lpVtbl = &g_traceDD7Vtbl;
    obj->refCount = 1;

    *lplpDD = obj;
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawCreate(
    GUID* lpGUID,
    LPDIRECTDRAW* lplpDD,
    IUnknown* pUnkOuter)
{
    TraceLog("DirectDrawCreate called");
    LPDIRECTDRAW7 dd7 = nullptr;
    HRESULT hr = DirectDrawCreateEx(lpGUID, (LPVOID*)&dd7, IID_IDirectDraw7, pUnkOuter);
    if (FAILED(hr)) return hr;
    if (lplpDD) {
        *lplpDD = (LPDIRECTDRAW)dd7;
    }
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawCreateClipper(
    DWORD dwFlags,
    LPDIRECTDRAWCLIPPER* lplpDDClipper,
    IUnknown* /*pUnkOuter*/)
{
    TraceLog("DirectDrawCreateClipper flags=0x%08lX", (unsigned long)dwFlags);
    if (lplpDDClipper) *lplpDDClipper = nullptr;
    return DDERR_UNSUPPORTED;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateA(
    LPDDENUMCALLBACKA lpCallback,
    LPVOID lpContext)
{
    TraceLog("DirectDrawEnumerateA called");
    if (!lpCallback) return DDERR_INVALIDPARAMS;
    // Сообщаем об одном "виртуальном" устройстве.
    if (!lpCallback(nullptr,
                    const_cast<char*>("DDRAW TRACE WRAPPER"),
                    const_cast<char*>("Trace Primary Display"),
                    lpContext)) {
        return DD_OK;
    }
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateExA(
    LPDDENUMCALLBACKEXA lpCallback,
    LPVOID lpContext,
    DWORD dwFlags)
{
    TraceLog("DirectDrawEnumerateExA called flags=0x%08lX", (unsigned long)dwFlags);
    if (!lpCallback) return DDERR_INVALIDPARAMS;
    if (!lpCallback(nullptr,
                    const_cast<char*>("DDRAW TRACE WRAPPER"),
                    const_cast<char*>("Trace Primary Display"),
                    lpContext,
                    nullptr)) {
        return DD_OK;
    }
    return DD_OK;
}

