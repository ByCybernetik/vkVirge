#pragma once

#include "ddvk_core.h"

namespace ddvk {

//=============================================================================
// Эмуляция blit операций
//=============================================================================

class BlitManager {
public:
    BlitManager(VulkanRenderer* renderer);
    ~BlitManager();
    
    // Основные операции
    HRESULT Blit(SurfaceImpl* dest, SurfaceImpl* src,
                 const RECT* destRect, const RECT* srcRect,
                 DWORD flags, const DDBLTFX* fx);
    
    HRESULT BlitFast(SurfaceImpl* dest, SurfaceImpl* src,
                     uint32_t x, uint32_t y,
                     const RECT* srcRect, DWORD trans);
    
    HRESULT ColorFill(SurfaceImpl* dest, const RECT* rect, uint32_t color);
    
    // Проверка возможностей
    bool CanBlit(SurfaceImpl* dest, SurfaceImpl* src) const;
    bool CanStretch(SurfaceImpl* dest, SurfaceImpl* src) const;
    bool CanColorFill(SurfaceImpl* dest) const;
    
    // Оптимизация
    void SetUseHardware(bool use) { m_useHardware = use; }
    bool GetUseHardware() const { return m_useHardware; }
    
private:
    VulkanRenderer* m_renderer;
    bool m_useHardware;
    
    // Реализации
    HRESULT BlitCPU(SurfaceImpl* dest, SurfaceImpl* src,
                    const RECT& destRect, const RECT& srcRect,
                    DWORD flags, const DDBLTFX* fx);
    
    HRESULT BlitGPU(SurfaceImpl* dest, SurfaceImpl* src,
                    const RECT& destRect, const RECT& srcRect,
                    DWORD flags, const DDBLTFX* fx);
    
    // Вспомогательные функции
    void ApplyColorKey(SurfaceImpl* src, SurfaceImpl* dest,
                       const RECT& destRect, const RECT& srcRect,
                       uint32_t colorKey);
    
    void ApplyStretch(SurfaceImpl* dest, SurfaceImpl* src,
                      const RECT& destRect, const RECT& srcRect);
};

} // namespace ddvk