#include "ddraw.h"
#include "ddraw_surface.h"
#include "ddraw_palette.h"
#include "ddraw_clipper.h"
#include "ddraw_utils.h"
#include "../ddvk/ddvk_renderer.h"
#include "../ddvk/ddvk_core.h"
#include "../ddvk/ddvk_utils.h"
#include <cstdio>
#include <cstddef>
#include <cstring>

/** Заглушка при E_NOINTERFACE из QueryInterface(IDirectDraw7); lpVtbl задаётся в InitDDraw7Vtbl(). */
IDirectDraw7 s_dummyDD7;
IDirectDraw s_dummyDD1;

static ddvk::VulkanRenderer* GetRenderer() {
    return ddvk::GetVulkanRenderer();
}

static void SafeReleaseRenderer() {
    ddvk::ReleaseVulkanRenderer();
}

//=============================================================================
// Вспомогательные функции для доступа к реализации
//=============================================================================

// Минимальный разумный указатель (объекты в куче); 0x10 и т.п. — часто хэндл/мусор → page fault.
static constexpr uintptr_t kMinValidPtr = 0x10000u;

static ddvk::DirectDrawImpl* GetDDrawImpl(IDirectDraw7* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) return nullptr;
    return reinterpret_cast<ddvk::DirectDrawImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static ddvk::SurfaceImpl* GetSurfaceImpl(IDirectDrawSurface7* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) return nullptr;
    return reinterpret_cast<ddvk::SurfaceImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static ddvk::PaletteImpl* GetPaletteImpl(IDirectDrawPalette* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) return nullptr;
    return reinterpret_cast<ddvk::PaletteImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static ddvk::ClipperImpl* GetClipperImpl(IDirectDrawClipper* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) return nullptr;
    return reinterpret_cast<ddvk::ClipperImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static const char* DDrawIidName(REFIID riid) {
    if (IsEqualIID(riid, IID_IDirectDraw)) return "IDirectDraw";
    if (IsEqualIID(riid, IID_IDirectDraw2)) return "IDirectDraw2";
    if (IsEqualIID(riid, IID_IDirectDraw4)) return "IDirectDraw4";
    if (IsEqualIID(riid, IID_IDirectDraw7)) return "IDirectDraw7";
    if (IsEqualIID(riid, IID_IUnknown)) return "IUnknown";
    return "Other";
}

namespace {
struct SurfaceWrapper {
    IDirectDrawSurface7 iface;  /* offset 0x00: lpVtbl (4 bytes) */
    ddvk::SurfaceImpl* impl;    /* offset 0x04 */
    DWORD reserved[6];          /* offset 0x08..0x1f */
    const void* vtblAt20; /* offset 0x20: game reads vtable here after Release */
};

static_assert(offsetof(SurfaceWrapper, impl) == sizeof(void*), "surface impl offset changed");
static_assert(offsetof(SurfaceWrapper, vtblAt20) == 0x20, "vtblAt20 must be at offset 0x20");

static IDirectDrawSurface7* InitSurfaceWrapper(char* mem, ddvk::SurfaceImpl* impl) {
    std::memset(mem, 0, sizeof(SurfaceWrapper));
    auto* wrap = reinterpret_cast<SurfaceWrapper*>(mem);
    wrap->iface.lpVtbl = const_cast<IDirectDrawSurface7Vtbl*>(&ddrawSurfaceVtbl);
    wrap->impl = impl;
    wrap->vtblAt20 = &ddrawSurfaceVtbl; /* keep valid vtable at +0x20 so game can call via it after stale ref */
    return &wrap->iface;
}
}

extern "C" {
volatile DWORD g_ddvk_lastCreatedSurfaceWrapper = 0;
volatile DWORD g_ddvk_prevCreatedSurfaceWrapper = 0;
/* Массив всех созданных surface в текущем процессе (для отладки stale pointers). */
volatile DWORD g_ddvk_allCreatedSurfaces[32] = {0};
volatile DWORD g_ddvk_allCreatedSurfaceCount = 0;
}

extern "C" void DDVK_SetSurfaceCompatVtable(IDirectDrawSurface7* surface, bool legacyV1);

//=============================================================================
// Функции создания объектов
//=============================================================================

static IDirectDraw7* CreateDirectDrawObject(ddvk::DirectDrawImpl* impl) {
    size_t totalSize = sizeof(void*) + sizeof(ddvk::DirectDrawImpl);
    char* mem = new char[totalSize];
    
    IDirectDraw7* obj = reinterpret_cast<IDirectDraw7*>(mem);
    obj->lpVtbl = nullptr;
    
    ddvk::DirectDrawImpl* implPtr = reinterpret_cast<ddvk::DirectDrawImpl*>(
        mem + sizeof(void*));
    
    if (impl) {
        memcpy(implPtr, impl, sizeof(ddvk::DirectDrawImpl));
    } else {
        new (implPtr) ddvk::DirectDrawImpl(GetRenderer());
    }
    return obj;
}

static bool IsLegacyDDrawV1Vtable(const void* vtbl);
static void SetDDrawCompatVtable(IDirectDraw7* obj, bool legacyV1);

extern "C" volatile DWORD g_ddvk_allCreatedSurfaces[32];
extern "C" volatile DWORD g_ddvk_allCreatedSurfaceCount;

static IDirectDrawSurface7* CreateSurfaceObject(ddvk::SurfaceImpl* impl) {
    if (!impl) return nullptr;
    size_t totalSize = sizeof(SurfaceWrapper);
    char* mem = new char[totalSize];
    IDirectDrawSurface7* obj = InitSurfaceWrapper(mem, impl);
    g_ddvk_prevCreatedSurfaceWrapper = g_ddvk_lastCreatedSurfaceWrapper;
    g_ddvk_lastCreatedSurfaceWrapper = (DWORD)(uintptr_t)obj;
    /* Добавляем в массив всех созданных surface. */
    DWORD idx = InterlockedIncrement((volatile LONG*)&g_ddvk_allCreatedSurfaceCount) - 1;
    if (idx < 32) g_ddvk_allCreatedSurfaces[idx] = (DWORD)(uintptr_t)obj;

    return obj;
}

IDirectDrawSurface7* DDrawWrapSurface(ddvk::SurfaceImpl* impl) {
    if (!impl) return nullptr;
    size_t totalSize = sizeof(SurfaceWrapper);
    char* mem = new char[totalSize];
    IDirectDrawSurface7* obj = InitSurfaceWrapper(mem, impl);
    impl->userData = obj;
    g_ddvk_prevCreatedSurfaceWrapper = g_ddvk_lastCreatedSurfaceWrapper;
    g_ddvk_lastCreatedSurfaceWrapper = (DWORD)(uintptr_t)obj;
    return obj;
}

static IDirectDrawPalette* CreatePaletteObject(DWORD flags, const PALETTEENTRY* entries) {
    size_t totalSize = sizeof(void*) + sizeof(ddvk::PaletteImpl);
    char* mem = new char[totalSize];
    
    IDirectDrawPalette* obj = reinterpret_cast<IDirectDrawPalette*>(mem);
    obj->lpVtbl = &ddrawPaletteVtbl;
    
    ddvk::PaletteImpl* implPtr = reinterpret_cast<ddvk::PaletteImpl*>(
        mem + sizeof(void*));
    new (implPtr) ddvk::PaletteImpl(flags, entries);
    return obj;
}

static IDirectDrawClipper* CreateClipperObject(ddvk::ClipperImpl* impl) {
    size_t totalSize = sizeof(void*) + sizeof(ddvk::ClipperImpl);
    char* mem = new char[totalSize];
    
    IDirectDrawClipper* obj = reinterpret_cast<IDirectDrawClipper*>(mem);
    obj->lpVtbl = &ddrawClipperVtbl;
    
    ddvk::ClipperImpl* implPtr = reinterpret_cast<ddvk::ClipperImpl*>(
        mem + sizeof(void*));
    
    if (impl) {
        memcpy(implPtr, impl, sizeof(ddvk::ClipperImpl));
    } else {
        new (implPtr) ddvk::ClipperImpl();
    }
    return obj;
}

//=============================================================================
// IDirectDraw7 implementation - основные методы
//=============================================================================

static HRESULT WINAPI IDirectDraw7_QueryInterface(
    IDirectDraw7* This,
    REFIID riid,
    LPVOID* ppvObj)
{
    if (!This || !ppvObj) return DDERR_INVALIDPARAMS;
    *ppvObj = &s_dummyDD7; /* при любой ошибке — заглушка */
    auto impl = GetDDrawImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    if (IsEqualIID(riid, IID_IDirectDraw7) ||
        IsEqualIID(riid, IID_IDirectDraw4) ||
        IsEqualIID(riid, IID_IDirectDraw2) ||
        IsEqualIID(riid, IID_IDirectDraw)  ||
        IsEqualIID(riid, IID_IUnknown)) {
        impl->AddRef();
        SetDDrawCompatVtable(This, IsEqualIID(riid, IID_IDirectDraw));
        *ppvObj = This;
        return DD_OK;
    }

    /* Игра может не проверять код и вызывать по *ppvObj → заглушка вместо nullptr (page fault 0x10). */
    *ppvObj = &s_dummyDD7;
    return E_NOINTERFACE;
}

//=============================================================================
// Заглушки/обёртки для остальных методов IDirectDraw7
//=============================================================================

static HRESULT WINAPI IDirectDraw7_Compact(IDirectDraw7* /*This*/)
{
    // Память управляется самим приложением/Vulkan, compact ни на что не влияет.
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_DuplicateSurface(
    IDirectDraw7* /*This*/,
    LPDIRECTDRAWSURFACE7 /*lpDDSrc*/,
    LPDIRECTDRAWSURFACE7* /*lplpDDDst*/)
{
    // Пока не требуется реальными примерами/играми; безопасная заглушка.
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDraw7_EnumDisplayModes(
    IDirectDraw7* /*This*/,
    DWORD dwFlags,
    LPDDSURFACEDESC2 lpDDSurfaceDesc2,
    LPVOID lpContext,
    LPDDENUMMODESCALLBACK2 lpEnumModesCallback)
{
    if (!lpEnumModesCallback) return DDERR_INVALIDPARAMS;
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;

    auto modes = renderer->GetSupportedModes();
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;

    for (const auto& mode : modes) {
        if (lpDDSurfaceDesc2) {
            if ((lpDDSurfaceDesc2->dwFlags & DDSD_WIDTH) && lpDDSurfaceDesc2->dwWidth != mode.width) continue;
            if ((lpDDSurfaceDesc2->dwFlags & DDSD_HEIGHT) && lpDDSurfaceDesc2->dwHeight != mode.height) continue;
            if ((lpDDSurfaceDesc2->dwFlags & DDSD_PIXELFORMAT) &&
                lpDDSurfaceDesc2->ddpfPixelFormat.dwRGBBitCount != mode.bpp) continue;
        }
        desc.dwWidth = mode.width;
        desc.dwHeight = mode.height;
        desc.dwRefreshRate = mode.refreshRate;
        desc.ddpfPixelFormat.dwRGBBitCount = mode.bpp;
        if (mode.bpp == 16) {
            desc.ddpfPixelFormat.dwRBitMask = 0xF800;
            desc.ddpfPixelFormat.dwGBitMask = 0x07E0;
            desc.ddpfPixelFormat.dwBBitMask = 0x001F;
        } else if (mode.bpp == 32) {
            desc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
            desc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
            desc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
        }
        HRESULT hr = lpEnumModesCallback(&desc, lpContext);
        if (hr == DDENUMRET_CANCEL) return DD_OK;
        if (FAILED(hr)) return hr;
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_EnumSurfaces(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    LPDDSURFACEDESC2 /*lpDDSD2*/,
    LPVOID /*lpContext*/,
    LPDDENUMSURFACESCALLBACK7 /*lpEnumSurfacesCallback*/)
{
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDraw7_FlipToGDISurface(IDirectDraw7* /*This*/)
{
    auto renderer = GetRenderer();
    if (renderer) {
        renderer->FlipToGDISurface();
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_GetFourCCCodes(
    IDirectDraw7* /*This*/,
    LPDWORD lpNumCodes,
    LPDWORD lpCodes)
{
    if (lpNumCodes) {
        *lpNumCodes = 0;
    }
    // Кодеки не поддерживаются — возвращаем 0 кодов.
    (void)lpCodes;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_GetGDISurface(
    IDirectDraw7* /*This*/,
    LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface)
{
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    *lplpGDIDDSSurface = &s_dummySurfaceNoAttach; /* при ошибке игра не получит nullptr */
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_INVALIDPARAMS;
    HRESULT hr = renderer->GetGDISurface(lplpGDIDDSSurface);
    if (FAILED(hr)) *lplpGDIDDSSurface = &s_dummySurfaceNoAttach;
    return hr;
}

static HRESULT WINAPI IDirectDraw7_GetMonitorFrequency(
    IDirectDraw7* /*This*/,
    LPDWORD lpdwFreq)
{
    if (lpdwFreq) {
        *lpdwFreq = 60;
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_GetScanLine(
    IDirectDraw7* /*This*/,
    LPDWORD lpdwScanLine)
{
    if (!lpdwScanLine) return DDERR_INVALIDPARAMS;
    // Точной строки не знаем — возвращаем 0 и DD_OK, как делают многие wrapper'ы.
    *lpdwScanLine = 0;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_GetVerticalBlankStatus(
    IDirectDraw7* /*This*/,
    LPBOOL lpInVB)
{
    auto renderer = GetRenderer();
    if (!renderer || !lpInVB) return DDERR_INVALIDPARAMS;
    *lpInVB = renderer->IsVerticalBlank() ? TRUE : FALSE;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_Initialize(
    IDirectDraw7* /*This*/,
    GUID* /*lpGUID*/)
{
    // Мы не поддерживаем повторную инициализацию существующего объекта.
    return DDERR_ALREADYINITIALIZED;
}

static ULONG WINAPI IDirectDraw7_AddRef(IDirectDraw7* This) {
    if (This == &s_dummyDD7) return 1;
    auto impl = GetDDrawImpl(This);
    return impl ? impl->AddRef() : 0;
}

extern "C" volatile DWORD g_ddvk_ddCreateCount = 0;
extern "C" volatile DWORD g_ddvk_ddReleaseCount = 0;

static ULONG WINAPI IDirectDraw7_Release(IDirectDraw7* This) {
    if (This == &s_dummyDD7) return 0;
    auto impl = GetDDrawImpl(This);
    if (!impl) return 0;

    ULONG ref = impl->Release();
    DWORD cnt = InterlockedIncrement((volatile LONG*)&g_ddvk_ddReleaseCount);
    if (ref == 0) {
        delete[] reinterpret_cast<char*>(This);
        SafeReleaseRenderer();
    }
    return ref;
}

static HRESULT WINAPI IDirectDraw7_CreatePalette(
    IDirectDraw7* This,
    DWORD dwFlags,
    LPPALETTEENTRY lpColorTable,
    LPDIRECTDRAWPALETTE* lplpDDPalette,
    IUnknown* pUnkOuter)
{
    (void)This;
    if (!lplpDDPalette) return DDERR_INVALIDPARAMS;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    IDirectDrawPalette* obj = CreatePaletteObject(dwFlags, lpColorTable);
    if (!obj) return DDERR_OUTOFMEMORY;
    *lplpDDPalette = obj;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_CreateSurface(
    IDirectDraw7* This,
    LPDDSURFACEDESC2 lpDDSurfaceDesc2,
    LPDIRECTDRAWSURFACE7* lplpDDSurface,
    IUnknown* pUnkOuter)
{
    if (!lpDDSurfaceDesc2 || !lplpDDSurface) return DDERR_INVALIDPARAMS;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!This) return DDERR_INVALIDOBJECT;
    auto ddImpl = GetDDrawImpl(This);
    if (!ddImpl) return DDERR_INVALIDOBJECT;

    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;

    DDSURFACEDESC2 desc2;
    if (lpDDSurfaceDesc2->dwSize == sizeof(DDSURFACEDESC)) {
        LPDDSURFACEDESC pDesc1 = (LPDDSURFACEDESC)lpDDSurfaceDesc2;
        std::memset(&desc2, 0, sizeof(desc2));
        desc2.dwSize = sizeof(desc2);
        desc2.dwFlags = pDesc1->dwFlags;
        desc2.dwHeight = pDesc1->dwHeight;
        desc2.dwWidth = pDesc1->dwWidth;
        desc2.dwBackBufferCount = pDesc1->dwBackBufferCount;
        desc2.dwAlphaBitDepth = pDesc1->dwAlphaBitDepth;
        desc2.dwReserved = pDesc1->dwReserved;
        desc2.lpSurface = pDesc1->lpSurface;
        desc2.ddckCKDestOverlay = pDesc1->ddckCKDestOverlay;
        desc2.ddckCKDestBlt = pDesc1->ddckCKDestBlt;
        desc2.ddckCKSrcOverlay = pDesc1->ddckCKSrcOverlay;
        desc2.ddckCKSrcBlt = pDesc1->ddckCKSrcBlt;
        desc2.ddpfPixelFormat = pDesc1->ddpfPixelFormat;
        desc2.ddsCaps.dwCaps = pDesc1->ddsCaps.dwCaps;
        desc2.ddsCaps.dwCaps2 = 0;
        desc2.ddsCaps.dwCaps3 = 0;
        desc2.ddsCaps.dwCaps4 = 0;
        lpDDSurfaceDesc2 = &desc2;
    }

    *lplpDDSurface = &s_dummySurfaceNoAttach; /* при ошибке игра не получит nullptr */
    auto surface = renderer->CreateSurface(*lpDDSurfaceDesc2);
    if (!surface) {
        return DDERR_OUTOFVIDEOMEMORY;
    }

    surface->parentDD = This;
    if (surface->backBuffer)
        surface->backBuffer->parentDD = This;

    auto obj = CreateSurfaceObject(surface);
    if (!obj) {
        renderer->DestroySurface(surface);
        return DDERR_OUTOFMEMORY;
    }
    DDVK_SetSurfaceCompatVtable(obj, IsLegacyDDrawV1Vtable(This ? This->lpVtbl : nullptr));

    surface->userData = obj;

    *lplpDDSurface = obj;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_SetCooperativeLevel(
    IDirectDraw7* This,
    HWND hWnd,
    DWORD dwFlags)
{
    auto impl = GetDDrawImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    impl->hwnd = hWnd;
    impl->cooperativeLevel = dwFlags;

    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;

    return renderer->SetCooperativeLevel(hWnd, dwFlags);
}

static HRESULT WINAPI IDirectDraw7_SetDisplayMode(
    IDirectDraw7* This,
    DWORD dwWidth,
    DWORD dwHeight,
    DWORD dwBPP,
    DWORD dwRefreshRate,
    DWORD dwFlags)
{
    auto impl = GetDDrawImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;

    impl->currentMode = ddvk::DisplayMode(dwWidth, dwHeight, dwBPP, dwRefreshRate);

    return renderer->SetDisplayMode(dwWidth, dwHeight, dwBPP, dwRefreshRate, dwFlags);
}

static HRESULT WINAPI IDirectDraw1_SetDisplayMode(
    IDirectDraw* This,
    DWORD dwWidth,
    DWORD dwHeight,
    DWORD dwBPP)
{
    return IDirectDraw7_SetDisplayMode(reinterpret_cast<IDirectDraw7*>(This), dwWidth, dwHeight, dwBPP, 0, 0);
}

static HRESULT WINAPI IDirectDraw7_GetDisplayMode(
    IDirectDraw7* /*This*/,
    LPDDSURFACEDESC2 lpDDSurfaceDesc2)
{
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;
    return renderer->GetDisplayMode(lpDDSurfaceDesc2);
}

static HRESULT WINAPI IDirectDraw7_RestoreDisplayMode(
    IDirectDraw7* /*This*/)
{
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;
    return renderer->RestoreDisplayMode();
}

static HRESULT WINAPI IDirectDraw7_GetAvailableVidMem(
    IDirectDraw7* /*This*/,
    LPDDSCAPS2 lpDDSCaps2,
    LPDWORD lpdwTotal,
    LPDWORD lpdwFree)
{
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;
    return renderer->GetAvailableVidMem(lpDDSCaps2, lpdwTotal, lpdwFree);
}

static HRESULT WINAPI IDirectDraw7_GetSurfaceFromDC(
    IDirectDraw7* /*This*/,
    HDC hdc,
    LPDIRECTDRAWSURFACE7* lpDDS)
{
    if (lpDDS) *lpDDS = &s_dummySurfaceNoAttach; /* при ошибке игра не получит nullptr */
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI DDVK_IDirectDraw7_TestCooperativeLevel(IDirectDraw7* /*This*/)
{
    // Для большинства игр достаточно сообщить, что всё в порядке.
    return DD_OK;
}

static HRESULT WINAPI DDVK_IDirectDraw7_GetDeviceIdentifier(
    IDirectDraw7* /*This*/,
    LPDDDEVICEIDENTIFIER2 lpDDDI,
    DWORD dwFlags)
{
    if (!lpDDDI) return DDERR_INVALIDPARAMS;
    std::memset(lpDDDI, 0, sizeof(DDDEVICEIDENTIFIER2));
    // Заполняем минимально необходимую информацию.
    std::snprintf(lpDDDI->szDriver, sizeof(lpDDDI->szDriver), "ddvk");
    std::snprintf(lpDDDI->szDescription, sizeof(lpDDDI->szDescription), "DDVK DirectDraw Wrapper");
    (void)dwFlags;
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_RestoreAllSurfaces(
    IDirectDraw7* /*This*/)
{
    // В Vulkan-реализации потери surface'ов не моделируем, поэтому
    // можно безопасно вернуть DD_OK.
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_WaitForVerticalBlank(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    HANDLE /*hEvent*/)
{
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;
    renderer->WaitForVerticalBlank();
    return DD_OK;
}

static HRESULT WINAPI IDirectDraw7_GetCaps(
    IDirectDraw7* /*This*/,
    LPDDCAPS lpDDDriverCaps,
    LPDDCAPS lpDDHELCaps)
{
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;
    return renderer->GetCaps(lpDDDriverCaps, lpDDHELCaps);
}

static HRESULT WINAPI IDirectDraw7_CreateClipper(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    LPDIRECTDRAWCLIPPER* lplpDDClipper,
    IUnknown* /*pUnkOuter*/)
{
    if (!lplpDDClipper) return DDERR_INVALIDPARAMS;

    auto impl = new ddvk::ClipperImpl();
    if (!impl) return DDERR_OUTOFMEMORY;

    auto obj = CreateClipperObject(impl);
    if (!obj) {
        delete impl;
        return DDERR_OUTOFMEMORY;
    }

    impl->userData = obj;
    *lplpDDClipper = obj;
    return DD_OK;
}

//=============================================================================
// IDirectDraw7 extension methods (DX7): StartModeTest / EvaluateMode
//=============================================================================

static HRESULT WINAPI DDVK_IDirectDraw7_StartModeTest(
    IDirectDraw7* /*This*/,
    LPSIZE /*lpModesToTest*/,
    DWORD /*dwNumEntries*/,
    DWORD dwFlags)
{
    // Простейшая эмуляция для примера ModeTest:
    // если приложение спрашивает, нужен ли тест (DDSMT_ISTESTREQUIRED),
    // сообщаем, что монитор не предоставляет EDID‑информацию, и тест
    // выполняться не будет. Это корректно обрабатывается ModeTest.
    if (dwFlags & DDSMT_ISTESTREQUIRED) {
        return DDERR_NOMONITORINFORMATION;
    }
    // При обычном вызове считаем, что тест уже завершён.
    return DDERR_TESTFINISHED;
}

static HRESULT WINAPI DDVK_IDirectDraw7_EvaluateMode(
    IDirectDraw7* /*This*/,
    DWORD /*dwFlags*/,
    DWORD* /*pdwTimeout*/)
{
    // Поскольку StartModeTest сообщает, что тест не требуется/уже завершён,
    // EvaluateMode никогда не должен вызываться, но на всякий случай
    // возвращаем DDERR_TESTFINISHED.
    return DDERR_TESTFINISHED;
}

// Vtable для IDirectDraw7. Инициализируется лениво, чтобы избежать
// строгих проверок типов в списке инициализации.
IDirectDrawVtbl ddrawVtbl = {};
IDirectDraw7Vtbl ddraw7Vtbl = {};

static void InitDDraw7Vtbl() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // IUnknown
    ddraw7Vtbl.QueryInterface          = IDirectDraw7_QueryInterface;
    ddraw7Vtbl.AddRef                  = IDirectDraw7_AddRef;
    ddraw7Vtbl.Release                 = IDirectDraw7_Release;
    // IDirectDraw
    ddraw7Vtbl.Compact                 = IDirectDraw7_Compact;
    ddraw7Vtbl.CreateClipper           = IDirectDraw7_CreateClipper;
    ddraw7Vtbl.CreatePalette           = IDirectDraw7_CreatePalette;
    ddraw7Vtbl.CreateSurface           = IDirectDraw7_CreateSurface;
    ddraw7Vtbl.DuplicateSurface        = IDirectDraw7_DuplicateSurface;
    ddraw7Vtbl.EnumDisplayModes        = IDirectDraw7_EnumDisplayModes;
    ddraw7Vtbl.EnumSurfaces            = IDirectDraw7_EnumSurfaces;
    ddraw7Vtbl.FlipToGDISurface        = IDirectDraw7_FlipToGDISurface;
    ddraw7Vtbl.GetCaps                 = IDirectDraw7_GetCaps;
    ddraw7Vtbl.GetDisplayMode          = IDirectDraw7_GetDisplayMode;
    ddraw7Vtbl.GetFourCCCodes          = IDirectDraw7_GetFourCCCodes;
    ddraw7Vtbl.GetGDISurface           = IDirectDraw7_GetGDISurface;
    ddraw7Vtbl.GetMonitorFrequency     = IDirectDraw7_GetMonitorFrequency;
    ddraw7Vtbl.GetScanLine             = IDirectDraw7_GetScanLine;
    ddraw7Vtbl.GetVerticalBlankStatus  = IDirectDraw7_GetVerticalBlankStatus;
    ddraw7Vtbl.Initialize              = IDirectDraw7_Initialize;
    ddraw7Vtbl.RestoreDisplayMode      = IDirectDraw7_RestoreDisplayMode;
    ddraw7Vtbl.SetCooperativeLevel     = IDirectDraw7_SetCooperativeLevel;
    ddraw7Vtbl.SetDisplayMode          = IDirectDraw7_SetDisplayMode;
    ddraw7Vtbl.WaitForVerticalBlank    = IDirectDraw7_WaitForVerticalBlank;
    // v2/v4/7 additions
    ddraw7Vtbl.GetAvailableVidMem      = IDirectDraw7_GetAvailableVidMem;
    ddraw7Vtbl.GetSurfaceFromDC        = IDirectDraw7_GetSurfaceFromDC;
    ddraw7Vtbl.RestoreAllSurfaces      = IDirectDraw7_RestoreAllSurfaces;
    ddraw7Vtbl.TestCooperativeLevel    = DDVK_IDirectDraw7_TestCooperativeLevel;
    ddraw7Vtbl.GetDeviceIdentifier     = DDVK_IDirectDraw7_GetDeviceIdentifier;
    ddraw7Vtbl.StartModeTest           = DDVK_IDirectDraw7_StartModeTest;
    ddraw7Vtbl.EvaluateMode            = DDVK_IDirectDraw7_EvaluateMode;
    s_dummyDD7.lpVtbl = &ddraw7Vtbl;

    ddrawVtbl.QueryInterface         = reinterpret_cast<decltype(ddrawVtbl.QueryInterface)>(IDirectDraw7_QueryInterface);
    ddrawVtbl.AddRef                 = reinterpret_cast<decltype(ddrawVtbl.AddRef)>(IDirectDraw7_AddRef);
    ddrawVtbl.Release                = reinterpret_cast<decltype(ddrawVtbl.Release)>(IDirectDraw7_Release);
    ddrawVtbl.Compact                = reinterpret_cast<decltype(ddrawVtbl.Compact)>(IDirectDraw7_Compact);
    ddrawVtbl.CreateClipper          = reinterpret_cast<decltype(ddrawVtbl.CreateClipper)>(IDirectDraw7_CreateClipper);
    ddrawVtbl.CreatePalette          = reinterpret_cast<decltype(ddrawVtbl.CreatePalette)>(IDirectDraw7_CreatePalette);
    ddrawVtbl.CreateSurface          = reinterpret_cast<decltype(ddrawVtbl.CreateSurface)>(IDirectDraw7_CreateSurface);
    ddrawVtbl.DuplicateSurface       = reinterpret_cast<decltype(ddrawVtbl.DuplicateSurface)>(IDirectDraw7_DuplicateSurface);
    ddrawVtbl.EnumDisplayModes       = reinterpret_cast<decltype(ddrawVtbl.EnumDisplayModes)>(IDirectDraw7_EnumDisplayModes);
    ddrawVtbl.EnumSurfaces           = reinterpret_cast<decltype(ddrawVtbl.EnumSurfaces)>(IDirectDraw7_EnumSurfaces);
    ddrawVtbl.FlipToGDISurface       = reinterpret_cast<decltype(ddrawVtbl.FlipToGDISurface)>(IDirectDraw7_FlipToGDISurface);
    ddrawVtbl.GetCaps                = reinterpret_cast<decltype(ddrawVtbl.GetCaps)>(IDirectDraw7_GetCaps);
    ddrawVtbl.GetDisplayMode         = reinterpret_cast<decltype(ddrawVtbl.GetDisplayMode)>(IDirectDraw7_GetDisplayMode);
    ddrawVtbl.GetFourCCCodes         = reinterpret_cast<decltype(ddrawVtbl.GetFourCCCodes)>(IDirectDraw7_GetFourCCCodes);
    ddrawVtbl.GetGDISurface          = reinterpret_cast<decltype(ddrawVtbl.GetGDISurface)>(IDirectDraw7_GetGDISurface);
    ddrawVtbl.GetMonitorFrequency    = reinterpret_cast<decltype(ddrawVtbl.GetMonitorFrequency)>(IDirectDraw7_GetMonitorFrequency);
    ddrawVtbl.GetScanLine            = reinterpret_cast<decltype(ddrawVtbl.GetScanLine)>(IDirectDraw7_GetScanLine);
    ddrawVtbl.GetVerticalBlankStatus = reinterpret_cast<decltype(ddrawVtbl.GetVerticalBlankStatus)>(IDirectDraw7_GetVerticalBlankStatus);
    ddrawVtbl.Initialize             = reinterpret_cast<decltype(ddrawVtbl.Initialize)>(IDirectDraw7_Initialize);
    ddrawVtbl.RestoreDisplayMode     = reinterpret_cast<decltype(ddrawVtbl.RestoreDisplayMode)>(IDirectDraw7_RestoreDisplayMode);
    ddrawVtbl.SetCooperativeLevel    = reinterpret_cast<decltype(ddrawVtbl.SetCooperativeLevel)>(IDirectDraw7_SetCooperativeLevel);
    ddrawVtbl.SetDisplayMode         = IDirectDraw1_SetDisplayMode;
    ddrawVtbl.WaitForVerticalBlank   = reinterpret_cast<decltype(ddrawVtbl.WaitForVerticalBlank)>(IDirectDraw7_WaitForVerticalBlank);
    s_dummyDD1.lpVtbl = &ddrawVtbl;
}

//=============================================================================
// Глобальные функции DirectDraw
//=============================================================================

HRESULT WINAPI DirectDrawCreate(
    LPGUID lpGUID,
    LPDIRECTDRAW* lplpDD,
    IUnknown* pUnkOuter)
{
    LPDIRECTDRAW7 lpDD7 = nullptr;
    HRESULT hr = DirectDrawCreateEx(lpGUID, (LPVOID*)&lpDD7, IID_IDirectDraw, pUnkOuter);
    
    if (SUCCEEDED(hr) && lplpDD) {
        *lplpDD = (LPDIRECTDRAW)lpDD7;
    }
    
    return hr;
}

extern "C" volatile DWORD g_ddvk_ddCreateCount;

HRESULT WINAPI DirectDrawCreateEx(
    LPGUID lpGUID,
    LPVOID* lplpDD,
    REFIID iid,
    IUnknown* pUnkOuter)
{
    if (!lplpDD) return DDERR_INVALIDPARAMS;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    
    // Поддерживаем несколько версий интерфейса:
    // IID_IDirectDraw, IID_IDirectDraw2, IID_IDirectDraw4, IID_IDirectDraw7.
    // Все они совместимы по префиксу vtable, поэтому возвращаем один
    // и тот же объект IDirectDraw7 для любого из этих IID.
    // Если придёт какой-то совсем чужой IID, по спецификации DirectDraw
    // корректно всё равно вернуть объект, большинство приложений этого ждут.
    
    auto renderer = GetRenderer();
    if (!renderer) return DDERR_OUTOFMEMORY;

    // Маркер для проверки загрузки нашей DLL (план «DDVK load and Dune 2000 fix»):
    // если в каталоге игры появился ddvk_loaded.txt — Wine использует нашу ddraw.
    {
        std::FILE* f = std::fopen("ddvk_loaded.txt", "w");
        if (f) {
            std::fprintf(f, "DDVK ddraw loaded\n");
            std::fclose(f);
        }
    }

    auto impl = new ddvk::DirectDrawImpl(renderer);
    if (!impl) return DDERR_OUTOFMEMORY;
    
    InitDDraw7Vtbl();

    auto obj = CreateDirectDrawObject(impl);
    if (!obj) {
        delete impl;
        return DDERR_OUTOFMEMORY;
    }
    
    SetDDrawCompatVtable(obj, IsEqualIID(iid, IID_IDirectDraw));
    *lplpDD = obj;

    return DD_OK;
}

static bool IsLegacyDDrawV1Vtable(const void* vtbl) {
    return vtbl == &ddrawVtbl;
}

static void SetDDrawCompatVtable(IDirectDraw7* obj, bool legacyV1) {
    if (!obj) return;
    obj->lpVtbl = legacyV1 ? reinterpret_cast<IDirectDraw7Vtbl*>(&ddrawVtbl) : &ddraw7Vtbl;
}

HRESULT WINAPI DirectDrawCreateClipper(
    DWORD dwFlags,
    LPDIRECTDRAWCLIPPER* lplpDDClipper,
    IUnknown* pUnkOuter)
{
    if (!lplpDDClipper) return DDERR_INVALIDPARAMS;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    
    auto clipper = new ddvk::ClipperImpl();
    if (!clipper) return DDERR_OUTOFMEMORY;
    
    auto obj = CreateClipperObject(clipper);
    if (!obj) {
        delete clipper;
        return DDERR_OUTOFMEMORY;
    }
    
    *lplpDDClipper = obj;
    return DD_OK;
}

//=============================================================================
// DirectDrawEnumerate / DirectDrawEnumerateEx (ANSI)
//=============================================================================

HRESULT WINAPI DirectDrawEnumerateA(
    LPDDENUMCALLBACKA lpCallback,
    LPVOID lpContext)
{
    if (!lpCallback) return DDERR_INVALIDPARAMS;

    // Минимальная реализация: одна "виртуальная" видеокарта через wrapper.
    if (!lpCallback(
            nullptr,
            (LPSTR)"DDVK DirectDraw Wrapper",
            (LPSTR)"Primary Display",
            lpContext)) {
        return DD_OK;
    }

    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExA(
    LPDDENUMCALLBACKEXA lpCallback,
    LPVOID lpContext,
    DWORD dwFlags)
{
    if (!lpCallback) return DDERR_INVALIDPARAMS;

    // Аналогично, сообщаем об одном логическом устройстве.
    if (!lpCallback(
            nullptr,
            (LPSTR)"DDVK DirectDraw Wrapper",
            (LPSTR)"Primary Display",
            lpContext,
            nullptr)) { // HMONITOR
        return DD_OK;
    }

    return DD_OK;
}