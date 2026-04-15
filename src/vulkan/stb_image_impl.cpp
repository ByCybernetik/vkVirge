// Определяем макрос для реализации stb_image перед включением заголовка
#define STB_IMAGE_IMPLEMENTATION

// Отключаем предупреждения для stb_image
#ifdef _MSC_VER
    #pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wsign-compare"
#endif

// Включаем stb_image
#include "../../third_party/stb/stb_image.h"

// Восстанавливаем предупреждения
#ifdef _MSC_VER
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

// Эта функция не нужна, но оставим для проверки
// Можно добавить небольшую функцию для проверки, что stb_image работает
bool stb_image_available() {
    return true;
}