#include "ddvk_utils.h"
#include "ddvk_renderer.h"
#include "ddvk_commands.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <algorithm>

namespace ddvk {

//=============================================================================
// VulkanUtils - реализация
//=============================================================================

VkFormat VulkanUtils::DDrawToVulkanFormat(const DDPIXELFORMAT& format) {
    if (format.dwFlags & DDPF_RGB) {
        switch (format.dwRGBBitCount) {
            case 16:
                if (format.dwFlags & DDPF_ALPHAPIXELS) {
                    return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
                } else {
                    return VK_FORMAT_R5G6B5_UNORM_PACK16;
                }
            case 24:
                return VK_FORMAT_R8G8B8_UNORM;
            case 32:
                if (format.dwFlags & DDPF_ALPHAPIXELS) {
                    return VK_FORMAT_R8G8B8A8_UNORM;
                } else {
                    return VK_FORMAT_B8G8R8A8_UNORM;
                }
        }
    } else if (format.dwFlags & DDPF_FOURCC) {
        // Обработка FOURCC форматов
        switch (format.dwFourCC) {
            case MAKEFOURCC('D', 'X', 'T', '1'):
                return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            case MAKEFOURCC('D', 'X', 'T', '3'):
                return VK_FORMAT_BC2_UNORM_BLOCK;
            case MAKEFOURCC('D', 'X', 'T', '5'):
                return VK_FORMAT_BC3_UNORM_BLOCK;
        }
    }
    
    return VK_FORMAT_UNDEFINED;
}

uint32_t VulkanUtils::VulkanToDDrawFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return 16;
        case VK_FORMAT_R8G8B8_UNORM:
            return 24;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return 32;
        default:
            return 0;
    }
}

bool VulkanUtils::IsDeviceLocal(VmaAllocation allocation) {
    if (!allocation) return false;
    
    // В VMA нет прямой функции vmaGetAllocator, используем другой подход
    // Для простоты считаем, что выделение в device local памяти
    // В реальном коде нужно использовать VmaAllocationInfo
    return true;
}

size_t VulkanUtils::GetAllocationSize(VmaAllocation allocation) {
    if (!allocation) return 0;
    
    // Упрощенная реализация
    return 0;
}

void VulkanUtils::PrintVkResult(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        printf("%s: %s\n", message, VkResultToString(result));
    }
}

const char* VulkanUtils::VkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        default: return "Unknown VkResult";
    }
}

std::vector<uint32_t> VulkanUtils::LoadSPIRVFromFile(const std::string& filename) {
    std::vector<uint32_t> spirv;
    
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return spirv;
    }
    
    size_t fileSize = (size_t)file.tellg();
    file.seekg(0);
    
    spirv.resize(fileSize / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    
    file.close();
    
    return spirv;
}

bool VulkanUtils::CreateImageWithData(VulkanRenderer* renderer, 
                                       VkImage* image, 
                                       VmaAllocation* allocation,
                                       uint32_t width, uint32_t height,
                                       VkFormat format,
                                       const void* data, size_t dataSize) {
    if (!renderer || !data) return false;
    
    VkDevice device = renderer->GetDevice();
    VmaAllocator allocator = renderer->GetAllocator();
    
    // Создаем изображение
    VkImageCreateInfo imageInfo = {};
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
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, image, allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    
    // Создаем staging buffer и загружаем данные
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    
    if (vmaCreateBuffer(allocator, &bufferInfo, &stagingAllocInfo, 
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        vmaDestroyImage(allocator, *image, *allocation);
        return false;
    }
    
    // Копируем данные в staging buffer
    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, data, dataSize);
    vmaUnmapMemory(allocator, stagingAllocation);
    
    // Копируем из staging buffer в изображение
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(renderer);
    
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
    
    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(commandBuffer,
                           stagingBuffer,
                           *image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copyRegion);
    
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
    
    EndSingleTimeCommands(renderer, commandBuffer);
    
    // Очищаем staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    
    return true;
}

VkCommandBuffer VulkanUtils::BeginSingleTimeCommands(VulkanRenderer* renderer) {
    VkDevice device = renderer->GetDevice();
    VkCommandPool commandPool = renderer->GetCommandPool();
    
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void VulkanUtils::EndSingleTimeCommands(VulkanRenderer* renderer, VkCommandBuffer commandBuffer) {
    VkDevice device = renderer->GetDevice();
    VkQueue graphicsQueue = renderer->GetGraphicsQueue();
    VkCommandPool commandPool = renderer->GetCommandPool();
    
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

//=============================================================================
// PerformanceTimer - реализация
//=============================================================================

PerformanceTimer::PerformanceTimer() : m_running(false) {
    QueryPerformanceFrequency(&m_frequency);
    Reset();
}

PerformanceTimer::~PerformanceTimer() {
}

void PerformanceTimer::Start() {
    QueryPerformanceCounter(&m_startTime);
    m_running = true;
}

void PerformanceTimer::Stop() {
    if (m_running) {
        QueryPerformanceCounter(&m_stopTime);
        m_running = false;
    }
}

void PerformanceTimer::Reset() {
    m_startTime.QuadPart = 0;
    m_stopTime.QuadPart = 0;
    m_running = false;
}

double PerformanceTimer::GetElapsedMilliseconds() const {
    LARGE_INTEGER endTime;
    
    if (m_running) {
        QueryPerformanceCounter(&endTime);
    } else {
        endTime = m_stopTime;
    }
    
    return static_cast<double>(endTime.QuadPart - m_startTime.QuadPart) * 1000.0 / m_frequency.QuadPart;
}

double PerformanceTimer::GetElapsedMicroseconds() const {
    return GetElapsedMilliseconds() * 1000.0;
}

double PerformanceTimer::GetFPS() const {
    double ms = GetElapsedMilliseconds();
    return (ms > 0) ? (1000.0 / ms) : 0.0;
}

//=============================================================================
// Logger - реализация
//=============================================================================

FILE* Logger::s_logFile = nullptr;
LogLevel Logger::s_logLevel = LOG_INFO;
std::mutex Logger::s_mutex;

void Logger::Initialize(const char* filename) {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    if (filename) {
#ifdef _MSC_VER
        fopen_s(&s_logFile, filename, "w");
#else
        s_logFile = fopen(filename, "w");
#endif
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    if (s_logFile) {
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void Logger::LogV(LogLevel level, const char* format, va_list args) {
    if (level < s_logLevel) return;

    std::lock_guard<std::mutex> lock(s_mutex);

    const char* levelStr = "";
    switch (level) {
        case LOG_DEBUG:   levelStr = "DEBUG"; break;
        case LOG_INFO:    levelStr = "INFO"; break;
        case LOG_WARNING: levelStr = "WARNING"; break;
        case LOG_ERROR:   levelStr = "ERROR"; break;
    }

    printf("[%s] ", levelStr);
    va_list argsCopy;
    va_copy(argsCopy, args);
    vprintf(format, argsCopy);
    va_end(argsCopy);
    printf("\n");

    if (s_logFile) {
        fprintf(s_logFile, "[%s] ", levelStr);
        va_copy(argsCopy, args);
        vfprintf(s_logFile, format, argsCopy);
        va_end(argsCopy);
        fprintf(s_logFile, "\n");
        fflush(s_logFile);
    }
}

void Logger::Log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}

void Logger::Debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(LOG_DEBUG, format, args);
    va_end(args);
}

void Logger::Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(LOG_INFO, format, args);
    va_end(args);
}

void Logger::Warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(LOG_WARNING, format, args);
    va_end(args);
}

void Logger::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(LOG_ERROR, format, args);
    va_end(args);
}

void Logger::SetLogLevel(LogLevel level) {
    s_logLevel = level;
}

void DebugSessionLog(const char* location, const char* message, void* obj, void* vtbl, const char* hypothesisId, const char* runId) {
    (void)location;
    (void)message;
    (void)obj;
    (void)vtbl;
    (void)hypothesisId;
    (void)runId;
}

void DebugSessionLogData(const char* location, const char* message, const char* dataJson, const char* hypothesisId, const char* runId) {
    (void)location;
    (void)message;
    (void)dataJson;
    (void)hypothesisId;
    (void)runId;
}

void DebugProbeLogData(const char* location, const char* message, const char* dataJson, const char* hypothesisId) {
    const char* path = "/home/cybernetik/Other/Sources/CPP/ddvk/.cursor/debug-777f2f.log";
    FILE* f = fopen(path, "a");
    if (!f) return;
    unsigned long ts = (unsigned long)time(nullptr);
    fprintf(f, "{\"sessionId\":\"777f2f\",\"location\":\"%s\",\"message\":\"%s\",\"data\":%s",
            location ? location : "", message ? message : "", dataJson && dataJson[0] ? dataJson : "{}");
    if (hypothesisId && hypothesisId[0]) fprintf(f, ",\"hypothesisId\":\"%s\"", hypothesisId);
    fprintf(f, ",\"timestamp\":%lu000}\n", ts);
    fclose(f);
}

} // namespace ddvk