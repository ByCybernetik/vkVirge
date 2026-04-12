cmake_minimum_required(VERSION 3.10)
project(VulkanTriangle)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск пакетов
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

# Добавление исполняемого файла
add_executable(triangle main.cpp)

# Подключение заголовочных файлов и библиотек
target_include_directories(triangle PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(triangle PRIVATE Vulkan::Vulkan glfw)
