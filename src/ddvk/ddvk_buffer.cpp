#include "ddvk_buffer.h"
#include "ddvk_renderer.h"
#include "ddvk_commands.h"
#include <cstring>

namespace ddvk {

//=============================================================================
// BufferManager - реализация
//=============================================================================

BufferManager::BufferManager(VulkanRenderer* renderer)
    : m_renderer(renderer) {
    m_allocator = renderer->GetAllocator();
}

BufferManager::~BufferManager() {
    for (auto buffer : m_buffers) {
        if (buffer->buffer && buffer->allocation) {
            vmaDestroyBuffer(m_allocator, buffer->buffer, buffer->allocation);
        }
        delete buffer;
    }
    m_buffers.clear();
}

Buffer* BufferManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;
    
    if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    Buffer* buffer = new Buffer();
    buffer->size = size;
    buffer->usage = usage;
    
    VmaAllocationInfo allocationInfo;
    
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, 
                       &buffer->buffer, &buffer->allocation, &allocationInfo) != VK_SUCCESS) {
        delete buffer;
        return nullptr;
    }
    
    if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
        buffer->mappedData = allocationInfo.pMappedData;
    }
    
    m_buffers.push_back(buffer);
    return buffer;
}

Buffer* BufferManager::CreateVertexBuffer(VkDeviceSize size, const void* data) {
    Buffer* buffer = CreateBuffer(size, 
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   data ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    if (buffer && data) {
        if (!UploadData(buffer, data, size)) {
            DestroyBuffer(buffer);
            return nullptr;
        }
    }
    
    return buffer;
}

Buffer* BufferManager::CreateIndexBuffer(VkDeviceSize size, const void* data) {
    Buffer* buffer = CreateBuffer(size, 
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   data ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    if (buffer && data) {
        if (!UploadData(buffer, data, size)) {
            DestroyBuffer(buffer);
            return nullptr;
        }
    }
    
    return buffer;
}

Buffer* BufferManager::CreateUniformBuffer(VkDeviceSize size) {
    return CreateBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}

Buffer* BufferManager::CreateStagingBuffer(VkDeviceSize size, const void* data) {
    Buffer* buffer = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    
    if (buffer && data) {
        if (!UploadData(buffer, data, size)) {
            DestroyBuffer(buffer);
            return nullptr;
        }
    }
    
    return buffer;
}

void BufferManager::DestroyBuffer(Buffer* buffer) {
    if (!buffer) return;
    
    auto it = std::find(m_buffers.begin(), m_buffers.end(), buffer);
    if (it != m_buffers.end()) {
        m_buffers.erase(it);
    }
    
    if (buffer->buffer && buffer->allocation) {
        vmaDestroyBuffer(m_allocator, buffer->buffer, buffer->allocation);
    }
    
    delete buffer;
}

bool BufferManager::UploadData(Buffer* buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!buffer || !data) return false;
    
    if (buffer->mappedData) {
        // Буфер уже отображен в памяти
        memcpy(static_cast<char*>(buffer->mappedData) + offset, data, size);
        return true;
    } else {
        // Создаем staging buffer и копируем
        Buffer* staging = CreateStagingBuffer(size, data);
        if (!staging) return false;
        
        bool result = CopyBuffer(staging, buffer, size, 0, offset);
        DestroyBuffer(staging);
        
        return result;
    }
}

bool BufferManager::DownloadData(Buffer* buffer, void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!buffer || !data) return false;
    
    if (buffer->mappedData) {
        memcpy(data, static_cast<char*>(buffer->mappedData) + offset, size);
        return true;
    }
    
    // Для GPU-only буферов нужно создавать staging buffer
    Buffer* staging = CreateStagingBuffer(size);
    if (!staging) return false;
    
    if (!CopyBuffer(buffer, staging, size, offset, 0)) {
        DestroyBuffer(staging);
        return false;
    }
    
    // Ждем завершения копирования
    VkDevice device = m_renderer->GetDevice();
    vkDeviceWaitIdle(device);
    
    // Читаем из staging буфера
    void* mapped = MapBuffer(staging);
    if (mapped) {
        memcpy(data, mapped, size);
        UnmapBuffer(staging);
    }
    
    DestroyBuffer(staging);
    return mapped != nullptr;
}

void* BufferManager::MapBuffer(Buffer* buffer) {
    if (!buffer) return nullptr;
    
    if (buffer->mappedData) {
        return buffer->mappedData;
    }
    
    void* data;
    if (vmaMapMemory(m_allocator, buffer->allocation, &data) == VK_SUCCESS) {
        return data;
    }
    
    return nullptr;
}

void BufferManager::UnmapBuffer(Buffer* buffer) {
    if (!buffer) return;
    
    if (!buffer->mappedData) {
        vmaUnmapMemory(m_allocator, buffer->allocation);
    }
}

bool BufferManager::CopyBuffer(Buffer* src, Buffer* dst, VkDeviceSize size, 
                               VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    if (!src || !dst) return false;
    
    VkDevice device = m_renderer->GetDevice();
    VkCommandPool commandPool = m_renderer->GetCommandPool();
    VkQueue graphicsQueue = m_renderer->GetGraphicsQueue();
    
    // Создаем временный командный буфер
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        return false;
    }
    
    // Записываем команды
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    
    vkCmdCopyBuffer(commandBuffer, src->buffer, dst->buffer, 1, &copyRegion);
    
    vkEndCommandBuffer(commandBuffer);
    
    // Отправляем
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    
    return true;
}

} // namespace ddvk