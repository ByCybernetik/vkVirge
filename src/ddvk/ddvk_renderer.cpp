#include "ddvk_renderer.h"
#include "ddvk_utils.h"
#include "ddvk_core.h"
#include "ddvk_swapchain.h"
#include "ddvk_commands.h"
#include "../hack/hack.h"
#include "../utils/scaler.h"

#include <cstdio>
#include <set>
#include <algorithm>
#include <cstring>
#include <string>
#include <cctype>
#include <thread>
#include <chrono>
#include <cmath>
#include <immintrin.h>

namespace ddvk {

VulkanRenderer::VulkanRenderer()
    : m_instance(VK_NULL_HANDLE)
    , m_physicalDevice(VK_NULL_HANDLE)
    , m_device(VK_NULL_HANDLE)
    , m_graphicsQueueFamilyIndex(0)
    , m_presentQueueFamilyIndex(0)
    , m_graphicsQueue(VK_NULL_HANDLE)
    , m_presentQueue(VK_NULL_HANDLE)
    , m_surface(VK_NULL_HANDLE)
    , m_renderPass(VK_NULL_HANDLE)
    , m_commandPool(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_hwnd(nullptr)
    , m_cooperativeLevel(0)
    , m_currentMode(640, 480, 32, 60)
    , m_isFullScreen(false)
    , m_primarySurface(nullptr)
    , m_imageAvailableSemaphore(VK_NULL_HANDLE)
    , m_renderFinishedSemaphore(VK_NULL_HANDLE)
    , m_swapchainManager(nullptr)
    , m_commandManager(nullptr)
    , m_commandBufferCount(0)
    , m_initialized(false)
    , m_primaryStagingBuffer(VK_NULL_HANDLE)
    , m_primaryStagingAllocation(VK_NULL_HANDLE)
    , m_primaryStagingSize(0)
    , m_primarySourceImage(VK_NULL_HANDLE)
    , m_primarySourceImageView(VK_NULL_HANDLE)
    , m_primarySourceAllocation(nullptr)
    , m_primarySourceWidth(0)
    , m_primarySourceHeight(0)
    , m_deferredPresent(false) {
}

VulkanRenderer::~VulkanRenderer() {
    Shutdown();
}

bool VulkanRenderer::CreateInstance() {
    if (m_instance) return true;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "DDVK";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "DDVK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions;
    extensions.push_back("VK_KHR_surface");
#ifdef _WIN32
    extensions.push_back("VK_KHR_win32_surface");
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool VulkanRenderer::PickPhysicalDevice() {
    if (m_physicalDevice) return true;
    if (!m_instance) return false;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Простейший выбор: первое устройство, которое умеет graphics+present на нашей поверхности
    for (auto dev : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, families.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

            VkBool32 presentSupport = VK_FALSE;
            if (m_surface) {
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &presentSupport);
            }

            if (presentSupport) {
                m_physicalDevice = dev;
                m_graphicsQueueFamilyIndex = i;
                m_presentQueueFamilyIndex = i;
                return true;
            }
        }
    }

    // fallback — просто первое устройство с графической очередью
    m_physicalDevice = devices[0];
    m_graphicsQueueFamilyIndex = 0;
    m_presentQueueFamilyIndex = 0;
    return true;
}

bool VulkanRenderer::CreateSurface() {
    if (m_surface) return true;
    if (!m_instance || !m_hwnd) return false;

#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandle(nullptr);
    info.hwnd = m_hwnd;

    auto vkCreateWin32SurfaceKHR_fn =
        (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(m_instance, "vkCreateWin32SurfaceKHR");
    if (!vkCreateWin32SurfaceKHR_fn) {
        return false;
    }
    if (vkCreateWin32SurfaceKHR_fn(m_instance, &info, nullptr, &m_surface) != VK_SUCCESS) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool VulkanRenderer::CreateDevice() {
    if (m_device) return true;
    if (!m_physicalDevice) return false;

    float queuePriority = 1.0f;
    std::set<uint32_t> uniqueFamilies = {
        m_graphicsQueueFamilyIndex,
        m_presentQueueFamilyIndex
    };

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &queuePriority;
        queueInfos.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};

    const char* deviceExtensions[] = {
        "VK_KHR_swapchain"
    };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamilyIndex, 0, &m_presentQueue);

    return true;
}

bool VulkanRenderer::CreateRenderPass() {
    if (m_renderPass) return true;
    if (!m_device) return false;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateCommandPool() {
    if (m_commandPool) return true;
    if (!m_device) return false;

    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &info, nullptr, &m_commandPool) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateSyncObjects() {
    if (!m_device) return false;
    if (m_imageAvailableSemaphore && m_renderFinishedSemaphore && m_inFlightFences[0]) return true;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS) {
        return false;
    }
    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS) {
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            while (i > 0) {
                vkDestroyFence(m_device, m_inFlightFences[--i], nullptr);
                m_inFlightFences[i] = VK_NULL_HANDLE;
            }
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::Initialize() {
    if (m_initialized) {
        return true;
    }

    Logger::Initialize("ddvk_log.txt");

    if (!m_hwnd) {
        return false;
    }

    if (!CreateInstance()) { return false; }
    if (!CreateSurface()) { return false; }
    if (!PickPhysicalDevice()) { return false; }
    if (!CreateDevice()) { return false; }
    if (!CreateRenderPass()) { return false; }
    if (!CreateCommandPool()) { return false; }
    if (!CreateSyncObjects()) { return false; }

    // VMA allocator
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        return false;
    }

    // Размер swapchain по текущему режиму / клиенту окна
    uint32_t swapWidth = m_currentMode.width;
    uint32_t swapHeight = m_currentMode.height;
#ifdef _WIN32
    RECT clientRect{};
    if (GetClientRect(m_hwnd, &clientRect) &&
        clientRect.right > clientRect.left &&
        clientRect.bottom > clientRect.top) {
        swapWidth = static_cast<uint32_t>(clientRect.right - clientRect.left);
        swapHeight = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
    } else {
        // Окно без размера (Wine/полноэкран): задаём клиентскую область, чтобы поверхность Vulkan стала валидной
        if (swapWidth == 0) swapWidth = 640;
        if (swapHeight == 0) swapHeight = 480;
        RECT want = { 0, 0, (LONG)swapWidth, (LONG)swapHeight };
        DWORD style = (DWORD)GetWindowLongPtr(m_hwnd, GWL_STYLE);
        DWORD exStyle = (DWORD)GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
        if (AdjustWindowRectEx(&want, style, FALSE, exStyle)) {
            SetWindowPos(m_hwnd, NULL, 0, 0, want.right - want.left, want.bottom - want.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    // Если окно в «игровом» разрешении (640x480 и т.п.) — растягиваем на весь экран, чтобы картинка заполняла монитор.
    if (swapWidth <= 800u && swapHeight <= 600u) {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (screenW > 0 && screenH > 0) {
            RECT want = { 0, 0, screenW, screenH };
            DWORD style = (DWORD)GetWindowLongPtr(m_hwnd, GWL_STYLE);
            DWORD exStyle = (DWORD)GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
            if (AdjustWindowRectEx(&want, style, FALSE, exStyle)) {
                SetWindowPos(m_hwnd, HWND_TOP, 0, 0, want.right - want.left, want.bottom - want.top, SWP_SHOWWINDOW);
                swapWidth = static_cast<uint32_t>(screenW);
                swapHeight = static_cast<uint32_t>(screenH);
            }
        }
    }
#endif

    m_swapchainManager = std::make_unique<SwapchainManager>(this);
    if (!m_swapchainManager->Initialize(m_hwnd, swapWidth, swapHeight)) {
        return false;
    }

    m_commandManager = std::make_unique<CommandManager>(this);
    // Два командных буфера для лучшей плавности (один готовится пока другой выполняется)
    if (!m_commandManager->CreateCommandBuffers(2)) {
        return false;
    }
    m_commandBufferCount = 2;

    m_initialized = true;
    return true;
}

void VulkanRenderer::Shutdown() {
    m_initialized = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
        m_primarySurface = nullptr;
    }

    if (m_device) {
        VkResult waitResult = vkDeviceWaitIdle(m_device);
        (void)waitResult;

        if (m_swapchainManager) {
            m_swapchainManager->Shutdown();
            m_swapchainManager.reset();
        }

        std::vector<SurfaceImpl*> toDelete;
        {
            std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
            toDelete = m_surfaces;
            m_surfaces.clear();
            for (SurfaceImpl* p : m_pendingDeleteSurfaces) {
                toDelete.push_back(p);
            }
            m_pendingDeleteSurfaces.clear();
        }
        for (auto surface : toDelete) {
            delete surface;
        }

        if (m_imageAvailableSemaphore) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
            m_imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_renderFinishedSemaphore) {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
            m_renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
            if (m_inFlightFences[i]) {
                vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
                m_inFlightFences[i] = VK_NULL_HANDLE;
            }
        }

        if (m_commandPool) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }

        if (m_renderPass) {
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }

        DestroyPrimaryStagingBuffer();
        DestroyPrimarySourceImage();
        if (m_allocator) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

        m_commandManager.reset();

        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
        m_graphicsQueue = VK_NULL_HANDLE;
        m_presentQueue = VK_NULL_HANDLE;
    }

    if (m_surface && m_instance) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_physicalDevice = VK_NULL_HANDLE;
    for (void* p : m_deferredFreeBuffers)
        free(p);
    m_deferredFreeBuffers.clear();
}

HRESULT VulkanRenderer::SetCooperativeLevel(HWND hWnd, DWORD dwFlags) {
    m_hwnd = hWnd;
    m_cooperativeLevel = dwFlags;
    m_isFullScreen = (dwFlags & DDSCL_FULLSCREEN) != 0;

    if (!m_initialized) {
        if (!Initialize()) {
            return DDERR_GENERIC;
        }
    } else if (m_swapchainManager) {
        // При смене coop‑уровня пересоздаём swapchain под новое окно/размер
        RecreateSwapchain();
    }
    return DD_OK;
}

HRESULT VulkanRenderer::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP,
                                       DWORD dwRefreshRate, DWORD /*dwFlags*/) {
    m_currentMode = DisplayMode(dwWidth, dwHeight, dwBPP, dwRefreshRate);
    // Не меняем размер swapchain под разрешение игры — окно/экран уже заданы; картинку масштабируем в BeginFrame.
    return DD_OK;
}

HRESULT VulkanRenderer::GetDisplayMode(LPDDSURFACEDESC2 lpDDSurfaceDesc2) {
    if (!lpDDSurfaceDesc2) return DDERR_INVALIDPARAMS;
    lpDDSurfaceDesc2->dwWidth = m_currentMode.width;
    lpDDSurfaceDesc2->dwHeight = m_currentMode.height;
    lpDDSurfaceDesc2->ddpfPixelFormat.dwRGBBitCount = m_currentMode.bpp;
    return DD_OK;
}

HRESULT VulkanRenderer::RestoreDisplayMode() {
    // Для простоты ничего не делаем
    return DD_OK;
}

HRESULT VulkanRenderer::GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps) {
    if (lpDDDriverCaps) {
        DWORD sz = lpDDDriverCaps->dwSize;
        if (sz > sizeof(DDCAPS)) sz = sizeof(DDCAPS);
        if (sz < 4) sz = 4;
        memset(lpDDDriverCaps, 0, sz);
        lpDDDriverCaps->dwSize = sz;
        if (sz >= 8)
            lpDDDriverCaps->dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTQUEUE |
                DDCAPS_BLTCOLORFILL | DDCAPS_PALETTEVSYNC | DDCAPS_OVERLAY | DDCAPS_OVERLAYSTRETCH | 0x00000200; /* DDSCAPS_PRIMARYSURFACE */
        if (sz >= 16)
            lpDDDriverCaps->dwCKeyCaps = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTOVERLAY;
        if (sz >= 44) {
            lpDDDriverCaps->dwVidMemTotal = 256 * 1024 * 1024;
            lpDDDriverCaps->dwVidMemFree  = 128 * 1024 * 1024;
        }
        if (sz >= 144) {
            lpDDDriverCaps->dwMinOverlayStretch = 1000;
            lpDDDriverCaps->dwMaxOverlayStretch = 8000;
        }
    }
    if (lpDDHELCaps) {
        DWORD sz = lpDDHELCaps->dwSize;
        if (sz > sizeof(DDCAPS)) sz = sizeof(DDCAPS);
        if (sz < 4) sz = 4;
        memset(lpDDHELCaps, 0, sz);
        lpDDHELCaps->dwSize = sz;
    }
    return DD_OK;
}

HRESULT VulkanRenderer::GetAvailableVidMem(LPDDSCAPS2 /*lpDDSCaps2*/,
                                           LPDWORD lpdwTotal, LPDWORD lpdwFree) {
    // Простейшие заглушки: 256 МБ всего, 128 МБ свободно
    if (lpdwTotal) *lpdwTotal = 256 * 1024 * 1024;
    if (lpdwFree)  *lpdwFree  = 128 * 1024 * 1024;
    return DD_OK;
}

bool VulkanRenderer::IsVerticalBlank() {
    // Не реализовано — считаем, что всегда не в VBlank
    return false;
}

void VulkanRenderer::WaitForVerticalBlank() {
    // StarCraft использует WaitForVerticalBlank для игрового тайминга
    // Ждём только один раз за кадр для 60 FPS, иначе игра замедляется
    static DWORD s_lastWaitTime = 0;
    static DWORD s_waitCount = 0;
    const DWORD targetFrameTime = 16; // 60 FPS
    
    DWORD currentTime = GetTickCount();
    DWORD elapsed = currentTime - s_lastWaitTime;
    
    // Сбрасываем счётчик если начался новый кадр
    if (elapsed >= targetFrameTime) {
        s_waitCount = 0;
        s_lastWaitTime = currentTime;
    }
    
    // Ждём только первый вызов за кадр
    if (s_waitCount == 0) {
#ifdef _WIN32
        // Мультимедийный таймер для точного Sleep()
        static bool s_timerSet = false;
        if (!s_timerSet) {
            timeBeginPeriod(1);
            s_timerSet = true;
        }
#endif
        Sleep(targetFrameTime);
        s_waitCount++;
        s_lastWaitTime = GetTickCount();
    }
}

void VulkanRenderer::FlipToGDISurface() {
    // Не используется в тестах — заглушка
}

HRESULT VulkanRenderer::GetGDISurface(LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface) {
    if (!lplpGDIDDSSurface) return DDERR_INVALIDPARAMS;
    *lplpGDIDDSSurface = nullptr;
    return DDERR_NOTFOUND;
}

std::vector<DisplayMode> VulkanRenderer::GetSupportedModes() {
    std::vector<DisplayMode> modes;
    // Dune 2000 и др.: 640x400 16bpp
    modes.emplace_back(640, 400, 16, 60);
    modes.emplace_back(640, 480, 16, 60);
    modes.emplace_back(640, 480, 32, 60);
    modes.emplace_back(800, 600, 16, 60);
    modes.emplace_back(800, 600, 32, 60);
    modes.emplace_back(1024, 768, 32, 60);
    modes.emplace_back(1280, 720, 32, 60);
    modes.emplace_back(1920, 1080, 32, 60);
    return modes;
}

void VulkanRenderer::GetPrimarySurfaceSize(uint32_t& outWidth, uint32_t& outHeight) const {
    outWidth = m_currentMode.width;
    outHeight = m_currentMode.height;
#ifdef _WIN32
    if (m_hwnd) {
        RECT r{};
        if (GetClientRect(m_hwnd, &r) && (r.right > r.left) && (r.bottom > r.top)) {
            outWidth = static_cast<uint32_t>(r.right - r.left);
            outHeight = static_cast<uint32_t>(r.bottom - r.top);
        }
    }
#endif
}

SurfaceImpl* VulkanRenderer::CreateSurface(const DDSURFACEDESC2& desc) {
    DDSURFACEDESC2 fixedDesc = desc;
    bool isPrimary = (desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    const bool wantBackBuffer = isPrimary && (desc.dwFlags & DDSD_BACKBUFFERCOUNT) != 0 && desc.dwBackBufferCount > 0;

    // Primary без размера/формата в дескрипторе — берём из текущего режима (SetDisplayMode).
    // Иначе игра, вызвав SetDisplayMode(640,400,16), получит 640x480 32bpp и цвета «поедут».
    if (isPrimary && (desc.dwWidth == 0 || desc.dwHeight == 0 || desc.ddpfPixelFormat.dwRGBBitCount == 0)) {
        fixedDesc.dwWidth = (m_currentMode.width > 0) ? m_currentMode.width : 640;
        fixedDesc.dwHeight = (m_currentMode.height > 0) ? m_currentMode.height : 480;
        if (fixedDesc.ddpfPixelFormat.dwRGBBitCount == 0) {
            fixedDesc.ddpfPixelFormat.dwRGBBitCount = (m_currentMode.bpp > 0) ? m_currentMode.bpp : 32;
            if (fixedDesc.ddpfPixelFormat.dwRGBBitCount == 16) {
                fixedDesc.ddpfPixelFormat.dwFlags = DDPF_RGB;
                fixedDesc.ddpfPixelFormat.dwRBitMask = 0xF800;
                fixedDesc.ddpfPixelFormat.dwGBitMask = 0x07E0;
                fixedDesc.ddpfPixelFormat.dwBBitMask = 0x001F;
            }
        }
    }

    auto surface = new SurfaceImpl(this);
    if (!surface->Create(&fixedDesc)) {
        delete surface;
        return nullptr;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
        m_surfaces.push_back(surface);
        if (surface->isPrimary)
            m_primarySurface = surface;
    }
    if (surface->isPrimary) {
        if (wantBackBuffer) {
            DDSURFACEDESC2 backDesc = {};
            backDesc.dwSize = sizeof(backDesc);
            backDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
            backDesc.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;
            backDesc.dwWidth = surface->width;
            backDesc.dwHeight = surface->height;
            backDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
            backDesc.ddpfPixelFormat.dwFlags = DDPF_RGB;
            backDesc.ddpfPixelFormat.dwRGBBitCount = fixedDesc.ddpfPixelFormat.dwRGBBitCount;
            backDesc.ddpfPixelFormat.dwRBitMask = fixedDesc.ddpfPixelFormat.dwRBitMask;
            backDesc.ddpfPixelFormat.dwGBitMask = fixedDesc.ddpfPixelFormat.dwGBitMask;
            backDesc.ddpfPixelFormat.dwBBitMask = fixedDesc.ddpfPixelFormat.dwBBitMask;

            auto back = new SurfaceImpl(this);
            if (back->Create(&backDesc)) {
                surface->backBuffer = back;
                back->frontBuffer = surface;
                {
                    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
                    m_surfaces.push_back(back);
                }
            } else {
                delete back;
            }
        }

        if (m_initialized && m_swapchainManager && m_commandManager && m_hwnd) {
            BeginFrame();
            Present();
        }
    }
    return surface;
}

void VulkanRenderer::DestroySurface(SurfaceImpl* surface) {
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    if (surface == m_primarySurface) {
        m_primarySurface = nullptr;
    }
    auto it = std::find(m_surfaces.begin(), m_surfaces.end(), surface);
    if (it != m_surfaces.end()) {
        m_surfaces.erase(it);
    }
    // Отложенное удаление: delete в начале следующего BeginFrame, чтобы не было use-after-free
    // при чтении primary->mappedData или src->mappedData в том же кадре.
    m_pendingDeleteSurfaces.push_back(surface);
}

void VulkanRenderer::SwapPrimaryAndBackBuffer(SurfaceImpl* backBuffer) {
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    if (!m_primarySurface || !backBuffer) return;
    std::swap(m_primarySurface->mappedData, backBuffer->mappedData);
    std::swap(m_primarySurface->dataSize, backBuffer->dataSize);
    std::swap(m_primarySurface->texture, backBuffer->texture);
    std::swap(m_primarySurface->isPrimary, backBuffer->isPrimary);
    std::swap(m_primarySurface->isBackBuffer, backBuffer->isBackBuffer);
}

void VulkanRenderer::DestroySurfaceByUserData(void* ptr) {
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        if ((*it)->userData == ptr) {
            SurfaceImpl* toDelete = *it;
            if (toDelete == m_primarySurface) {
                m_primarySurface = nullptr;
            }
            // По документации DX7: при освобождении primary неявно освобождаются все attached surfaces
            // (back buffer). Добавляем back buffer в pending delete первым, чтобы удалить до primary.
            if (toDelete->backBuffer) {
                SurfaceImpl* back = toDelete->backBuffer;
                toDelete->backBuffer = nullptr;  // чтобы IsValidSurface(back) сразу стал false
                if (back) back->frontBuffer = nullptr;
                auto itBack = std::find(m_surfaces.begin(), m_surfaces.end(), back);
                if (itBack != m_surfaces.end()) {
                    m_surfaces.erase(itBack);
                    m_pendingDeleteSurfaces.push_back(back);
                }
            }
            m_surfaces.erase(it);
            m_pendingDeleteSurfaces.push_back(toDelete);
            break;
        }
    }
}

bool VulkanRenderer::EnsurePrimaryStagingBuffer(size_t requiredSize) {
    if (m_primaryStagingBuffer != VK_NULL_HANDLE &&
        m_primaryStagingSize >= requiredSize) {
        return true;
    }
    DestroyPrimaryStagingBuffer();
    if (!m_allocator || requiredSize == 0) return false;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = requiredSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(m_allocator, &bufInfo, &allocInfo,
                        &m_primaryStagingBuffer,
                        &m_primaryStagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    m_primaryStagingSize = requiredSize;
    return true;
}

void VulkanRenderer::DestroyPrimaryStagingBuffer() {
    if (m_primaryStagingBuffer != VK_NULL_HANDLE && m_allocator) {
        vmaDestroyBuffer(m_allocator, m_primaryStagingBuffer, m_primaryStagingAllocation);
    }
    m_primaryStagingBuffer = VK_NULL_HANDLE;
    m_primaryStagingAllocation = VK_NULL_HANDLE;
    m_primaryStagingSize = 0;
}

bool VulkanRenderer::EnsurePrimarySourceImage(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (m_primarySourceImage != VK_NULL_HANDLE &&
        m_primarySourceWidth == width && m_primarySourceHeight == height) {
        return true;
    }
    DestroyPrimarySourceImage();
    if (!m_device || !m_allocator) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = { width, height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_allocator, &imgInfo, &allocInfo,
                       &m_primarySourceImage, &m_primarySourceAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    m_primarySourceWidth = width;
    m_primarySourceHeight = height;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_primarySourceImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_primarySourceImageView) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, m_primarySourceImage, m_primarySourceAllocation);
        m_primarySourceImage = VK_NULL_HANDLE;
        m_primarySourceAllocation = nullptr;
        m_primarySourceWidth = 0;
        m_primarySourceHeight = 0;
        return false;
    }
    return true;
}

void VulkanRenderer::DestroyPrimarySourceImage() {
    if (m_primarySourceImageView != VK_NULL_HANDLE && m_device) {
        vkDestroyImageView(m_device, m_primarySourceImageView, nullptr);
        m_primarySourceImageView = VK_NULL_HANDLE;
    }
    if (m_primarySourceImage != VK_NULL_HANDLE && m_allocator) {
        vmaDestroyImage(m_allocator, m_primarySourceImage, m_primarySourceAllocation);
        m_primarySourceImage = VK_NULL_HANDLE;
        m_primarySourceAllocation = nullptr;
    }
    m_primarySourceWidth = 0;
    m_primarySourceHeight = 0;
}

void VulkanRenderer::DeferFreeMappedData(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    m_deferredFreeBuffers.push_back(ptr);
}

bool VulkanRenderer::IsValidSurface(SurfaceImpl* impl) const {
    if (!impl) return false;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    for (SurfaceImpl* s : m_surfaces) {
        if (s == impl) return true;
        if (s && s->backBuffer == impl) return true;
    }
    return false;
}

bool VulkanRenderer::IsValidSurfaceWrapper(const void* ptr) const {
    if (!ptr) return false;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    for (SurfaceImpl* s : m_surfaces) {
        if (s && s->userData == ptr) return true;
        if (s && s->backBuffer && s->backBuffer->userData == ptr) return true;
    }
    return false;
}

bool VulkanRenderer::IsWrapperFreed(const void* ptr) const {
    if (!ptr) return false;
    std::lock_guard<std::mutex> lock(m_freedWrappersMutex);
    return m_freedWrappers.count(const_cast<void*>(ptr)) != 0;
}

bool VulkanRenderer::IsDeferredBuffer(const void* ptr) const {
    if (!ptr) return false;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    for (void* p : m_deferredFreeBuffers) {
        if (p == ptr) return true;
    }
    return false;
}

HRESULT VulkanRenderer::BltCopyUnderLock(SurfaceImpl* dest, SurfaceImpl* src, const RECT* srcRect, const RECT* destRect) {
    if (!dest || !src) return DDERR_INVALIDPARAMS;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    if (!IsValidSurface(src)) return DDERR_INVALIDOBJECT;
    if (!src->mappedData || IsDeferredBuffer(src->mappedData)) return DDERR_SURFACELOST;
    dest->CopyFrom(src, srcRect, destRect);
    return DD_OK;
}

SurfaceImpl* VulkanRenderer::GetSurfaceByWrapperUnderLock(const void* ptr) const {
    if (!ptr) return nullptr;
    for (SurfaceImpl* s : m_surfaces) {
        if (s && s->userData == ptr) return s;
        if (s && s->backBuffer && s->backBuffer->userData == ptr) return s->backBuffer;
    }
    return nullptr;
}

SurfaceImpl* VulkanRenderer::GetSurfaceByWrapper(const void* ptr) const {
    if (!ptr) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
    return GetSurfaceByWrapperUnderLock(ptr);
}

void VulkanRenderer::BeginFrame() {
    // Освобождаем отложенные обёртки поверхностей (сделаны в Release при ref==0), чтобы повторный вызов по тому же указателю не читал уже освобождённую память.
    {
        std::vector<void*> toFree;
        { std::lock_guard<std::mutex> lock(m_pendingFreeWrappersMutex); toFree = std::move(m_pendingFreeWrappers); }
        {
            std::lock_guard<std::mutex> freedLock(m_freedWrappersMutex);
            for (void* p : toFree) m_freedWrappers.insert(p);
        }
        for (void* p : toFree) delete[] static_cast<char*>(p);
        /* Удаляем освобождённые указатели из m_freedWrappers, чтобы при повторном использовании
         * памяти по тому же адресу IsWrapperFreed возвращал false. */
        {
            std::lock_guard<std::mutex> freedLock(m_freedWrappersMutex);
            for (void* p : toFree) m_freedWrappers.erase(p);
        }
    }
    // Удаление pending перенесено в конец кадра: если BeginFrame вызван из FlushDeferredPresent
    // внутри Blt, то после возврата из Flush тот же Blt ещё использует источник — не удалять до
    // конца кадра.

    // Не ждём fence - FIFO present mode (vsync) сам ограничивает FPS
    // Дополнительное ожидание здесь вызывает "тормоза"

    if (!m_swapchainManager || !m_commandManager) {
        return;
    }

    if (!m_swapchainManager->IsValid()) {
        if (!m_swapchainManager->EnsureSwapchain()) return;
        if (!m_swapchainManager->IsValid()) return; /* still deferred */
    }

    // Чередуем командные буферы (double buffering)
    static uint32_t s_currentCmdBuffer = 0;
    const uint32_t cmdIndex = s_currentCmdBuffer % m_commandBufferCount;
    s_currentCmdBuffer++;

    // Wait only for the fence of the command buffer we're about to reuse.
    // Allows up to 2 frames in flight and avoids blocking every frame when GPU is busy.
    if (cmdIndex < kMaxFramesInFlight && m_inFlightFences[cmdIndex]) {
        VkResult waitResult = vkWaitForFences(m_device, 1, &m_inFlightFences[cmdIndex], VK_TRUE, 50000000); // 50ms
        if (waitResult == VK_SUCCESS) {
            vkResetFences(m_device, 1, &m_inFlightFences[cmdIndex]);
        }
    }

    if (!m_swapchainManager->AcquireNextImage()) {
        RecreateSwapchain();
        if (!m_swapchainManager->AcquireNextImage()) {
            return;
        }
    }

    VkCommandBuffer cmd = m_commandManager->BeginCommandBuffer(cmdIndex);
    if (cmd == VK_NULL_HANDLE) {
        return;
    }
    
    VkImage image = m_swapchainManager->GetCurrentImage();
    if (image == VK_NULL_HANDLE) {
        m_commandManager->EndCommandBuffer(cmdIndex);
        return;
    }

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    const uint32_t swapW = m_swapchainManager->GetWidth();
    const uint32_t swapH = m_swapchainManager->GetHeight();
    bool didCopyPrimary = false;
    uint32_t primW = 0, primH = 0;
    uint32_t primBpp = 0;
    bool didCopyToStaging = false;

    // Держим мьютекс на всё время чтения primary и копирования в staging, чтобы
    // Release()/DestroySurface() не удалили surface до завершения копирования (use-after-free).
    {
        std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
        SurfaceImpl* primary = m_primarySurface;
        int surfaceCount = 0;
        int primaryCount = 0;
        if (!primary) {
            for (auto* surface : m_surfaces) {
                surfaceCount++;
                if (surface && surface->isPrimary) {
                    primaryCount++;
                    primary = surface;
                    break;
                }
            }
        }
        if (primary && !IsValidSurface(primary)) primary = nullptr;
        if (primary && primary->mappedData && IsDeferredBuffer(primary->mappedData)) primary = nullptr;

        if (primary && primary->mappedData &&
            primary->width > 0 && primary->height > 0 &&
            (primary->bpp == 32 || primary->bpp == 16 || primary->bpp == 8)) {
            primW = primary->width;
            primH = primary->height;
            primBpp = primary->bpp;
            const size_t primSizeRGBA = static_cast<size_t>(primW) * primH * 4;
            if (primSizeRGBA > 0 && EnsurePrimaryStagingBuffer(primSizeRGBA)) {
                void* mapped = nullptr;
                if (vmaMapMemory(m_allocator, m_primaryStagingAllocation, &mapped) == VK_SUCCESS) {
                    if (primary->bpp == 32) {
                        memcpy(mapped, primary->mappedData, primSizeRGBA);
                    } else if (primary->bpp == 16) {
                        const uint16_t* src = static_cast<const uint16_t*>(primary->mappedData);
                        uint32_t* dst = static_cast<uint32_t*>(mapped);
                        const uint32_t pixelCount = primW * primH;
                        // Dune 2000 и Tiberian Dawn используют BGR565, Tiberian Sun — RGB565
                        const bool isBGR565 = hack::IsBGR565Game();
                        const uint32_t redMask = primary->format.redMask ? primary->format.redMask : 0xF800u;
                        const uint32_t greenMask = primary->format.greenMask ? primary->format.greenMask : 0x07E0u;
                        const uint32_t blueMask = primary->format.blueMask ? primary->format.blueMask : 0x001Fu;
                        // Определяем порядок каналов: если красная маска в младших битах (0x001F) — BGR
                        const bool isBGR = isBGR565 || (redMask == 0x001Fu || redMask < blueMask);
                        auto expandMasked = [](uint32_t value, uint32_t mask) -> uint32_t {
                            if (!mask) return 0;
                            uint32_t shift = 0;
                            while (((mask >> shift) & 1u) == 0u) shift++;
                            uint32_t bits = 0;
                            uint32_t tmp = mask >> shift;
                            while (tmp & 1u) {
                                bits++;
                                tmp >>= 1;
                            }
                            const uint32_t component = (value & mask) >> shift;
                            const uint32_t maxValue = (1u << bits) - 1u;
                            return maxValue ? (component * 255u / maxValue) : 0u;
                        };
                        for (uint32_t i = 0; i < pixelCount; ++i) {
                            uint16_t p = src[i];
                            uint32_t r = 0;
                            uint32_t g = 0;
                            uint32_t b = 0;
                            if (isBGR) {
                                // BGR565: B в старших битах (15-11), R в младших (4-0)
                                r = (p & 0x001F) * 255 / 31;
                                g = ((p >> 5) & 0x003F) * 255 / 63;
                                b = ((p >> 11) & 0x001F) * 255 / 31;
                            } else {
                                // RGB565: R в старших битах (15-11), B в младших (4-0)
                                r = expandMasked(p, redMask);
                                g = expandMasked(p, greenMask);
                                b = expandMasked(p, blueMask);
                            }
                            dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
                        }
                    } else {
                        uint32_t* dst = static_cast<uint32_t*>(mapped);
                        const uint8_t* src = static_cast<const uint8_t*>(primary->mappedData);
                        const uint32_t pixelCount = primW * primH;
                        const uint32_t* paletteColors = nullptr;
                        if (primary->paletteImpl && !primary->paletteImpl->vulkanColors.empty()) {
                            paletteColors = primary->paletteImpl->vulkanColors.data();
                        }
                        // Палитра по умолчанию при 8bpp без SetPalette (план DDVK load and Dune 2000 fix):
                        // градиент 0..255 → чёрный..белый, чтобы не было зелёно-фиолетового мусора.
                        static uint32_t s_default8bitPalette[256];
                        static bool s_defaultPaletteFilled = false;
                        if (!s_defaultPaletteFilled) {
                            for (int i = 0; i < 256; ++i) {
                                s_default8bitPalette[i] = 0xFF000000u | (static_cast<uint32_t>(i) * 0x010101u);
                            }
                            s_defaultPaletteFilled = true;
                        }
                        const uint32_t* fallbackPalette = s_default8bitPalette;
                        for (uint32_t i = 0; i < pixelCount; ++i) {
                            const uint8_t idx = src[i];
                            uint32_t color = (paletteColors ? paletteColors[idx] : fallbackPalette[idx]);
                            dst[i] = color;
                        }
                    }
                    vmaUnmapMemory(m_allocator, m_primaryStagingAllocation);
                    didCopyToStaging = true;
                }
            }
        }
    }

    if (didCopyToStaging && primW > 0 && primH > 0) {
        const uint32_t copyW = (primW < swapW) ? primW : swapW;
        const uint32_t copyH = (primH < swapH) ? primH : swapH;
        if (copyW > 0 && copyH > 0) {
            VkImageMemoryBarrier toDst{};
            toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.image = image;
            toDst.subresourceRange = range;
            toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &toDst);

            // Очищаем весь swapchain в чёрный.
            VkClearColorValue black = {};
            black.float32[0] = 0.0f;
            black.float32[1] = 0.0f;
            black.float32[2] = 0.0f;
            black.float32[3] = 1.0f;
            vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

            // Масштабирование через utils::ScaleBufferToImage
            utils::ScaleParams scaleParams;
            scaleParams.srcWidth = primW;
            scaleParams.srcHeight = primH;
            scaleParams.dstWidth = swapW;
            scaleParams.dstHeight = swapH;
            scaleParams.preserveAspect = true;  // Сохранять пропорции
            scaleParams.center = true;          // Центрировать изображение

            utils::ScaleResult scaleResult = utils::CalculateScale(scaleParams);

            if (scaleResult.needsScaling && EnsurePrimarySourceImage(primW, primH)) {
                // Масштабирование с использованием staging изображения
                utils::ScaleBufferToImage(
                    cmd,
                    m_primaryStagingBuffer,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    scaleParams,
                    m_primarySourceImage,
                    m_primarySourceAllocation);
            } else {
                // 1:1 копирование buffer -> swapchain.
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = primW;
                region.bufferImageHeight = primH;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {copyW, copyH, 1};
                vkCmdCopyBufferToImage(cmd, m_primaryStagingBuffer, image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }

            VkImageMemoryBarrier toPresent{};
            toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.image = image;
            toPresent.subresourceRange = range;
            toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &toPresent);
            didCopyPrimary = true;
        }
    }

    if (!didCopyPrimary) {
        VkImageMemoryBarrier toClear{};
        toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toClear.image = image;
        toClear.subresourceRange = range;
        toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toClear);

        VkClearColorValue clearColor{};
        clearColor.float32[0] = 0.0f;
        clearColor.float32[1] = 0.1f;
        clearColor.float32[2] = 0.2f;
        clearColor.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor, 1, &range);

        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.image = image;
        toPresent.subresourceRange = range;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toPresent);
    }

    m_commandManager->EndCommandBuffer(cmdIndex);

    VkSemaphore imageAvailable = m_imageAvailableSemaphore;
    VkSemaphore renderFinished = m_renderFinishedSemaphore;
    // Используем fence для синхронизации с vsync
    VkFence submitFence = (cmdIndex < kMaxFramesInFlight) ? m_inFlightFences[cmdIndex] : VK_NULL_HANDLE;
    if (!m_commandManager->SubmitCommandBuffer(cmdIndex, imageAvailable,
                                               renderFinished, submitFence)) {
    }

    // Удаляем поверхности, помеченные на удаление ранее (Release/DestroySurfaceByUserData).
    // В конце кадра, после всего копирования и submit — тогда Blt, вызвавший Flush→BeginFrame,
    // после возврата не будет обращаться к уже удалённой поверхности.
    {
        std::vector<SurfaceImpl*> toDelete;
        {
            std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
            toDelete = std::move(m_pendingDeleteSurfaces);
            m_pendingDeleteSurfaces.clear();
        }
        for (SurfaceImpl* s : toDelete) {
            delete s;
        }
    }
}

void VulkanRenderer::EndFrame() {
    // Ничего не делаем — логика в BeginFrame/Present.
}

void VulkanRenderer::WaitForGpu() {
    if (!m_device) return;
    // Ждём только наши кадры (fence), а не vkDeviceWaitIdle — в меню DDFLIP_WAIT
    // вызывают каждый кадр и vkDeviceWaitIdle давал сильные подтормаживания курсора.
    const uint64_t timeoutNs = 33000000; // 33ms
    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        if (m_inFlightFences[i]) {
            vkWaitForFences(m_device, 1, &m_inFlightFences[i], VK_TRUE, timeoutNs);
        }
    }
}

void VulkanRenderer::Present() {
    if (!m_swapchainManager) {
        return;
    }
    if (!m_swapchainManager->IsValid()) return; /* deferred swapchain */

    // Fence is waited at the start of BeginFrame(), not here, so Present() does not block
    // and FPS stays consistent (avoids dips to ~15 FPS when GPU/compositor is slow).

    bool presentResult = m_swapchainManager->Present();
}

void VulkanRenderer::RequestDeferredPresent() {
    m_deferredPresent = true;
}

void VulkanRenderer::FlushDeferredPresent() {
    if (!m_deferredPresent) return;
    m_deferredPresent = false;
    if (m_swapchainManager && m_commandManager && m_hwnd) {
        BeginFrame();
        Present();
    }
}

void VulkanRenderer::ClearDeferredPresent() {
    m_deferredPresent = false;
}

void VulkanRenderer::DeferFreeWrapper(void* wrapper) {
    if (!wrapper) return;
    std::lock_guard<std::mutex> lock(m_pendingFreeWrappersMutex);
    m_pendingFreeWrappers.push_back(wrapper);
}

void VulkanRenderer::MarkWrapperFreed(void* wrapper) {
    if (!wrapper) return;
    std::lock_guard<std::mutex> lock(m_freedWrappersMutex);
    m_freedWrappers.insert(wrapper);
}

void VulkanRenderer::CleanupSwapchain() {
    if (m_swapchainManager) {
        m_swapchainManager->Shutdown();
    }
}

void VulkanRenderer::RecreateSwapchain() {
    if (!m_swapchainManager) return;
    uint32_t swapWidth = m_currentMode.width;
    uint32_t swapHeight = m_currentMode.height;
#ifdef _WIN32
    if (m_hwnd) {
        RECT r{};
        if (GetClientRect(m_hwnd, &r) && (r.right > r.left) && (r.bottom > r.top)) {
            swapWidth = static_cast<uint32_t>(r.right - r.left);
            swapHeight = static_cast<uint32_t>(r.bottom - r.top);
        }
    }
    // В полноэкранном режиме окно может ещё не успеть растянуться — подставляем размер экрана.
    if (m_isFullScreen && (swapWidth <= 640 || swapHeight <= 480)) {
        swapWidth = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
        swapHeight = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    }
#endif
    if (swapWidth == 0) swapWidth = 640;
    if (swapHeight == 0) swapHeight = 480;
    m_swapchainManager->Resize(swapWidth, swapHeight);
}

} // namespace ddvk

