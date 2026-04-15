#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace ddvk {

//=============================================================================
// Управление дескрипторами Vulkan
//=============================================================================

class DescriptorManager {
public:
    DescriptorManager(VulkanRenderer* renderer);
    ~DescriptorManager();
    
    // Создание layout'ов
    bool CreateTextureLayout();
    bool CreateUniformLayout();
    bool CreateCombinedLayout();
    
    // Получение layout'ов
    VkDescriptorSetLayout GetTextureLayout() const { return m_textureLayout; }
    VkDescriptorSetLayout GetUniformLayout() const { return m_uniformLayout; }
    VkDescriptorSetLayout GetCombinedLayout() const { return m_combinedLayout; }
    
    // Создание пулов
    bool CreateTexturePool(uint32_t maxSets);
    bool CreateUniformPool(uint32_t maxSets);
    bool CreateCombinedPool(uint32_t maxSets);
    
    // Выделение дескрипторных наборов
    bool AllocateTextureSet(VkDescriptorSet* set, VkImageView imageView, VkSampler sampler);
    bool AllocateUniformSet(VkDescriptorSet* set, VkBuffer buffer, VkDeviceSize size);
    bool AllocateCombinedSet(VkDescriptorSet* set, VkImageView imageView, VkSampler sampler,
                             VkBuffer buffer, VkDeviceSize size);
    
    // Освобождение
    void FreeDescriptorSet(VkDescriptorSet set);
    void ResetPools();
    
private:
    VulkanRenderer* m_renderer;
    
    // Descriptor set layouts
    VkDescriptorSetLayout m_textureLayout;
    VkDescriptorSetLayout m_uniformLayout;
    VkDescriptorSetLayout m_combinedLayout;
    
    // Descriptor pools
    VkDescriptorPool m_texturePool;
    VkDescriptorPool m_uniformPool;
    VkDescriptorPool m_combinedPool;
    
    // Вспомогательные методы
    VkDescriptorSetLayout CreateLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    VkDescriptorPool CreatePool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets);
};

} // namespace ddvk