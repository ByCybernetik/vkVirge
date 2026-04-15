#include "ddraw_surface.h"
#include "ddraw_palette.h"
#include "ddraw_utils.h"
#include "../ddvk/ddvk_core.h"
#include "../ddvk/ddvk_renderer.h"
#include "../ddvk/ddvk_texture.h"
#include "../ddvk/ddvk_utils.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// Отключаем макросы, которые мешают
#undef IDirectDrawSurface7_QueryInterface
#undef IDirectDrawSurface7_AddRef
#undef IDirectDrawSurface7_Release
#undef IDirectDrawSurface7_AddAttachedSurface
#undef IDirectDrawSurface7_AddOverlayDirtyRect
#undef IDirectDrawSurface7_Blt
#undef IDirectDrawSurface7_BltBatch
#undef IDirectDrawSurface7_BltFast
#undef IDirectDrawSurface7_DeleteAttachedSurfac
#undef IDirectDrawSurface7_EnumAttachedSurfaces
#undef IDirectDrawSurface7_EnumOverlayZOrders
#undef IDirectDrawSurface7_Flip
#undef IDirectDrawSurface7_GetAttachedSurface
#undef IDirectDrawSurface7_GetBltStatus
#undef IDirectDrawSurface7_GetCaps
#undef IDirectDrawSurface7_GetClipper
#undef IDirectDrawSurface7_GetColorKey
#undef IDirectDrawSurface7_GetDC
#undef IDirectDrawSurface7_GetFlipStatus
#undef IDirectDrawSurface7_GetOverlayPosition
#undef IDirectDrawSurface7_GetPalette
#undef IDirectDrawSurface7_GetPixelFormat
#undef IDirectDrawSurface7_GetSurfaceDesc
#undef IDirectDrawSurface7_Initialize
#undef IDirectDrawSurface7_IsLost
#undef IDirectDrawSurface7_Lock
#undef IDirectDrawSurface7_ReleaseDC
#undef IDirectDrawSurface7_Restore
#undef IDirectDrawSurface7_SetClipper
#undef IDirectDrawSurface7_SetColorKey
#undef IDirectDrawSurface7_SetOverlayPosition
#undef IDirectDrawSurface7_SetPalette
#undef IDirectDrawSurface7_Unlock
#undef IDirectDrawSurface7_UpdateOverlay
#undef IDirectDrawSurface7_UpdateOverlayDisplay
#undef IDirectDrawSurface7_UpdateOverlayZOrder
#undef IDirectDrawSurface7_GetDDInterface
#undef IDirectDrawSurface7_PageLock
#undef IDirectDrawSurface7_PageUnlock
#undef IDirectDrawSurface7_SetSurfaceDesc
#undef IDirectDrawSurface7_SetPrivateData
#undef IDirectDrawSurface7_GetPrivateData
#undef IDirectDrawSurface7_FreePrivateData
#undef IDirectDrawSurface7_GetUniquenessValue
#undef IDirectDrawSurface7_ChangeUniquenessValue
#undef IDirectDrawSurface7_SetPriority
#undef IDirectDrawSurface7_GetPriority
#undef IDirectDrawSurface7_SetLOD
#undef IDirectDrawSurface7_GetLOD

//=============================================================================
// Вспомогательные функции
//=============================================================================

static ddvk::SurfaceImpl* GetImpl(IDirectDrawSurface7* This) {
    if (!This) return nullptr;
    if (reinterpret_cast<uintptr_t>(This) < 0x10000u) return nullptr; // хэндл/мусор → page fault 0x10
    // Объект поверхности: [vtable pointer][SurfaceImpl*] — указатель на оригинал.
    return *reinterpret_cast<ddvk::SurfaceImpl**>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

//=============================================================================
// IDirectDrawSurface7 implementation
//=============================================================================

static HRESULT WINAPI IDirectDrawSurface7_QueryInterface(
    IDirectDrawSurface7* This,
    REFIID riid,
    LPVOID* ppvObj
) {
    if (!This || !ppvObj) return DDERR_INVALIDPARAMS;
    
    if (IsEqualIID(riid, IID_IDirectDrawSurface7) || 
        IsEqualIID(riid, IID_IDirectDrawSurface) ||
        IsEqualIID(riid, IID_IUnknown)) {
        *ppvObj = This;
        This->lpVtbl->AddRef(This);
        return DD_OK;
    }
    
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI IDirectDrawSurface7_AddRef(IDirectDrawSurface7* This) {
    auto impl = GetImpl(This);
    return impl ? ++impl->refCount : 0;
}

static ULONG WINAPI IDirectDrawSurface7_Release(IDirectDrawSurface7* This) {
    auto impl = GetImpl(This);
    if (!impl) return 0;
    
    ULONG ref = --impl->refCount;
    if (ref == 0) {
        if (impl->renderer) {
            impl->renderer->DestroySurface(impl);
        } else {
            delete impl;
        }
        delete[] reinterpret_cast<char*>(This);
    }
    return ref;
}

static HRESULT WINAPI IDirectDrawSurface7_AddAttachedSurface(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface
) {
    // Упрощенная реализация
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_AddOverlayDirtyRect(
    IDirectDrawSurface7* This,
    LPRECT lpRect
) {
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_Blt(
    IDirectDrawSurface7* This,
    LPRECT lpDestRect,
    LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect,
    DWORD dwFlags,
    LPDDBLTFX lpDDBltFx
) {
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:Blt", "entry", This, This ? This->lpVtbl : nullptr);
    // #endregion
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

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
        auto srcImpl = lpDDSrcSurface ? GetImpl(lpDDSrcSurface) : nullptr;
        if (!lpDDSrcSurface && (dwFlags & DDBLT_COLORFILL) && lpDDBltFx) {
            RECT rect = { 0, 0, (LONG)impl->width, (LONG)impl->height };
            if (lpDestRect) rect = *lpDestRect;
            impl->FillColor(lpDDBltFx->dwFillColor, &rect);
            hr = DD_OK;
        } else {
            hr = impl->Blt(lpDestRect, srcImpl, lpSrcRect, dwFlags);
        }
    }

    if (SUCCEEDED(hr) && impl->isPrimary && impl->renderer) {
        static uint32_t s_bltPresentCount = 0;
        if (s_bltPresentCount++ < 3) {
            ddvk::Logger::Info("[DDVK] Present triggered from primary Blt (count=%u)", s_bltPresentCount);
        }
        impl->renderer->BeginFrame();
        impl->renderer->Present();
    }
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:Blt", "return", nullptr, nullptr);
    // #endregion
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_BltBatch(
    IDirectDrawSurface7* This,
    LPDDBLTBATCH lpDDBltBatch,
    DWORD dwCount,
    DWORD dwFlags
) {
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_BltFast(
    IDirectDrawSurface7* This,
    DWORD dwX,
    DWORD dwY,
    LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect,
    DWORD dwTrans
) {
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (!lpDDSrcSurface) return DDERR_INVALIDPARAMS;

    if (impl->renderer) {
        return impl->renderer->ExecuteUnderSurfaceLock([&]() -> HRESULT {
            ddvk::SurfaceImpl* srcImpl = impl->renderer->GetSurfaceByWrapperUnderLock(lpDDSrcSurface);
            if (!srcImpl) return DDERR_INVALIDOBJECT;
            if (!impl->renderer->IsValidSurface(srcImpl)) return DDERR_INVALIDOBJECT;
            if (srcImpl->mappedData && impl->renderer->IsDeferredBuffer(srcImpl->mappedData)) return DDERR_SURFACELOST;
            RECT destRect;
            destRect.left = dwX;
            destRect.top = dwY;
            destRect.right = dwX + (lpSrcRect ? (lpSrcRect->right - lpSrcRect->left) : srcImpl->width);
            destRect.bottom = dwY + (lpSrcRect ? (lpSrcRect->bottom - lpSrcRect->top) : srcImpl->height);
            DWORD flags = 0;
            if (dwTrans & DDBLTFAST_SRCCOLORKEY) flags |= DDBLT_KEYSRC;
            return impl->Blt(&destRect, srcImpl, lpSrcRect, flags);
        });
    }
    auto srcImpl = GetImpl(lpDDSrcSurface);
    if (!srcImpl) return DDERR_INVALIDOBJECT;
    RECT destRect;
    destRect.left = dwX;
    destRect.top = dwY;
    destRect.right = dwX + (lpSrcRect ? (lpSrcRect->right - lpSrcRect->left) : srcImpl->width);
    destRect.bottom = dwY + (lpSrcRect ? (lpSrcRect->bottom - lpSrcRect->top) : srcImpl->height);
    DWORD flags = 0;
    if (dwTrans & DDBLTFAST_SRCCOLORKEY) flags |= DDBLT_KEYSRC;
    return impl->Blt(&destRect, srcImpl, lpSrcRect, flags);
}

static HRESULT WINAPI IDirectDrawSurface7_DeleteAttachedSurface(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_EnumAttachedSurfaces(
    IDirectDrawSurface7* This,
    LPVOID lpContext,
    LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_EnumOverlayZOrders(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPVOID lpContext,
    LPDDENUMSURFACESCALLBACK7 lpfnCallback
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Flip(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride,
    DWORD dwFlags
) {
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:Flip", "entry", This, lpDDSurfaceTargetOverride);
    // #endregion
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    ddvk::SurfaceImpl* targetImpl = lpDDSurfaceTargetOverride ? GetImpl(lpDDSurfaceTargetOverride) : nullptr;
    if (targetImpl && targetImpl->renderer != impl->renderer)
        targetImpl = nullptr;
    if (!targetImpl && impl->backBuffer && impl->backBuffer->userData) {
        targetImpl = GetImpl(reinterpret_cast<IDirectDrawSurface7*>(impl->backBuffer->userData));
    }

    if (!impl->backBuffer && !targetImpl) {
        return DDERR_INVALIDPARAMS;
    }

    if (impl->backBuffer && impl->renderer) {
        impl->renderer->SwapPrimaryAndBackBuffer(impl->backBuffer);
    }
    HRESULT hr = targetImpl ? impl->Flip(targetImpl, dwFlags) : DD_OK;

    if (SUCCEEDED(hr) && impl->renderer) {
        impl->renderer->BeginFrame();
        impl->renderer->Present();
    }

    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_GetAttachedSurface(
    IDirectDrawSurface7* This,
    LPDDSCAPS2 lpDDSCaps,
    LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface
) {
    if (!lplpDDAttachedSurface) return DDERR_INVALIDPARAMS;
    *lplpDDAttachedSurface = nullptr;

    if (lpDDSCaps && (lpDDSCaps->dwCaps & DDSCAPS_BACKBUFFER)) {
        auto impl = GetImpl(This);
        if (impl && impl->backBuffer) {
            ddvk::SurfaceImpl* back = impl->backBuffer;
            if (!back->userData) {
                IDirectDrawSurface7* wrapped = DDrawWrapSurface(back);
                if (!wrapped) return DDERR_OUTOFMEMORY;
            }
            *lplpDDAttachedSurface = reinterpret_cast<IDirectDrawSurface7*>(back->userData);
            // #region agent log
            ddvk::DebugSessionLog("ddvk_surface:GetAttachedSurface", "return", *lplpDDAttachedSurface, (*lplpDDAttachedSurface) ? (*lplpDDAttachedSurface)->lpVtbl : nullptr);
            // #endregion
            (*lplpDDAttachedSurface)->lpVtbl->AddRef(*lplpDDAttachedSurface);
            return DD_OK;
        }
    }

    return DDERR_NOTFOUND;
}

static HRESULT WINAPI IDirectDrawSurface7_GetBltStatus(
    IDirectDrawSurface7* This,
    DWORD dwFlags
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetCaps(
    IDirectDrawSurface7* This,
    LPDDSCAPS2 lpDDSCaps
) {
    if (!lpDDSCaps) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
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
    LPDIRECTDRAWCLIPPER* lplpDDClipper
) {
    if (!lplpDDClipper) return DDERR_INVALIDPARAMS;
    
    *lplpDDClipper = nullptr;
    return DDERR_NOCLIPPERATTACHED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetColorKey(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDDCOLORKEY lpDDColorKey
) {
    return DDERR_NOCOLORKEY;
}

static HRESULT WINAPI IDirectDrawSurface7_GetDC(
    IDirectDrawSurface7* This,
    HDC* lphDC
) {
    if (!lphDC) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    // Создаем DC для поверхности
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return DDERR_GENERIC;
    
    // Создаем DIB раздел
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = impl->width;
    bmi.bmiHeader.biHeight = -(LONG)impl->height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = (WORD)impl->bpp;
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
    DWORD dwFlags
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetOverlayPosition(
    IDirectDrawSurface7* This,
    LPLONG lplX,
    LPLONG lplY
) {
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPalette(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWPALETTE* lplpDDPalette
) {
    if (!lplpDDPalette) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    *lplpDDPalette = (IDirectDrawPalette*)impl->palette;
    return *lplpDDPalette ? DD_OK : DDERR_NOPALETTEATTACHED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPixelFormat(
    IDirectDrawSurface7* This,
    LPDDPIXELFORMAT lpDDPixelFormat
) {
    if (!lpDDPixelFormat) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
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
    LPDDSURFACEDESC2 lpDDSurfaceDesc
) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    lpDDSurfaceDesc->dwWidth = impl->width;
    lpDDSurfaceDesc->dwHeight = impl->height;
    lpDDSurfaceDesc->lPitch = impl->width * (impl->bpp / 8);
    lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = impl->bpp;
    
    if (impl->isPrimary) {
        lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_PRIMARYSURFACE;
    }
    
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Initialize(
    IDirectDrawSurface7* This,
    LPDIRECTDRAW lpDD,
    LPDDSURFACEDESC2 lpDDSurfaceDesc
) {
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI IDirectDrawSurface7_IsLost(IDirectDrawSurface7* This) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Lock(
    IDirectDrawSurface7* This,
    LPRECT lpDestRect,
    LPDDSURFACEDESC2 lpDDSurfaceDesc,
    DWORD dwFlags,
    HANDLE hEvent
) {
    if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
    
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    return impl->Lock(lpDestRect, lpDDSurfaceDesc, dwFlags);
}

static HRESULT WINAPI IDirectDrawSurface7_ReleaseDC(
    IDirectDrawSurface7* This,
    HDC hDC
) {
    if (!hDC) return DDERR_INVALIDPARAMS;
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:ReleaseDC", "entry", This, This ? This->lpVtbl : nullptr);
    // #endregion
    auto impl = GetImpl(This);
    // Получаем битмап из DC и сохраняем изменения
    HBITMAP hbm = (HBITMAP)GetCurrentObject(hDC, OBJ_BITMAP);
    if (hbm) {
        DIBSECTION ds = {};
        if (GetObject(hbm, sizeof(ds), &ds) == sizeof(ds)) {
            if (impl && impl->mappedData) {
                memcpy(impl->mappedData, ds.dsBm.bmBits, 
                       impl->width * impl->height * (impl->bpp / 8));
            }
        }
    }
    
    DeleteDC(hDC);
    if (impl && impl->isPrimary && impl->renderer) {
        static uint32_t s_releaseDCPresentCount = 0;
        if (s_releaseDCPresentCount++ < 3) ddvk::Logger::Info("[DDVK] Present triggered from primary ReleaseDC (count=%u)", s_releaseDCPresentCount);
        impl->renderer->BeginFrame();
        impl->renderer->Present();
    }
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:ReleaseDC", "return", nullptr, nullptr);
    // #endregion
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Restore(IDirectDrawSurface7* This) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetClipper(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWCLIPPER lpDDClipper
) {
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    impl->clipper = lpDDClipper;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetColorKey(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDDCOLORKEY lpDDColorKey
) {
    auto impl = GetImpl(This);
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
    LONG lY
) {
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPalette(
    IDirectDrawSurface7* This,
    LPDIRECTDRAWPALETTE lpDDPalette
) {
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    
    impl->palette = lpDDPalette;
    // Выставляем paletteImpl и в vtable-пути (H3), только если палитра наша (H1).
    impl->paletteImpl = (lpDDPalette && lpDDPalette->lpVtbl == &ddrawPaletteVtbl)
        ? reinterpret_cast<ddvk::PaletteImpl*>(reinterpret_cast<char*>(lpDDPalette) + sizeof(void*))
        : nullptr;
    // #region agent log
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"lpDDPalette\":\"%p\"}", (void*)lpDDPalette);
        ddvk::DebugSessionLogData("ddvk_surface:SetPalette", "vtable_path_paletteImpl_not_set", buf, "H3", nullptr);
    }
    // #endregion
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_Unlock(
    IDirectDrawSurface7* This,
    LPRECT lpRect
) {
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:Unlock", "entry", This, This ? This->lpVtbl : nullptr);
    // #endregion
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (impl->isPrimary) ddvk::Logger::Info("[DBG-Unlock] Unlock primary");
    
    HRESULT hr = impl->Unlock(lpRect);
    if (SUCCEEDED(hr) && impl->isPrimary && impl->renderer) {
        static uint32_t s_unlockPresentCount = 0;
        if (s_unlockPresentCount++ < 3) ddvk::Logger::Info("[DDVK] Present triggered from primary Unlock (count=%u)", s_unlockPresentCount);
        impl->renderer->BeginFrame();
        impl->renderer->Present();
    }
    // #region agent log
    ddvk::DebugSessionLog("ddvk_surface:Unlock", "return", nullptr, nullptr);
    // #endregion
    return hr;
}

static HRESULT WINAPI IDirectDrawSurface7_UpdateOverlay(
    IDirectDrawSurface7* This,
    LPRECT lpSrcRect,
    LPDIRECTDRAWSURFACE7 lpDDDestSurface,
    LPRECT lpDestRect,
    DWORD dwFlags,
    LPDDOVERLAYFX lpDDOverlayFx
) {
    (void)lpDDOverlayFx;

    auto srcImpl = GetImpl(This);
    if (!srcImpl) return DDERR_INVALIDOBJECT;

    if (!lpDDDestSurface) {
        return DD_OK;
    }

    auto dstImpl = GetImpl(lpDDDestSurface);
    if (!dstImpl) return DDERR_INVALIDOBJECT;

    if (dwFlags & DDOVER_HIDE) {
        return DD_OK;
    }

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
    DWORD dwFlags
) {
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_UpdateOverlayZOrder(
    IDirectDrawSurface7* This,
    DWORD dwFlags,
    LPDIRECTDRAWSURFACE7 lpDDSReference
) {
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_GetDDInterface(
    IDirectDrawSurface7* This,
    LPVOID* lplpDD
) {
    if (!lplpDD) return DDERR_INVALIDPARAMS;
    *lplpDD = nullptr;
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    if (!impl->parentDD) return DDERR_UNSUPPORTED;
    *lplpDD = impl->parentDD;
    // #region agent log
    void* vtbl = reinterpret_cast<IDirectDraw7*>(impl->parentDD)->lpVtbl;
    ddvk::DebugSessionLog("ddvk_surface:GetDDInterface", "return", impl->parentDD, vtbl);
    // #endregion
    reinterpret_cast<IDirectDraw7*>(impl->parentDD)->lpVtbl->AddRef(reinterpret_cast<IDirectDraw7*>(impl->parentDD));
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_PageLock(
    IDirectDrawSurface7* This,
    DWORD dwFlags
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_PageUnlock(
    IDirectDrawSurface7* This,
    DWORD dwFlags
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetSurfaceDesc(
    IDirectDrawSurface7* This,
    LPDDSURFACEDESC2 lpddsd2,
    DWORD dwFlags
) {
    return DDERR_UNSUPPORTED;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag,
    LPVOID lpData,
    DWORD cbSize,
    DWORD dwFlags
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag,
    LPVOID lpBuffer,
    LPDWORD lpcbBufferSize
) {
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI IDirectDrawSurface7_FreePrivateData(
    IDirectDrawSurface7* This,
    REFGUID guidTag
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetUniquenessValue(
    IDirectDrawSurface7* This,
    LPDWORD lpValue
) {
    if (lpValue) *lpValue = (DWORD)(uintptr_t)This;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_ChangeUniquenessValue(
    IDirectDrawSurface7* This
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetPriority(
    IDirectDrawSurface7* This,
    DWORD dwPriority
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetPriority(
    IDirectDrawSurface7* This,
    LPDWORD lpdwPriority
) {
    if (lpdwPriority) *lpdwPriority = 0;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_SetLOD(
    IDirectDrawSurface7* This,
    DWORD dwMaxLOD
) {
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawSurface7_GetLOD(
    IDirectDrawSurface7* This,
    LPDWORD lpdwMaxLOD
) {
    if (lpdwMaxLOD) *lpdwMaxLOD = 0;
    return DD_OK;
}

// Vtable для IDirectDrawSurface7 определяется в модуле ddraw_surface.cpp