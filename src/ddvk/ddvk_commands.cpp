#include "ddvk_commands.h"
#include "ddvk_renderer.h"

namespace ddvk {

//=============================================================================
// CommandManager - реализация
//=============================================================================

CommandManager::CommandManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_commandPool(VK_NULL_HANDLE) {
}

CommandManager::~CommandManager() {
    DestroyCommandBuffers();
    
    if (m_commandPool && m_renderer) {
        VkDevice device = m_renderer->GetDevice();
        vkDestroyCommandPool(device, m_commandPool, nullptr);
    }
}

bool CommandManager::CreateCommandBuffers(uint32_t count) {
    VkDevice device = m_renderer->GetDevice();
    
    // Создаем пул команд если нужно (семейство очереди — как у graphics queue)
    if (!m_commandPool) {
        uint32_t queueFamilyIndex = m_renderer->GetGraphicsQueueFamilyIndex();
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            return false;
        }
    }
    
    // Создаем буферы команд
    m_commandBuffers.resize(count);
    m_commandBufferInUse.resize(count, false);
    
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = count;
    
    if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        m_commandBuffers.clear();
        return false;
    }
    
    // Инициализируем очередь свободных буферов
    for (uint32_t i = 0; i < count; i++) {
        m_freeCommandBuffers.push(i);
    }
    
    return true;
}

void CommandManager::DestroyCommandBuffers() {
    if (m_commandPool && m_renderer && !m_commandBuffers.empty()) {
        VkDevice device = m_renderer->GetDevice();
        vkFreeCommandBuffers(device, m_commandPool, 
                             static_cast<uint32_t>(m_commandBuffers.size()),
                             m_commandBuffers.data());
    }
    
    m_commandBuffers.clear();
    m_commandBufferInUse.clear();
    
    while (!m_freeCommandBuffers.empty()) {
        m_freeCommandBuffers.pop();
    }
}

VkCommandBuffer CommandManager::BeginCommandBuffer(uint32_t index) {
    if (index >= m_commandBuffers.size()) return VK_NULL_HANDLE;
    
    VkCommandBuffer cmd = m_commandBuffers[index];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    m_commandBufferInUse[index] = true;
    
    return cmd;
}

void CommandManager::EndCommandBuffer(uint32_t index) {
    if (index >= m_commandBuffers.size()) return;
    
    vkEndCommandBuffer(m_commandBuffers[index]);
}

bool CommandManager::SubmitCommandBuffer(uint32_t index, VkSemaphore waitSemaphore,
                                         VkSemaphore signalSemaphore, VkFence fence) {
    if (index >= m_commandBuffers.size()) return false;
    
    VkQueue graphicsQueue = m_renderer->GetGraphicsQueue();
    VkCommandBuffer cmd = m_commandBuffers[index];
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    if (waitSemaphore) {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
    }
    
    if (signalSemaphore) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;
    }
    
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) == VK_SUCCESS) {
        m_commandBufferInUse[index] = false;
        m_freeCommandBuffers.push(index);
        return true;
    }
    
    return false;
}

bool CommandManager::SubmitAndWait(uint32_t index) {
    VkDevice device = m_renderer->GetDevice();
    
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        return false;
    }
    
    bool result = SubmitCommandBuffer(index, VK_NULL_HANDLE, VK_NULL_HANDLE, fence);
    
    if (result) {
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    
    vkDestroyFence(device, fence, nullptr);
    
    return result;
}

void CommandManager::CmdBeginRenderPass(VkCommandBuffer cmd, VkRenderPass renderPass,
                                        VkFramebuffer framebuffer, const VkRect2D& renderArea,
                                        const VkClearValue* clearValues, uint32_t clearCount) {
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea = renderArea;
    renderPassInfo.clearValueCount = clearCount;
    renderPassInfo.pClearValues = clearValues;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandManager::CmdEndRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void CommandManager::CmdBindPipeline(VkCommandBuffer cmd, VkPipeline pipeline) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void CommandManager::CmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineLayout layout,
                                           uint32_t firstSet, uint32_t setCount,
                                           const VkDescriptorSet* sets) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout, firstSet, setCount, sets, 0, nullptr);
}

void CommandManager::CmdDraw(VkCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandManager::CmdBlitImage(VkCommandBuffer cmd, VkImage srcImage, VkImageLayout srcLayout,
                                   VkImage dstImage, VkImageLayout dstLayout,
                                   uint32_t regionCount, const VkImageBlit* regions,
                                   VkFilter filter) {
    vkCmdBlitImage(cmd, srcImage, srcLayout, dstImage, dstLayout,
                   regionCount, regions, filter);
}

void CommandManager::CmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
                                        const VkClearColorValue* color,
                                        uint32_t rangeCount, const VkImageSubresourceRange* ranges) {
    vkCmdClearColorImage(cmd, image, layout, color, rangeCount, ranges);
}

VkCommandBuffer CommandManager::GetCommandBuffer(uint32_t index) const {
    if (index < m_commandBuffers.size()) {
        return m_commandBuffers[index];
    }
    return VK_NULL_HANDLE;
}

} // namespace ddvk