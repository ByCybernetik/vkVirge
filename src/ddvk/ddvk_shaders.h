#pragma once

#include "ddvk_core.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace ddvk {

//=============================================================================
// Управление шейдерами
//=============================================================================

class ShaderManager {
public:
    ShaderManager(VulkanRenderer* renderer);
    ~ShaderManager();
    
    // Загрузка шейдеров
    bool LoadShader(const std::string& name, 
                    const std::vector<uint32_t>& spirv,
                    VkShaderStageFlagBits stage);
    
    // Получение шейдеров для разных операций
    VkShaderModule GetBlitVertexShader();
    VkShaderModule GetBlitFragmentShader();
    VkShaderModule GetFillVertexShader();
    VkShaderModule GetFillFragmentShader();
    VkShaderModule GetClearVertexShader();
    VkShaderModule GetClearFragmentShader();
    
    // Создание шейдерных модулей из SPIR-V
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
    void DestroyShaderModule(VkShaderModule module);
    
private:
    VulkanRenderer* m_renderer;
    
    // Шейдерные модули
    VkShaderModule m_blitVert;
    VkShaderModule m_blitFrag;
    VkShaderModule m_fillVert;
    VkShaderModule m_fillFrag;
    VkShaderModule m_clearVert;
    VkShaderModule m_clearFrag;
    
    // Встроенные шейдеры (SPIR-V)
    std::vector<uint32_t> GetBlitVertSPIRV();
    std::vector<uint32_t> GetBlitFragSPIRV();
    std::vector<uint32_t> GetFillVertSPIRV();
    std::vector<uint32_t> GetFillFragSPIRV();
    std::vector<uint32_t> GetClearVertSPIRV();
    std::vector<uint32_t> GetClearFragSPIRV();
};

} // namespace ddvk