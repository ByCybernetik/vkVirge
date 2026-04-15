#pragma once

// Все настройки CINTERFACE/COBJMACROS и подключение <windows.h>/<ddraw.h>
// выполняются внутри `ddvk_core.h`, чтобы гарантировать, что COM-типы
// (IDirectDraw7/IDirectDrawSurface7 и т.п.) имеют поле lpVtbl.
#include "../ddvk/ddvk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Глобальные функции DirectDraw
//=============================================================================

HRESULT WINAPI DirectDrawCreate(
    LPGUID lpGUID,
    LPDIRECTDRAW* lplpDD,
    IUnknown* pUnkOuter
);

HRESULT WINAPI DirectDrawCreateEx(
    LPGUID lpGUID,
    LPVOID* lplpDD,
    REFIID iid,
    IUnknown* pUnkOuter
);

HRESULT WINAPI DirectDrawEnumerate(
    LPDDENUMCALLBACK lpCallback,
    LPVOID lpContext
);

HRESULT WINAPI DirectDrawEnumerateEx(
    LPDDENUMCALLBACKEX lpCallback,
    LPVOID lpContext,
    DWORD dwFlags
);

HRESULT WINAPI DirectDrawCreateClipper(
    DWORD dwFlags,
    LPDIRECTDRAWCLIPPER* lplpDDClipper,
    IUnknown* pUnkOuter
);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/** Dummy IDirectDraw7 for error paths so app never gets nullptr (avoids page fault 0x10). */
extern IDirectDraw7 s_dummyDD7;
#endif