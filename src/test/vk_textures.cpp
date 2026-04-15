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
#include <iomanip>

// Подключаем модули - используем core версию
#include "../vulkan/vk_utils_core.h"
#include "../vulkan/vk_window.h"
#include "../vulkan/vk_memory.h"
#include "../vulkan/vk_shaders.h"
#include "../vulkan/vk_pipeline.h"
#include "../vulkan/vk_swapchain.h"
#include "../vulkan/vk_sync.h"
#include "../vulkan/vk_images.h"

// Структура вершины с текстурными координатами
struct TexturedVertex {
    float pos[2];
    float texCoord[2];
};

// Начальные вершины для прямоугольника с текстурой (будут обновлены после загрузки текстуры)
const std::vector<TexturedVertex> initialVertices = {
    {{-0.5f, -0.5f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f}, {0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0
};

// Класс приложения для текстуры
class TextureApp {
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
    vk_memory::BufferData vertexBuffer;
    vk_memory::BufferData indexBuffer;
    vk_images::ImageData texture;
    vk_sync::SyncObjects sync;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // НОВОЕ: Uniform buffer для гаммы
    std::vector<vk_pipeline::UniformBufferData> uniformBuffers;
    float gammaValue = 1.0f;  // Значение гаммы по умолчанию
    const float GAMMA_MIN = 0.3f;
    const float GAMMA_MAX = 3.0f;
    const float GAMMA_STEP = 0.1f;

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

    // Для отображения информации на консоль
    std::chrono::steady_clock::time_point lastGammaPrint;

    void initWindow() {
#ifdef _WIN32
        window.width = windowedWidth;
        window.height = windowedHeight;
        window.onResize = [this](int w, int h) {
            this->window.width = w;
            this->window.height = h;
            this->window.framebufferResized = true;

            // Обновляем вершины при изменении размера окна
            if (vertexBuffer.buffer != VK_NULL_HANDLE && texture.width > 0 && texture.height > 0) {
                updateVerticesWithAspectRatio();
            }
        };

        if (!vk_window::createWindow(window, "Vulkan Texture - Gamma Correction")) {
            throw std::runtime_error("Failed to create window!");
        }

        // Сохраняем начальную позицию и размер окна
        GetWindowRect(window.hwnd, &windowedRect);
        windowedX = windowedRect.left;
        windowedY = windowedRect.top;
        windowedWidth = windowedRect.right - windowedRect.left;
        windowedHeight = windowedRect.bottom - windowedRect.top;
        windowedStyle = GetWindowLong(window.hwnd, GWL_STYLE);

        std::cout << "=== Vulkan Texture with Gamma Correction ===" << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  F11    - Toggle fullscreen" << std::endl;
        std::cout << "  + / =  - Increase gamma" << std::endl;
        std::cout << "  - / _  - Decrease gamma" << std::endl;
        std::cout << "  R      - Reset gamma to 1.0" << std::endl;
        std::cout << "Current gamma: " << std::fixed << std::setprecision(2) << gammaValue << std::endl;
        std::cout << "===========================================" << std::endl;

        lastGammaPrint = std::chrono::steady_clock::now();
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
        if (vertexBuffer.buffer != VK_NULL_HANDLE && texture.width > 0 && texture.height > 0) {
            updateVerticesWithAspectRatio();
        }
#endif
    }

    // НОВОЕ: Обработка изменения гаммы
    void adjustGamma(float delta) {
        float oldGamma = gammaValue;
        gammaValue += delta;

        // Ограничиваем значения
        if (gammaValue < GAMMA_MIN) gammaValue = GAMMA_MIN;
        if (gammaValue > GAMMA_MAX) gammaValue = GAMMA_MAX;

        if (oldGamma != gammaValue) {
            updateGammaUniform();

            // Печатаем новое значение не чаще чем раз в 100мс
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGammaPrint).count();
            if (elapsed > 100) {
                std::cout << "Gamma: " << std::fixed << std::setprecision(2) << gammaValue
                          << " (min: " << GAMMA_MIN << ", max: " << GAMMA_MAX << ")" << std::endl;
                lastGammaPrint = now;
            }
        }
    }

    void resetGamma() {
        gammaValue = 1.0f;
        updateGammaUniform();
        std::cout << "Gamma reset to 1.0" << std::endl;
    }

    // НОВОЕ: Обновление uniform-буфера с текущим значением гаммы
    void updateGammaUniform() {
        if (!uniformBuffers.empty() && uniformBuffers[0].mappedData) {
            vk_pipeline::GammaUniformData gammaData;
            gammaData.gammaValue = gammaValue;

            // Обновляем все uniform-буферы (для каждого кадра)
            for (auto& ub : uniformBuffers) {
                memcpy(ub.mappedData, &gammaData, sizeof(gammaData));
            }
        }
    }

    void handleKeyPress(WPARAM wParam) {
        if (wParam == VK_F11) {
            toggleFullscreen();
        }
        // НОВОЕ: Обработка клавиш для гаммы
        else if (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == 0xBB) {  // + или =
            adjustGamma(GAMMA_STEP);
        }
        else if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == 0xBD) {  // - или _
            adjustGamma(-GAMMA_STEP);
        }
        else if (wParam == 0x52) {  // R
            resetGamma();
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
        createRenderPass();
        createDescriptorSetLayout();
        createUniformBuffers();  // НОВОЕ
        createGraphicsPipeline();
        createFramebuffers();
        createVertexBuffer();
        createIndexBuffer();

        // Обновляем вершины с учетом пропорций после создания всех буферов
        updateVerticesWithAspectRatio();

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
        appInfo.pApplicationName = "Vulkan Texture with Gamma";
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

    void updateVerticesWithAspectRatio() {
        if (texture.width == 0 || texture.height == 0 || window.width == 0 || window.height == 0) {
            std::cout << "Cannot update vertices: missing dimensions" << std::endl;
            return;
        }

        float textureAspect = (float)texture.width / (float)texture.height;
        float windowAspect = (float)window.width / (float)window.height;

        float widthScale = 1.0f;
        float heightScale = 1.0f;

        if (windowAspect > textureAspect) {
            heightScale = 1.0f;
            widthScale = textureAspect / windowAspect;
        } else {
            widthScale = 1.0f;
            heightScale = windowAspect / textureAspect;
        }

        std::cout << "Updating vertices: texture=" << texture.width << "x" << texture.height
                  << " (" << textureAspect << "), window=" << window.width << "x" << window.height
                  << " (" << windowAspect << "), scale=" << widthScale << "x" << heightScale << std::endl;

        std::vector<TexturedVertex> newVertices = {
            {{-widthScale, -heightScale}, {0.0f, 0.0f}},
            {{widthScale, -heightScale}, {1.0f, 0.0f}},
            {{widthScale, heightScale}, {1.0f, 1.0f}},
            {{-widthScale, heightScale}, {0.0f, 1.0f}}
        };

        VkDeviceSize bufferSize = sizeof(newVertices[0]) * newVertices.size();

        void* data;
        vmaMapMemory(allocator, vertexBuffer.allocation, &data);
        memcpy(data, newVertices.data(), bufferSize);
        vmaUnmapMemory(allocator, vertexBuffer.allocation);
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

    // НОВОЕ: Создание дескрипторов с uniform-буфером
    void createDescriptorSetLayout() {
        descriptors = vk_pipeline::createTextureWithUniformDescriptorSetLayout(device, vk_sync::MAX_FRAMES_IN_FLIGHT);
        std::cout << "Descriptor set layout with uniform buffer created successfully" << std::endl;
    }

    // НОВОЕ: Создание uniform-буферов для гаммы
    void createUniformBuffers() {
        uniformBuffers.resize(vk_sync::MAX_FRAMES_IN_FLIGHT);

        VkDeviceSize bufferSize = sizeof(vk_pipeline::GammaUniformData);

        for (int i = 0; i < vk_sync::MAX_FRAMES_IN_FLIGHT; i++) {
            uniformBuffers[i] = vk_pipeline::createUniformBuffer(allocator, device, bufferSize);

            // Обновляем дескрипторный сет для этого кадра
            vk_pipeline::updateTextureWithUniformDescriptorSet(device, descriptors, i,
                                                              uniformBuffers[i].buffer.buffer,
                                                              texture.view, texture.sampler);
        }

        // Инициализируем uniform-буферы начальным значением гаммы
        updateGammaUniform();

        std::cout << "Uniform buffers created for gamma correction" << std::endl;
    }

    void createGraphicsPipeline() {
        // Загружаем шейдеры для текстуры
        std::string vertPath = vk_shaders::findShaderFile("texture_vert.spv");
        std::string fragPath = vk_shaders::findShaderFile("texture_frag.spv");

        if (vertPath.empty() || fragPath.empty()) {
            throw std::runtime_error("Texture shader files not found! Please compile shaders first.");
        }

        vk_shaders::ShaderModules shaders;
        shaders.vertex = vk_shaders::loadShaderModule(device, vertPath);
        shaders.fragment = vk_shaders::loadShaderModule(device, fragPath);

        // Создаем пайплайн с дескрипторами
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptors.layout;

        VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline.layout);
        vk_utils::checkVkResult(result, "Failed to create pipeline layout!");

        // Vertex input для текстурированных вершин
        auto bindingDescription = getBindingDescription();
        auto attributeDescriptions = getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)swapChain.extent.width;
        viewport.height = (float)swapChain.extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = swapChain.extent;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, shaders.vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, shaders.fragment, "main", nullptr}
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipeline.layout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline);
        vk_utils::checkVkResult(result, "Failed to create graphics pipeline!");

        vkDestroyShaderModule(device, shaders.fragment, nullptr);
        vkDestroyShaderModule(device, shaders.vertex, nullptr);

        std::cout << "Graphics pipeline created successfully" << std::endl;
    }

    VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(TexturedVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(TexturedVertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(TexturedVertex, texCoord);

        return attributeDescriptions;
    }

    void createFramebuffers() {
        swapChainFramebuffers = vk_swapchain::createFramebuffers(device, renderPass, swapChain);
        std::cout << "Created " << swapChainFramebuffers.size() << " framebuffers" << std::endl;
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(initialVertices[0]) * initialVertices.size();
        vertexBuffer = vk_memory::createVertexBuffer(allocator, initialVertices.data(), bufferSize);
        std::cout << "Vertex buffer created with " << initialVertices.size() << " vertices" << std::endl;
    }

    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

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
        memcpy(data, indices.data(), bufferSize);
        vmaUnmapMemory(allocator, indexBuffer.allocation);

        std::cout << "Index buffer created with " << indices.size() << " indices" << std::endl;
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

            // НОВОЕ: Привязываем дескрипторный сет с учетом текущего кадра
            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pipeline.layout, 0, 1, &descriptors.sets[i % vk_sync::MAX_FRAMES_IN_FLIGHT], 0, nullptr);

            VkBuffer vertexBuffers[] = {vertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

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

        updateVerticesWithAspectRatio();
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
            vk_memory::destroyBuffer(allocator, indexBuffer);

            // НОВОЕ: Очистка uniform-буферов
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
    TextureApp app;
    app.run();
    return 0;
}
