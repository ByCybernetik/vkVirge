// ddraw_impl.h — новый файл
#pragma once
#include <ddraw.h>
#include "../ddvk/ddvk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Явное определение layout COM-объекта (совместимо с C)
typedef struct DirectDraw7Object {
    const struct IDirectDraw7Vtbl* lpVtbl;
    ddvk::DirectDrawImpl impl;
} DirectDraw7Object;

typedef struct DirectDrawSurface7Object {
    const struct IDirectDrawSurface7Vtbl* lpVtbl;
    ddvk::SurfaceImpl impl;
} DirectDrawSurface7Object;

// Аналогично для Palette, Clipper...

#ifdef __cplusplus
}
#endif