#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include "vk_utils_core.h"

namespace vk_images {

    // Структура для изображения с поддержкой mip-уровней
    struct ImageData {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t mipLevels = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    // Создание изображения с поддержкой mip-уровней
    ImageData createImage(VmaAllocator allocator,
                          VkDevice device,
                          uint32_t width,
                          uint32_t height,
                          uint32_t mipLevels,
                          VkFormat format,
                          VkImageTiling tiling,
                          VkImageUsageFlags usage,
                          VkMemoryPropertyFlags properties);

    // Создание image view
    VkImageView createImageView(VkDevice device,
                                VkImage image,
                                VkFormat format,
                                VkImageAspectFlags aspectFlags,
                                uint32_t mipLevels);

    // Создание сэмплера с поддержкой mip-уровней
    VkSampler createSampler(VkDevice device,
                            VkPhysicalDevice physicalDevice,
                            uint32_t mipLevels);

    // Загрузка текстуры из файла JPEG с генерацией mip-цепочки
    ImageData loadTextureFromFile(VmaAllocator allocator,
                                  VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  VkQueue queue,
                                  VkCommandPool commandPool,
                                  const std::string& filename);

    // Генерация mip-цепочки
    void generateMipmaps(VkDevice device,
                         VkQueue queue,
                         VkCommandPool commandPool,
                         VkImage image,
                         VkFormat format,
                         uint32_t width,
                         uint32_t height,
                         uint32_t mipLevels);

    // Переход layout изображения
    void transitionImageLayout(VkDevice device,
                               VkQueue queue,
                               VkCommandPool commandPool,
                               VkImage image,
                               VkFormat format,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               uint32_t mipLevels);

    // Копирование буфера в изображение
    void copyBufferToImage(VkDevice device,
                           VkQueue queue,
                           VkCommandPool commandPool,
                           VkBuffer buffer,
                           VkImage image,
                           uint32_t width,
                           uint32_t height);

    // Очистка изображения
    void destroyImage(VmaAllocator allocator, VkDevice device, ImageData& image);

} // namespace vk_images
