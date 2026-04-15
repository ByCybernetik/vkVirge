#pragma once

#include "ddraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Используем тип IDirectDrawSurface7Vtbl из SDK (ddraw.h).
// Здесь только объявляем сам vtable, определяемый в реализации.
extern const IDirectDrawSurface7Vtbl ddrawSurfaceVtbl;
/** For fault-at-0 handler: vtable to inject when game loads vtable from null surface pointer. */
#ifdef __cplusplus
extern "C"
#endif
const void* GetDDrawSurfaceVtblForNullThis(void);
/** Vtable для освобождённых surface — возвращает ошибки вместо краша при вызове через stale указатель. */
#ifdef __cplusplus
extern "C"
#endif
const void* GetDDrawFreedSurfaceVtbl(void);
/** Dummy surface с freed vtable — используется VEH когда this невалиден. */
#ifdef __cplusplus
extern "C"
#endif
struct IDirectDrawSurface7* GetDDrawDummyFreedSurface(void);
// Заглушка при ошибках GetAttachedSurface/CreateSurface — игра не вызывает по nullptr (page fault 0x10).
extern IDirectDrawSurface7 s_dummySurfaceNoAttach;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace ddvk { struct SurfaceImpl; }
// Создаёт COM-обёртку для уже существующей SurfaceImpl (например, back buffer) и
// записывает в impl->userData указатель на неё. Используется из GetAttachedSurface.
IDirectDrawSurface7* DDrawWrapSurface(ddvk::SurfaceImpl* impl);
#endif