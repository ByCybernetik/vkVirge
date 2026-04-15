#include "ddraw_palette.h"
#include "../ddvk/ddvk_core.h"
#include "../ddvk/ddvk_utils.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

/** Заглушка при E_NOINTERFACE — игра не вызывает по nullptr (page fault 0x10). */
static IDirectDrawPalette s_dummyPalette;

static ddvk::PaletteImpl* GetImpl(IDirectDrawPalette* This) {
    if (!This || reinterpret_cast<uintptr_t>(This) < 0x10000u) return nullptr;
    return reinterpret_cast<ddvk::PaletteImpl*>(
        reinterpret_cast<char*>(This) + sizeof(void*));
}

static HRESULT WINAPI IDirectDrawPalette_QueryInterface(
    IDirectDrawPalette* This,
    REFIID riid,
    LPVOID* ppvObj)
{
    if (!This || !ppvObj) return DDERR_INVALIDPARAMS;
    static bool s_dummyPaletteInit = (s_dummyPalette.lpVtbl = &ddrawPaletteVtbl, true);
    (void)s_dummyPaletteInit;
    *ppvObj = &s_dummyPalette;
    if (IsEqualIID(riid, IID_IDirectDrawPalette) ||
        IsEqualIID(riid, IID_IUnknown)) {
        *ppvObj = This;
        auto impl = GetImpl(This);
        if (impl) impl->AddRef();
        return DD_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI IDirectDrawPalette_AddRef(IDirectDrawPalette* This) {
    if (This == &s_dummyPalette) return 1;
    auto impl = GetImpl(This);
    return impl ? impl->AddRef() : 0;
}

static ULONG WINAPI IDirectDrawPalette_Release(IDirectDrawPalette* This) {
    if (This == &s_dummyPalette) return 0;
    auto impl = GetImpl(This);
    if (!impl) return 0;

    ULONG ref = impl->Release();
    if (ref == 0) {
        delete[] reinterpret_cast<char*>(This);
    }
    return ref;
}

static HRESULT WINAPI IDirectDrawPalette_GetEntries(
    IDirectDrawPalette* This,
    DWORD dwFlags,
    DWORD dwBase,
    DWORD dwNumEntries,
    LPPALETTEENTRY lpEntries)
{
    (void)dwFlags;
    if (!lpEntries) return DDERR_INVALIDPARAMS;
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    impl->GetEntries(dwBase, dwNumEntries, lpEntries);
    return DD_OK;
}

static HRESULT WINAPI IDirectDrawPalette_SetEntries(
    IDirectDrawPalette* This,
    DWORD dwFlags,
    DWORD dwBase,
    DWORD dwNumEntries,
    LPPALETTEENTRY lpEntries)
{
    (void)dwFlags;
    if (!lpEntries) return DDERR_INVALIDPARAMS;
    auto impl = GetImpl(This);
    if (!impl) return DDERR_INVALIDOBJECT;
    impl->SetEntries(dwBase, dwNumEntries, lpEntries);
    return DD_OK;
}

IDirectDrawPaletteVtbl ddrawPaletteVtbl = {
    IDirectDrawPalette_QueryInterface,
    IDirectDrawPalette_AddRef,
    IDirectDrawPalette_Release,
    nullptr,                       // GetCaps
    IDirectDrawPalette_GetEntries,
    nullptr,                       // Initialize
    IDirectDrawPalette_SetEntries
};