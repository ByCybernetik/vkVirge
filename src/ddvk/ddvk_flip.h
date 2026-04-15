#pragma once

#include "ddvk_core.h"

namespace ddvk {

//=============================================================================
// Эмуляция флипа страниц
//=============================================================================

class FlipManager {
public:
    FlipManager(VulkanRenderer* renderer);
    ~FlipManager();
    
    // Настройка флипа
    void SetFlipChain(SurfaceImpl* front, SurfaceImpl* back);
    void AddFlipSurface(SurfaceImpl* surface);
    void RemoveFlipSurface(SurfaceImpl* surface);
    
    // Выполнение флипа
    HRESULT Flip(SurfaceImpl* target, DWORD flags);
    bool IsFlipPending() const;
    void WaitForFlip();
    
    // Управление цепочкой
    SurfaceImpl* GetFrontBuffer() const { return m_frontBuffer; }
    SurfaceImpl* GetBackBuffer() const { return m_backBuffer; }
    SurfaceImpl* GetNextSurface() const;
    
private:
    VulkanRenderer* m_renderer;
    SurfaceImpl* m_frontBuffer;
    SurfaceImpl* m_backBuffer;
    std::vector<SurfaceImpl*> m_flipChain;
    bool m_flipPending;
    
    // Реализация разных типов флипа
    HRESULT FlipWithSwap(SurfaceImpl* target);
    HRESULT FlipWithCopy(SurfaceImpl* target);
    HRESULT FlipWithTexture(SurfaceImpl* target);
};

} // namespace ddvk