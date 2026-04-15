#include "vk_shaders.h"
#include "vk_utils_core.h"  // Заменяем vk_utils.h на vk_utils_core.h
#include <fstream>
#include <iostream>

namespace vk_shaders {

    std::string findShaderFile(const std::string& basename) {
        // Получаем текущую рабочую директорию
        char* pwd = getenv("PWD");
        std::string currentDir = pwd ? pwd : "";

        std::cout << "Searching for shader: " << basename << std::endl;
        std::cout << "Current directory: " << currentDir << std::endl;

        std::vector<std::string> searchPaths = {
            basename,
            "build/" + basename,
            "builddir/" + basename,
            "../build/" + basename,
            "../builddir/" + basename,
            "./" + basename,
            "src/shaders/" + basename,
            "../src/shaders/" + basename
        };

        // Добавляем пути с учетом текущей директории
        if (!currentDir.empty()) {
            searchPaths.push_back(currentDir + "/" + basename);
            searchPaths.push_back(currentDir + "/build/" + basename);
            searchPaths.push_back(currentDir + "/builddir/" + basename);
        }

        for (const auto& path : searchPaths) {
            std::ifstream file(path);
            if (file.good()) {
                std::cout << "Found shader at: " << path << std::endl;
                return path;
            }
        }

        std::cerr << "Shader not found: " << basename << std::endl;
        return "";
    }

    VkShaderModule loadShaderModule(VkDevice device, const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = buffer.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

        VkShaderModule shaderModule;
        VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
        vk_utils::checkVkResult(result, "Failed to create shader module from " + filename);

        std::cout << "Loaded shader: " << filename << std::endl;
        return shaderModule;
    }

    ShaderModules createTriangleShaders(VkDevice device) {
        ShaderModules shaders;

        std::string vertPath = findShaderFile("triangle_vert.spv");
        std::string fragPath = findShaderFile("triangle_frag.spv");

        if (vertPath.empty() || fragPath.empty()) {
            throw std::runtime_error("Shader files not found! Please compile shaders first.");
        }

        shaders.vertex = loadShaderModule(device, vertPath);
        shaders.fragment = loadShaderModule(device, fragPath);

        return shaders;
    }

    void destroyShaders(VkDevice device, ShaderModules& shaders) {
        if (shaders.vertex) {
            vkDestroyShaderModule(device, shaders.vertex, nullptr);
            shaders.vertex = VK_NULL_HANDLE;
        }
        if (shaders.fragment) {
            vkDestroyShaderModule(device, shaders.fragment, nullptr);
            shaders.fragment = VK_NULL_HANDLE;
        }
    }

} // namespace vk_shaders
