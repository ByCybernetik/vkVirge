#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "vk_utils_core.h"


namespace vk_swapchain {

    // Создание swapchain
    struct SwapChainData {
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        VkFormat imageFormat;
        VkExtent2D extent;
    };
    
    SwapChainData createSwapChain(VkDevice device, 
                                   VkPhysicalDevice physicalDevice,
                                   VkSurfaceKHR surface,
                                   int width, int height,
                                   uint32_t graphicsFamily,
                                   uint32_t presentFamily);

    // Создание framebuffers
    std::vector<VkFramebuffer> createFramebuffers(VkDevice device,
                                                   VkRenderPass renderPass,
                                                   const SwapChainData& swapChain);

    // Очистка swapchain
    void cleanupSwapChain(VkDevice device, SwapChainData& swapChain);

    // Очистка framebuffers
    void cleanupFramebuffers(VkDevice device, std::vector<VkFramebuffer>& framebuffers);

} // namespace vk_swapchain
