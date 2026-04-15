project('ddraw-vulkan-wrapper', 'cpp',
    version: '0.1.0',
    default_options: [
        'cpp_std=c++17',
        'buildtype=release',
        'b_staticpic=false',
        'warning_level=0'
    ]
)

#=============================================================================
# Глобальные определения
#=============================================================================

# Добавляем определение макроса для Win32 поверхности
add_project_arguments(
    '-DVK_USE_PLATFORM_WIN32_KHR=1',
    '-D_WIN32_WINNT=0x0601',
    '-DVMA_DEBUG_LOG=',
    '-DVMA_DEBUG_LOG_FORMAT=',
    '-DVMA_DEBUG_LOG2=',
    '-DVMA_RECORDING_ENABLED=0',
    '-DUNICODE',
    '-D_UNICODE',
    language: 'cpp'
)

#=============================================================================
# Пути к include директориям
#=============================================================================

# Vulkan и связанные библиотеки
vulkan_headers = include_directories('third_party/Vulkan-Headers/include')
vma_headers = include_directories('third_party/vma/include')
stb_headers = include_directories('third_party/stb')
glm_headers = include_directories('third_party/glm')

# DirectX 7 SDK для оригинальных заголовков
dx7_headers = include_directories('third_party/dx7sdk/include')

# Hack модули
hack_module_headers = include_directories('src/hack')

# Utils модули (масштабирование и др.)
utils_module_headers = include_directories('src/utils')

# Наши модули
vulkan_module_headers = include_directories('src/vulkan')
ddraw_module_headers = include_directories('src/ddraw')
ddvk_module_headers = include_directories('src/ddvk')

# Объединяем все include директории
all_headers = [
    vulkan_headers,
    vma_headers,
    stb_headers,
    glm_headers,
    dx7_headers,
    hack_module_headers,
    utils_module_headers,
    vulkan_module_headers,
    ddraw_module_headers,
    ddvk_module_headers
]

#=============================================================================
# Исходные файлы модулей Vulkan
#=============================================================================

vulkan_sources = files(
    'src/vulkan/vk_utils_core.cpp',
    'src/vulkan/vk_window.cpp',
    'src/vulkan/vk_memory.cpp',
    'src/vulkan/vk_shaders.cpp',
    'src/vulkan/vk_pipeline.cpp',
    'src/vulkan/vk_swapchain.cpp',
    'src/vulkan/vk_sync.cpp',
    'src/vulkan/vk_images.cpp',
    'src/vulkan/vma_impl.cpp',
    'src/vulkan/stb_image_impl.cpp'
)

#=============================================================================
# Исходные файлы враппера DirectDraw
#=============================================================================

ddraw_wrapper_sources = files(
    # Основные файлы враппера
    'src/ddraw/ddraw.cpp',
    'src/ddraw/ddraw_guids.cpp',
    'src/ddraw/ddraw_surface.cpp',
    'src/ddraw/ddraw_palette.cpp',
    'src/ddraw/ddraw_clipper.cpp',
    'src/ddraw/ddraw_utils.cpp'
)

#=============================================================================
# Исходные файлы трансляции DDraw → Vulkan
#=============================================================================

ddvk_sources = files(
    # Базовые классы
    'src/ddvk/ddvk_core.cpp',
    'src/ddvk/ddvk_utils.cpp',

    # Управление ресурсами
    'src/ddvk/ddvk_renderer.cpp',
    'src/ddvk/ddvk_surface.cpp',
    'src/ddvk/ddvk_texture.cpp',
    'src/ddvk/ddvk_buffer.cpp',
    'src/ddvk/ddvk_swapchain.cpp',

    # Графический конвейер
    'src/ddvk/ddvk_shaders.cpp',
    'src/ddvk/ddvk_pipeline.cpp',
    'src/ddvk/ddvk_descriptors.cpp',

    # Синхронизация и команды
    'src/ddvk/ddvk_sync.cpp',
    'src/ddvk/ddvk_commands.cpp',

    # Эмуляция DirectDraw операций
    'src/ddvk/ddvk_flip.cpp',
    'src/ddvk/ddvk_blt.cpp'
)

#=============================================================================
# Исходные файлы хакoв для игр
#=============================================================================

hack_sources = files(
    'src/hack/hack.cpp'
)

#=============================================================================
# Исходные файлы утилит (масштабирование)
#=============================================================================

utils_sources = files(
    'src/utils/scaler.cpp'
)

#=============================================================================
# Общие аргументы компиляции и линковки
#=============================================================================

common_cpp_args = [
    '-m32',
    '-fexceptions',
    '-fno-rtti',
    '-ffunction-sections',
    '-fdata-sections',
    '-static-libgcc',
    '-static-libstdc++',
    '-Wno-unused-variable',
    '-Wno-unused-parameter',
    '-Wno-attributes',
    '-Wno-missing-field-initializers',
    '-Wno-sign-compare',
    '-pthread'
]

common_link_args = [
    '-m32',
    '-static',
    '-static-libgcc',
    '-static-libstdc++',
    '-Wl,-Bstatic',
    '-Wl,--gc-sections',
    '-Wl,--strip-all',
    '-L' + meson.current_source_dir() + '/third_party/vk1',
    '-Wl,--whole-archive',
    '-lvulkan-1',
    '-Wl,--no-whole-archive',
    '-lwinpthread',
    '-luser32',
    '-lgdi32',
    '-lkernel32',
    '-lwinmm',
    '-lshell32',
    '-ladvapi32',
    '-luuid',
    '-lole32',
    '-loleaut32'
]

#=============================================================================
# Цель сборки: только библиотека ddraw.dll (DirectDraw через Vulkan)
#=============================================================================

ddraw_dll = shared_library('ddraw',
    [ddraw_wrapper_sources, ddvk_sources, vulkan_sources, hack_sources, utils_sources, 'src/main/dllmain.cpp'],
    include_directories: all_headers,
    cpp_args: common_cpp_args + ['-DDDRAW_VULKAN_WRAPPER'],
    link_args: common_link_args,
    vs_module_defs: 'src/main/ddraw.def',
    install: true
)

#=============================================================================
# Установка заголовочных файлов
#=============================================================================

install_headers(
    'src/ddraw/ddraw.h',
    'src/ddraw/ddraw_surface.h',
    'src/ddraw/ddraw_palette.h',
    'src/ddraw/ddraw_clipper.h',
    'src/ddraw/ddraw_utils.h',
    subdir: 'ddraw'
)

install_headers(
    'src/ddvk/ddvk_core.h',
    'src/ddvk/ddvk_renderer.h',
    'src/ddvk/ddvk_surface.h',
    'src/ddvk/ddvk_texture.h',
    'src/ddvk/ddvk_buffer.h',
    'src/ddvk/ddvk_swapchain.h',
    'src/ddvk/ddvk_shaders.h',
    'src/ddvk/ddvk_pipeline.h',
    'src/ddvk/ddvk_descriptors.h',
    'src/ddvk/ddvk_sync.h',
    'src/ddvk/ddvk_commands.h',
    'src/ddvk/ddvk_flip.h',
    'src/ddvk/ddvk_blt.h',
    'src/ddvk/ddvk_utils.h',
    subdir: 'ddvk'
)

install_headers(
    'src/hack/hack.h',
    subdir: 'hack'
)

install_headers(
    'src/utils/scaler.h',
    subdir: 'utils'
)