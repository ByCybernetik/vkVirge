#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace ddvk {

//=============================================================================
// Управление синхронизацией Vulkan
//=============================================================================

class SyncManager {
public:
    SyncManager(VulkanRenderer* renderer);
    ~SyncManager();
    
    // Создание объектов синхронизации
    bool CreateSemaphores(uint32_t count);
    bool CreateFences(uint32_t count, bool signaled);
    
    // Получение объектов
    VkSemaphore GetImageAvailableSemaphore(uint32_t index = 0) const;
    VkSemaphore GetRenderFinishedSemaphore(uint32_t index = 0) const;
    VkFence GetFence(uint32_t index = 0) const;
    
    // Ожидание
    bool WaitForFence(uint32_t index, uint64_t timeout = UINT64_MAX);
    bool WaitForAllFences(uint64_t timeout = UINT64_MAX);
    
    // Сброс
    bool ResetFence(uint32_t index);
    bool ResetAllFences();
    
    // Текущий индекс
    uint32_t GetCurrentFrame() const { return m_currentFrame; }
    void AdvanceFrame() { m_currentFrame = (m_currentFrame + 1) % m_frameCount; }
    
private:
    VulkanRenderer* m_renderer;
    
    uint32_t m_frameCount;
    uint32_t m_currentFrame;
    
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_fences;
};

//=============================================================================
// Барьеры памяти
//=============================================================================

class BarrierManager {
public:
    static VkImageMemoryBarrier CreateImageBarrier(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
        uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED);
    
    static VkBufferMemoryBarrier CreateBufferBarrier(
        VkBuffer buffer,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
        uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED);
    
    static void PipelineBarrier(
        VkCommandBuffer cmd,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask,
        const std::vector<VkImageMemoryBarrier>& imageBarriers,
        const std::vector<VkBufferMemoryBarrier>& bufferBarriers = {});
};

} // namespace ddvk