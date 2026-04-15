#pragma once

// Используем C-интерфейс COM для DirectDraw (с полем lpVtbl)
#ifndef CINTERFACE
#define CINTERFACE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <windows.h>
#include <ddraw.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <functional>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <set>

// Отключаем проблемные макросы
#undef IDirectDraw7_QueryInterface
#undef IDirectDraw7_AddRef
#undef IDirectDraw7_Release
#undef IDirectDraw7_Compact
#undef IDirectDraw7_CreateClipper
#undef IDirectDraw7_CreatePalette
#undef IDirectDraw7_CreateSurface
#undef IDirectDraw7_DuplicateSurface
#undef IDirectDraw7_EnumDisplayModes
#undef IDirectDraw7_EnumSurfaces
#undef IDirectDraw7_FlipToGDISurface
#undef IDirectDraw7_GetCaps
#undef IDirectDraw7_GetDisplayMode
#undef IDirectDraw7_GetFourCCCodes
#undef IDirectDraw7_GetGDISurface
#undef IDirectDraw7_GetMonitorFrequency
#undef IDirectDraw7_GetScanLine
#undef IDirectDraw7_GetVerticalBlankStatus
#undef IDirectDraw7_Initialize
#undef IDirectDraw7_RestoreAllSurfaces
#undef IDirectDraw7_RestoreDisplayMode
#undef IDirectDraw7_SetCooperativeLevel
#undef IDirectDraw7_SetDisplayMode
#undef IDirectDraw7_WaitForVerticalBlank
#undef IDirectDraw7_GetAvailableVidMem
#undef IDirectDraw7_GetSurfaceFromDC
#undef IDirectDraw7_RestoreSurfaceFromDC

#undef IDirectDrawSurface7_QueryInterface
#undef IDirectDrawSurface7_AddRef
#undef IDirectDrawSurface7_Release
#undef IDirectDrawSurface7_AddAttachedSurface
#undef IDirectDrawSurface7_AddOverlayDirtyRect
#undef IDirectDrawSurface7_Blt
#undef IDirectDrawSurface7_BltBatch
#undef IDirectDrawSurface7_BltFast
#undef IDirectDrawSurface7_DeleteAttachedSurface
#undef IDirectDrawSurface7_EnumAttachedSurfaces
#undef IDirectDrawSurface7_EnumOverlayZOrders
#undef IDirectDrawSurface7_Flip
#undef IDirectDrawSurface7_GetAttachedSurface
#undef IDirectDrawSurface7_GetBltStatus
#undef IDirectDrawSurface7_GetCaps
#undef IDirectDrawSurface7_GetClipper
#undef IDirectDrawSurface7_GetColorKey
#undef IDirectDrawSurface7_GetDC
#undef IDirectDrawSurface7_GetFlipStatus
#undef IDirectDrawSurface7_GetOverlayPosition
#undef IDirectDrawSurface7_GetPalette
#undef IDirectDrawSurface7_GetPixelFormat
#undef IDirectDrawSurface7_GetSurfaceDesc
#undef IDirectDrawSurface7_Initialize
#undef IDirectDrawSurface7_IsLost
#undef IDirectDrawSurface7_Lock
#undef IDirectDrawSurface7_ReleaseDC
#undef IDirectDrawSurface7_Restore
#undef IDirectDrawSurface7_SetClipper
#undef IDirectDrawSurface7_SetColorKey
#undef IDirectDrawSurface7_SetOverlayPosition
#undef IDirectDrawSurface7_SetPalette
#undef IDirectDrawSurface7_Unlock
#undef IDirectDrawSurface7_UpdateOverlay
#undef IDirectDrawSurface7_UpdateOverlayDisplay
#undef IDirectDrawSurface7_UpdateOverlayZOrder
#undef IDirectDrawSurface7_GetDDInterface
#undef IDirectDrawSurface7_PageLock
#undef IDirectDrawSurface7_PageUnlock
#undef IDirectDrawSurface7_SetSurfaceDesc
#undef IDirectDrawSurface7_SetPrivateData
#undef IDirectDrawSurface7_GetPrivateData
#undef IDirectDrawSurface7_FreePrivateData
#undef IDirectDrawSurface7_GetUniquenessValue
#undef IDirectDrawSurface7_ChangeUniquenessValue
#undef IDirectDrawSurface7_SetPriority
#undef IDirectDrawSurface7_GetPriority
#undef IDirectDrawSurface7_SetLOD
#undef IDirectDrawSurface7_GetLOD

#undef IDirectDrawPalette_QueryInterface
#undef IDirectDrawPalette_AddRef
#undef IDirectDrawPalette_Release
#undef IDirectDrawPalette_GetCaps
#undef IDirectDrawPalette_GetEntries
#undef IDirectDrawPalette_Initialize
#undef IDirectDrawPalette_SetEntries

#undef IDirectDrawClipper_QueryInterface
#undef IDirectDrawClipper_AddRef
#undef IDirectDrawClipper_Release
#undef IDirectDrawClipper_GetClipList
#undef IDirectDrawClipper_GetHWnd
#undef IDirectDrawClipper_Initialize
#undef IDirectDrawClipper_IsClipListChanged
#undef IDirectDrawClipper_SetClipList
#undef IDirectDrawClipper_SetHWnd

// Определения для совместимости с DirectDraw
#ifndef DD_OK
#define DD_OK 0
#endif

#ifndef DDERR_INVALIDPARAMS
#define DDERR_INVALIDPARAMS 0x88760066L
#endif

#ifndef DDERR_OUTOFMEMORY
#define DDERR_OUTOFMEMORY 0x8876000EL
#endif

#ifndef DDERR_UNSUPPORTED
#define DDERR_UNSUPPORTED 0x8876001EL
#endif

#ifndef DDERR_SURFACEBUSY
#define DDERR_SURFACEBUSY 0x88760022L
#endif

#ifndef DDERR_NOTLOCKED
#define DDERR_NOTLOCKED 0x8876001AL
#endif

#ifndef DDERR_INVALIDRECT
#define DDERR_INVALIDRECT 0x88760028L
#endif

#ifndef DDERR_NOFLIPHW
#define DDERR_NOFLIPHW 0x88760017L
#endif

#ifndef DDERR_WASSTILLDRAWING
#define DDERR_WASSTILLDRAWING 0x8876001EL
#endif

#ifndef DDPF_PALETTEINDEXED8
#define DDPF_PALETTEINDEXED8 0x00000020L
#endif

#ifndef DDPF_PALETTEINDEXED4
#define DDPF_PALETTEINDEXED4 0x00000008L
#endif

#ifndef DDSCAPS_PRIMARYSURFACE
#define DDSCAPS_PRIMARYSURFACE 0x00000200L
#endif

#ifndef DDSCAPS_BACKBUFFER
#define DDSCAPS_BACKBUFFER 0x00000004L
#endif

#ifndef DDSCAPS_OFFSCREENPLAIN
#define DDSCAPS_OFFSCREENPLAIN 0x00000040L
#endif

#ifndef DDSCAPS_TEXTURE
#define DDSCAPS_TEXTURE 0x00001000L
#endif

namespace ddvk {

//=============================================================================
// Forward declarations
//=============================================================================

class VulkanRenderer;
class SurfaceImpl;
class PaletteImpl;
class TextureImpl;
class PipelineImpl;

//=============================================================================
// Структуры данных
//=============================================================================

// Режим дисплея
struct DisplayMode {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t refreshRate;
    
    DisplayMode() : width(0), height(0), bpp(32), refreshRate(60) {}
    DisplayMode(uint32_t w, uint32_t h, uint32_t b, uint32_t r) 
        : width(w), height(h), bpp(b), refreshRate(r) {}
};

// Формат пикселя
struct PixelFormat {
    uint32_t bitsPerPixel;
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
    
    PixelFormat() : bitsPerPixel(32), redMask(0x00FF0000), 
                   greenMask(0x0000FF00), blueMask(0x000000FF), alphaMask(0xFF000000) {}
};

//=============================================================================
// Базовый класс для COM-подобных объектов
//=============================================================================

class BaseImpl {
public:
    std::atomic<ULONG> refCount;
    void* userData; // Для хранения указателя на объект DirectDraw
    
    BaseImpl() : refCount(1), userData(nullptr) {}
    virtual ~BaseImpl() {}
    
    ULONG AddRef() { return ++refCount; }
    ULONG Release() { 
        ULONG ref = --refCount;
        if (ref == 0) {
            delete this;
        }
        return ref;
    }
};

//=============================================================================
// Реализация DirectDraw объекта
//=============================================================================

class DirectDrawImpl : public BaseImpl {
public:
    VulkanRenderer* renderer;
    HWND hwnd;
    DWORD cooperativeLevel;
    DisplayMode currentMode;
    std::vector<SurfaceImpl*> surfaces;
    SurfaceImpl* primarySurface;
    
    DirectDrawImpl(VulkanRenderer* r);
    virtual ~DirectDrawImpl();
};

//=============================================================================
// Реализация поверхности
//=============================================================================

class SurfaceImpl : public BaseImpl {
public:
    VulkanRenderer* renderer;
    
    // Характеристики поверхности
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    PixelFormat format;
    
    // Типы поверхностей
    bool isPrimary;
    bool isBackBuffer;
    bool isOffscreen;
    bool isTexture;
    
    // Данные
    void* mappedData;
    size_t dataSize;
    bool isLocked;
    
    // Связанные объекты
    SurfaceImpl* backBuffer;
    SurfaceImpl* frontBuffer;
    struct IDirectDraw7* parentDD;  // DirectDraw, создавший поверхность (для GetDDInterface)
    struct IDirectDrawPalette* palette;
    PaletteImpl* paletteImpl;      // наш PaletteImpl при SetPalette (чтобы не кастовать чужой указатель в Present)
    struct IDirectDrawClipper* clipper;
    // Цветовой ключ источника (для Blt/BltFast с DDBLT_KEYSRC / SRCCOLORKEY)
    bool hasColorKey;
    DWORD colorKeyLow;
    DWORD colorKeyHigh;
    
    // Текстура Vulkan
    TextureImpl* texture;
    
    SurfaceImpl(VulkanRenderer* r);
    virtual ~SurfaceImpl();
    
    // Методы
    bool Create(const DDSURFACEDESC2* desc);
    bool Duplicate(SurfaceImpl* original);
    HRESULT Lock(const RECT* rect, DDSURFACEDESC2* desc, DWORD flags);
    HRESULT Unlock(const RECT* rect);
    HRESULT Blt(const RECT* destRect, SurfaceImpl* src, const RECT* srcRect, DWORD flags);
    HRESULT Flip(SurfaceImpl* target, DWORD flags);
    HRESULT UpdateFromDC(HDC hdc, const RECT* rect);
    
    // Вспомогательные
    void* GetPixelAddress(int32_t x, int32_t y);
    void FillColor(uint32_t color, const RECT* rect);
    void CopyFrom(SurfaceImpl* src, const RECT* srcRect, const RECT* destRect);
};

//=============================================================================
// Реализация палитры
//=============================================================================

class PaletteImpl : public BaseImpl {
public:
    DWORD flags;
    std::vector<PALETTEENTRY> entries;
    std::vector<uint32_t> vulkanColors; // Преобразованные цвета для Vulkan
    
    PaletteImpl(DWORD f, const PALETTEENTRY* colors);
    virtual ~PaletteImpl();
    
    void SetEntries(DWORD start, DWORD count, const PALETTEENTRY* colors);
    void GetEntries(DWORD start, DWORD count, PALETTEENTRY* colors) const;
    void UpdateVulkanColors();
};

//=============================================================================
// Реализация клиппера
//=============================================================================

class ClipperImpl : public BaseImpl {
public:
    HWND hwnd;
    std::vector<RECT> clipRects;
    bool isHWndBased;
    
    ClipperImpl();
    virtual ~ClipperImpl();
    
    void SetHWnd(HWND h);
    void SetClipList(const RECT* rects, DWORD count);
    bool IsPointVisible(int32_t x, int32_t y) const;
    bool IntersectRect(RECT* result, const RECT* rect) const;
};

//=============================================================================
// Текстура Vulkan (ЕДИНСТВЕННОЕ ОПРЕДЕЛЕНИЕ)
//=============================================================================

class TextureImpl : public BaseImpl {
public:
    VkImage image;
    VmaAllocation allocation;
    VkImageView view;
    VkSampler sampler;
    VkFormat vkFormat;
    uint32_t width;
    uint32_t height;
    VkImageLayout currentLayout;
    bool ownsSampler;
    
    TextureImpl();
    virtual ~TextureImpl();
    
    bool Create(VulkanRenderer* renderer, uint32_t w, uint32_t h, VkFormat fmt);
    void Destroy(VulkanRenderer* renderer);
    bool UpdateFromData(const void* data, size_t size);
    
    VkImage GetImage() const { return image; }
    VkImageView GetImageView() const { return view; }
    VkSampler GetSampler() const { return sampler; }
    VkFormat GetFormat() const { return vkFormat; }
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }
    VkImageLayout GetCurrentLayout() const { return currentLayout; }
    void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    VkDescriptorImageInfo GetDescriptorInfo() const;
    bool CopyFromTexture(TextureImpl* src, VkCommandBuffer cmd = VK_NULL_HANDLE);
    bool CopyToBuffer(VkBuffer buffer, VkCommandBuffer cmd = VK_NULL_HANDLE);
};

//=============================================================================
// Пайплайн Vulkan
//=============================================================================

struct PipelineInfo {
    VkPrimitiveTopology topology;
    VkPolygonMode polygonMode;
    VkCullModeFlags cullMode;
    VkFrontFace frontFace;
    bool depthTest;
    bool depthWrite;
    bool blending;
};

class PipelineImpl : public BaseImpl {
public:
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    PipelineInfo info;
    
    PipelineImpl();
    virtual ~PipelineImpl();
    
    bool Create(VulkanRenderer* renderer, const PipelineInfo& inf, 
                const std::vector<uint32_t>& vertShader,
                const std::vector<uint32_t>& fragShader);
    void Destroy(VkDevice device);
};

//=============================================================================
// Контекст команд
//=============================================================================

struct DrawCommand {
    enum Type {
        CMD_BLT,
        CMD_FILL,
        CMD_FLIP,
        CMD_CLEAR,
        CMD_PRESENT
    };
    
    Type type;
    SurfaceImpl* dest;
    SurfaceImpl* src;
    RECT destRect;
    RECT srcRect;
    uint32_t color;
    DWORD flags;
};

class CommandQueue {
public:
    std::vector<DrawCommand> commands;
    std::mutex mutex;
    
    void Push(const DrawCommand& cmd);
    bool Pop(DrawCommand& cmd);
    void Clear();
    size_t Size();
};

//=============================================================================
// Глобальные функции
//=============================================================================

// Получение глобального рендерера
VulkanRenderer* GetVulkanRenderer();

// Освобождение рендерера
void ReleaseVulkanRenderer();

// Преобразование форматов
VkFormat DDrawFormatToVulkan(uint32_t bpp, const DDPIXELFORMAT* format);
uint32_t DDrawColorToRGBA(uint32_t color, const DDPIXELFORMAT* format);

} // namespace ddvk