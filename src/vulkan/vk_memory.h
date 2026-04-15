#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "vk_utils_core.h"

namespace vk_memory {

    // Создание аллокатора VMA
    VmaAllocator createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);

    // Уничтожение аллокатора
    void destroyAllocator(VmaAllocator allocator);

    // Создание вершинного буфера
    struct BufferData {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    
    BufferData createVertexBuffer(VmaAllocator allocator, 
                                   const void* data, 
                                   VkDeviceSize size);

    // Уничтожение буфера
    void destroyBuffer(VmaAllocator allocator, BufferData& bufferData);

} // namespace vk_memory
