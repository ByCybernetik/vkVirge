#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "vk_utils_core.h"

namespace vk_sync {

    const int MAX_FRAMES_IN_FLIGHT = 2;

    struct SyncObjects {
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        uint32_t currentFrame = 0;
    };

    // Создание объектов синхронизации
    SyncObjects createSyncObjects(VkDevice device);

    // Ожидание предыдущего кадра
    void waitForFrame(VkDevice device, const SyncObjects& sync, uint32_t frameIndex);

    // Сброс fence для текущего кадра
    void resetFence(VkDevice device, const SyncObjects& sync, uint32_t frameIndex);

    // Очистка объектов синхронизации
    void cleanupSyncObjects(VkDevice device, SyncObjects& sync);

} // namespace vk_sync
