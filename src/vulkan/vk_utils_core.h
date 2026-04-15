#pragma once

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <array>
#include <optional>
#include <set>
#include <algorithm>
#include <fstream>
#include <chrono>

namespace vk_utils {

    // Проверка доступности расширений
    bool checkExtensionSupport(const std::vector<const char*>& requiredExtensions);

    // Поиск индексов очередей
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }

        // Вспомогательные методы для получения значений
        uint32_t getGraphics() const { return graphicsFamily.value(); }
        uint32_t getPresent() const { return presentFamily.value(); }
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    // Структура для поддержки swapchain
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    // Выбор наилучших параметров swapchain
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height);

    // Вершинные данные для 2D (треугольник)
    struct Vertex {
        float pos[2];
        float color[3];
    };

    // Обработка ошибок
    void checkVkResult(VkResult result, const std::string& message);

} // namespace vk_utils
