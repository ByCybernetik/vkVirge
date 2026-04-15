#include "ddvk_sync.h"
#include "ddvk_renderer.h"

namespace ddvk {

//=============================================================================
// SyncManager - реализация
//=============================================================================

SyncManager::SyncManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_frameCount(2)
    , m_currentFrame(0) {
}

SyncManager::~SyncManager() {
    VkDevice device = m_renderer->GetDevice();
    
    for (auto semaphore : m_imageAvailableSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_imageAvailableSemaphores.clear();
    
    for (auto semaphore : m_renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores.clear();
    
    for (auto fence : m_fences) {
        vkDestroyFence(device, fence, nullptr);
    }
    m_fences.clear();
}

bool SyncManager::CreateSemaphores(uint32_t count) {
    VkDevice device = m_renderer->GetDevice();
    
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    m_imageAvailableSemaphores.resize(count);
    m_renderFinishedSemaphores.resize(count);
    
    for (uint32_t i = 0; i < count; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS) {
            return false;
        }
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    m_frameCount = count;
    return true;
}

bool SyncManager::CreateFences(uint32_t count, bool signaled) {
    VkDevice device = m_renderer->GetDevice();
    
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    if (signaled) {
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    
    m_fences.resize(count);
    
    for (uint32_t i = 0; i < count; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_fences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

VkSemaphore SyncManager::GetImageAvailableSemaphore(uint32_t index) const {
    if (index < m_imageAvailableSemaphores.size()) {
        return m_imageAvailableSemaphores[index];
    }
    return VK_NULL_HANDLE;
}

VkSemaphore SyncManager::GetRenderFinishedSemaphore(uint32_t index) const {
    if (index < m_renderFinishedSemaphores.size()) {
        return m_renderFinishedSemaphores[index];
    }
    return VK_NULL_HANDLE;
}

VkFence SyncManager::GetFence(uint32_t index) const {
    if (index < m_fences.size()) {
        return m_fences[index];
    }
    return VK_NULL_HANDLE;
}

bool SyncManager::WaitForFence(uint32_t index, uint64_t timeout) {
    if (index >= m_fences.size()) return false;
    
    VkDevice device = m_renderer->GetDevice();
    return vkWaitForFences(device, 1, &m_fences[index], VK_TRUE, timeout) == VK_SUCCESS;
}

bool SyncManager::WaitForAllFences(uint64_t timeout) {
    VkDevice device = m_renderer->GetDevice();
    
    if (m_fences.empty()) return true;
    
    return vkWaitForFences(device, static_cast<uint32_t>(m_fences.size()), 
                          m_fences.data(), VK_TRUE, timeout) == VK_SUCCESS;
}

bool SyncManager::ResetFence(uint32_t index) {
    if (index >= m_fences.size()) return false;
    
    VkDevice device = m_renderer->GetDevice();
    return vkResetFences(device, 1, &m_fences[index]) == VK_SUCCESS;
}

bool SyncManager::ResetAllFences() {
    VkDevice device = m_renderer->GetDevice();
    
    if (m_fences.empty()) return true;
    
    return vkResetFences(device, static_cast<uint32_t>(m_fences.size()), m_fences.data()) == VK_SUCCESS;
}

//=============================================================================
// BarrierManager - реализация
//=============================================================================

VkImageMemoryBarrier BarrierManager::CreateImageBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    uint32_t srcQueueFamily,
    uint32_t dstQueueFamily) {
    
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    
    return barrier;
}

VkBufferMemoryBarrier BarrierManager::CreateBufferBarrier(
    VkBuffer buffer,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    uint32_t srcQueueFamily,
    uint32_t dstQueueFamily) {
    
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    
    return barrier;
}

void BarrierManager::PipelineBarrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    const std::vector<VkImageMemoryBarrier>& imageBarriers,
    const std::vector<VkBufferMemoryBarrier>& bufferBarriers) {
    
    vkCmdPipelineBarrier(
        cmd,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
        static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data()
    );
}

} // namespace ddvk