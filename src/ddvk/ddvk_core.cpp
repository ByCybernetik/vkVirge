#include "ddvk_core.h"
#include "ddvk_renderer.h"
#include "ddvk_utils.h"
#include <cstring>
#include <algorithm>

namespace ddvk {

static inline int RectWidth(const RECT& r) {
    return r.right - r.left;
}

static inline int RectHeight(const RECT& r) {
    return r.bottom - r.top;
}

DirectDrawImpl::DirectDrawImpl(VulkanRenderer* r) 
    : renderer(r)
    , hwnd(nullptr)
    , cooperativeLevel(0)
    , primarySurface(nullptr) {
    
    currentMode = DisplayMode(640, 480, 32, 60);
}

DirectDrawImpl::~DirectDrawImpl() {
    for (auto surface : surfaces) {
        surface->Release();
    }
    surfaces.clear();
}

SurfaceImpl::SurfaceImpl(VulkanRenderer* r)
    : renderer(r)
    , width(0)
    , height(0)
    , bpp(32)
    , isPrimary(false)
    , isBackBuffer(false)
    , isOffscreen(true)
    , isTexture(false)
    , mappedData(nullptr)
    , dataSize(0)
    , isLocked(false)
    , backBuffer(nullptr)
    , frontBuffer(nullptr)
    , parentDD(nullptr)
    , palette(nullptr)
    , paletteImpl(nullptr)
    , clipper(nullptr)
    , texture(nullptr)
    , hasColorKey(false)
    , colorKeyLow(0)
    , colorKeyHigh(0) {
    
    format = PixelFormat();
}

SurfaceImpl::~SurfaceImpl() {
    if (mappedData) {
        if (renderer)
            renderer->DeferFreeMappedData(mappedData);
        else
            free(mappedData);
        mappedData = nullptr;
    }
    if (texture) {
        texture->Release();
    }
}

bool SurfaceImpl::Create(const DDSURFACEDESC2* desc) {
    if (!desc) return false;
    
    width = desc->dwWidth;
    height = desc->dwHeight;
    
    if (desc->ddpfPixelFormat.dwRGBBitCount) {
        bpp = desc->ddpfPixelFormat.dwRGBBitCount;
    } else if (renderer) {
        // Если формат явно не задан, подстраиваемся под текущий режим
        // отображения DirectDraw. Это важно для старых приложений (например,
        // Westwood), которые создают primary/offscreen поверхности только
        // через CAPS/WIDTH/HEIGHT и полагаются на bpp, заданный SetDisplayMode.
        DDSURFACEDESC2 mode{};
        mode.dwSize = sizeof(mode);
        if (SUCCEEDED(renderer->GetDisplayMode(&mode)) &&
            mode.ddpfPixelFormat.dwRGBBitCount) {
            bpp = mode.ddpfPixelFormat.dwRGBBitCount;
        }
    }
    
    isPrimary = (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    isBackBuffer = (desc->ddsCaps.dwCaps & DDSCAPS_BACKBUFFER) != 0;
    isOffscreen = (desc->ddsCaps.dwCaps & DDSCAPS_OFFSCREENPLAIN) != 0;
    isTexture = (desc->ddsCaps.dwCaps & DDSCAPS_TEXTURE) != 0;

    format.bitsPerPixel = bpp;
    if (desc->ddpfPixelFormat.dwSize >= sizeof(DDPIXELFORMAT) && (desc->ddpfPixelFormat.dwFlags & DDPF_RGB)) {
        format.redMask   = desc->ddpfPixelFormat.dwRBitMask;
        format.greenMask = desc->ddpfPixelFormat.dwGBitMask;
        format.blueMask  = desc->ddpfPixelFormat.dwBBitMask;
    } else if (bpp == 16) {
        format.redMask   = 0xF800;
        format.greenMask = 0x07E0;
        format.blueMask  = 0x001F;
    } else if (bpp == 32) {
        format.redMask   = 0x00FF0000;
        format.greenMask = 0x0000FF00;
        format.blueMask  = 0x000000FF;
    }

    dataSize = width * height * (bpp / 8);
    mappedData = malloc(dataSize);
    if (!mappedData) return false;
    
    memset(mappedData, 0, dataSize);
    
    return true;
}

bool SurfaceImpl::Duplicate(SurfaceImpl* original) {
    if (!original) return false;
    
    width = original->width;
    height = original->height;
    bpp = original->bpp;
    format = original->format;
    isPrimary = false;
    isBackBuffer = original->isBackBuffer;
    isOffscreen = true;
    
    dataSize = width * height * (bpp / 8);
    mappedData = malloc(dataSize);
    if (!mappedData) return false;
    
    if (original->mappedData) {
        memcpy(mappedData, original->mappedData, dataSize);
    }
    
    return true;
}

HRESULT SurfaceImpl::Lock(const RECT* rect, DDSURFACEDESC2* desc, DWORD flags) {
    if (!desc) return DDERR_INVALIDPARAMS;
    if (isLocked) return DDERR_SURFACEBUSY;
    
    // Заполняем структуру аналогично soft‑реализации (other/ddvk),
    // чтобы приложения могли полагаться на поля desc.
    std::memset(desc, 0, sizeof(DDSURFACEDESC2));
    desc->dwSize  = sizeof(DDSURFACEDESC2);
    desc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH |
                    DDSD_LPSURFACE | DDSD_PIXELFORMAT | DDSD_CAPS;

    desc->dwWidth  = width;
    desc->dwHeight = height;
    desc->lPitch   = width * (bpp / 8);
    desc->lpSurface = mappedData;

    desc->ddpfPixelFormat.dwSize       = sizeof(DDPIXELFORMAT);
    desc->ddpfPixelFormat.dwFlags      = DDPF_RGB;
    desc->ddpfPixelFormat.dwRGBBitCount = bpp;
    if (bpp == 32) {
        desc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
        desc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
        desc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
    } else if (bpp == 16) {
        desc->ddpfPixelFormat.dwRBitMask = 0xF800;
        desc->ddpfPixelFormat.dwGBitMask = 0x07E0;
        desc->ddpfPixelFormat.dwBBitMask = 0x001F;
    }

    desc->ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;
    if (isPrimary)   desc->ddsCaps.dwCaps |= DDSCAPS_PRIMARYSURFACE;
    if (isBackBuffer) desc->ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
    
    if (rect) {
        size_t bytesPerPixel = bpp / 8;
        size_t offset = rect->top * desc->lPitch + rect->left * bytesPerPixel;
        desc->lpSurface = static_cast<char*>(mappedData) + offset;
    }
    
    isLocked = true;
    (void)flags;
    return DD_OK;
}

HRESULT SurfaceImpl::Unlock(const RECT* rect) {
    if (!isLocked) return DDERR_NOTLOCKED;
    isLocked = false;
    return DD_OK;
}

HRESULT SurfaceImpl::Blt(const RECT* destRect, SurfaceImpl* src, const RECT* srcRect, DWORD flags) {
    if (!src || !mappedData) return DDERR_INVALIDPARAMS;

    RECT srcR = {0, 0, (LONG)src->width, (LONG)src->height};
    if (srcRect) srcR = *srcRect;

    RECT destR = {0, 0, (LONG)width, (LONG)height};
    if (destRect) destR = *destRect;

    if (destR.left < 0 || destR.top < 0 ||
        destR.right > (LONG)width || destR.bottom > (LONG)height) {
        return DDERR_INVALIDRECT;
    }

    if (renderer) {
        return renderer->BltCopyUnderLock(this, src, &srcR, &destR);
    }
    if (!src->mappedData) return DDERR_SURFACELOST;
    CopyFrom(src, &srcR, &destR);
    return DD_OK;
}

HRESULT SurfaceImpl::Flip(SurfaceImpl* target, DWORD flags) {
    if (!target) return DDERR_INVALIDPARAMS;
    
    if (width != target->width || height != target->height || bpp != target->bpp) {
        return DDERR_INVALIDPARAMS;
    }
    
    std::swap(mappedData, target->mappedData);
    std::swap(dataSize, target->dataSize);
    std::swap(texture, target->texture);
    
    bool tempPrimary = isPrimary;
    isPrimary = target->isPrimary;
    target->isPrimary = tempPrimary;
    
    bool tempBack = isBackBuffer;
    isBackBuffer = target->isBackBuffer;
    target->isBackBuffer = tempBack;
    
    return DD_OK;
}

void* SurfaceImpl::GetPixelAddress(int32_t x, int32_t y) {
    if (!mappedData) return nullptr;
    if (x < 0 || x >= (int32_t)width || y < 0 || y >= (int32_t)height) return nullptr;
    
    size_t bytesPerPixel = bpp / 8;
    size_t offset = y * width * bytesPerPixel + x * bytesPerPixel;
    
    return static_cast<char*>(mappedData) + offset;
}

void SurfaceImpl::FillColor(uint32_t color, const RECT* rect) {
    if (!mappedData) return;
    
    RECT fillRect = rect ? *rect : RECT{0, 0, (LONG)width, (LONG)height};
    size_t bytesPerPixel = bpp / 8;
    size_t rowStride = width * bytesPerPixel;
    
    for (int y = fillRect.top; y < fillRect.bottom; y++) {
        void* rowStart = static_cast<char*>(mappedData) + y * rowStride + fillRect.left * bytesPerPixel;
        if (bytesPerPixel == 4) {
            uint32_t* pixels = static_cast<uint32_t*>(rowStart);
            for (int x = fillRect.left; x < fillRect.right; x++) {
                *pixels++ = color;
            }
        } else if (bytesPerPixel == 1) {
            uint8_t c = static_cast<uint8_t>(color & 0xFF);
            memset(rowStart, c, (fillRect.right - fillRect.left) * bytesPerPixel);
        }
    }
}

void SurfaceImpl::CopyFrom(SurfaceImpl* src, const RECT* srcRect, const RECT* destRect) {
    if (!src || !src->mappedData || !mappedData) return;
    
    RECT srcR = srcRect ? *srcRect : RECT{0, 0, (LONG)src->width, (LONG)src->height};
    RECT destR = destRect ? *destRect : RECT{0, 0, (LONG)width, (LONG)height};
    
    int copyWidth = RectWidth(srcR);
    int copyHeight = RectHeight(srcR);
    
    if (copyWidth > RectWidth(destR)) copyWidth = RectWidth(destR);
    if (copyHeight > RectHeight(destR)) copyHeight = RectHeight(destR);
    
    size_t bytesPerPixel = bpp / 8;
    size_t srcStride = src->width * bytesPerPixel;
    size_t dstStride = width * bytesPerPixel;

    const bool useColorKey = src->hasColorKey && (src->colorKeyLow == src->colorKeyHigh);
    uint32_t key = src->colorKeyLow;

    // Копирование с учётом возможного перекрытия областей в рамках одной поверхности.
    if (!useColorKey && src == this) {
        // Определяем направление копирования для поведения, эквивалентного memmove.
        const bool copyForward =
            (destR.top < srcR.top) ||
            (destR.top == srcR.top && destR.left <= srcR.left);

        if (copyForward) {
            for (int y = 0; y < copyHeight; ++y) {
                char* srcLine = static_cast<char*>(mappedData) +
                                (srcR.top + y) * srcStride + srcR.left * bytesPerPixel;
                char* dstLine = static_cast<char*>(mappedData) +
                                (destR.top + y) * dstStride + destR.left * bytesPerPixel;
                std::memmove(dstLine, srcLine, copyWidth * bytesPerPixel);
            }
        } else {
            for (int y = copyHeight - 1; y >= 0; --y) {
                char* srcLine = static_cast<char*>(mappedData) +
                                (srcR.top + y) * srcStride + srcR.left * bytesPerPixel;
                char* dstLine = static_cast<char*>(mappedData) +
                                (destR.top + y) * dstStride + destR.left * bytesPerPixel;
                std::memmove(dstLine, srcLine, copyWidth * bytesPerPixel);
            }
        }
        return;
    }

    for (int y = 0; y < copyHeight; y++) {
        char* srcLine = static_cast<char*>(src->mappedData) + (srcR.top + y) * srcStride + srcR.left * bytesPerPixel;
        char* dstLine = static_cast<char*>(mappedData) + (destR.top + y) * dstStride + destR.left * bytesPerPixel;

        if (!useColorKey) {
            std::memcpy(dstLine, srcLine, copyWidth * bytesPerPixel);
            continue;
        }

        for (int x = 0; x < copyWidth; x++) {
            char* s = srcLine + x * bytesPerPixel;
            char* d = dstLine + x * bytesPerPixel;
            uint32_t pixel = 0;
            // читаем до 4 байт (для 8/16/32 bpp)
            std::memcpy(&pixel, s, bytesPerPixel);
            if (pixel == key) {
                continue; // пропускаем прозрачный пиксель
            }
            std::memcpy(d, s, bytesPerPixel);
        }
    }
}

PaletteImpl::PaletteImpl(DWORD f, const PALETTEENTRY* colors) 
    : flags(f) {
    
    DWORD count = 0;
    if (flags & DDPCAPS_1BIT) count = 2;
    else if (flags & DDPCAPS_2BIT) count = 4;
    else if (flags & DDPCAPS_4BIT) count = 16;
    else if (flags & DDPCAPS_8BIT) count = 256;
    
    entries.resize(count);
    vulkanColors.resize(count);
    
    if (colors) {
        memcpy(entries.data(), colors, count * sizeof(PALETTEENTRY));
    }
    
    UpdateVulkanColors();
}

PaletteImpl::~PaletteImpl() {
}

void PaletteImpl::UpdateVulkanColors() {
    for (size_t i = 0; i < entries.size(); i++) {
        uint8_t red = entries[i].peRed;
        uint8_t green = entries[i].peGreen;
        uint8_t blue = entries[i].peBlue;
        std::swap(red, blue);
        vulkanColors[i] = (0xFF << 24) | 
                          (red << 16) |
                          (green << 8) |
                          blue;
    }
}

void PaletteImpl::SetEntries(DWORD start, DWORD count, const PALETTEENTRY* colors) {
    if (!colors || count == 0) return;
    if (start >= entries.size()) return;
    size_t maxCount = std::min<size_t>(static_cast<size_t>(count), entries.size() - start);
    std::memcpy(entries.data() + start, colors, maxCount * sizeof(PALETTEENTRY));
    UpdateVulkanColors();
}

void PaletteImpl::GetEntries(DWORD start, DWORD count, PALETTEENTRY* colors) const {
    if (!colors || count == 0) return;
    if (start >= entries.size()) return;
    size_t maxCount = std::min<size_t>(static_cast<size_t>(count), entries.size() - start);
    std::memcpy(colors, entries.data() + start, maxCount * sizeof(PALETTEENTRY));
}

ClipperImpl::ClipperImpl() 
    : hwnd(nullptr)
    , isHWndBased(false) {
}

ClipperImpl::~ClipperImpl() {
}

void ClipperImpl::SetHWnd(HWND h) {
    hwnd = h;
    isHWndBased = true;
    clipRects.clear();
}

//=============================================================================
// Глобальный доступ к VulkanRenderer
//=============================================================================

static VulkanRenderer* g_vulkanRenderer = nullptr;

VulkanRenderer* GetVulkanRenderer() {
    // Отложенная инициализация: только создаём объект,
    // реальный Initialize вызывается в SetCooperativeLevel,
    // когда уже известен HWND.
    if (!g_vulkanRenderer) {
        g_vulkanRenderer = new VulkanRenderer();
    }
    return g_vulkanRenderer;
}

void ReleaseVulkanRenderer() {
    if (g_vulkanRenderer) {
        g_vulkanRenderer->Shutdown();
        delete g_vulkanRenderer;
        g_vulkanRenderer = nullptr;
    }
}

void ClipperImpl::SetClipList(const RECT* rects, DWORD count) {
    isHWndBased = false;
    clipRects.clear();
    
    if (rects && count > 0) {
        clipRects.insert(clipRects.end(), rects, rects + count);
    }
}

// Реализация TextureImpl находится в ddvk_texture.cpp

} // namespace ddvk