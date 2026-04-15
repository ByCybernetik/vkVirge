#include "ddvk_blt.h"
#include "ddvk_renderer.h"
#include <algorithm>
#include <cstring>

namespace ddvk {

//=============================================================================
// BlitManager - реализация
//=============================================================================

BlitManager::BlitManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_useHardware(true) {
}

BlitManager::~BlitManager() {
}

HRESULT BlitManager::Blit(SurfaceImpl* dest, SurfaceImpl* src,
                          const RECT* destRect, const RECT* srcRect,
                          DWORD flags, const DDBLTFX* fx) {
    if (!dest || !src) return DDERR_INVALIDPARAMS;
    
    // Проверяем возможность blit
    if (!CanBlit(dest, src)) {
        return DDERR_NOBLTHW;
    }
    
    // Определяем прямоугольники
    RECT srcR = {0, 0, (LONG)src->width, (LONG)src->height};
    if (srcRect) srcR = *srcRect;
    
    RECT destR = {0, 0, (LONG)dest->width, (LONG)dest->height};
    if (destRect) destR = *destRect;
    
    // Проверяем на выход за границы
    if (destR.left < 0 || destR.top < 0 || 
        destR.right > (LONG)dest->width || destR.bottom > (LONG)dest->height) {
        return DDERR_INVALIDRECT;
    }
    
    // Выбираем метод blit
    HRESULT result;
    
    if (m_useHardware && src->texture && dest->texture) {
        result = BlitGPU(dest, src, destR, srcR, flags, fx);
    } else {
        result = BlitCPU(dest, src, destR, srcR, flags, fx);
    }
    
    return result;
}

HRESULT BlitManager::BlitFast(SurfaceImpl* dest, SurfaceImpl* src,
                              uint32_t x, uint32_t y,
                              const RECT* srcRect, DWORD trans) {
    if (!dest || !src) return DDERR_INVALIDPARAMS;
    
    // Для BlitFast координаты задаются позицией верхнего левого угла
    RECT srcR = {0, 0, (LONG)src->width, (LONG)src->height};
    if (srcRect) srcR = *srcRect;
    
    RECT destR = {(LONG)x, (LONG)y, 
                  (LONG)x + (srcR.right - srcR.left),
                  (LONG)y + (srcR.bottom - srcR.top)};
    
    return Blit(dest, src, &destR, &srcR, trans, nullptr);
}

HRESULT BlitManager::ColorFill(SurfaceImpl* dest, const RECT* rect, uint32_t color) {
    if (!dest) return DDERR_INVALIDPARAMS;
    
    RECT fillRect = {0, 0, (LONG)dest->width, (LONG)dest->height};
    if (rect) fillRect = *rect;
    
    // Заполняем цветом
    dest->FillColor(color, &fillRect);
    
    return DD_OK;
}

bool BlitManager::CanBlit(SurfaceImpl* dest, SurfaceImpl* src) const {
    if (!dest || !src) return false;
    
    // Проверяем совместимость форматов
    if (dest->bpp != src->bpp) {
        // В реальном коде здесь может быть конвертация
        return false;
    }
    
    return true;
}

bool BlitManager::CanStretch(SurfaceImpl* dest, SurfaceImpl* src) const {
    // Проверяем возможность масштабирования
    if (!dest || !src) return false;
    
    // В реальном коде проверяем поддержку hardware stretching
    return true;
}

bool BlitManager::CanColorFill(SurfaceImpl* dest) const {
    return dest != nullptr;
}

HRESULT BlitManager::BlitCPU(SurfaceImpl* dest, SurfaceImpl* src,
                             const RECT& destRect, const RECT& srcRect,
                             DWORD flags, const DDBLTFX* fx) {
    if (!dest->mappedData || !src->mappedData) {
        return DDERR_SURFACELOST;
    }
    if (m_renderer && m_renderer->IsDeferredBuffer(src->mappedData)) {
        return DDERR_SURFACELOST;
    }
    
    int srcW = srcRect.right - srcRect.left;
    int srcH = srcRect.bottom - srcRect.top;
    int destW = destRect.right - destRect.left;
    int destH = destRect.bottom - destRect.top;
    
    size_t bytesPerPixel = dest->bpp / 8;
    
    // Проверяем на цветовой ключ
    uint32_t colorKey = 0;
    bool useColorKey = false;
    
    if (flags & (DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE)) {
        if (fx && (flags & DDBLT_KEYSRCOVERRIDE)) {
            colorKey = fx->ddckSrcColorkey.dwColorSpaceLowValue;
        } else {
            // Используем цветовой ключ из поверхности
            // В реальном коде нужно получить из src
        }
        useColorKey = true;
    }
    
    // Копируем с преобразованием
    for (int y = 0; y < destH; y++) {
        int srcY = srcRect.top + (y * srcH) / destH;
        srcY = std::min(srcY, (int)src->height - 1);
        
        for (int x = 0; x < destW; x++) {
            int srcX = srcRect.left + (x * srcW) / destW;
            srcX = std::min(srcX, (int)src->width - 1);
            
            void* srcPixel = src->GetPixelAddress(srcX, srcY);
            void* dstPixel = dest->GetPixelAddress(destRect.left + x, destRect.top + y);
            
            if (srcPixel && dstPixel) {
                if (useColorKey) {
                    // Проверяем цветовой ключ
                    uint32_t pixelColor = 0;
                    memcpy(&pixelColor, srcPixel, bytesPerPixel);
                    
                    if (pixelColor != colorKey) {
                        memcpy(dstPixel, srcPixel, bytesPerPixel);
                    }
                } else {
                    memcpy(dstPixel, srcPixel, bytesPerPixel);
                }
            }
        }
    }
    
    return DD_OK;
}

HRESULT BlitManager::BlitGPU(SurfaceImpl* dest, SurfaceImpl* src,
                             const RECT& destRect, const RECT& srcRect,
                             DWORD flags, const DDBLTFX* fx) {
    // Здесь должна быть реализация GPU blit через Vulkan
    // Для простоты используем CPU версию
    return BlitCPU(dest, src, destRect, srcRect, flags, fx);
}

void BlitManager::ApplyColorKey(SurfaceImpl* src, SurfaceImpl* dest,
                                const RECT& destRect, const RECT& srcRect,
                                uint32_t colorKey) {
    // Применение цветового ключа при копировании
    size_t bytesPerPixel = dest->bpp / 8;
    const int srcWidth  = srcRect.right  - srcRect.left;
    const int srcHeight = srcRect.bottom - srcRect.top;
    
    for (int y = 0; y < srcHeight; y++) {
        for (int x = 0; x < srcWidth; x++) {
            void* srcPixel = src->GetPixelAddress(srcRect.left + x, srcRect.top + y);
            void* dstPixel = dest->GetPixelAddress(destRect.left + x, destRect.top + y);
            
            if (srcPixel && dstPixel) {
                uint32_t pixelColor = 0;
                memcpy(&pixelColor, srcPixel, bytesPerPixel);
                
                if (pixelColor != colorKey) {
                    memcpy(dstPixel, srcPixel, bytesPerPixel);
                }
            }
        }
    }
}

void BlitManager::ApplyStretch(SurfaceImpl* dest, SurfaceImpl* src,
                               const RECT& destRect, const RECT& srcRect) {
    // Масштабирование с простой интерполяцией
    float scaleX = (float)(srcRect.right - srcRect.left) / (destRect.right - destRect.left);
    float scaleY = (float)(srcRect.bottom - srcRect.top) / (destRect.bottom - destRect.top);
    const int destWidth  = destRect.right  - destRect.left;
    const int destHeight = destRect.bottom - destRect.top;

    size_t bytesPerPixel = dest->bpp / 8;
    
    for (int y = 0; y < destHeight; y++) {
        float srcY = srcRect.top + y * scaleY;
        int srcY0 = (int)srcY;
        int srcY1 = std::min(srcY0 + 1, (int)src->height - 1);
        float fy = srcY - srcY0;
        
        for (int x = 0; x < destWidth; x++) {
            float srcX = srcRect.left + x * scaleX;
            int srcX0 = (int)srcX;
            int srcX1 = std::min(srcX0 + 1, (int)src->width - 1);
            float fx = srcX - srcX0;
            
            // Билинейная интерполяция
            uint32_t p00, p01, p10, p11;
            memcpy(&p00, src->GetPixelAddress(srcX0, srcY0), bytesPerPixel);
            memcpy(&p01, src->GetPixelAddress(srcX1, srcY0), bytesPerPixel);
            memcpy(&p10, src->GetPixelAddress(srcX0, srcY1), bytesPerPixel);
            memcpy(&p11, src->GetPixelAddress(srcX1, srcY1), bytesPerPixel);
            
            // Простая интерполяция для демонстрации
            uint32_t result = p00; // Упрощенно
            
            void* dstPixel = dest->GetPixelAddress(destRect.left + x, destRect.top + y);
            if (dstPixel) {
                memcpy(dstPixel, &result, bytesPerPixel);
            }
        }
    }
}

} // namespace ddvk