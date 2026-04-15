#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace ddvk {

//=============================================================================
// Управление буферами Vulkan
//=============================================================================

struct Buffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    void* mappedData;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    
    Buffer() : buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), 
               mappedData(nullptr), size(0), usage(0) {}
};

class BufferManager {
public:
    BufferManager(VulkanRenderer* renderer);
    ~BufferManager();
    
    // Создание буферов
    Buffer* CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                         VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    Buffer* CreateVertexBuffer(VkDeviceSize size, const void* data = nullptr);
    Buffer* CreateIndexBuffer(VkDeviceSize size, const void* data = nullptr);
    Buffer* CreateUniformBuffer(VkDeviceSize size);
    Buffer* CreateStagingBuffer(VkDeviceSize size, const void* data = nullptr);
    
    // Уничтожение
    void DestroyBuffer(Buffer* buffer);
    
    // Загрузка данных
    bool UploadData(Buffer* buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    bool DownloadData(Buffer* buffer, void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    
    // Отображение памяти
    void* MapBuffer(Buffer* buffer);
    void UnmapBuffer(Buffer* buffer);
    
    // Копирование между буферами
    bool CopyBuffer(Buffer* src, Buffer* dst, VkDeviceSize size, 
                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    
private:
    VulkanRenderer* m_renderer;
    VmaAllocator m_allocator;
    
    std::vector<Buffer*> m_buffers;
};

} // namespace ddvk