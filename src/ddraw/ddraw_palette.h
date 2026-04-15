#pragma once

#include "ddraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Используем IDirectDrawPaletteVtbl из SDK, здесь только объявление vtable.
extern IDirectDrawPaletteVtbl ddrawPaletteVtbl;

#ifdef __cplusplus
}
#endif