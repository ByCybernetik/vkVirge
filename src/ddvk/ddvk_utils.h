#pragma once

#include "ddvk_core.h"
#include <cstdarg>
#include <string>
#include <vector>

namespace ddvk {

//=============================================================================
// Вспомогательные функции для работы с Vulkan
//=============================================================================

class VulkanUtils {
public:
    // Преобразование форматов
    static VkFormat DDrawToVulkanFormat(const DDPIXELFORMAT& format);
    static uint32_t VulkanToDDrawFormat(VkFormat format);
    
    // Работа с памятью
    static bool IsDeviceLocal(VmaAllocation allocation);
    static size_t GetAllocationSize(VmaAllocation allocation);
    
    // Отладка
    static void PrintVkResult(VkResult result, const char* message);
    static const char* VkResultToString(VkResult result);
    
    // Загрузка шейдеров из файлов
    static std::vector<uint32_t> LoadSPIRVFromFile(const std::string& filename);
    
    // Создание изображения с данными
    static bool CreateImageWithData(VulkanRenderer* renderer, 
                                     VkImage* image, 
                                     VmaAllocation* allocation,
                                     uint32_t width, uint32_t height,
                                     VkFormat format,
                                     const void* data, size_t dataSize);
    
    // Создание командного буфера одноразового использования
    static VkCommandBuffer BeginSingleTimeCommands(VulkanRenderer* renderer);
    static void EndSingleTimeCommands(VulkanRenderer* renderer, VkCommandBuffer commandBuffer);
};

//=============================================================================
// Таймер для измерения производительности
//=============================================================================

class PerformanceTimer {
public:
    PerformanceTimer();
    ~PerformanceTimer();
    
    void Start();
    void Stop();
    void Reset();
    
    double GetElapsedMilliseconds() const;
    double GetElapsedMicroseconds() const;
    double GetFPS() const;
    
private:
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_startTime;
    LARGE_INTEGER m_stopTime;
    bool m_running;
};

//=============================================================================
// Логгер
//=============================================================================

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

class Logger {
public:
    static void Initialize(const char* filename = nullptr);
    static void Shutdown();

    /** Internal: log with va_list (used by Info/Error/... to forward variadic args). */
    static void LogV(LogLevel level, const char* format, va_list args);

    static void Log(LogLevel level, const char* format, ...);
    static void Debug(const char* format, ...);
    static void Info(const char* format, ...);
    static void Warning(const char* format, ...);
    static void Error(const char* format, ...);
    
    static void SetLogLevel(LogLevel level);
    
private:
    static FILE* s_logFile;
    static LogLevel s_logLevel;
    static std::mutex s_mutex;
};

void DebugSessionLog(const char* location, const char* message, void* obj, void* vtbl, const char* hypothesisId = nullptr, const char* runId = nullptr);
void DebugSessionLogData(const char* location, const char* message, const char* dataJson, const char* hypothesisId = nullptr, const char* runId = nullptr);
void DebugProbeLogData(const char* location, const char* message, const char* dataJson, const char* hypothesisId = nullptr);

} // namespace ddvk