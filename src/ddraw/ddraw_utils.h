#pragma once

#include "ddraw.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Вспомогательные функции для работы с поверхностями
//=============================================================================

// Преобразование форматов пикселей
DWORD DDrawColorToVulkan(DWORD color, const DDPIXELFORMAT* format);

// Создание поверхности из GDI битмапа
HRESULT DDrawCreateSurfaceFromBitmap(
    LPDIRECTDRAW7 lpDD,
    LPDIRECTDRAWSURFACE7* lplpDDSurface,
    HBITMAP hBitmap,
    const DDSURFACEDESC2* desc
);

// Копирование данных между поверхностями с преобразованием формата
HRESULT DDrawCopySurface(
    LPDIRECTDRAWSURFACE7 lpDest,
    LPDIRECTDRAWSURFACE7 lpSrc,
    LPRECT lpDestRect,
    LPRECT lpSrcRect
);

// Проверка поддержки режима дисплея
BOOL DDrawIsModeSupported(DWORD width, DWORD height, DWORD bpp);

// Получение информации о текущем режиме рабочего стола
void DDrawGetDesktopMode(DWORD* width, DWORD* height, DWORD* bpp);

// Работа с палитрами
void DDrawConvertPaletteToVulkan(
    const PALETTEENTRY* src,
    uint32_t* dst,
    DWORD count
);

#ifdef __cplusplus
}
#endif