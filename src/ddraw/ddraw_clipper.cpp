#include "ddraw_clipper.h"
#include "../ddvk/ddvk_core.h"
#include "../ddvk/ddvk_utils.h"
#include <cstdio>
#include <cstring>

/** Заглушка при E_NOINTERFACE — игра не вызывает по nullptr (page fault 0x10). */
static IDirectDrawClipper s_dummyClipper;

static ddvk::ClipperImpl* GetImpl(IDirectDrawClipper* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < 0x10000u) return nullptr;
    return reinterpret_cast<ddvk::ClipperImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static HRESULT WINAPI IDirectDrawClipper_QueryInterface(
    IDirectDrawClipper* This,
    REFIID riid,
    LPVOID* ppvObj)
{
    if (!This || !ppvObj) return DDERR_INVALIDPARAMS;
    static bool s_dummyClipperInit = (s_dummyClipper.lpVtbl = &ddrawClipperVtbl, true);
    (void)s_dummyClipperInit;
    *ppvObj = &s_dummyClipper;
    if (IsEqualIID(riid, IID_IDirectDrawClipper) ||
        IsEqualIID(riid, IID_IUnknown)) {
        *ppvObj = This;
        auto impl = GetImpl(This);
        if (impl) impl->AddRef();
        return DD_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI IDirectDrawClipper_AddRef(IDirectDrawClipper* This) {
    if (This == &s_dummyClipper) return 1;
    auto impl = GetImpl(This);
    return impl ? impl->AddRef() : 0;
}

static ULONG WINAPI IDirectDrawClipper_Release(IDirectDrawClipper* This) {
    if (This == &s_dummyClipper) return 0;
    auto impl = GetImpl(This);
    if (!impl) return 0;
    
    ULONG ref = impl->Release();
    if (ref == 0) {
        delete[] reinterpret_cast<char*>(This);
    }
    return ref;
}

static HRESULT WINAPI IDirectDrawClipper_GetClipList(
    IDirectDrawClipper* This,
    LPRECT lpRect,
    LPRGNDATA lpClipList,
    LPDWORD lpdwSize)
{
    // Простая заглушка: клип-лист не поддерживается.
    (void)This; (void)lpRect; (void)lpClipList;
    if (lpdwSize) *lpdwSize = 0;
    return DDERR_NOCLIPLIST;
}

static HRESULT WINAPI IDirectDrawClipper_GetHWnd(
    IDirectDrawClipper* This,
    HWND FAR* lphWnd)
{
    if (!lphWnd) return DDERR_INVALIDPARAMS;
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    *lphWnd = impl->hwnd;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawClipper_Initialize(
    IDirectDrawClipper* This,
    LPDIRECTDRAW /*lpDD*/,
    DWORD /*dwFlags*/)
{
    // Наш клиппер не требует отдельной инициализации.
    (void)This;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawClipper_IsClipListChanged(
    IDirectDrawClipper* /*This*/,
    BOOL FAR* lpbChanged)
{
    if (lpbChanged) *lpbChanged = FALSE;
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawClipper_SetClipList(
    IDirectDrawClipper* This,
    LPRGNDATA lpClipList,
    DWORD /*dwFlags*/)
{
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;

    if (!lpClipList) {
        impl->clipRects.clear();
        return DD_OK;
    }

    // lpClipList содержит список RECT в rdh_rects.
    const RGNDATAHEADER* hdr = &lpClipList->rdh;
    const RECT* rects = reinterpret_cast<const RECT*>(lpClipList->Buffer);
    if (hdr->nCount == 0) {
        impl->clipRects.clear();
        return DD_OK;
    }

    impl->clipRects.assign(rects, rects + hdr->nCount);
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawClipper_SetHWnd(
    IDirectDrawClipper* This,
    DWORD /*dwFlags*/,
    HWND hWnd)
{
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    impl->SetHWnd(hWnd);
    return DD_OK;
}

IDirectDrawClipperVtbl ddrawClipperVtbl = {
    IDirectDrawClipper_QueryInterface,
    IDirectDrawClipper_AddRef,
    IDirectDrawClipper_Release,
    IDirectDrawClipper_GetClipList,
    IDirectDrawClipper_GetHWnd,
    IDirectDrawClipper_Initialize,
    IDirectDrawClipper_IsClipListChanged,
    IDirectDrawClipper_SetClipList,
    IDirectDrawClipper_SetHWnd
};