#include "ddvk_shaders.h"
#include "ddvk_renderer.h"
#include <vector>
#include <cstdint>

namespace ddvk {

//=============================================================================
// Встроенные SPIR-V шейдеры (упрощенные для демонстрации)
// В реальном коде здесь должны быть скомпилированные SPIR-V бинарники
//=============================================================================

// Вершинный шейдер для blit (простой прямоугольник)
static const uint32_t g_blitVertSPIRV[] = {
    0x07230203, 0x00010000, 0x0008000a, 0x0000002c, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030003,
    0x00000001, 0x00000136, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
    0x00050005, 0x00000009, 0x63786574, 0x64726f6f, 0x00000000, 0x00060005,
    0x0000000b, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00040047,
    0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000b, 0x0000000b,
    0x0000001f, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x00040020,
    0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x00000009,
    0x00000001, 0x00040020, 0x0000000c, 0x00000003, 0x00000007, 0x0004003b,
    0x0000000c, 0x0000000b, 0x00000003, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007,
    0x0000000d, 0x00000009, 0x00050041, 0x00000008, 0x0000000e, 0x0000000b,
    0x00000000, 0x0003003e, 0x0000000e, 0x0000000d, 0x000100fd, 0x00010038
};

// Фрагментный шейдер для blit
static const uint32_t g_blitFragSPIRV[] = {
    0x07230203, 0x00010000, 0x0008000a, 0x00000014, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000001, 0x00000136, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x63786574,
    0x64726f6f, 0x00000000, 0x00050005, 0x0000000d, 0x63786574, 0x64726f6f,
    0x00000000, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000d, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x00040020, 0x0000000a, 0x00000000, 0x00000007, 0x0004003b,
    0x0000000a, 0x00000009, 0x00000000, 0x0004003b, 0x00000008, 0x0000000d,
    0x00000003, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000b, 0x00000009,
    0x0003003e, 0x0000000d, 0x0000000b, 0x000100fd, 0x00010038
};

//=============================================================================
// ShaderManager - реализация
//=============================================================================

ShaderManager::ShaderManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_blitVert(VK_NULL_HANDLE)
    , m_blitFrag(VK_NULL_HANDLE)
    , m_fillVert(VK_NULL_HANDLE)
    , m_fillFrag(VK_NULL_HANDLE)
    , m_clearVert(VK_NULL_HANDLE)
    , m_clearFrag(VK_NULL_HANDLE) {
}

ShaderManager::~ShaderManager() {
    VkDevice device = m_renderer->GetDevice();
    
    DestroyShaderModule(m_blitVert);
    DestroyShaderModule(m_blitFrag);
    DestroyShaderModule(m_fillVert);
    DestroyShaderModule(m_fillFrag);
    DestroyShaderModule(m_clearVert);
    DestroyShaderModule(m_clearFrag);
}

bool ShaderManager::LoadShader(const std::string& name, 
                               const std::vector<uint32_t>& spirv,
                               VkShaderStageFlagBits stage) {
    VkShaderModule module = CreateShaderModule(spirv);
    if (!module) return false;
    
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        if (name == "blit") m_blitVert = module;
        else if (name == "fill") m_fillVert = module;
        else if (name == "clear") m_clearVert = module;
        else return false;
    } else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        if (name == "blit") m_blitFrag = module;
        else if (name == "fill") m_fillFrag = module;
        else if (name == "clear") m_clearFrag = module;
        else return false;
    } else {
        return false;
    }
    
    return true;
}

VkShaderModule ShaderManager::GetBlitVertexShader() {
    if (!m_blitVert) {
        std::vector<uint32_t> spirv(GetBlitVertSPIRV());
        m_blitVert = CreateShaderModule(spirv);
    }
    return m_blitVert;
}

VkShaderModule ShaderManager::GetBlitFragmentShader() {
    if (!m_blitFrag) {
        std::vector<uint32_t> spirv(GetBlitFragSPIRV());
        m_blitFrag = CreateShaderModule(spirv);
    }
    return m_blitFrag;
}

VkShaderModule ShaderManager::GetFillVertexShader() {
    // Используем тот же вершинный шейдер для fill
    return GetBlitVertexShader();
}

VkShaderModule ShaderManager::GetFillFragmentShader() {
    if (!m_fillFrag) {
        // Для fill используем тот же фрагментный шейдер
        std::vector<uint32_t> spirv(GetBlitFragSPIRV());
        m_fillFrag = CreateShaderModule(spirv);
    }
    return m_fillFrag;
}

VkShaderModule ShaderManager::GetClearVertexShader() {
    return GetBlitVertexShader();
}

VkShaderModule ShaderManager::GetClearFragmentShader() {
    if (!m_clearFrag) {
        std::vector<uint32_t> spirv(GetBlitFragSPIRV());
        m_clearFrag = CreateShaderModule(spirv);
    }
    return m_clearFrag;
}

VkShaderModule ShaderManager::CreateShaderModule(const std::vector<uint32_t>& spirv) {
    VkDevice device = m_renderer->GetDevice();
    
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    
    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return module;
}

void ShaderManager::DestroyShaderModule(VkShaderModule module) {
    if (module && m_renderer) {
        VkDevice device = m_renderer->GetDevice();
        vkDestroyShaderModule(device, module, nullptr);
    }
}

std::vector<uint32_t> ShaderManager::GetBlitVertSPIRV() {
    size_t size = sizeof(g_blitVertSPIRV) / sizeof(uint32_t);
    return std::vector<uint32_t>(g_blitVertSPIRV, g_blitVertSPIRV + size);
}

std::vector<uint32_t> ShaderManager::GetBlitFragSPIRV() {
    size_t size = sizeof(g_blitFragSPIRV) / sizeof(uint32_t);
    return std::vector<uint32_t>(g_blitFragSPIRV, g_blitFragSPIRV + size);
}

std::vector<uint32_t> ShaderManager::GetFillVertSPIRV() {
    return GetBlitVertSPIRV();
}

std::vector<uint32_t> ShaderManager::GetFillFragSPIRV() {
    return GetBlitFragSPIRV();
}

std::vector<uint32_t> ShaderManager::GetClearVertSPIRV() {
    return GetBlitVertSPIRV();
}

std::vector<uint32_t> ShaderManager::GetClearFragSPIRV() {
    return GetBlitFragSPIRV();
}

} // namespace ddvk