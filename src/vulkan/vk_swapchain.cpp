#include "vk_swapchain.h"
#include "vk_utils_core.h"
#include <algorithm>
#include <set>

namespace vk_swapchain {

    SwapChainData createSwapChain(VkDevice device, 
                                   VkPhysicalDevice physicalDevice,
                                   VkSurfaceKHR surface,
                                   int width, int height,
                                   uint32_t graphicsFamily,
                                   uint32_t presentFamily) {
        SwapChainData result;
        
        auto support = vk_utils::querySwapChainSupport(physicalDevice, surface);
        
        if (support.formats.empty() || support.presentModes.empty()) {
            throw std::runtime_error("No suitable swap chain formats or present modes!");
        }
        
        VkSurfaceFormatKHR surfaceFormat = vk_utils::chooseSwapSurfaceFormat(support.formats);
        VkPresentModeKHR presentMode = vk_utils::chooseSwapPresentMode(support.presentModes);
        VkExtent2D extent = vk_utils::chooseSwapExtent(support.capabilities, width, height);
        
        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }
        
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        if (graphicsFamily != presentFamily) {
            uint32_t queueFamilyIndices[] = {graphicsFamily, presentFamily};
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;
        
        PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = 
            reinterpret_cast<PFN_vkCreateSwapchainKHR>(
                vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR")
            );
        
        if (!vkCreateSwapchainKHR) {
            throw std::runtime_error("Failed to load vkCreateSwapchainKHR!");
        }
        
        VkResult vkResult = vkCreateSwapchainKHR(device, &createInfo, nullptr, &result.swapChain);
        vk_utils::checkVkResult(vkResult, "Failed to create swap chain!");
        
        PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = 
            reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
                vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR")
            );
        
        vkGetSwapchainImagesKHR(device, result.swapChain, &imageCount, nullptr);
        result.images.resize(imageCount);
        vkGetSwapchainImagesKHR(device, result.swapChain, &imageCount, result.images.data());
        
        result.imageFormat = surfaceFormat.format;
        result.extent = extent;
        
        // Создание image views
        result.imageViews.resize(result.images.size());
        for (size_t i = 0; i < result.images.size(); i++) {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = result.images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = result.imageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            
            vkResult = vkCreateImageView(device, &viewInfo, nullptr, &result.imageViews[i]);
            vk_utils::checkVkResult(vkResult, "Failed to create image views!");
        }
        
        return result;
    }

    std::vector<VkFramebuffer> createFramebuffers(VkDevice device,
                                                   VkRenderPass renderPass,
                                                   const SwapChainData& swapChain) {
        std::vector<VkFramebuffer> framebuffers(swapChain.imageViews.size());
        
        for (size_t i = 0; i < swapChain.imageViews.size(); i++) {
            VkImageView attachments[] = {swapChain.imageViews[i]};
            
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapChain.extent.width;
            framebufferInfo.height = swapChain.extent.height;
            framebufferInfo.layers = 1;
            
            VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]);
            vk_utils::checkVkResult(result, "Failed to create framebuffer!");
        }
        
        return framebuffers;
    }

    void cleanupSwapChain(VkDevice device, SwapChainData& swapChain) {
        for (auto imageView : swapChain.imageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        swapChain.imageViews.clear();
        
        if (swapChain.swapChain) {
            PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = 
                reinterpret_cast<PFN_vkDestroySwapchainKHR>(
                    vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR")
                );
            if (vkDestroySwapchainKHR) {
                vkDestroySwapchainKHR(device, swapChain.swapChain, nullptr);
            }
            swapChain.swapChain = VK_NULL_HANDLE;
        }
    }

    void cleanupFramebuffers(VkDevice device, std::vector<VkFramebuffer>& framebuffers) {
        for (auto framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        framebuffers.clear();
    }

} // namespace vk_swapchain
