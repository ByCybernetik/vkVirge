#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace ddvk {

//=============================================================================
// Управление графическими пайплайнами
//=============================================================================

class PipelineManager {
public:
    PipelineManager(VulkanRenderer* renderer);
    ~PipelineManager();
    
    // Создание пайплайнов для разных операций
    bool CreateBlitPipeline(VkRenderPass renderPass);
    bool CreateFillPipeline(VkRenderPass renderPass);
    bool CreateClearPipeline(VkRenderPass renderPass);
    
    // Получение пайплайнов
    VkPipeline GetBlitPipeline() const { return m_blitPipeline; }
    VkPipeline GetFillPipeline() const { return m_fillPipeline; }
    VkPipeline GetClearPipeline() const { return m_clearPipeline; }
    
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    
    // Дескрипторы
    bool CreateDescriptorSetLayout();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();
    
    VkDescriptorSet GetDescriptorSet(uint32_t index) const;
    
private:
    VulkanRenderer* m_renderer;
    
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_blitPipeline;
    VkPipeline m_fillPipeline;
    VkPipeline m_clearPipeline;
    
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;
    
    // Вспомогательные методы
    VkPipeline CreatePipeline(const PipelineInfo& info,
                              const std::vector<uint32_t>& vertShaderCode,
                              const std::vector<uint32_t>& fragShaderCode,
                              VkRenderPass renderPass);
    
    void DestroyPipelines();
};

} // namespace ddvk