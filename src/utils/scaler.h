#pragma once

#include "../ddvk/ddvk_core.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace ddvk {
namespace utils {

/**
 * Параметры масштабирования изображения
 */
struct ScaleParams {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
    bool preserveAspect;  // Сохранять пропорции
    bool center;          // Центрировать изображение

    ScaleParams()
        : srcWidth(0), srcHeight(0)
        , dstWidth(0), dstHeight(0)
        , preserveAspect(true), center(true) {}

    ScaleParams(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
        : srcWidth(srcW), srcHeight(srcH)
        , dstWidth(dstW), dstHeight(dstH)
        , preserveAspect(true), center(true) {}
};

/**
 * Результат расчёта масштабирования
 */
struct ScaleResult {
    int32_t dstX;
    int32_t dstY;
    int32_t dstW;
    int32_t dstH;
    float scaleX;
    float scaleY;
    bool needsScaling;

    ScaleResult()
        : dstX(0), dstY(0), dstW(0), dstH(0)
        , scaleX(1.0f), scaleY(1.0f), needsScaling(false) {}
};

/**
 * Рассчитать параметры масштабирования
 * @param params Входные параметры
 * @return Результат масштабирования
 */
ScaleResult CalculateScale(const ScaleParams& params);

/**
 * Выполнить масштабирование изображения через Vulkan blit
 * @param cmd Командный буфер
 * @param srcImage Исходное изображение
 * @param srcLayout Текущий layout исходного изображения
 * @param dstImage Целевое изображение
 * @param dstLayout Текущий layout целевого изображения
 * @param params Параметры масштабирования
 */
void ScaleImage(
    VkCommandBuffer cmd,
    VkImage srcImage,
    VkImageLayout srcLayout,
    VkImage dstImage,
    VkImageLayout dstLayout,
    const ScaleParams& params);

/**
 * Выполнить масштабирование с буфером в изображение
 * @param cmd Командный буфер
 * @param srcBuffer Исходный буфер
 * @param dstImage Целевое изображение
 * @param dstLayout Текущий layout целевого изображения
 * @param params Параметры масштабирования
 * @param stagingImage Промежуточное изображение для масштабирования
 * @param stagingAllocation Выделение памяти для staging изображения
 */
void ScaleBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstLayout,
    const ScaleParams& params,
    VkImage stagingImage,
    VmaAllocation stagingAllocation);

/**
 * Создать промежуточное изображение для масштабирования
 * @param device Vulkan устройство
 * @param allocator VMA аллокатор
 * @param width Ширина изображения
 * @param height Высота изображения
 * @param format Формат пикселей
 * @param outImage Выходное изображение
 * @param outAllocation Выделение памяти
 * @param outImageView View изображения
 * @return true при успехе
 */
bool CreateStagingImage(
    VkDevice device,
    VmaAllocator allocator,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImage* outImage,
    VmaAllocation* outAllocation,
    VkImageView* outImageView);

/**
 * Уничтожить промежуточное изображение
 * @param device Vulkan устройство
 * @param image Изображение
 * @param allocation Выделение памяти
 * @param imageView View изображения
 */
void DestroyStagingImage(
    VkDevice device,
    VkImage image,
    VmaAllocation allocation,
    VkImageView imageView);

} // namespace utils
} // namespace ddvk
