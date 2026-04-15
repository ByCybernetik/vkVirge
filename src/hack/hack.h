#pragma once

#include <windows.h>
#include <string>

namespace ddvk {
namespace hack {

// Проверка, является ли процесс Westwood игрой
bool IsWestwoodProcess();

// Проверка, использует ли игра BGR565 формат (Dune 2000, Tiberian Dawn)
// В отличие от RGB565 (Tiberian Sun, Red Alert 2)
bool IsBGR565Game();

// Вспомогательные функции для работы с путями
bool EndsWithIgnoreCase(const std::string& text, const char* suffix);
bool PathContainsIgnoreCase(std::string text, const char* needle);

} // namespace hack
} // namespace ddvk
