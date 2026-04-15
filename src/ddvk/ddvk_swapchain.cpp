#include "ddvk_swapchain.h"
#include "ddvk_renderer.h"
#include "ddvk_utils.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <immintrin.h>
#endif

namespace ddvk {

// Frame pacing to cap FPS to 60.
static bool s_firstPresent = true;
static std::chrono::steady_clock::time_point s_nextPresentDeadline;
static constexpr double kTargetFrameTimeMs = 1000.0 / 60.0; // 16.667ms

// #region agent log
static void DebugSessionFileLog_Swapchain(const char* location,
                                         const char* message,
                                         const char* hypothesisId,
                                         const char* dataJson) {
    static const char* kPathUnix = "/home/cybernetik/Other/Sources/CPP/ddvk/.cursor/debug-ea945f.log";
    static const char* kPathWineZ = "Z:\\\\home\\\\cybernetik\\\\Other\\\\Sources\\\\CPP\\\\ddvk\\\\.cursor\\\\debug-ea945f.log";
    FILE* f = std::fopen(kPathWineZ, "a");
    if (!f) f = std::fopen(kPathUnix, "a");
    if (!f) return;
    const unsigned long ts = (unsigned long)std::time(nullptr) * 1000ul;
    std::fprintf(
        f,
        "{\"sessionId\":\"ea945f\",\"runId\":\"post-fix-yield\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%lu}\n",
        hypothesisId ? hypothesisId : "",
        location ? location : "",
        message ? message : "",
        dataJson ? dataJson : "{}",
        ts);
    std::fclose(f);
}
// #endregion

//=============================================================================
// SwapchainManager - реализация
//=============================================================================

SwapchainManager::SwapchainManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_swapchain(VK_NULL_HANDLE)
    , m_currentImageIndex(0)
    , m_width(0)
    , m_height(0)
    , m_format(VK_FORMAT_UNDEFINED) {
}

SwapchainManager::~SwapchainManager() {
    Shutdown();
}

bool SwapchainManager::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    Logger::Info("[DDVK] SwapchainManager::Initialize %u x %u", width, height);
    bool ok = CreateSwapchain();
    if (ok && m_swapchain != VK_NULL_HANDLE)
        ok = CreateImageViews();
    if (!ok) Logger::Error("[DDVK] SwapchainManager::Initialize failed");
    return ok;
}

bool SwapchainManager::EnsureSwapchain() {
    if (m_swapchain != VK_NULL_HANDLE) return true;
    if (!CreateSwapchain()) return false;
    if (m_swapchain == VK_NULL_HANDLE) return true; /* still deferred */
    if (!CreateImageViews()) {
        DestroySwapchain();
        return false;
    }
    return true;
}

void SwapchainManager::Shutdown() {
    DestroyImageViews();
    DestroySwapchain();
}

bool SwapchainManager::CreateSwapchain() {
    VkDevice device = m_renderer->GetDevice();
    VkPhysicalDevice physicalDevice = m_renderer->GetPhysicalDevice();
    VkSurfaceKHR surface = m_renderer->GetSurface();
    
    if (!surface) return false;
    
    // Получаем возможности поверхности
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    
    // Выбираем количество изображений (минимум 3 для тройной буферизации)
    // Это важно для плавности при FIFO (vsync)
    uint32_t imageCount = capabilities.minImageCount + 2;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount < 3) imageCount = 3; // Минимум 3 для тройной буферизации
    
    // Выбираем формат поверхности
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && 
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }
    m_format = surfaceFormat.format;
    
    // Выбираем режим презентации
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    
    // Приоритет: FIFO (vsync on) > MAILBOX > IMMEDIATE
    // FIFO гарантирует синхронизацию с частотой монитора - важно для старых игр
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool hasFifo = false;
    bool hasMailbox = false;
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_FIFO_KHR) {
            hasFifo = true;
        }
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            hasMailbox = true;
        }
    }
    // Используем FIFO (vsync) по умолчанию для правильного тайминга старых игр
    // MAILBOX только если FIFO недоступен (маловероятно)
    if (!hasFifo && hasMailbox) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        Logger::Info("[DDVK] FIFO unavailable, using MAILBOX");
    } else if (!hasFifo) {
        // Крайний случай - FIFO должен быть всегда по спецификации Vulkan
        Logger::Warning("[DDVK] FIFO not available! Using first available mode");
        if (!presentModes.empty()) {
            presentMode = presentModes[0];
        }
    } else {
        Logger::Info("[DDVK] Using FIFO (vsync) for proper frame timing");
    }
    
    // Размер swapchain: при невалидном currentExtent (0, UINT32_MAX или мусор от драйвера) используем m_width/m_height
    const uint32_t maxReasonable = 8192;
    VkExtent2D extent = capabilities.currentExtent;
    bool useRequested = (capabilities.currentExtent.width == UINT32_MAX ||
                         capabilities.currentExtent.width == 0 ||
                         capabilities.currentExtent.height == 0 ||
                         capabilities.currentExtent.width > maxReasonable ||
                         capabilities.currentExtent.height > maxReasonable);
    if (useRequested) {
        extent.width = (m_width > 0 && m_width <= maxReasonable) ? m_width : 640;
        extent.height = (m_height > 0 && m_height <= maxReasonable) ? m_height : 480;
        // Ограничиваем сверху только разумным max; min применяем лишь если он адекватный (не мусор от драйвера)
        uint32_t maxW = capabilities.maxImageExtent.width;
        uint32_t maxH = capabilities.maxImageExtent.height;
        if (maxW == 0 || maxW > maxReasonable) maxW = maxReasonable;
        if (maxH == 0 || maxH > maxReasonable) maxH = maxReasonable;
        uint32_t minW = capabilities.minImageExtent.width;
        uint32_t minH = capabilities.minImageExtent.height;
        if (minW <= maxReasonable) extent.width = std::max(extent.width, minW);
        if (minH <= maxReasonable) extent.height = std::max(extent.height, minH);
        extent.width = std::min(extent.width, maxW);
        extent.height = std::min(extent.height, maxH);
        if (extent.width == 0) extent.width = 1;
        if (extent.height == 0) extent.height = 1;
    }
    m_width = extent.width;
    m_height = extent.height;
    Logger::Info("[DDVK] CreateSwapchain: extent %u x %u (currentExtent was %u x %u)", extent.width, extent.height, capabilities.currentExtent.width, capabilities.currentExtent.height);

    // Окно ещё без размера (height 0): драйвер отклонит vkCreateSwapchainKHR. Откладываем создание до первого валидного размера.
    if (useRequested && capabilities.currentExtent.height == 0) {
        Logger::Info("[DDVK] CreateSwapchain: deferred (surface height 0), will retry on next frame");
        return true;
    }

    // Создаем swapchain
    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t queueFamilyIndices[] = {
        m_renderer->GetGraphicsQueueFamilyIndex(),
        m_renderer->GetPresentQueueFamilyIndex()
    };
    if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;
    
    auto vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)
        vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");

    if (!vkCreateSwapchainKHR) return false;
    
    VkResult scResult = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &m_swapchain);
    if (scResult != VK_SUCCESS) {
        Logger::Error("[DDVK] vkCreateSwapchainKHR failed: %d", (int)scResult);
        return false;
    }

    uint32_t imageCountActual;
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCountActual, nullptr);
    m_images.resize(imageCountActual);
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCountActual, m_images.data());
    Logger::Info("[DDVK] CreateSwapchain: %u images, sharing=%s", imageCountActual, (swapchainInfo.imageSharingMode == VK_SHARING_MODE_CONCURRENT) ? "CONCURRENT" : "EXCLUSIVE");
    return true;
}

void SwapchainManager::DestroySwapchain() {
    if (m_swapchain && m_renderer) {
        VkDevice device = m_renderer->GetDevice();
        
        auto vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)
            vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
        
        if (vkDestroySwapchainKHR) {
            vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        }
        m_swapchain = VK_NULL_HANDLE;
    }
    m_images.clear();
}

bool SwapchainManager::CreateImageViews() {
    VkDevice device = m_renderer->GetDevice();
    
    m_imageViews.resize(m_images.size());
    
    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            // Очищаем уже созданные view
            for (size_t j = 0; j < i; j++) {
                vkDestroyImageView(device, m_imageViews[j], nullptr);
            }
            m_imageViews.clear();
            return false;
        }
    }
    
    return true;
}

void SwapchainManager::DestroyImageViews() {
    if (m_renderer) {
        VkDevice device = m_renderer->GetDevice();
        
        for (auto imageView : m_imageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
    }
    m_imageViews.clear();
}

VkImage SwapchainManager::GetCurrentImage() {
    if (m_currentImageIndex < m_images.size()) {
        return m_images[m_currentImageIndex];
    }
    return VK_NULL_HANDLE;
}

VkImageView SwapchainManager::GetCurrentImageView() {
    if (m_currentImageIndex < m_imageViews.size()) {
        return m_imageViews[m_currentImageIndex];
    }
    return VK_NULL_HANDLE;
}

uint32_t SwapchainManager::GetCurrentImageIndex() {
    return m_currentImageIndex;
}

bool SwapchainManager::AcquireNextImage() {
    VkDevice device = m_renderer->GetDevice();

    auto vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)
        vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");

    if (!vkAcquireNextImageKHR) {
        Logger::Error("[DDVK] AcquireNextImage: vkAcquireNextImageKHR is null");
        return false;
    }

    VkSemaphore semaphore = m_renderer->GetImageAvailableSemaphore();
    VkResult result = vkAcquireNextImageKHR(device, m_swapchain, UINT64_MAX,
                                             semaphore, VK_NULL_HANDLE,
                                             &m_currentImageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Logger::Error("[DDVK] AcquireNextImage failed: %d", (int)result);
        return false;
    }
    return true;
}

bool SwapchainManager::Present() {
    if (m_swapchain == VK_NULL_HANDLE) return true; /* deferred, no-op */
    
    VkDevice device = m_renderer->GetDevice();
    VkQueue presentQueue = m_renderer->GetPresentQueue();

    auto vkQueuePresentKHR = (PFN_vkQueuePresentKHR)
        vkGetDeviceProcAddr(device, "vkQueuePresentKHR");

    if (!vkQueuePresentKHR) {
        Logger::Error("[DDVK] Present: vkQueuePresentKHR is null");
        return false;
    }

    // FPS cap to 60 using an accumulated deadline (stable pacing).
    // This avoids drift/over-waiting that can happen when using "last present time" directly.
    {
        const auto paceStart = std::chrono::steady_clock::now();
        const auto now = paceStart;
        const auto step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(kTargetFrameTimeMs));

        if (s_firstPresent) {
            s_firstPresent = false;
            s_nextPresentDeadline = now;
        } else {
            s_nextPresentDeadline += step;
            // If we're far behind (e.g. long stall), resync to avoid a burst of "no-wait" frames.
            if (now - s_nextPresentDeadline > std::chrono::milliseconds(250)) {
                s_nextPresentDeadline = now;
            }
        }

        if (now < s_nextPresentDeadline) {
            const auto remaining = s_nextPresentDeadline - now;
            const double remMs = std::chrono::duration<double, std::milli>(remaining).count();
            // No sleep_for: it overshoots by ~12ms under Wine regardless of requested time.
            // Phase 1: yield (microseconds per call) while >2ms left.
            while (std::chrono::steady_clock::now() + std::chrono::milliseconds(2) < s_nextPresentDeadline) {
                std::this_thread::yield();
            }
            // Phase 2: precision busy-spin the last 2ms.
            while (std::chrono::steady_clock::now() < s_nextPresentDeadline) {
#ifdef _WIN32
                _mm_pause();
#endif
            }
            char j[160];
            std::snprintf(j, sizeof(j), "{\"remainingMs\":%.3f}", remMs);
            DebugSessionFileLog_Swapchain("ddvk_swapchain:Present", "pace_wait", "H_cap_pacer", j);
        }

        const auto paceEnd = std::chrono::steady_clock::now();
        const double paceMs = std::chrono::duration<double, std::milli>(paceEnd - paceStart).count();
        if (paceMs > 18.0) {
            char j[160];
            std::snprintf(j, sizeof(j), "{\"paceMs\":%.3f}", paceMs);
            DebugSessionFileLog_Swapchain("ddvk_swapchain:Present", "pace_wait_long", "H_cap_pacer", j);
        }
    }

    VkSemaphore semaphore = m_renderer->GetRenderFinishedSemaphore();
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    const auto tPresent0 = std::chrono::steady_clock::now();
    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    const auto tPresent1 = std::chrono::steady_clock::now();
    const double vkPresentMs = std::chrono::duration<double, std::milli>(tPresent1 - tPresent0).count();
    if (vkPresentMs > 1.0) {
        char j[160];
        std::snprintf(j, sizeof(j), "{\"vkQueuePresentMs\":%.3f,\"imageIndex\":%u,\"result\":%d}", vkPresentMs, m_currentImageIndex, (int)result);
        DebugSessionFileLog_Swapchain("ddvk_swapchain:Present", "vkQueuePresentKHR_time", "H_present_block", j);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Logger::Error("[DDVK] vkQueuePresentKHR failed: %d", (int)result);
        return false;
    }
    return true;
}

bool SwapchainManager::Resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    Recreate();
    return IsValid();
}

void SwapchainManager::Recreate() {
    if (!m_renderer) return;
    
    VkDevice device = m_renderer->GetDevice();
    vkDeviceWaitIdle(device);
    
    DestroyImageViews();
    DestroySwapchain();
    
    CreateSwapchain();
    CreateImageViews();
}

} // namespace ddvk