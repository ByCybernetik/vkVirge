#include "ddraw_surface.h"
#include "ddraw_palette.h"
#include "ddraw_utils.h"
#include "ddraw.h"
#include "../ddvk/ddvk_core.h"
#include "../ddvk/ddvk_renderer.h"
#include "../ddvk/ddvk_utils.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>

//=============================================================================
// Вспомогательные функции для получения реализации
//=============================================================================

static constexpr uintptr_t kMinValidPtr = 0x10000u;
static IDirectDrawSurface s_dummySurfaceV1;

extern "C" {
volatile DWORD g_ddvk_lastReleasedSurfaceWrapper = 0;
volatile DWORD g_ddvk_prevReleasedSurfaceWrapper = 0;
volatile DWORD g_ddvk_prev2ReleasedSurfaceWrapper = 0;
volatile DWORD g_ddvk_prev3ReleasedSurfaceWrapper = 0;
}

static ddvk::SurfaceImpl* GetSurfaceImpl(IDirectDrawSurface7* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) return nullptr;
    // Обёртка: [vtable][SurfaceImpl*] — указатель на оригинал
    return *reinterpret_cast<ddvk::SurfaceImpl**>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

/** Поверхность ещё в m_surfaces (не освобождена; после Release primary back buffer невалиден). */
static bool IsSurfaceImplValid(ddvk::SurfaceImpl* impl) {
    return impl && (!impl->renderer || impl->renderer->IsValidSurface(impl));
}

/** Обёртка уже освобождена в BeginFrame — не разыменовывать This (use-after-free). */
static bool IsSurfaceWrapperFreed(const void* This) {
    if (!This) return false;
    auto* r = ddvk::GetVulkanRenderer();
    return r && r->IsWrapperFreed(This);
}

/* Проверка, был ли указатель создан в этом процессе (защита от "зомби" указателей). */
extern "C" volatile DWORD g_ddvk_allCreatedSurfaces[32];
extern "C" volatile DWORD g_ddvk_allCreatedSurfaceCount;
static bool IsCreatedInThisProcess(const void* ptr) {
    if (!ptr) return false;
    DWORD p = (DWORD)(uintptr_t)ptr;
    DWORD cnt = g_ddvk_allCreatedSurfaceCount;
    if (cnt > 32) cnt = 32;
    for (DWORD i = 0; i < cnt; ++i) {
        if (g_ddvk_allCreatedSurfaces[i] == p) return true;
    }
    return false;
}

struct SurfaceWrapperCompatShadow {
    IDirectDrawSurface7 iface;
    ddvk::SurfaceImpl* impl;
    DWORD reserved[6];
    const void* vtblAt20;
};

static_assert(offsetof(SurfaceWrapperCompatShadow, vtblAt20) == 0x20, "surface compat shadow offset changed");

extern "C" void DDVK_SetSurfaceCompatVtable(IDirectDrawSurface7* surface, bool legacyV1);

/** Безопасно получить impl: через рендерер по указателю обёртки, без разыменования обёртки (защита от use-after-free после DestroySurface back buffer). */
static ddvk::SurfaceImpl* GetSurfaceImplSafe(IDirectDrawSurface7* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr) {
        return nullptr;
    }
    if (IsSurfaceWrapperFreed(This)) return nullptr;
    auto* r = ddvk::GetVulkanRenderer();
    if (r) {
        ddvk::SurfaceImpl* impl = r->GetSurfaceByWrapper(This);
        if (impl) return impl;
        /* surface destroyed (e.g. back buffer with primary), do not read wrapper */
        return nullptr;
    }
    ddvk::SurfaceImpl* impl = GetSurfaceImpl(This);
    return (impl && IsSurfaceImplValid(impl)) ? impl : nullptr;
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

static const char* SurfaceIidName(REFIID riid) {
    if (IsEqualIID(riid, IID_IDirectDrawSurface)) return "IDirectDrawSurface";
    if (IsEqualIID(riid, IID_IDirectDrawSurface2)) return "IDirectDrawSurface2";
    if (IsEqualIID(riid, IID_IDirectDrawSurface3)) return "IDirectDrawSurface3";
    if (IsEqualIID(riid, IID_IDirectDrawSurface4)) return "IDirectDrawSurface4";
    if (IsEqualIID(riid, IID_IDirectDrawSurface7)) return "IDirectDrawSurface7";
    if (IsEqualIID(riid, IID_IUnknown)) return "IUnknown";
    return "Other";
}

//=============================================================================
// IDirectDrawSurface7 implementation
//=============================================================================

static HRESULT WINAPI IDirectDrawSurface7_QueryInterface(
    IDirectDrawSurface7* This,
    REFIID riid,
    LPVOID* ppvObj)
{
    if (!This || !ppvObj) return DDERR_INVALIDPARAMS;
    *ppvObj = &s_dummySurfaceNoAttach; /* при любой ошибке — заглушка, не nullptr */
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    if (IsEqualIID(riid, IID_IDirectDrawSurface7) ||
        IsEqualIID(riid, IID_IDirectDrawSurface)  ||
        IsEqualIID(riid, IID_IDirectDrawSurface2) ||
        IsEqualIID(riid, IID_IDirectDrawSurface3) ||
        IsEqualIID(riid, IID_IDirectDrawSurface4) ||
        IsEqualIID(riid, IID_IUnknown)) {
        /* Проверяем, не является ли This "зомби" указателем из предыдущего состояния. */
        if (!IsCreatedInThisProcess(This)) {
            return DDERR_INVALIDOBJECT;
        }
        impl->AddRef();
        DDVK_SetSurfaceCompatVtable(This, IsEqualIID(riid, IID_IDirectDrawSurface));
        *ppvObj = This;
        return DD_OK;
    }

    /* Игра может не проверять код и вызывать по *ppvObj → не отдавать nullptr (защита от page fault 0x10). */
    *ppvObj = &s_dummySurfaceNoAttach;
    return E_NOINTERFACE;
}

static ULONG WINAPI IDirectDrawSurface7_AddRef(IDirectDrawSurface7* This) {
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return 0;
    return impl->AddRef();
}

static ULONG WINAPI IDirectDrawSurface7_Release(IDirectDrawSurface7* This) {
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) {
        return 0;
    }

    ULONG ref = --impl->refCount;
    if (ref == 0) {
        g_ddvk_prev3ReleasedSurfaceWrapper = g_ddvk_prev2ReleasedSurfaceWrapper;
        g_ddvk_prev2ReleasedSurfaceWrapper = g_ddvk_prevReleasedSurfaceWrapper;
        g_ddvk_prevReleasedSurfaceWrapper = g_ddvk_lastReleasedSurfaceWrapper;
        g_ddvk_lastReleasedSurfaceWrapper = (DWORD)(uintptr_t)This;
        if (impl->renderer) {
            if (impl->isPrimary)
                impl->renderer->FlushDeferredPresent();
            impl->renderer->DestroySurfaceByUserData(This);
            /* Заменяем vtable на "freed" vtable перед отложенным освобождением:
             * если игра использует stale указатель, она получит DDERR_SURFACELOST вместо краша. */
            This->lpVtbl = (IDirectDrawSurface7Vtbl*)GetDDrawFreedSurfaceVtbl();
            impl->renderer->DeferFreeWrapper(reinterpret_cast<char*>(This));
        } else {
            delete[] reinterpret_cast<char*>(This);
        }
    }
    return ref;
}

static HRESULT WINAPI IDirectDrawSurface7_AddAttachedSurface(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_AddOverlayDirtyRect(
    IDirectDrawSurface7* This,
    LPRECT lpRect)
{
    /* Fault-at-0 handler can inject our vtable so game calls us with This=0; return success to avoid crash. */
    if (!This || reinterpret_cast<uintptr_t>(This) < kMinValidPtr)
        return DD_OK;
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_Blt(
    IDirectDrawSurface7* This,
    LPRECT lpDestRect,
    LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect,
    DWORD dwFlags,
    LPDDBLTFX lpDDBltFx)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    if (impl->renderer) impl->renderer->FlushDeferredPresent();

    // Получаем srcImpl и выполняем Blt под замком: не разыменовываем lpDDSrcSurface вне замка (use-after-free
    // после Release источника в другом потоке или при реентрантности во FlushDeferredPresent).
    HRESULT hr;
    if (impl->renderer) {
        hr = impl->renderer->ExecuteUnderSurfaceLock([&]() -> HRESULT {
            ddvk::SurfaceImpl* srcImpl = lpDDSrcSurface ? impl->renderer->GetSurfaceByWrapperUnderLock(lpDDSrcSurface) : nullptr;
            if (lpDDSrcSurface && !srcImpl) return DDERR_INVALIDOBJECT;
            if (srcImpl && !impl->renderer->IsValidSurface(srcImpl)) return DDERR_INVALIDOBJECT;
            if (srcImpl && srcImpl->mappedData && impl->renderer->IsDeferredBuffer(srcImpl->mappedData)) return DDERR_SURFACELOST;
            if (!lpDDSrcSurface && (dwFlags & DDBLT_COLORFILL) && lpDDBltFx) {
                RECT rect = { 0, 0, (LONG)impl->width, (LONG)impl->height };
                if (lpDestRect) rect = *lpDestRect;
                impl->FillColor(lpDDBltFx->dwFillColor, &rect);
                return DD_OK;
            }
            return impl->Blt(lpDestRect, srcImpl, lpSrcRect, dwFlags);
        });
    } else {
        auto srcImpl = lpDDSrcSurface ? GetSurfaceImpl(lpDDSrcSurface) : nullptr;
        if (srcImpl && !IsSurfaceImplValid(srcImpl)) srcImpl = nullptr;
        if (!lpDDSrcSurface && (dwFlags & DDBLT_COLORFILL) && lpDDBltFx) {
            RECT rect = { 0, 0, (LONG)impl->width, (LONG)impl->height };
            if (lpDestRect) rect = *lpDestRect;
            impl->FillColor(lpDDBltFx->dwFillColor, &rect);
            hr = DD_OK;
        } else {
            hr = impl->Blt(lpDestRect, srcImpl, lpSrcRect, dwFlags);
        }
    }

    if (SUCCEEDED(hr) && impl->isPrimary && impl->renderer)
        impl->renderer->RequestDeferredPresent();
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_BltBatch(
    IDirectDrawSurface7* This,
    LPDDBLTBATCH lpDDBltBatch,
    DWORD dwCount,
    DWORD dwFlags)
{
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_BltFast(
    IDirectDrawSurface7* This,
    DWORD dwX,
    DWORD dwY,
    LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect,
    DWORD dwTrans)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    ddvk::SurfaceImpl* srcImpl = nullptr;
    if (lpDDSrcSurface) {
        if (impl->renderer)
            srcImpl = impl->renderer->GetSurfaceByWrapper(lpDDSrcSurface);
        else
            srcImpl = GetSurfaceImpl(lpDDSrcSurface);
    }
    if (!srcImpl || !IsSurfaceImplValid(srcImpl)) return DDERR_INVALIDOBJECT;
    if (impl->renderer) impl->renderer->FlushDeferredPresent();

    RECT destRect;
    destRect.left = static_cast<LONG>(dwX);
    destRect.top = static_cast<LONG>(dwY);
    destRect.right = static_cast<LONG>(dwX + (lpSrcRect ? (lpSrcRect->right - lpSrcRect->left) : srcImpl->width));
    destRect.bottom = static_cast<LONG>(dwY + (lpSrcRect ? (lpSrcRect->bottom - lpSrcRect->top) : srcImpl->height));
    
    DWORD flags = 0;
    if (dwTrans & DDBLTFAST_SRCCOLORKEY) {
        flags |= DDBLT_KEYSRC;
    }
    HRESULT hr = impl->Blt(&destRect, srcImpl, lpSrcRect, flags);

    if (SUCCEEDED(hr) && impl->isPrimary && impl->renderer)
        impl->renderer->RequestDeferredPresent();
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_DeleteAttachedSurface(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_EnumAttachedSurfaces(
    IDirectDrawSurface7* This,
    LPVOID lpContext,
    LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_EnumOverlayZOrders(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPVOID lpContext,
    LPDDENUMSURFACESCALLBACK7 lpfnCallback)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Flip(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride,
    DWORD dwFlags)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (impl->renderer) impl->renderer->FlushDeferredPresent();

    ddvk::SurfaceImpl* targetImpl = nullptr;
    if (lpDDSurfaceTargetOverride && impl->renderer)
        targetImpl = impl->renderer->GetSurfaceByWrapper(lpDDSurfaceTargetOverride);
    if (targetImpl && (targetImpl->renderer != impl->renderer || !IsSurfaceImplValid(targetImpl)))
        targetImpl = nullptr;
    if (!targetImpl && impl->backBuffer && impl->renderer) {
        targetImpl = impl->renderer->GetSurfaceByWrapper(reinterpret_cast<void*>(impl->backBuffer->userData));
        if (targetImpl && !IsSurfaceImplValid(targetImpl)) targetImpl = nullptr;
    }

    if (!impl->backBuffer && !targetImpl) return DDERR_INVALIDPARAMS;

    if (impl->backBuffer && impl->renderer) {
        impl->renderer->SwapPrimaryAndBackBuffer(impl->backBuffer);
    }
    HRESULT hr = targetImpl ? impl->Flip(targetImpl, dwFlags) : DD_OK;

    if (SUCCEEDED(hr) && impl->renderer) {
        impl->renderer->BeginFrame();
        impl->renderer->Present();

        // Если установлен DDFLIP_WAIT - ждём завершения GPU
        // Это важно для правильного тайминга в старых играх
        if (dwFlags & DDFLIP_WAIT) {
            impl->renderer->WaitForGpu();
        }
    }
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_GetAttachedSurface(
    IDirectDrawSurface7* This,
    LPDDSCAPS2 lpDDSCaps,
    LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface)
{
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    /* Игра может не проверять код возврата и вызывать метод по указателю → при ошибке отдаём заглушку вместо nullptr (защита от page fault 0x10). */
    *lplpDDAttachedSurface = &s_dummySurfaceNoAttach;
    if (!This) return DDERR_INVALIDOBJECT;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) {
        return DDERR_INVALIDOBJECT;
    }

    if (lpDDSCaps && (lpDDSCaps->dwCaps & DDSCAPS_BACKBUFFER) && impl->backBuffer) {
        ddvk::SurfaceImpl* back = impl->backBuffer;
        if (!IsSurfaceImplValid(back)) {
            return DDERR_NOTFOUND; /* back уже неявно освобождён с primary */
        }
        /* Проверяем, не является ли userData "зомби" указателем из предыдущего состояния. */
        if (back->userData && !IsCreatedInThisProcess(back->userData)) {
            back->userData = nullptr; /* сбрасываем zombie указатель, создадим новый */
        }
        if (!back->userData) {
            IDirectDrawSurface7* wrapped = DDrawWrapSurface(back);
            if (!wrapped) return DDERR_OUTOFMEMORY;
        }
        *lplpDDAttachedSurface = reinterpret_cast<IDirectDrawSurface7*>(back->userData);
        (*lplpDDAttachedSurface)->lpVtbl->AddRef(*lplpDDAttachedSurface);
        return DD_OK;
    }

    return DDERR_NOTFOUND;
}

static HRESULT WINAPI IDirectDrawSurface7_GetBltStatus(
    IDirectDrawSurface7* This,
    DWORD dwFlags)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetCaps(
    IDirectDrawSurface7* This,
    LPDDSCAPS2 lpDDSCaps)
{
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    lpDDSCaps->dwCaps = 0;
    
    if (impl->isPrimary) {
        lpDDSCaps->dwCaps |= DDSCAPS_PRIMARYSURFACE;
    }
    if (impl->isBackBuffer) {
        lpDDSCaps->dwCaps |= DDSCAPS_BACKBUFFER;
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetClipper(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWCLIPPER* lplpDDClipper)
{
    if (!lplpDDClipper) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    *lplpDDClipper = impl->clipper;
    if (*lplpDDClipper) {
        auto clipperImpl = GetClipperImpl(*lplpDDClipper);
        if (clipperImpl) clipperImpl->AddRef();
    }
    
    return *lplpDDClipper ? DD_OK : DDERR_NOCLIPPERATTACHED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetColorKey(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDDCOLORKEY lpDDColorKey)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    (void)dwFlags;
    // В Vulkan-реализации цветовой ключ пока не поддерживается, но для
    // совместимости с софтварным враппером корректнее возвращать
    // DDERR_NOCOLORKEY только при запросе конкретного ключа.
    if (!lpDDColorKey) return DDERR_INVALIDPARAMS;
    return DDERR_NOCOLORKEY;
}

static HRESULT WINAPI IDirectDrawSurface7_GetDC(
    IDirectDrawSurface7* This,
    HDC* lphDC)
{
    if (!lphDC) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    // Создаем DC для поверхности
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return DDERR_GENERIC;

    // Создаем DIB раздел
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = impl->width;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(impl->height); // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = static_cast<WORD>(impl->bpp);
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (!hbm || !bits) {
        DeleteDC(hdc);
        return DDERR_OUTOFMEMORY;
    }

    HRESULT copyHr = DD_OK;
    if (bits && impl->renderer) {
        copyHr = impl->renderer->ExecuteUnderSurfaceLock([&]() -> HRESULT {
            if (!impl->renderer->IsValidSurface(impl)) return DDERR_INVALIDOBJECT;
            if (!impl->mappedData || impl->renderer->IsDeferredBuffer(impl->mappedData)) return DDERR_SURFACELOST;
            size_t size = static_cast<size_t>(impl->width) * impl->height * (impl->bpp / 8);
            memcpy(bits, impl->mappedData, size);
            return DD_OK;
        });
    } else if (impl->mappedData && bits) {
        memcpy(bits, impl->mappedData, impl->width * impl->height * (impl->bpp / 8));
    }
    if (FAILED(copyHr)) {
        DeleteObject(hbm);
        DeleteDC(hdc);
        return copyHr;
    }

    SelectObject(hdc, hbm);

    *lphDC = hdc;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetFlipStatus(
    IDirectDrawSurface7* This,
    DWORD dwFlags)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetOverlayPosition(
    IDirectDrawSurface7* This,
    LPLONG lplX,
    LPLONG lplY)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPalette(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWPALETTE* lplpDDPalette)
{
    if (!lplpDDPalette) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    *lplpDDPalette = impl->palette;
    if (*lplpDDPalette) {
        auto paletteImpl = GetPaletteImpl(*lplpDDPalette);
        if (paletteImpl) paletteImpl->AddRef();
    }
    
    return *lplpDDPalette ? DD_OK : DDERR_NOPALETTEATTACHED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPixelFormat(
    IDirectDrawSurface7* This,
    LPDDPIXELFORMAT lpDDPixelFormat)
{
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    lpDDPixelFormat->dwSize = sizeof(DDPIXELFORMAT);
    lpDDPixelFormat->dwFlags = DDPF_RGB;
    lpDDPixelFormat->dwRGBBitCount = impl->bpp;
    
    if (impl->bpp == 16) {
        lpDDPixelFormat->dwRBitMask = 0xF800;
        lpDDPixelFormat->dwGBitMask = 0x07E0;
        lpDDPixelFormat->dwBBitMask = 0x001F;
    } else if (impl->bpp == 32) {
        lpDDPixelFormat->dwRBitMask = 0x00FF0000;
        lpDDPixelFormat->dwGBitMask = 0x0000FF00;
        lpDDPixelFormat->dwBBitMask = 0x000000FF;
    }
    
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetSurfaceDesc(
    IDirectDrawSurface7* This,
    LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    /* Game may pass DDSURFACEDESC when using old surface interface; never write more than 108 bytes unless sure. */
    if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC2)) {
        LPDDSURFACEDESC pOut = (LPDDSURFACEDESC)lpDDSurfaceDesc;
        std::memset(pOut, 0, sizeof(DDSURFACEDESC));
        pOut->dwSize = sizeof(DDSURFACEDESC);
        pOut->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_CAPS;
        pOut->dwWidth = impl->width;
        pOut->dwHeight = impl->height;
        pOut->lPitch = impl->width * (impl->bpp / 8);
        pOut->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        pOut->ddpfPixelFormat.dwFlags = DDPF_RGB;
        pOut->ddpfPixelFormat.dwRGBBitCount = impl->bpp;
        if (impl->bpp == 32) {
            pOut->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
            pOut->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
            pOut->ddpfPixelFormat.dwBBitMask = 0x000000FF;
        } else if (impl->bpp == 16) {
            pOut->ddpfPixelFormat.dwRBitMask = 0xF800;
            pOut->ddpfPixelFormat.dwGBitMask = 0x07E0;
            pOut->ddpfPixelFormat.dwBBitMask = 0x001F;
        }
        pOut->ddsCaps.dwCaps = 0;
        if (impl->isPrimary)   pOut->ddsCaps.dwCaps |= DDSCAPS_PRIMARYSURFACE;
        if (impl->isBackBuffer) pOut->ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
        return DD_OK;
    }

    std::memset(lpDDSurfaceDesc, 0, sizeof(DDSURFACEDESC2));
    lpDDSurfaceDesc->dwSize  = sizeof(DDSURFACEDESC2);
    lpDDSurfaceDesc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH |
                               DDSD_PIXELFORMAT | DDSD_CAPS;

    lpDDSurfaceDesc->dwWidth  = impl->width;
    lpDDSurfaceDesc->dwHeight = impl->height;
    lpDDSurfaceDesc->lPitch   = impl->width * (impl->bpp / 8);

    lpDDSurfaceDesc->ddpfPixelFormat.dwSize       = sizeof(DDPIXELFORMAT);
    lpDDSurfaceDesc->ddpfPixelFormat.dwFlags      = DDPF_RGB;
    lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = impl->bpp;
    if (impl->bpp == 32) {
        lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
        lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
        lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
    } else if (impl->bpp == 16) {
        lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0xF800;
        lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x07E0;
        lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x001F;
    }

    lpDDSurfaceDesc->ddsCaps.dwCaps = 0;
    if (impl->isPrimary)    lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_PRIMARYSURFACE;
    if (impl->isBackBuffer) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Initialize(
    IDirectDrawSurface7* This,
    LPDIRECTDRAW lpDD,
    LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI IDirectDrawSurface7_IsLost(IDirectDrawSurface7* This) {
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Lock(
    IDirectDrawSurface7* This,
    LPRECT lpDestRect,
    LPDDSURFACEDESC2 lpDDSurfaceDesc,
    DWORD dwFlags,
    HANDLE hEvent)
{
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) {
        return DDERR_INVALIDOBJECT;
    }

    /* Game may pass DDSURFACEDESC (108 bytes); writing 124 corrupts stack. */
    if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC2)) {
        DDSURFACEDESC2 desc2 = {};
        HRESULT hr = impl->Lock(lpDestRect, &desc2, dwFlags);
        if (FAILED(hr)) return hr;
        LPDDSURFACEDESC pOut = (LPDDSURFACEDESC)lpDDSurfaceDesc;
        std::memset(pOut, 0, sizeof(DDSURFACEDESC));
        pOut->dwSize = sizeof(DDSURFACEDESC);
        pOut->dwFlags = desc2.dwFlags;
        pOut->dwHeight = desc2.dwHeight;
        pOut->dwWidth = desc2.dwWidth;
        pOut->lPitch = desc2.lPitch;
        pOut->dwBackBufferCount = desc2.dwBackBufferCount;
        pOut->dwAlphaBitDepth = desc2.dwAlphaBitDepth;
        pOut->lpSurface = desc2.lpSurface;
        pOut->ddckCKDestOverlay = desc2.ddckCKDestOverlay;
        pOut->ddckCKDestBlt = desc2.ddckCKDestBlt;
        pOut->ddckCKSrcOverlay = desc2.ddckCKSrcOverlay;
        pOut->ddckCKSrcBlt = desc2.ddckCKSrcBlt;
        pOut->ddpfPixelFormat = desc2.ddpfPixelFormat;
        pOut->ddsCaps.dwCaps = desc2.ddsCaps.dwCaps;
        return DD_OK;
    }
    HRESULT hr = impl->Lock(lpDestRect, lpDDSurfaceDesc, dwFlags);
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_ReleaseDC(
    IDirectDrawSurface7* This,
    HDC hDC)
{
    if (!hDC) return DDERR_INVALIDPARAMS;
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (impl->renderer) impl->renderer->FlushDeferredPresent();
    // Получаем битмап из DC и сохраняем изменения
    HBITMAP hbm = static_cast<HBITMAP>(GetCurrentObject(hDC, OBJ_BITMAP));
    if (hbm) {
        DIBSECTION ds = {};
        if (GetObject(hbm, sizeof(ds), &ds) == sizeof(ds)) {
            if (impl && impl->mappedData) {
                memcpy(impl->mappedData, ds.dsBm.bmBits,
                       impl->width * impl->height * (impl->bpp / 8));
            }
        }
        // Удаляем DIBSection, чтобы не накапливать GDI-объекты
        DeleteObject(hbm);
    }

    DeleteDC(hDC);

    if (impl && impl->isPrimary && impl->renderer) {
        impl->renderer->BeginFrame();
        impl->renderer->Present();
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Restore(IDirectDrawSurface7* This) {
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetClipper(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWCLIPPER lpDDClipper)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    impl->clipper = lpDDClipper;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetColorKey(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDDCOLORKEY lpDDColorKey)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    if (!lpDDColorKey) {
        impl->hasColorKey = false;
        impl->colorKeyLow = impl->colorKeyHigh = 0;
        return DD_OK;
    }

    if (dwFlags & DDCKEY_SRCBLT) {
        impl->hasColorKey = true;
        impl->colorKeyLow = lpDDColorKey->dwColorSpaceLowValue;
        impl->colorKeyHigh = lpDDColorKey->dwColorSpaceHighValue;
    }
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetOverlayPosition(
    IDirectDrawSurface7* This,
    LONG lX,
    LONG lY)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPalette(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWPALETTE lpDDPalette)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    impl->palette = lpDDPalette;
    // Только наша палитра: иначе в Present разыменуем чужой указатель (H1).
    impl->paletteImpl = (lpDDPalette && lpDDPalette->lpVtbl == &ddrawPaletteVtbl)
        ? GetPaletteImpl(lpDDPalette) : nullptr;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Unlock(
    IDirectDrawSurface7* This,
    LPRECT lpRect)
{
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) {
        return DDERR_INVALIDOBJECT;
    }
    /* Do not call FlushDeferredPresent at Unlock entry: it would clear m_deferredPresent before direct Present below. */

    HRESULT hr = impl->Unlock(lpRect);
    if (SUCCEEDED(hr) && impl->renderer) {
        if (impl->isPrimary) {
            impl->renderer->BeginFrame();
            impl->renderer->Present();
            impl->renderer->ClearDeferredPresent();
        } else if (impl->isBackBuffer) {
            impl->renderer->SwapPrimaryAndBackBuffer(impl);
            impl->renderer->BeginFrame();
            impl->renderer->Present();
            impl->renderer->ClearDeferredPresent();
        } else {
            impl->renderer->BeginFrame();
            impl->renderer->Present();
            impl->renderer->ClearDeferredPresent();
        }
    }
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface1_Unlock(
    IDirectDrawSurface* This,
    LPVOID lpSurfaceData)
{
    return IDirectDrawSurface7_Unlock(reinterpret_cast<IDirectDrawSurface7*>(This), nullptr);
}

static HRESULT WINAPI IDirectDrawSurface7_UpdateOverlay(
    IDirectDrawSurface7* This,
    LPRECT lpSrcRect,
    LPDIRECTDRAWSURFACE7 lpDDDestSurface,
    LPRECT lpDestRect,
    DWORD dwFlags,
    LPDDOVERLAYFX lpDDOverlayFx)
{
    (void)lpDDOverlayFx;

    // Эмулируем overlay через обычный Blt в primary surface.
    // DDOverlay использует UpdateOverlay только с primary в качестве
    // целевой поверхности и флагами DDOVER_SHOW/DDOVER_HIDE.

    auto srcImpl = GetSurfaceImplSafe(This);
    if (!srcImpl) return DDERR_INVALIDOBJECT;

    if (!lpDDDestSurface) {
        // Нет целевой поверхности — ничего не рисуем, но не считаем ошибкой.
        return DD_OK;
    }

    ddvk::SurfaceImpl* dstImpl = nullptr;
    if (srcImpl->renderer)
        dstImpl = srcImpl->renderer->GetSurfaceByWrapper(lpDDDestSurface);
    if (!dstImpl)
        dstImpl = GetSurfaceImpl(lpDDDestSurface);
    if (!dstImpl || !IsSurfaceImplValid(dstImpl)) return DDERR_INVALIDOBJECT;

    // Обработка DDOVER_HIDE: просто не рисуем overlay.
    if (dwFlags & DDOVER_HIDE) {
        return DD_OK;
    }

    // Ожидаемый путь в примере: dst — primary surface.
    RECT srcRect{};
    RECT dstRect{};
    if (lpSrcRect) {
        srcRect = *lpSrcRect;
    } else {
        srcRect.left = 0;
        srcRect.top = 0;
        srcRect.right = (LONG)srcImpl->width;
        srcRect.bottom = (LONG)srcImpl->height;
    }
    if (lpDestRect) {
        dstRect = *lpDestRect;
    } else {
        dstRect.left = 0;
        dstRect.top = 0;
        dstRect.right = srcRect.right - srcRect.left;
        dstRect.bottom = srcRect.bottom - srcRect.top;
    }

    return dstImpl->Blt(&dstRect, srcImpl, &srcRect, 0);
}

static HRESULT WINAPI IDirectDrawSurface7_UpdateOverlayDisplay(
    IDirectDrawSurface7* This,
    DWORD dwFlags)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_UpdateOverlayZOrder(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDIRECTDRAWSURFACE7 lpDDSReference)
{
    if (!GetSurfaceImplSafe(This)) return DDERR_INVALIDOBJECT;
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetDDInterface(
    IDirectDrawSurface7* This,
    LPVOID* lplpDD)
{
    if (!lplpDD) return DDERR_INVALIDPARAMS;
    *lplpDD = &s_dummyDD7; /* never leave nullptr — app may use on error (page fault 0x10) */
    auto impl = GetSurfaceImplSafe(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (!impl->parentDD) return DDERR_UNSUPPORTED;
    *lplpDD = impl->parentDD;
    reinterpret_cast<IDirectDraw7*>(impl->parentDD)->lpVtbl->AddRef(
        reinterpret_cast<IDirectDraw7*>(impl->parentDD));
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_PageLock(
    IDirectDrawSurface7* This,
    DWORD dwFlags)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_PageUnlock(
    IDirectDrawSurface7* This,
    DWORD dwFlags)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetSurfaceDesc(
    IDirectDrawSurface7* This,
    LPDDSURFACEDESC2 lpddsd2,
    DWORD dwFlags)
{
    // Для совместимости с примерами DX7 (multimon и др.) достаточно
    // принимать SetSurfaceDesc как успешный no-op. Они уже скопировали
    // изображение в поверхность через GDI/Lock, а SetSurfaceDesc
    // используется только для оптимизации шаринга системной памяти.
    (void)This;
    (void)lpddsd2;
    (void)dwFlags;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag,
    LPVOID lpData,
    DWORD cbSize,
    DWORD dwFlags)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag,
    LPVOID lpBuffer,
    LPDWORD lpcbBufferSize)
{
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI IDirectDrawSurface7_FreePrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetUniquenessValue(
    IDirectDrawSurface7* This,
    LPDWORD lpValue)
{
    if (lpValue) *lpValue = reinterpret_cast<DWORD>(This);
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_ChangeUniquenessValue(
    IDirectDrawSurface7* This)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPriority(
    IDirectDrawSurface7* This,
    DWORD dwPriority)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPriority(
    IDirectDrawSurface7* This,
    LPDWORD lpdwPriority)
{
    if (lpdwPriority) *lpdwPriority = 0;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetLOD(
    IDirectDrawSurface7* This,
    DWORD dwMaxLOD)
{
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetLOD(
    IDirectDrawSurface7* This,
    LPDWORD lpdwMaxLOD)
{
    if (lpdwMaxLOD) *lpdwMaxLOD = 0;
    return DD_OK;
}

//=============================================================================
// Vtable для освобождённых surface (freed/stale) — возвращает ошибки вместо краша
//=============================================================================
static HRESULT WINAPI FreedSurface_QueryInterface(IDirectDrawSurface7* /*This*/, REFIID /*riid*/, LPVOID* ppvObj) { if (ppvObj) *ppvObj = nullptr; return E_NOINTERFACE; }
static ULONG WINAPI FreedSurface_AddRef(IDirectDrawSurface7* /*This*/) { return 0; }
static ULONG WINAPI FreedSurface_Release(IDirectDrawSurface7* /*This*/) { return 0; }
static HRESULT WINAPI FreedSurface_AddAttachedSurface(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWSURFACE7 /*lpDDSAttachedSurface*/) { return DDERR_SURFACELOST; }
extern "C" volatile DWORD g_ddvk_freedSurfaceCallCount = 0;

static HRESULT WINAPI FreedSurface_AddOverlayDirtyRect(IDirectDrawSurface7* /*This*/, LPRECT /*lpRect*/) { 
    InterlockedIncrement((volatile LONG*)&g_ddvk_freedSurfaceCallCount);
    /* Не разыменовываем This — он может быть невалидным. */
    return DDERR_SURFACELOST; 
}
static HRESULT WINAPI FreedSurface_Blt(IDirectDrawSurface7* /*This*/, LPRECT /*lpDestRect*/, LPDIRECTDRAWSURFACE7 /*lpDDSrcSurface*/, LPRECT /*lpSrcRect*/, DWORD /*dwFlags*/, LPDDBLTFX /*lpDDBltFx*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_BltBatch(IDirectDrawSurface7* /*This*/, LPDDBLTBATCH /*lpDDBltBatch*/, DWORD /*dwCount*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_BltFast(IDirectDrawSurface7* /*This*/, DWORD /*dwX*/, DWORD /*dwY*/, LPDIRECTDRAWSURFACE7 /*lpDDSrcSurface*/, LPRECT /*lpSrcRect*/, DWORD /*dwTrans*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_DeleteAttachedSurface(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/, LPDIRECTDRAWSURFACE7 /*lpDDSAttachedSurface*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_EnumAttachedSurfaces(IDirectDrawSurface7* /*This*/, LPVOID /*lpContext*/, LPDDENUMSURFACESCALLBACK7 /*lpEnumSurfacesCallback*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_EnumOverlayZOrders(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/, LPVOID /*lpContext*/, LPDDENUMSURFACESCALLBACK7 /*lpEnumSurfacesCallback*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_Flip(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWSURFACE7 /*lpDDSurfaceTargetOverride*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetAttachedSurface(IDirectDrawSurface7* /*This*/, LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface) { 
    (void)lpDDSCaps;
    if (lplpDDAttachedSurface) *lplpDDAttachedSurface = nullptr; 
    return DDERR_SURFACELOST; 
}
static HRESULT WINAPI FreedSurface_GetBltStatus(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetCaps(IDirectDrawSurface7* /*This*/, LPDDSCAPS2 /*lpDDSCaps*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetClipper(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWCLIPPER* /*lplpDDClipper*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetColorKey(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/, LPDDCOLORKEY /*lpDDColorKey*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetDC(IDirectDrawSurface7* /*This*/, HDC* /*lphDC*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetFlipStatus(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetOverlayPosition(IDirectDrawSurface7* /*This*/, LPLONG /*lplX*/, LPLONG /*lplY*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetPalette(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWPALETTE* /*lplpDDPalette*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetPixelFormat(IDirectDrawSurface7* /*This*/, LPDDPIXELFORMAT /*lpDDPixelFormat*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetSurfaceDesc(IDirectDrawSurface7* /*This*/, LPDDSURFACEDESC2 /*lpDDSurfaceDesc*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_Initialize(IDirectDrawSurface7* /*This*/, LPDIRECTDRAW /*lpDD*/, LPDDSURFACEDESC2 /*lpDDSurfaceDesc*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_IsLost(IDirectDrawSurface7* /*This*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_Lock(IDirectDrawSurface7* /*This*/, LPRECT /*lpDestRect*/, LPDDSURFACEDESC2 /*lpDDSurfaceDesc*/, DWORD /*dwFlags*/, HANDLE /*hEvent*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_ReleaseDC(IDirectDrawSurface7* /*This*/, HDC /*hDC*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_Restore(IDirectDrawSurface7* /*This*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_SetClipper(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWCLIPPER /*lpDDClipper*/) { return DD_OK; }
static HRESULT WINAPI FreedSurface_SetColorKey(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/, LPDDCOLORKEY /*lpDDColorKey*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_SetOverlayPosition(IDirectDrawSurface7* /*This*/, LONG /*lX*/, LONG /*lY*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_SetPalette(IDirectDrawSurface7* /*This*/, LPDIRECTDRAWPALETTE /*lpDDPalette*/) { return DD_OK; }
static HRESULT WINAPI FreedSurface_Unlock(IDirectDrawSurface7* /*This*/, LPRECT /*lpRect*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_UpdateOverlay(IDirectDrawSurface7* /*This*/, LPRECT /*lpSrcRect*/, LPDIRECTDRAWSURFACE7 /*lpDDDestSurface*/, LPRECT /*lpDestRect*/, DWORD /*dwFlags*/, LPDDOVERLAYFX /*lpDDOverlayFx*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_UpdateOverlayDisplay(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_UpdateOverlayZOrder(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/, LPDIRECTDRAWSURFACE7 /*lpDDSurface*/) { return DDERR_SURFACELOST; }
// DD7 extensions
static HRESULT WINAPI FreedSurface_GetDDInterface(IDirectDrawSurface7* /*This*/, LPVOID* /*lplpDD*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_PageLock(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_PageUnlock(IDirectDrawSurface7* /*This*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_SetSurfaceDesc(IDirectDrawSurface7* /*This*/, LPDDSURFACEDESC2 /*lpDDSurfaceDesc*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_SetPrivateData(IDirectDrawSurface7* /*This*/, REFGUID /*tag*/, LPVOID /*lpData*/, DWORD /*cbSize*/, DWORD /*dwFlags*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetPrivateData(IDirectDrawSurface7* /*This*/, REFGUID /*tag*/, LPVOID /*lpBuffer*/, LPDWORD /*lpBufferSize*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_FreePrivateData(IDirectDrawSurface7* /*This*/, REFGUID /*tag*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_GetUniquenessValue(IDirectDrawSurface7* /*This*/, LPDWORD /*lpValue*/) { return DDERR_SURFACELOST; }
static HRESULT WINAPI FreedSurface_ChangeUniquenessValue(IDirectDrawSurface7* /*This*/) { return DDERR_SURFACELOST; }

static const IDirectDrawSurface7Vtbl s_freedSurfaceVtbl = {
    FreedSurface_QueryInterface, FreedSurface_AddRef, FreedSurface_Release,
    FreedSurface_AddAttachedSurface, FreedSurface_AddOverlayDirtyRect, FreedSurface_Blt,
    FreedSurface_BltBatch, FreedSurface_BltFast, FreedSurface_DeleteAttachedSurface,
    FreedSurface_EnumAttachedSurfaces, FreedSurface_EnumOverlayZOrders, FreedSurface_Flip,
    FreedSurface_GetAttachedSurface, FreedSurface_GetBltStatus, FreedSurface_GetCaps,
    FreedSurface_GetClipper, FreedSurface_GetColorKey, FreedSurface_GetDC,
    FreedSurface_GetFlipStatus, FreedSurface_GetOverlayPosition, FreedSurface_GetPalette,
    FreedSurface_GetPixelFormat, FreedSurface_GetSurfaceDesc, FreedSurface_Initialize,
    FreedSurface_IsLost, FreedSurface_Lock, FreedSurface_ReleaseDC, FreedSurface_Restore,
    FreedSurface_SetClipper, FreedSurface_SetColorKey, FreedSurface_SetOverlayPosition,
    FreedSurface_SetPalette, FreedSurface_Unlock, FreedSurface_UpdateOverlay,
    FreedSurface_UpdateOverlayDisplay, FreedSurface_UpdateOverlayZOrder,
    FreedSurface_GetDDInterface, FreedSurface_PageLock, FreedSurface_PageUnlock,
    FreedSurface_SetSurfaceDesc, FreedSurface_SetPrivateData, FreedSurface_GetPrivateData,
    FreedSurface_FreePrivateData, FreedSurface_GetUniquenessValue, FreedSurface_ChangeUniquenessValue
};

/* Dummy surface для VEH — используется когда игра вызывает метод с невалидным this. */
static IDirectDrawSurface7 s_dummyFreedSurface = { const_cast<IDirectDrawSurface7Vtbl*>(&s_freedSurfaceVtbl) };

extern "C" const void* GetDDrawFreedSurfaceVtbl(void) { return &s_freedSurfaceVtbl; }
extern "C" IDirectDrawSurface7* GetDDrawDummyFreedSurface(void) { return &s_dummyFreedSurface; }

//=============================================================================
// Vtable для IDirectDrawSurface7
//=============================================================================

extern "C" const void* GetDDrawSurfaceVtblForNullThis(void) {
    return &ddrawSurfaceVtbl;
}

IDirectDrawSurfaceVtbl ddrawSurfaceVtblV1 = {};

const IDirectDrawSurface7Vtbl ddrawSurfaceVtbl = {
    IDirectDrawSurface7_QueryInterface,
    IDirectDrawSurface7_AddRef,
    IDirectDrawSurface7_Release,
    IDirectDrawSurface7_AddAttachedSurface,
    IDirectDrawSurface7_AddOverlayDirtyRect,
    IDirectDrawSurface7_Blt,
    IDirectDrawSurface7_BltBatch,
    IDirectDrawSurface7_BltFast,
    IDirectDrawSurface7_DeleteAttachedSurface,
    IDirectDrawSurface7_EnumAttachedSurfaces,
    IDirectDrawSurface7_EnumOverlayZOrders,
    IDirectDrawSurface7_Flip,
    IDirectDrawSurface7_GetAttachedSurface,
    IDirectDrawSurface7_GetBltStatus,
    IDirectDrawSurface7_GetCaps,
    IDirectDrawSurface7_GetClipper,
    IDirectDrawSurface7_GetColorKey,
    IDirectDrawSurface7_GetDC,
    IDirectDrawSurface7_GetFlipStatus,
    IDirectDrawSurface7_GetOverlayPosition,
    IDirectDrawSurface7_GetPalette,
    IDirectDrawSurface7_GetPixelFormat,
    IDirectDrawSurface7_GetSurfaceDesc,
    IDirectDrawSurface7_Initialize,
    IDirectDrawSurface7_IsLost,
    IDirectDrawSurface7_Lock,
    IDirectDrawSurface7_ReleaseDC,
    IDirectDrawSurface7_Restore,
    IDirectDrawSurface7_SetClipper,
    IDirectDrawSurface7_SetColorKey,
    IDirectDrawSurface7_SetOverlayPosition,
    IDirectDrawSurface7_SetPalette,
    IDirectDrawSurface7_Unlock,
    IDirectDrawSurface7_UpdateOverlay,
    IDirectDrawSurface7_UpdateOverlayDisplay,
    IDirectDrawSurface7_UpdateOverlayZOrder,
    IDirectDrawSurface7_GetDDInterface,
    IDirectDrawSurface7_PageLock,
    IDirectDrawSurface7_PageUnlock,
    IDirectDrawSurface7_SetSurfaceDesc,
    IDirectDrawSurface7_SetPrivateData,
    IDirectDrawSurface7_GetPrivateData,
    IDirectDrawSurface7_FreePrivateData,
    IDirectDrawSurface7_GetUniquenessValue,
    IDirectDrawSurface7_ChangeUniquenessValue,
    IDirectDrawSurface7_SetPriority,
    IDirectDrawSurface7_GetPriority,
    IDirectDrawSurface7_SetLOD,
    IDirectDrawSurface7_GetLOD
};

// Заглушка при ошибке GetAttachedSurface (определение после vtable)
IDirectDrawSurface7 s_dummySurfaceNoAttach = { const_cast<IDirectDrawSurface7Vtbl*>(&ddrawSurfaceVtbl) };
static void InitCompatSurfaceVtables() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    ddrawSurfaceVtblV1.QueryInterface         = reinterpret_cast<decltype(ddrawSurfaceVtblV1.QueryInterface)>(IDirectDrawSurface7_QueryInterface);
    ddrawSurfaceVtblV1.AddRef                 = reinterpret_cast<decltype(ddrawSurfaceVtblV1.AddRef)>(IDirectDrawSurface7_AddRef);
    ddrawSurfaceVtblV1.Release                = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Release)>(IDirectDrawSurface7_Release);
    ddrawSurfaceVtblV1.AddAttachedSurface     = reinterpret_cast<decltype(ddrawSurfaceVtblV1.AddAttachedSurface)>(IDirectDrawSurface7_AddAttachedSurface);
    ddrawSurfaceVtblV1.AddOverlayDirtyRect    = reinterpret_cast<decltype(ddrawSurfaceVtblV1.AddOverlayDirtyRect)>(IDirectDrawSurface7_AddOverlayDirtyRect);
    ddrawSurfaceVtblV1.Blt                    = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Blt)>(IDirectDrawSurface7_Blt);
    ddrawSurfaceVtblV1.BltBatch               = reinterpret_cast<decltype(ddrawSurfaceVtblV1.BltBatch)>(IDirectDrawSurface7_BltBatch);
    ddrawSurfaceVtblV1.BltFast                = reinterpret_cast<decltype(ddrawSurfaceVtblV1.BltFast)>(IDirectDrawSurface7_BltFast);
    ddrawSurfaceVtblV1.DeleteAttachedSurface  = reinterpret_cast<decltype(ddrawSurfaceVtblV1.DeleteAttachedSurface)>(IDirectDrawSurface7_DeleteAttachedSurface);
    ddrawSurfaceVtblV1.EnumAttachedSurfaces   = reinterpret_cast<decltype(ddrawSurfaceVtblV1.EnumAttachedSurfaces)>(IDirectDrawSurface7_EnumAttachedSurfaces);
    ddrawSurfaceVtblV1.EnumOverlayZOrders     = reinterpret_cast<decltype(ddrawSurfaceVtblV1.EnumOverlayZOrders)>(IDirectDrawSurface7_EnumOverlayZOrders);
    ddrawSurfaceVtblV1.Flip                   = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Flip)>(IDirectDrawSurface7_Flip);
    ddrawSurfaceVtblV1.GetAttachedSurface     = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetAttachedSurface)>(IDirectDrawSurface7_GetAttachedSurface);
    ddrawSurfaceVtblV1.GetBltStatus           = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetBltStatus)>(IDirectDrawSurface7_GetBltStatus);
    ddrawSurfaceVtblV1.GetCaps                = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetCaps)>(IDirectDrawSurface7_GetCaps);
    ddrawSurfaceVtblV1.GetClipper             = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetClipper)>(IDirectDrawSurface7_GetClipper);
    ddrawSurfaceVtblV1.GetColorKey            = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetColorKey)>(IDirectDrawSurface7_GetColorKey);
    ddrawSurfaceVtblV1.GetDC                  = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetDC)>(IDirectDrawSurface7_GetDC);
    ddrawSurfaceVtblV1.GetFlipStatus          = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetFlipStatus)>(IDirectDrawSurface7_GetFlipStatus);
    ddrawSurfaceVtblV1.GetOverlayPosition     = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetOverlayPosition)>(IDirectDrawSurface7_GetOverlayPosition);
    ddrawSurfaceVtblV1.GetPalette             = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetPalette)>(IDirectDrawSurface7_GetPalette);
    ddrawSurfaceVtblV1.GetPixelFormat         = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetPixelFormat)>(IDirectDrawSurface7_GetPixelFormat);
    ddrawSurfaceVtblV1.GetSurfaceDesc         = reinterpret_cast<decltype(ddrawSurfaceVtblV1.GetSurfaceDesc)>(IDirectDrawSurface7_GetSurfaceDesc);
    ddrawSurfaceVtblV1.Initialize             = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Initialize)>(IDirectDrawSurface7_Initialize);
    ddrawSurfaceVtblV1.IsLost                 = reinterpret_cast<decltype(ddrawSurfaceVtblV1.IsLost)>(IDirectDrawSurface7_IsLost);
    ddrawSurfaceVtblV1.Lock                   = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Lock)>(IDirectDrawSurface7_Lock);
    ddrawSurfaceVtblV1.ReleaseDC              = reinterpret_cast<decltype(ddrawSurfaceVtblV1.ReleaseDC)>(IDirectDrawSurface7_ReleaseDC);
    ddrawSurfaceVtblV1.Restore                = reinterpret_cast<decltype(ddrawSurfaceVtblV1.Restore)>(IDirectDrawSurface7_Restore);
    ddrawSurfaceVtblV1.SetClipper             = reinterpret_cast<decltype(ddrawSurfaceVtblV1.SetClipper)>(IDirectDrawSurface7_SetClipper);
    ddrawSurfaceVtblV1.SetColorKey            = reinterpret_cast<decltype(ddrawSurfaceVtblV1.SetColorKey)>(IDirectDrawSurface7_SetColorKey);
    ddrawSurfaceVtblV1.SetOverlayPosition     = reinterpret_cast<decltype(ddrawSurfaceVtblV1.SetOverlayPosition)>(IDirectDrawSurface7_SetOverlayPosition);
    ddrawSurfaceVtblV1.SetPalette             = reinterpret_cast<decltype(ddrawSurfaceVtblV1.SetPalette)>(IDirectDrawSurface7_SetPalette);
    ddrawSurfaceVtblV1.Unlock                 = IDirectDrawSurface1_Unlock;
    ddrawSurfaceVtblV1.UpdateOverlay          = reinterpret_cast<decltype(ddrawSurfaceVtblV1.UpdateOverlay)>(IDirectDrawSurface7_UpdateOverlay);
    ddrawSurfaceVtblV1.UpdateOverlayDisplay   = reinterpret_cast<decltype(ddrawSurfaceVtblV1.UpdateOverlayDisplay)>(IDirectDrawSurface7_UpdateOverlayDisplay);
    ddrawSurfaceVtblV1.UpdateOverlayZOrder    = reinterpret_cast<decltype(ddrawSurfaceVtblV1.UpdateOverlayZOrder)>(IDirectDrawSurface7_UpdateOverlayZOrder);
    s_dummySurfaceV1.lpVtbl = &ddrawSurfaceVtblV1;
}

extern "C" void DDVK_SetSurfaceCompatVtable(IDirectDrawSurface7* surface, bool legacyV1) {
    if (!surface) return;
    InitCompatSurfaceVtables();
    auto* shadow = reinterpret_cast<SurfaceWrapperCompatShadow*>(surface);
    const void* vtbl = legacyV1 ? static_cast<const void*>(&ddrawSurfaceVtblV1)
                                : static_cast<const void*>(&ddrawSurfaceVtbl);
    surface->lpVtbl = legacyV1 ? reinterpret_cast<IDirectDrawSurface7Vtbl*>(&ddrawSurfaceVtblV1)
                               : const_cast<IDirectDrawSurface7Vtbl*>(&ddrawSurfaceVtbl);
    shadow->vtblAt20 = vtbl;
}

extern "C" volatile DWORD g_ddvk_lastReleasedSurfaceWrapper;