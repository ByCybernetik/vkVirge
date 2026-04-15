#define INITGUID
#include "../ddvk/ddvk_core.h"
#include <ddraw.h>

// Этот TU порождает определения всех IID_* GUID'ов из ddraw.h
// (IID_IDirectDraw*, IID_IDirectDrawSurface*, IID_IDirectDrawPalette и т.д.)
// через DEFINE_GUID макросы DX7 SDK. Благодаря этому внутри нашей DLL
// используются те же значения GUID, что и в приложениях, скомпилированных
// против DX7 ddraw.h.

