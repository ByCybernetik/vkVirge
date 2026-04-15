#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "vk_utils_core.h"

namespace vk_shaders {

    // Загрузка шейдерного модуля из SPIR-V файла
    VkShaderModule loadShaderModule(VkDevice device, const std::string& filename);

    // Поиск файла шейдера в различных директориях
    std::string findShaderFile(const std::string& basename);

    // Создание шейдерных модулей для вершинного и фрагментного шейдеров
    struct ShaderModules {
        VkShaderModule vertex = VK_NULL_HANDLE;
        VkShaderModule fragment = VK_NULL_HANDLE;
    };
    
    ShaderModules createTriangleShaders(VkDevice device);

    // Уничтожение шейдерных модулей
    void destroyShaders(VkDevice device, ShaderModules& shaders);

} // namespace vk_shaders
