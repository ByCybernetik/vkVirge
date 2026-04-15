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

// Подключаем модули
#include "../vulkan/vk_utils_glm.h"
#include "../vulkan/vk_window.h"
#include "../vulkan/vk_memory.h"
#include "../vulkan/vk_shaders.h"
#include "../vulkan/vk_pipeline.h"
#include "../vulkan/vk_swapchain.h"
#include "../vulkan/vk_sync.h"
#include "../vulkan/vk_images.h"
#include "cube_data.h"

// Класс приложения для куба
class CubeApp {
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
    vk_pipeline::DescriptorSetLayoutData descriptors;
    vk_pipeline::DepthImageData depthImage;
    vk_memory::BufferData vertexBuffer;
    vk_memory::BufferData indexBuffer;
    vk_images::ImageData texture;
    vk_sync::SyncObjects sync;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Uniform buffers для каждого кадра
    std::vector<vk_pipeline::UniformBufferData> uniformBuffers;

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

    // Время для анимации
    std::chrono::steady_clock::time_point startTime;

    void initWindow() {
#ifdef _WIN32
        window.width = windowedWidth;
        window.height = windowedHeight;
        window.onResize = [this](int w, int h) {
            this->window.width = w;
            this->window.height = h;
            this->window.framebufferResized = true;
        };

        if (!vk_window::createWindow(window, "Vulkan Cube")) {
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
        startTime = std::chrono::steady_clock::now();
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
        createCommandPool();
        createTexture();
        createSwapChain();
        createDepthImage();
        createRenderPass();
        createDescriptorSetLayout();
        createUniformBuffers();
        createGraphicsPipeline();
        createFramebuffers();
        createVertexBuffer();
        createIndexBuffer();
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
        appInfo.pApplicationName = "Vulkan Cube";
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

        // Используем getGraphics() и getPresent()
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
        deviceFeatures.samplerAnisotropy = VK_TRUE;

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

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueIndices.getGraphics();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
        vk_utils::checkVkResult(result, "Failed to create command pool!");

        std::cout << "Command pool created successfully" << std::endl;
    }

    void createTexture() {
        std::vector<std::string> searchPaths = {
            "src/test/texture.jpeg",
            "texture.jpeg",
            "build/texture.jpeg",
            "../src/test/texture.jpeg",
            "./texture.jpeg"
        };

        std::string texturePath;
        for (const auto& path : searchPaths) {
            std::ifstream file(path);
            if (file.good()) {
                texturePath = path;
                std::cout << "Found texture at: " << path << std::endl;
                break;
            }
        }

        if (texturePath.empty()) {
            throw std::runtime_error("Texture file not found! Please place texture.jpeg in src/test/ directory");
        }

        texture = vk_images::loadTextureFromFile(
            allocator, device, physicalDevice,
            graphicsQueue, commandPool,
            texturePath
        );
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

    void createDepthImage() {
        depthImage = vk_pipeline::createDepthImage(allocator, device, physicalDevice,
                                                   swapChain.extent.width, swapChain.extent.height);
        std::cout << "Depth image created" << std::endl;
    }

    void createRenderPass() {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // Будет определено внутри функции
        renderPass = vk_pipeline::createRenderPassWithDepth(device, swapChain.imageFormat, depthFormat);
        std::cout << "Render pass with depth created successfully" << std::endl;
    }

    void createDescriptorSetLayout() {
        descriptors = vk_pipeline::createCubeDescriptorSetLayout(device, vk_sync::MAX_FRAMES_IN_FLIGHT);
        std::cout << "Cube descriptor set layout created" << std::endl;
    }

    void createUniformBuffers() {
        uniformBuffers.resize(vk_sync::MAX_FRAMES_IN_FLIGHT);

        VkDeviceSize bufferSize = sizeof(vk_utils::UniformBufferObject);

        for (int i = 0; i < vk_sync::MAX_FRAMES_IN_FLIGHT; i++) {
            uniformBuffers[i] = vk_pipeline::createUniformBuffer(allocator, device, bufferSize);

            // Обновляем дескрипторный сет для этого кадра
            vk_pipeline::updateCubeDescriptorSet(device, descriptors, i,
                                                uniformBuffers[i].buffer.buffer,
                                                texture.view, texture.sampler);
        }

        std::cout << "Uniform buffers created" << std::endl;
    }

    void createGraphicsPipeline() {
        std::string vertPath = vk_shaders::findShaderFile("cube_vert.spv");
        std::string fragPath = vk_shaders::findShaderFile("cube_frag.spv");

        if (vertPath.empty() || fragPath.empty()) {
            throw std::runtime_error("Cube shader files not found! Please compile shaders first.");
        }

        vk_shaders::ShaderModules shaders;
        shaders.vertex = vk_shaders::loadShaderModule(device, vertPath);
        shaders.fragment = vk_shaders::loadShaderModule(device, fragPath);

        pipeline = vk_pipeline::createCubePipeline(device, renderPass, swapChain.extent, shaders, descriptors.layout);

        vkDestroyShaderModule(device, shaders.fragment, nullptr);
        vkDestroyShaderModule(device, shaders.vertex, nullptr);

        std::cout << "Cube graphics pipeline created successfully" << std::endl;
    }

    void createFramebuffers() {
        swapChainFramebuffers.resize(swapChain.imageViews.size());

        for (size_t i = 0; i < swapChain.imageViews.size(); i++) {
            std::array<VkImageView, 2> attachments = {
                swapChain.imageViews[i],
                depthImage.view
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChain.extent.width;
            framebufferInfo.height = swapChain.extent.height;
            framebufferInfo.layers = 1;

            VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]);
            vk_utils::checkVkResult(result, "Failed to create framebuffer!");
        }

        std::cout << "Created " << swapChainFramebuffers.size() << " framebuffers with depth" << std::endl;
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(cubeVertices[0]) * cubeVertices.size();
        vertexBuffer = vk_memory::createVertexBuffer(allocator, cubeVertices.data(), bufferSize);
        std::cout << "Vertex buffer created with " << cubeVertices.size() << " vertices" << std::endl;
    }

    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(cubeIndices[0]) * cubeIndices.size();

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                          &indexBuffer.buffer, &indexBuffer.allocation, nullptr);
        vk_utils::checkVkResult(result, "Failed to create index buffer!");

        void* data;
        vmaMapMemory(allocator, indexBuffer.allocation, &data);
        memcpy(data, cubeIndices.data(), bufferSize);
        vmaUnmapMemory(allocator, indexBuffer.allocation);

        std::cout << "Index buffer created with " << cubeIndices.size() << " indices" << std::endl;
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

            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapChain.extent;
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pipeline.layout, 0, 1, &descriptors.sets[i % vk_sync::MAX_FRAMES_IN_FLIGHT], 0, nullptr);

            VkBuffer vertexBuffers[] = {vertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);

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

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        vk_utils::UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.model = glm::rotate(ubo.model, time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), swapChain.extent.width / (float) swapChain.extent.height, 0.1f, 10.0f);
        ubo.proj[1][1] *= -1;

        memcpy(uniformBuffers[currentImage].mappedData, &ubo, sizeof(ubo));
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

        // Обновляем uniform buffer перед отрисовкой
        updateUniformBuffer(sync.currentFrame);

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
        createDepthImage();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandBuffers();
    }

    void cleanupSwapChain() {
        vk_pipeline::destroyDepthImage(allocator, device, depthImage);
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
            vk_memory::destroyBuffer(allocator, indexBuffer);

            for (auto& ub : uniformBuffers) {
                vk_memory::destroyBuffer(allocator, ub.buffer);
            }

            vk_images::destroyImage(allocator, device, texture);
            vk_pipeline::destroyDescriptorSetLayout(device, descriptors);

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
    CubeApp app;
    app.run();
    return 0;
}
