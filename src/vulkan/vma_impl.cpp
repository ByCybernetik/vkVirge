// Отключаем все отладочные логи VMA
#define VMA_DEBUG_LOG(...)
#define VMA_DEBUG_LOG_FORMAT(...)
#define VMA_DEBUG_LOG2(...)
#define VMA_RECORDING_ENABLED 0

// Теперь включаем реализацию VMA
#define VMA_IMPLEMENTATION
#include "../../third_party/vma/include/vk_mem_alloc.h"
