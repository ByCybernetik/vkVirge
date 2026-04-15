#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace ddvk {

//=============================================================================
// Управление swapchain
//=============================================================================

class SwapchainManager {
public:
    SwapchainManager(VulkanRenderer* renderer);
    ~SwapchainManager();
    
    // Инициализация
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();
    
    // Получение изображений
    VkImage GetCurrentImage();
    VkImageView GetCurrentImageView();
    uint32_t GetCurrentImageIndex();
    
    // Презентация
    bool AcquireNextImage();
    bool Present();
    
    // Пересоздание
    bool Resize(uint32_t width, uint32_t height);
    void Recreate();
    /** Создать swapchain при отложенной инициализации (окно без размера при SetCooperativeLevel). */
    bool EnsureSwapchain();

    // Состояние
    bool IsValid() const { return m_swapchain != VK_NULL_HANDLE; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    
private:
    VulkanRenderer* m_renderer;
    VkSwapchainKHR m_swapchain;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    uint32_t m_currentImageIndex;
    uint32_t m_width;
    uint32_t m_height;
    VkFormat m_format;
    
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateImageViews();
    void DestroyImageViews();
};

} // namespace ddvk