#include "scaler.h"
#include "../ddvk/ddvk_utils.h"
#include <algorithm>
#include <cmath>

namespace ddvk {
namespace utils {

ScaleResult CalculateScale(const ScaleParams& params) {
    ScaleResult result;

    if (params.srcWidth == 0 || params.srcHeight == 0 ||
        params.dstWidth == 0 || params.dstHeight == 0) {
        return result;
    }

    result.needsScaling = (params.srcWidth != params.dstWidth || params.srcHeight != params.dstHeight);

    if (params.preserveAspect) {
        // Сохранение пропорций: вписываем в целевое разрешение
        const float scaleW = static_cast<float>(params.dstWidth) / static_cast<float>(params.srcWidth);
        const float scaleH = static_cast<float>(params.dstHeight) / static_cast<float>(params.srcHeight);
        const float scale = std::min(scaleW, scaleH);

        result.scaleX = scale;
        result.scaleY = scale;
        result.dstW = static_cast<int32_t>(params.srcWidth * scale + 0.5f);
        result.dstH = static_cast<int32_t>(params.srcHeight * scale + 0.5f);

        if (params.center) {
            result.dstX = (static_cast<int32_t>(params.dstWidth) - result.dstW) / 2;
            result.dstY = (static_cast<int32_t>(params.dstHeight) - result.dstH) / 2;
        } else {
            result.dstX = 0;
            result.dstY = 0;
        }
    } else {
        // Масштабирование без сохранения пропорций (растягивание)
        result.scaleX = static_cast<float>(params.dstWidth) / static_cast<float>(params.srcWidth);
        result.scaleY = static_cast<float>(params.dstHeight) / static_cast<float>(params.srcHeight);
        result.dstW = static_cast<int32_t>(params.dstWidth);
        result.dstH = static_cast<int32_t>(params.dstHeight);
        result.dstX = 0;
        result.dstY = 0;
    }

    return result;
}

void ScaleImage(
    VkCommandBuffer cmd,
    VkImage srcImage,
    VkImageLayout srcLayout,
    VkImage dstImage,
    VkImageLayout dstLayout,
    const ScaleParams& params)
{
    if (!cmd || !srcImage || !dstImage) return;

    const ScaleResult scale = CalculateScale(params);
    if (!scale.needsScaling && params.srcWidth == params.dstWidth && params.srcHeight == params.dstHeight) {
        // Масштабирование не требуется — копирование 1:1
        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource = region.srcSubresource;
        region.dstOffset = {0, 0, 0};
        region.extent = {params.srcWidth, params.srcHeight, 1};
        vkCmdCopyImage(cmd, srcImage, srcLayout, dstImage, dstLayout, 1, &region);
        return;
    }

    // Масштабирование через vkCmdBlitImage
    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(params.srcWidth), static_cast<int32_t>(params.srcHeight), 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;

    blit.dstOffsets[0] = {scale.dstX, scale.dstY, 0};
    blit.dstOffsets[1] = {scale.dstX + scale.dstW, scale.dstY + scale.dstH, 1};
    blit.dstSubresource = blit.srcSubresource;

    vkCmdBlitImage(
        cmd,
        srcImage, srcLayout,
        dstImage, dstLayout,
        1, &blit,
        VK_FILTER_LINEAR);
}

void ScaleBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstLayout,
    const ScaleParams& params,
    VkImage stagingImage,
    VmaAllocation stagingAllocation)
{
    (void)stagingAllocation; // Пока не используется напрямую

    if (!cmd || !srcBuffer || !dstImage || !stagingImage) return;

    const ScaleResult scale = CalculateScale(params);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    // 1. Переводим staging изображение в TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier toStagingDst{};
    toStagingDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toStagingDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toStagingDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toStagingDst.image = stagingImage;
    toStagingDst.subresourceRange = range;
    toStagingDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toStagingDst);

    // 2. Копируем буфер в staging изображение
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = params.srcWidth;
    copyRegion.bufferImageHeight = params.srcHeight;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {params.srcWidth, params.srcHeight, 1};
    vkCmdCopyBufferToImage(cmd, srcBuffer, stagingImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // 3. Переводим staging изображение в TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier toStagingSrc{};
    toStagingSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toStagingSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toStagingSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toStagingSrc.image = stagingImage;
    toStagingSrc.subresourceRange = range;
    toStagingSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toStagingSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toStagingSrc);

    // 4. Масштабируем из staging в целевое изображение
    if (scale.needsScaling) {
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {static_cast<int32_t>(params.srcWidth), static_cast<int32_t>(params.srcHeight), 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = 0;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = {scale.dstX, scale.dstY, 0};
        blit.dstOffsets[1] = {scale.dstX + scale.dstW, scale.dstY + scale.dstH, 1};
        blit.dstSubresource = blit.srcSubresource;

        vkCmdBlitImage(
            cmd,
            stagingImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dstImage, dstLayout,
            1, &blit,
            VK_FILTER_LINEAR);
    } else {
        // Копирование 1:1
        VkImageCopy copyRegion2{};
        copyRegion2.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion2.srcSubresource.mipLevel = 0;
        copyRegion2.srcSubresource.baseArrayLayer = 0;
        copyRegion2.srcSubresource.layerCount = 1;
        copyRegion2.srcOffset = {0, 0, 0};
        copyRegion2.dstSubresource = copyRegion2.srcSubresource;
        copyRegion2.dstOffset = {0, 0, 0};
        copyRegion2.extent = {params.srcWidth, params.srcHeight, 1};
        vkCmdCopyImage(cmd, stagingImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage, dstLayout, 1, &copyRegion2);
    }
}

bool CreateStagingImage(
    VkDevice device,
    VmaAllocator allocator,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImage* outImage,
    VmaAllocation* outAllocation,
    VkImageView* outImageView)
{
    if (!outImage || !outAllocation || !outImageView) return false;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = 0;
    allocInfo.preferredFlags = 0;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, outImage, outAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, outImageView) != VK_SUCCESS) {
        vmaDestroyImage(allocator, *outImage, *outAllocation);
        *outImage = VK_NULL_HANDLE;
        *outAllocation = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void DestroyStagingImage(
    VkDevice device,
    VkImage image,
    VmaAllocation allocation,
    VkImageView imageView)
{
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    if (image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(VK_NULL_HANDLE, image, allocation);
    }
}

} // namespace utils
} // namespace ddvk
