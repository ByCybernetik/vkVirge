#pragma once

#include "ddvk_core.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <chrono>

namespace ddvk {

class SwapchainManager;
class CommandManager;

// Основной класс, инкапсулирующий Vulkan‑рендерер для DirectDraw.
class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    // Инициализация/выгрузка Vulkan. Initialize вызывается лениво из SetCooperativeLevel.
    bool Initialize();
    void Shutdown();

    // Доступ к объектам Vulkan
    VkDevice         GetDevice()                 const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice()         const { return m_physicalDevice; }
    VmaAllocator     GetAllocator()              const { return m_allocator; }
    uint32_t         GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    uint32_t         GetPresentQueueFamilyIndex()  const { return m_presentQueueFamilyIndex; }
    VkQueue          GetGraphicsQueue()          const { return m_graphicsQueue; }
    VkQueue          GetPresentQueue()           const { return m_presentQueue; }
    VkCommandPool    GetCommandPool()            const { return m_commandPool; }
    VkSurfaceKHR     GetSurface()                const { return m_surface; }

    VkSemaphore GetImageAvailableSemaphore() const { return m_imageAvailableSemaphore; }
    VkSemaphore GetRenderFinishedSemaphore() const { return m_renderFinishedSemaphore; }

    // Интеграция с DirectDraw
    HRESULT SetCooperativeLevel(HWND hWnd, DWORD dwFlags);
    HRESULT SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP,
                           DWORD dwRefreshRate, DWORD dwFlags);
    HRESULT GetDisplayMode(LPDDSURFACEDESC2 lpDDSurfaceDesc2);
    HRESULT RestoreDisplayMode();

    // Простейшие заглушки для caps / видеопамяти
    HRESULT GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps);
    HRESULT GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps2, LPDWORD lpdwTotal, LPDWORD lpdwFree);

    bool IsVerticalBlank();
    void WaitForVerticalBlank();
    void FlipToGDISurface();
    HRESULT GetGDISurface(LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface);

    std::vector<DisplayMode> GetSupportedModes();

    // Работа с поверхностями ddvk
    void GetPrimarySurfaceSize(uint32_t& outWidth, uint32_t& outHeight) const;
    SurfaceImpl* CreateSurface(const DDSURFACEDESC2& desc);
    void DestroySurface(SurfaceImpl* surface);
    /** Меняет местами mappedData и isPrimary у primary и back buffer (для Flip). */
    void SwapPrimaryAndBackBuffer(SurfaceImpl* backBuffer);
    // Поиск и уничтожение поверхности по userData (COM‑объекту IDirectDrawSurface7)
    void DestroySurfaceByUserData(void* ptr);

    void BeginFrame();
    void EndFrame();
    void Present();
    
    /** Ждёт завершения текущего кадра GPU (для DDFLIP_WAIT) */
    void WaitForGpu();

    /** Request that Present run at the next FlushDeferredPresent (avoids presenting inside Blt call stack). */
    void RequestDeferredPresent();
    /** If a present was deferred, run BeginFrame+Present now. Call at entry of Flip/ReleaseDC/Blt (not Unlock: Unlock would clear flag before direct Present). */
    void FlushDeferredPresent();
    /** Clear deferred present flag after direct Present (e.g. in Unlock) to avoid double-present. */
    void ClearDeferredPresent();
    /** Отложить освобождение COM-обёртки поверхности до следующего BeginFrame (избегаем use-after-free при повторном вызове). */
    void DeferFreeWrapper(void* wrapper);

    /** Отложить освобождение буфера до Shutdown (избегает use-after-free при чтении из Lock()). */
    void DeferFreeMappedData(void* ptr);
    /** Проверить, что поверхность ещё в списке живых (защита от stale COM при Blt). */
    bool IsValidSurface(SurfaceImpl* impl) const;
    /** Проверить, что указатель — наша COM-обёртка поверхности (без разыменования; защита от page fault). */
    bool IsValidSurfaceWrapper(const void* ptr) const;
    /** Проверить, что обёртка уже освобождена (не разыменовывать — защита от use-after-free). */
    bool IsWrapperFreed(const void* ptr) const;
    /** Пометить обёртку как освобождённую без освобождения памяти (игра может вызвать по указателю позже — тогда войдём в наш код и вернём 0 вместо page fault по нулевому vtable). */
    void MarkWrapperFreed(void* wrapper);
    /** Проверить, что буфер в списке отложенных (не читать из него — мог быть передан куда-то снаружи). */
    bool IsDeferredBuffer(const void* ptr) const;

    /** Выполнить CopyFrom(dest, src, ...) под мьютексом, чтобы src не удалили во время копирования (use-after-free в Blt). */
    HRESULT BltCopyUnderLock(SurfaceImpl* dest, SurfaceImpl* src, const RECT* srcRect, const RECT* destRect);

    /** Найти SurfaceImpl по указателю на COM-обёртку (s->userData == ptr). Вызывать только при удержанном m_surfacesMutex. */
    SurfaceImpl* GetSurfaceByWrapperUnderLock(const void* ptr) const;

    /** Найти SurfaceImpl по указателю обёртки под замком (для безопасного доступа без разыменования обёртки после уничтожения поверхности). */
    SurfaceImpl* GetSurfaceByWrapper(const void* ptr) const;

    /** Выполнить код под мьютексом поверхностей (для Blt: не разыменовывать lpDDSrcSurface вне замка). */
    template<typename F>
    HRESULT ExecuteUnderSurfaceLock(F&& f) {
        std::lock_guard<std::recursive_mutex> lock(m_surfacesMutex);
        return f();
    }

    bool IsInitialized() const { return m_initialized; }

private:
    // Вспомогательные методы инициализации Vulkan
    bool CreateInstance();
    bool PickPhysicalDevice();
    bool CreateSurface();
    bool CreateDevice();
    bool CreateRenderPass();
    bool CreateCommandPool();
    bool CreateSyncObjects();

    void CleanupSwapchain();
    void RecreateSwapchain();

    bool EnsurePrimaryStagingBuffer(size_t requiredSize);
    void DestroyPrimaryStagingBuffer();

private:
    VkInstance       m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice         m_device;
    uint32_t         m_graphicsQueueFamilyIndex;
    uint32_t         m_presentQueueFamilyIndex;
    VkQueue          m_graphicsQueue;
    VkQueue          m_presentQueue;
    VkSurfaceKHR     m_surface;
    VkRenderPass     m_renderPass;
    VkCommandPool    m_commandPool;
    VmaAllocator     m_allocator;

    HWND   m_hwnd;
    DWORD  m_cooperativeLevel;
    DisplayMode m_currentMode;
    bool   m_isFullScreen;

    std::vector<SurfaceImpl*> m_surfaces;
    SurfaceImpl*              m_primarySurface;
    std::vector<SurfaceImpl*> m_pendingDeleteSurfaces;  // удаляем в начале BeginFrame, не при Release
    mutable std::recursive_mutex m_surfacesMutex;

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkFence     m_inFlightFences[kMaxFramesInFlight] = {};

    std::unique_ptr<SwapchainManager> m_swapchainManager;
    std::unique_ptr<CommandManager>   m_commandManager;
    uint32_t m_commandBufferCount;
    bool     m_initialized;

    // Staging‑буфер для копирования primary surface в swapchain
    VkBuffer      m_primaryStagingBuffer;
    VmaAllocation m_primaryStagingAllocation;
    size_t        m_primaryStagingSize;

    // Изображение для масштабирования primary -> swapchain (растягиваем при разных размерах)
    VkImage       m_primarySourceImage;
    VkImageView   m_primarySourceImageView;
    VmaAllocation m_primarySourceAllocation;
    uint32_t      m_primarySourceWidth;
    uint32_t      m_primarySourceHeight;

    bool EnsurePrimarySourceImage(uint32_t width, uint32_t height);
    void DestroyPrimarySourceImage();

    bool m_deferredPresent;

    // Буферы уничтоженных поверхностей — освобождаем только в Shutdown (игра может держать указатель из Lock())
    std::vector<void*> m_deferredFreeBuffers;

    // Обёртки поверхностей, освобождение которых отложено до BeginFrame (избегаем use-after-free при повторном вызове трассера/игрой)
    std::vector<void*> m_pendingFreeWrappers;
    std::mutex m_pendingFreeWrappersMutex;
    // Уже освобождённые обёртки — не разыменовывать при последующих вызовах (use-after-free)
    mutable std::mutex m_freedWrappersMutex;
    std::set<void*> m_freedWrappers;

    // Тайминг для WaitForVerticalBlank - эмулируем 60Hz vblank
    std::chrono::steady_clock::time_point m_vblankLastTime;
    bool m_vblankInitialized = false;
    static constexpr double kVblankIntervalMs = 1000.0 / 60.0; // ~16.667ms для 60Hz
};

} // namespace ddvk

