#include "ddvk_descriptors.h"
#include "ddvk_renderer.h"

namespace ddvk {

//=============================================================================
// DescriptorManager - реализация
//=============================================================================

DescriptorManager::DescriptorManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_textureLayout(VK_NULL_HANDLE)
    , m_uniformLayout(VK_NULL_HANDLE)
    , m_combinedLayout(VK_NULL_HANDLE)
    , m_texturePool(VK_NULL_HANDLE)
    , m_uniformPool(VK_NULL_HANDLE)
    , m_combinedPool(VK_NULL_HANDLE) {
}

DescriptorManager::~DescriptorManager() {
    VkDevice device = m_renderer->GetDevice();
    
    if (m_texturePool) {
        vkDestroyDescriptorPool(device, m_texturePool, nullptr);
    }
    if (m_uniformPool) {
        vkDestroyDescriptorPool(device, m_uniformPool, nullptr);
    }
    if (m_combinedPool) {
        vkDestroyDescriptorPool(device, m_combinedPool, nullptr);
    }
    
    if (m_textureLayout) {
        vkDestroyDescriptorSetLayout(device, m_textureLayout, nullptr);
    }
    if (m_uniformLayout) {
        vkDestroyDescriptorSetLayout(device, m_uniformLayout, nullptr);
    }
    if (m_combinedLayout) {
        vkDestroyDescriptorSetLayout(device, m_combinedLayout, nullptr);
    }
}

bool DescriptorManager::CreateTextureLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings(1);
    
    // Binding 0: Combined image sampler (текстура)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    m_textureLayout = CreateLayout(bindings);
    return m_textureLayout != VK_NULL_HANDLE;
}

bool DescriptorManager::CreateUniformLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings(1);
    
    // Binding 0: Uniform buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    m_uniformLayout = CreateLayout(bindings);
    return m_uniformLayout != VK_NULL_HANDLE;
}

bool DescriptorManager::CreateCombinedLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    
    // Binding 0: Combined image sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    // Binding 1: Uniform buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    
    m_combinedLayout = CreateLayout(bindings);
    return m_combinedLayout != VK_NULL_HANDLE;
}

bool DescriptorManager::CreateTexturePool(uint32_t maxSets) {
    std::vector<VkDescriptorPoolSize> poolSizes(1);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = maxSets;
    
    m_texturePool = CreatePool(poolSizes, maxSets);
    return m_texturePool != VK_NULL_HANDLE;
}

bool DescriptorManager::CreateUniformPool(uint32_t maxSets) {
    std::vector<VkDescriptorPoolSize> poolSizes(1);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxSets;
    
    m_uniformPool = CreatePool(poolSizes, maxSets);
    return m_uniformPool != VK_NULL_HANDLE;
}

bool DescriptorManager::CreateCombinedPool(uint32_t maxSets) {
    std::vector<VkDescriptorPoolSize> poolSizes(2);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = maxSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxSets;
    
    m_combinedPool = CreatePool(poolSizes, maxSets);
    return m_combinedPool != VK_NULL_HANDLE;
}

bool DescriptorManager::AllocateTextureSet(VkDescriptorSet* set, VkImageView imageView, VkSampler sampler) {
    if (!m_texturePool || !m_textureLayout) return false;
    
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_texturePool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_textureLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, set) != VK_SUCCESS) {
        return false;
    }
    
    // Обновляем дескриптор
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = *set;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    return true;
}

bool DescriptorManager::AllocateUniformSet(VkDescriptorSet* set, VkBuffer buffer, VkDeviceSize size) {
    if (!m_uniformPool || !m_uniformLayout) return false;
    
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_uniformPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_uniformLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, set) != VK_SUCCESS) {
        return false;
    }
    
    // Обновляем дескриптор
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;
    
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = *set;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    return true;
}

bool DescriptorManager::AllocateCombinedSet(VkDescriptorSet* set, VkImageView imageView, VkSampler sampler,
                                            VkBuffer buffer, VkDeviceSize size) {
    if (!m_combinedPool || !m_combinedLayout) return false;
    
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_combinedPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_combinedLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, set) != VK_SUCCESS) {
        return false;
    }
    
    std::vector<VkWriteDescriptorSet> descriptorWrites(2);
    
    // Binding 0: Combined image sampler
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = *set;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfo;
    
    // Binding 1: Uniform buffer
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = *set;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &bufferInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), 
                          descriptorWrites.data(), 0, nullptr);
    
    return true;
}

void DescriptorManager::FreeDescriptorSet(VkDescriptorSet set) {
    // В Vulkan дескрипторы освобождаются автоматически при сбросе пула
    // Этот метод оставлен для совместимости
}

void DescriptorManager::ResetPools() {
    VkDevice device = m_renderer->GetDevice();
    
    if (m_texturePool) {
        vkResetDescriptorPool(device, m_texturePool, 0);
    }
    if (m_uniformPool) {
        vkResetDescriptorPool(device, m_uniformPool, 0);
    }
    if (m_combinedPool) {
        vkResetDescriptorPool(device, m_combinedPool, 0);
    }
}

VkDescriptorSetLayout DescriptorManager::CreateLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return layout;
}

VkDescriptorPool DescriptorManager::CreatePool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets) {
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    
    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return pool;
}

} // namespace ddvk