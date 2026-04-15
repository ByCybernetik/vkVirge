#pragma once

#include <vulkan/vulkan.h>
#include "vk_shaders.h"
#include "vk_utils_core.h"
#include "vk_memory.h"
#include <array>
#include <vector>

// Этот макрос будет определен в vk_cube.cpp при компиляции куба
// Если он не определен, функции для куба будут недоступны
#ifndef VK_USE_GLM
#define VK_USE_GLM 0
#endif

namespace vk_pipeline {

    // Создание render pass (без глубины)
    VkRenderPass createRenderPass(VkDevice device, VkFormat swapChainImageFormat);

    // Создание render pass с глубиной
    VkRenderPass createRenderPassWithDepth(VkDevice device, VkFormat swapChainImageFormat, VkFormat depthFormat);

    // Создание пайплайна для треугольника
    struct PipelineData {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    PipelineData createGraphicsPipeline(VkDevice device,
                                         VkRenderPass renderPass,
                                         VkExtent2D swapChainExtent,
                                         const vk_shaders::ShaderModules& shaders);

    // Структура для дескрипторов (единая для всех случаев)
    struct DescriptorSetLayoutData {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;        // Для одного сета (текстура)
        std::vector<VkDescriptorSet> sets;           // Для нескольких сетов (куб)
    };

    // Создание layout дескрипторов для текстуры (один binding)
    DescriptorSetLayoutData createTextureDescriptorSetLayout(VkDevice device);

    // Обновление дескрипторного сета с текстурой
    void updateTextureDescriptorSet(VkDevice device,
                                    DescriptorSetLayoutData& descriptors,
                                    VkImageView imageView,
                                    VkSampler sampler);

    // НОВОЕ: Создание layout дескрипторов для текстуры с uniform-буфером (гамма)
    DescriptorSetLayoutData createTextureWithUniformDescriptorSetLayout(VkDevice device, uint32_t frameCount);

    // НОВОЕ: Обновление дескрипторного сета с текстурой и uniform-буфером
    void updateTextureWithUniformDescriptorSet(VkDevice device,
                                              DescriptorSetLayoutData& descriptors,
                                              uint32_t frameIndex,
                                              VkBuffer uniformBuffer,
                                              VkImageView imageView,
                                              VkSampler sampler);

    // НОВОЕ: Структура для uniform buffer (гамма)
    struct GammaUniformData {
        float gammaValue;
        float padding[3];  // Выравнивание для 16 байт (std140)
    };

    // НОВОЕ: Создание uniform buffer для гаммы
    struct UniformBufferData {
        vk_memory::BufferData buffer;
        void* mappedData = nullptr;
    };

    UniformBufferData createUniformBuffer(VmaAllocator allocator, VkDevice device, size_t bufferSize);

    // Структура для depth image
    struct DepthImageData {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    // Создание depth image
    DepthImageData createDepthImage(VmaAllocator allocator,
                                    VkDevice device,
                                    VkPhysicalDevice physicalDevice,
                                    uint32_t width,
                                    uint32_t height);

    // Уничтожение depth image
    void destroyDepthImage(VmaAllocator allocator, VkDevice device, DepthImageData& depthImage);

    // Очистка дескрипторов
    void destroyDescriptorSetLayout(VkDevice device, DescriptorSetLayoutData& descriptors);

    // Уничтожение пайплайна
    void destroyPipeline(VkDevice device, PipelineData& pipeline);

#if VK_USE_GLM
    // Функции для куба (доступны только при использовании GLM)

    // Создание пайплайна для куба
    PipelineData createCubePipeline(VkDevice device,
                                     VkRenderPass renderPass,
                                     VkExtent2D swapChainExtent,
                                     const vk_shaders::ShaderModules& shaders,
                                     VkDescriptorSetLayout descriptorSetLayout);

    // Создание layout дескрипторов для куба (два binding'а)
    DescriptorSetLayoutData createCubeDescriptorSetLayout(VkDevice device, uint32_t frameCount);

    // Обновление дескрипторного сета для куба
    void updateCubeDescriptorSet(VkDevice device,
                                 DescriptorSetLayoutData& descriptors,
                                 uint32_t frameIndex,
                                 VkBuffer uniformBuffer,
                                 VkImageView imageView,
                                 VkSampler sampler);
#endif

} // namespace vk_pipeline
