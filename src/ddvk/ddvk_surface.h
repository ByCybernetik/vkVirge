#pragma once

#include "ddvk_core.h"

namespace ddvk {

//=============================================================================
// Класс для работы с поверхностями в Vulkan
//=============================================================================

class SurfaceManager {
public:
    SurfaceManager(VulkanRenderer* renderer);
    ~SurfaceManager();
    
    // Создание и удаление
    SurfaceImpl* CreatePrimarySurface(const DDSURFACEDESC2& desc);
    SurfaceImpl* CreateOffscreenSurface(const DDSURFACEDESC2& desc);
    SurfaceImpl* CreateTextureSurface(const DDSURFACEDESC2& desc);
    
    // Поиск поверхностей
    SurfaceImpl* FindSurfaceByHandle(IDirectDrawSurface7* handle);
    SurfaceImpl* GetPrimarySurface() const { return m_primarySurface; }
    
    // Обновление
    void UpdateAllSurfaces();
    void MarkDirty(SurfaceImpl* surface);
    
    // Рендеринг
    void RenderSurface(SurfaceImpl* surface, const Rect& destRect);
    void BlitSurface(SurfaceImpl* src, SurfaceImpl* dst, 
                     const Rect& srcRect, const Rect& dstRect);
    
private:
    VulkanRenderer* m_renderer;
    SurfaceImpl* m_primarySurface;
    std::vector<SurfaceImpl*> m_surfaces;
    std::vector<SurfaceImpl*> m_dirtySurfaces;
    
    // Вспомогательные методы
    void UploadSurfaceToGPU(SurfaceImpl* surface);
    void DownloadSurfaceFromGPU(SurfaceImpl* surface);
};

} // namespace ddvk