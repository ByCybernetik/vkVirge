// Добавьте эту строку в начало файлов, где не используется GLM
#define VK_USE_GLM 0

// Этот макрос должен быть определен до включения vulkan.h
#define VK_USE_PLATFORM_WIN32_KHR 1

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <set>
#include <cstring>
#include <array>
#include <fstream>
#include <chrono>

// Подключаем модули - используем core версию
#include "../vulkan/vk_utils_core.h"
#include "../vulkan/vk_window.h"
#include "../vulkan/vk_memory.h"
#include "../vulkan/vk_shaders.h"
#include "../vulkan/vk_pipeline.h"
#include "../vulkan/vk_swapchain.h"
#include "../vulkan/vk_sync.h"
#include "../vulkan/vk_images.h"

// Данные вершин треугольника
const std::vector<vk_utils::Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

// Класс приложения для треугольника
class TriangleApp {
public:
    void run() {
        try {
            initWindow();
            initVulkan();
            mainLoop();
            cleanup();
        } catch (const std::exception& e) {
            std::cerr << "Fatal error: " << e.what() << std::endl;
            cleanup();
            throw;
        }
    }

private:
    // Vulkan объекты
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // Модульные объекты
    vk_window::WindowData window;
    vk_utils::QueueFamilyIndices queueIndices;
    vk_swapchain::SwapChainData swapChain;
    std::vector<VkFramebuffer> swapChainFramebuffers;
    vk_pipeline::PipelineData pipeline;
    vk_memory::BufferData vertexBuffer;
    vk_sync::SyncObjects sync;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Расширения
    std::vector<const char*> instanceExtensions;
    std::vector<const char*> deviceExtensions;

    // Параметры окна и полноэкранного режима
    bool fullscreen = false;
    int windowedX = 100;
    int windowedY = 100;
    int windowedWidth = 800;
    int windowedHeight = 600;
    DWORD windowedStyle = 0;
    RECT windowedRect = {0, 0, 800, 600};

    void initWindow() {
#ifdef _WIN32
        window.width = windowedWidth;
        window.height = windowedHeight;
        window.onResize = [this](int w, int h) {
            this->window.width = w;
            this->window.height = h;
            this->window.framebufferResized = true;
        };

        if (!vk_window::createWindow(window, "Vulkan Triangle")) {
            throw std::runtime_error("Failed to create window!");
        }

        // Сохраняем начальную позицию и размер окна
        GetWindowRect(window.hwnd, &windowedRect);
        windowedX = windowedRect.left;
        windowedY = windowedRect.top;
        windowedWidth = windowedRect.right - windowedRect.left;
        windowedHeight = windowedRect.bottom - windowedRect.top;
        windowedStyle = GetWindowLong(window.hwnd, GWL_STYLE);

        std::cout << "Press F11 to toggle fullscreen" << std::endl;
#else
        throw std::runtime_error("Unsupported platform");
#endif
    }

    void toggleFullscreen() {
#ifdef _WIN32
        fullscreen = !fullscreen;

        if (fullscreen) {
            std::cout << "Switching to fullscreen mode" << std::endl;

            GetWindowRect(window.hwnd, &windowedRect);
            windowedStyle = GetWindowLong(window.hwnd, GWL_STYLE);

            HMONITOR hMonitor = MonitorFromWindow(window.hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            GetMonitorInfo(hMonitor, &monitorInfo);

            SetWindowLong(window.hwnd, GWL_STYLE, windowedStyle & ~(WS_CAPTION | WS_THICKFRAME));

            SetWindowPos(window.hwnd, HWND_TOP,
                         monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);

            window.width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
            window.height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        } else {
            std::cout << "Switching to windowed mode" << std::endl;

            SetWindowLong(window.hwnd, GWL_STYLE, windowedStyle);

            SetWindowPos(window.hwnd, HWND_TOP,
                         windowedRect.left,
                         windowedRect.top,
                         windowedRect.right - windowedRect.left,
                         windowedRect.bottom - windowedRect.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);

            window.width = windowedRect.right - windowedRect.left;
            window.height = windowedRect.bottom - windowedRect.top;
        }

        window.framebufferResized = true;
#endif
    }

    void handleKeyPress(WPARAM wParam) {
        if (wParam == VK_F11) {
            toggleFullscreen();
        }
    }

    void initVulkan() {
        setupExtensions();
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createAllocator();
        createSwapChain();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createCommandBuffers();
        createSyncObjects();
    }

    void setupExtensions() {
        instanceExtensions.push_back("VK_KHR_surface");
        instanceExtensions.push_back("VK_KHR_win32_surface");
        deviceExtensions.push_back("VK_KHR_swapchain");

        std::cout << "Required instance extensions:" << std::endl;
        for (const auto& ext : instanceExtensions) {
            std::cout << "  " << ext << std::endl;
        }

        if (!vk_utils::checkExtensionSupport(instanceExtensions)) {
            throw std::runtime_error("Required instance extensions not supported!");
        }
    }

    void createInstance() {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Vulkan Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();
        createInfo.enabledLayerCount = 0;

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
        vk_utils::checkVkResult(result, "Failed to create instance!");

        std::cout << "Vulkan instance created successfully" << std::endl;
    }

    void createSurface() {
#ifdef _WIN32
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHRFunc =
            reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
                vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR")
            );

        if (!vkCreateWin32SurfaceKHRFunc) {
            throw std::runtime_error("Failed to load vkCreateWin32SurfaceKHR function!");
        }

        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = window.hwnd;
        createInfo.hinstance = window.hinstance;

        VkResult result = vkCreateWin32SurfaceKHRFunc(instance, &createInfo, nullptr, &surface);
        vk_utils::checkVkResult(result, "Failed to create window surface!");

        std::cout << "Window surface created successfully" << std::endl;
#endif
    }

    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            throw std::runtime_error("No Vulkan-capable GPUs found!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        physicalDevice = devices[0];

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        std::cout << "Using GPU: " << deviceProperties.deviceName << std::endl;
    }

    void createLogicalDevice() {
        queueIndices = vk_utils::findQueueFamilies(physicalDevice, surface);

        if (!queueIndices.isComplete()) {
            throw std::runtime_error("Required queue families not found!");
        }

        // Используем getGraphics() и getPresent() для получения значений
        std::cout << "Graphics queue family: " << queueIndices.getGraphics() << std::endl;
        std::cout << "Present queue family: " << queueIndices.getPresent() << std::endl;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            queueIndices.getGraphics(),
            queueIndices.getPresent()
        };

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
        vk_utils::checkVkResult(result, "Failed to create logical device!");

        vkGetDeviceQueue(device, queueIndices.getGraphics(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, queueIndices.getPresent(), 0, &presentQueue);

        std::cout << "Logical device created successfully" << std::endl;
    }

    void createAllocator() {
        allocator = vk_memory::createAllocator(instance, physicalDevice, device);
        std::cout << "VMA allocator created successfully" << std::endl;
    }

    void createSwapChain() {
        swapChain = vk_swapchain::createSwapChain(
            device, physicalDevice, surface,
            window.width, window.height,
            queueIndices.getGraphics(),
            queueIndices.getPresent()
        );
        std::cout << "Swap chain created with " << swapChain.images.size() << " images" << std::endl;
    }

    void createRenderPass() {
        renderPass = vk_pipeline::createRenderPass(device, swapChain.imageFormat);
        std::cout << "Render pass created successfully" << std::endl;
    }

    void createGraphicsPipeline() {
        // Загружаем шейдеры для треугольника
        std::string vertPath = vk_shaders::findShaderFile("triangle_vert.spv");
        std::string fragPath = vk_shaders::findShaderFile("triangle_frag.spv");

        if (vertPath.empty() || fragPath.empty()) {
            throw std::runtime_error("Triangle shader files not found! Please compile shaders first.");
        }

        vk_shaders::ShaderModules shaders;
        shaders.vertex = vk_shaders::loadShaderModule(device, vertPath);
        shaders.fragment = vk_shaders::loadShaderModule(device, fragPath);

        pipeline = vk_pipeline::createGraphicsPipeline(device, renderPass, swapChain.extent, shaders);

        vkDestroyShaderModule(device, shaders.fragment, nullptr);
        vkDestroyShaderModule(device, shaders.vertex, nullptr);

        std::cout << "Graphics pipeline created successfully" << std::endl;
    }

    void createFramebuffers() {
        swapChainFramebuffers = vk_swapchain::createFramebuffers(device, renderPass, swapChain);
        std::cout << "Created " << swapChainFramebuffers.size() << " framebuffers" << std::endl;
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueIndices.getGraphics();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
        vk_utils::checkVkResult(result, "Failed to create command pool!");

        std::cout << "Command pool created successfully" << std::endl;
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        vertexBuffer = vk_memory::createVertexBuffer(allocator, vertices.data(), bufferSize);
        std::cout << "Vertex buffer created with " << vertices.size() << " vertices" << std::endl;
    }

    void createCommandBuffers() {
        commandBuffers.resize(swapChainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        VkResult result = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
        vk_utils::checkVkResult(result, "Failed to allocate command buffers!");

        for (size_t i = 0; i < commandBuffers.size(); i++) {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            result = vkBeginCommandBuffer(commandBuffers[i], &beginInfo);
            vk_utils::checkVkResult(result, "Failed to begin command buffer!");

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapChain.extent;

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

            VkBuffer vertexBuffers[] = {vertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            vkCmdDraw(commandBuffers[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0);

            vkCmdEndRenderPass(commandBuffers[i]);

            result = vkEndCommandBuffer(commandBuffers[i]);
            vk_utils::checkVkResult(result, "Failed to record command buffer!");
        }

        std::cout << "Created " << commandBuffers.size() << " command buffers" << std::endl;
    }

    void createSyncObjects() {
        sync = vk_sync::createSyncObjects(device);
        std::cout << "Created synchronization objects" << std::endl;
    }

    void mainLoop() {
#ifdef _WIN32
        std::cout << "Entering main loop..." << std::endl;
        std::cout << "Press F11 to toggle fullscreen" << std::endl;

        MSG msg = {};
        while (true) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    break;
                }

                if (msg.message == WM_KEYDOWN) {
                    handleKeyPress(msg.wParam);
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                drawFrame();
            }
        }

        std::cout << "Exiting main loop" << std::endl;
#endif
    }

    void drawFrame() {
        vk_sync::waitForFrame(device, sync, sync.currentFrame);

        uint32_t imageIndex;
        PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR =
            reinterpret_cast<PFN_vkAcquireNextImageKHR>(
                vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR")
            );

        if (!vkAcquireNextImageKHR) {
            throw std::runtime_error("Failed to load vkAcquireNextImageKHR!");
        }

        VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX,
            sync.imageAvailableSemaphores[sync.currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swap chain image!");
        }

        vk_sync::resetFence(device, sync, sync.currentFrame);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {sync.imageAvailableSemaphores[sync.currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = {sync.renderFinishedSemaphores[sync.currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, sync.inFlightFences[sync.currentFrame]);
        vk_utils::checkVkResult(result, "Failed to submit draw command buffer!");

        PFN_vkQueuePresentKHR vkQueuePresentKHR =
            reinterpret_cast<PFN_vkQueuePresentKHR>(
                vkGetDeviceProcAddr(device, "vkQueuePresentKHR")
            );

        if (!vkQueuePresentKHR) {
            throw std::runtime_error("Failed to load vkQueuePresentKHR!");
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {swapChain.swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.framebufferResized) {
            window.framebufferResized = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swap chain image!");
        }

        sync.currentFrame = (sync.currentFrame + 1) % vk_sync::MAX_FRAMES_IN_FLIGHT;
    }

    void recreateSwapChain() {
        if (window.width == 0 || window.height == 0) return;

        std::cout << "Recreating swap chain..." << std::endl;

        vkDeviceWaitIdle(device);

        cleanupSwapChain();

        createSwapChain();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandBuffers();
    }

    void cleanupSwapChain() {
        vk_swapchain::cleanupFramebuffers(device, swapChainFramebuffers);
        vk_swapchain::cleanupSwapChain(device, swapChain);
        vk_pipeline::destroyPipeline(device, pipeline);
        if (renderPass) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
    }

    void cleanup() {
        std::cout << "Cleaning up..." << std::endl;

        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);

            vk_memory::destroyBuffer(allocator, vertexBuffer);

            cleanupSwapChain();
            vk_sync::cleanupSyncObjects(device, sync);

            if (commandPool) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
            }

            if (allocator) {
                vk_memory::destroyAllocator(allocator);
                allocator = VK_NULL_HANDLE;
            }

            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (surface && instance) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (instance) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        vk_window::destroyWindow(window);

        std::cout << "Cleanup complete" << std::endl;
    }
};

// Точка входа
int main() {
    TriangleApp app;
    app.run();
    return 0;
}
