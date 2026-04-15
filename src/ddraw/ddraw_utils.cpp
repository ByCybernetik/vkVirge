#include "ddraw_utils.h"
#include "../ddvk/ddvk_renderer.h"
#include <cstring>

//=============================================================================
// Реализация утилит
//=============================================================================

DWORD DDrawColorToVulkan(DWORD color, const DDPIXELFORMAT* format) {
    if (!format) return color;
    
    // Преобразование из формата DirectDraw в RGBA
    DWORD r = 0, g = 0, b = 0, a = 0xFF;
    
    if (format->dwFlags & DDPF_RGB) {
        if (format->dwRGBBitCount == 16) {
            // 16-bit RGB (5-6-5)
            r = ((color & format->dwRBitMask) >> 11) * 255 / 31;
            g = ((color & format->dwGBitMask) >> 5) * 255 / 63;
            b = (color & format->dwBBitMask) * 255 / 31;
        } else if (format->dwRGBBitCount == 32) {
            // 32-bit RGB (8-8-8)
            r = (color & format->dwRBitMask) >> 16;
            g = (color & format->dwGBitMask) >> 8;
            b = color & format->dwBBitMask;
        }
    }
    
    return (a << 24) | (r << 16) | (g << 8) | b;
}

HRESULT DDrawCreateSurfaceFromBitmap(
    LPDIRECTDRAW7 lpDD,
    LPDIRECTDRAWSURFACE7* lplpDDSurface,
    HBITMAP hBitmap,
    const DDSURFACEDESC2* desc
) {
    if (!lpDD || !lplpDDSurface || !hBitmap) return DDERR_INVALIDPARAMS;
    
    // Получаем информацию о битмапе
    BITMAP bm = {};
    if (!GetObject(hBitmap, sizeof(bm), &bm)) {
        return DDERR_INVALIDPARAMS;
    }
    
    // Создаем описание поверхности
    DDSURFACEDESC2 ddsd = {};
    if (desc) {
        ddsd = *desc;
    } else {
        ddsd.dwSize = sizeof(ddsd);
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        ddsd.dwWidth = bm.bmWidth;
        ddsd.dwHeight = bm.bmHeight;
        
        // Устанавливаем формат пикселей
        ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
        ddsd.ddpfPixelFormat.dwRGBBitCount = bm.bmBitsPixel;
        
        if (bm.bmBitsPixel == 16) {
            ddsd.ddpfPixelFormat.dwRBitMask = 0xF800;
            ddsd.ddpfPixelFormat.dwGBitMask = 0x07E0;
            ddsd.ddpfPixelFormat.dwBBitMask = 0x001F;
        } else if (bm.bmBitsPixel == 32) {
            ddsd.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
            ddsd.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
            ddsd.ddpfPixelFormat.dwBBitMask = 0x000000FF;
        }
    }
    
    // Создаем поверхность через vtable
    HRESULT hr = lpDD->lpVtbl->CreateSurface(lpDD, &ddsd, lplpDDSurface, nullptr);
    if (FAILED(hr)) return hr;
    
    // Копируем данные битмапа в поверхность
    HDC hdcMem = CreateCompatibleDC(nullptr);
    if (!hdcMem) {
        (*lplpDDSurface)->lpVtbl->Release(*lplpDDSurface);
        return DDERR_OUTOFMEMORY;
    }
    
    SelectObject(hdcMem, hBitmap);
    
    HDC hdcSurface;
    hr = (*lplpDDSurface)->lpVtbl->GetDC(*lplpDDSurface, &hdcSurface);
    if (SUCCEEDED(hr)) {
        BitBlt(hdcSurface, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
        (*lplpDDSurface)->lpVtbl->ReleaseDC(*lplpDDSurface, hdcSurface);
    }
    
    DeleteDC(hdcMem);
    
    return hr;
}

HRESULT DDrawCopySurface(
    LPDIRECTDRAWSURFACE7 lpDest,
    LPDIRECTDRAWSURFACE7 lpSrc,
    LPRECT lpDestRect,
    LPRECT lpSrcRect
) {
    if (!lpDest || !lpSrc) return DDERR_INVALIDPARAMS;
    
    // Используем Blt для копирования через vtable
    return lpDest->lpVtbl->Blt(lpDest, lpDestRect, lpSrc, lpSrcRect, 0, nullptr);
}

BOOL DDrawIsModeSupported(DWORD width, DWORD height, DWORD bpp) {
    // Минимальное разумное разрешение (в т.ч. Dune 2000: 640x400)
    if (width < 320 || height < 200) return FALSE;
    if (bpp != 8 && bpp != 16 && bpp != 32) return FALSE;

    auto renderer = ddvk::GetVulkanRenderer();
    if (renderer) {
        auto modes = renderer->GetSupportedModes();
        for (const auto& mode : modes) {
            if (mode.width == width && mode.height == height && mode.bpp == bpp) {
                return TRUE;
            }
        }
        // Разрешение может быть не в списке — Vulkan поддерживает произвольный размер
        return TRUE;
    }
    return TRUE;
}

void DDrawGetDesktopMode(DWORD* width, DWORD* height, DWORD* bpp) {
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        if (width) *width = GetDeviceCaps(hdc, HORZRES);
        if (height) *height = GetDeviceCaps(hdc, VERTRES);
        if (bpp) *bpp = GetDeviceCaps(hdc, BITSPIXEL);
        ReleaseDC(nullptr, hdc);
    }
}

void DDrawConvertPaletteToVulkan(
    const PALETTEENTRY* src,
    uint32_t* dst,
    DWORD count
) {
    for (DWORD i = 0; i < count; i++) {
        dst[i] = (0xFF << 24) | (src[i].peRed << 16) | (src[i].peGreen << 8) | src[i].peBlue;
    }
}