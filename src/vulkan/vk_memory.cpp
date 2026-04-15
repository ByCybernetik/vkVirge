#include "vk_memory.h"
#include "vk_utils_core.h"
#include <cstring>

namespace vk_memory {

    VmaAllocator createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;
        
        VmaAllocator allocator;
        VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
        vk_utils::checkVkResult(result, "Failed to create VMA allocator!");
        
        return allocator;
    }

    void destroyAllocator(VmaAllocator allocator) {
        if (allocator) {
            vmaDestroyAllocator(allocator);
        }
    }

    BufferData createVertexBuffer(VmaAllocator allocator, const void* data, VkDeviceSize size) {
        BufferData result;
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        
        VkResult vkResult = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, 
                                           &result.buffer, &result.allocation, nullptr);
        vk_utils::checkVkResult(vkResult, "Failed to create vertex buffer!");
        
        // Копирование данных
        void* mappedData;
        vmaMapMemory(allocator, result.allocation, &mappedData);
        memcpy(mappedData, data, size);
        vmaUnmapMemory(allocator, result.allocation);
        
        return result;
    }

    void destroyBuffer(VmaAllocator allocator, BufferData& bufferData) {
        if (bufferData.buffer && bufferData.allocation) {
            vmaDestroyBuffer(allocator, bufferData.buffer, bufferData.allocation);
            bufferData.buffer = VK_NULL_HANDLE;
            bufferData.allocation = VK_NULL_HANDLE;
        }
    }

} // namespace vk_memory
