#include "vk_sync.h"
#include "vk_utils_core.h"  // Исправлено: убраны лишние кавычки

namespace vk_sync {

    SyncObjects createSyncObjects(VkDevice device) {
        SyncObjects sync;

        sync.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        sync.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        sync.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &sync.imageAvailableSemaphores[i]);
            vk_utils::checkVkResult(result, "Failed to create image available semaphore!");

            result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &sync.renderFinishedSemaphores[i]);
            vk_utils::checkVkResult(result, "Failed to create render finished semaphore!");

            result = vkCreateFence(device, &fenceInfo, nullptr, &sync.inFlightFences[i]);
            vk_utils::checkVkResult(result, "Failed to create fence!");
        }

        return sync;
    }

    void waitForFrame(VkDevice device, const SyncObjects& sync, uint32_t frameIndex) {
        vkWaitForFences(device, 1, &sync.inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);
    }

    void resetFence(VkDevice device, const SyncObjects& sync, uint32_t frameIndex) {
        vkResetFences(device, 1, &sync.inFlightFences[frameIndex]);
    }

    void cleanupSyncObjects(VkDevice device, SyncObjects& sync) {
        for (size_t i = 0; i < sync.imageAvailableSemaphores.size(); i++) {
            if (sync.imageAvailableSemaphores[i]) {
                vkDestroySemaphore(device, sync.imageAvailableSemaphores[i], nullptr);
            }
            if (sync.renderFinishedSemaphores[i]) {
                vkDestroySemaphore(device, sync.renderFinishedSemaphores[i], nullptr);
            }
            if (sync.inFlightFences[i]) {
                vkDestroyFence(device, sync.inFlightFences[i], nullptr);
            }
        }
        sync.imageAvailableSemaphores.clear();
        sync.renderFinishedSemaphores.clear();
        sync.inFlightFences.clear();
    }

} // namespace vk_sync
