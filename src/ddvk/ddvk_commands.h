#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <queue>

namespace ddvk {

//=============================================================================
// Управление командами
//=============================================================================

class CommandManager {
public:
    CommandManager(VulkanRenderer* renderer);
    ~CommandManager();
    
    // Создание буферов команд
    bool CreateCommandBuffers(uint32_t count);
    void DestroyCommandBuffers();
    
    // Запись команд
    VkCommandBuffer BeginCommandBuffer(uint32_t index);
    void EndCommandBuffer(uint32_t index);
    
    // Выполнение команд
    bool SubmitCommandBuffer(uint32_t index, VkSemaphore waitSemaphore = VK_NULL_HANDLE,
                             VkSemaphore signalSemaphore = VK_NULL_HANDLE,
                             VkFence fence = VK_NULL_HANDLE);
    
    bool SubmitAndWait(uint32_t index);
    
    // Команды рендеринга
    void CmdBeginRenderPass(VkCommandBuffer cmd, VkRenderPass renderPass,
                            VkFramebuffer framebuffer, const VkRect2D& renderArea,
                            const VkClearValue* clearValues, uint32_t clearCount);
    
    void CmdEndRenderPass(VkCommandBuffer cmd);
    
    void CmdBindPipeline(VkCommandBuffer cmd, VkPipeline pipeline);
    void CmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineLayout layout,
                               uint32_t firstSet, uint32_t setCount,
                               const VkDescriptorSet* sets);
    
    void CmdDraw(VkCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount,
                 uint32_t firstVertex, uint32_t firstInstance);
    
    void CmdBlitImage(VkCommandBuffer cmd, VkImage srcImage, VkImageLayout srcLayout,
                      VkImage dstImage, VkImageLayout dstLayout,
                      uint32_t regionCount, const VkImageBlit* regions,
                      VkFilter filter);
    
    void CmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
                            const VkClearColorValue* color,
                            uint32_t rangeCount, const VkImageSubresourceRange* ranges);
    
    // Получение буферов
    VkCommandBuffer GetCommandBuffer(uint32_t index) const;
    uint32_t GetCommandBufferCount() const { return (uint32_t)m_commandBuffers.size(); }
    
private:
    VulkanRenderer* m_renderer;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<bool> m_commandBufferInUse;
    std::queue<uint32_t> m_freeCommandBuffers;
    
    VkCommandPool m_commandPool;
};

} // namespace ddvk