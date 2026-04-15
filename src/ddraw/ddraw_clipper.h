#pragma once

#include "ddraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Используем IDirectDrawClipperVtbl из SDK, здесь только объявление vtable.
extern IDirectDrawClipperVtbl ddrawClipperVtbl;

#ifdef __cplusplus
}
#endif