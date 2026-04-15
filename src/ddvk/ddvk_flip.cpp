#include "ddvk_flip.h"
#include "ddvk_renderer.h"
#include <algorithm>

namespace ddvk {

//=============================================================================
// FlipManager - реализация
//=============================================================================

FlipManager::FlipManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_frontBuffer(nullptr)
    , m_backBuffer(nullptr)
    , m_flipPending(false) {
}

FlipManager::~FlipManager() {
    m_flipChain.clear();
}

void FlipManager::SetFlipChain(SurfaceImpl* front, SurfaceImpl* back) {
    m_frontBuffer = front;
    m_backBuffer = back;
    
    if (front && back) {
        m_flipChain.clear();
        m_flipChain.push_back(front);
        m_flipChain.push_back(back);
        
        // Устанавливаем связи
        front->frontBuffer = nullptr;
        front->backBuffer = back;
        back->frontBuffer = front;
        back->backBuffer = nullptr;
    }
}

void FlipManager::AddFlipSurface(SurfaceImpl* surface) {
    if (!surface) return;
    
    // Проверяем, не в цепочке ли уже
    if (std::find(m_flipChain.begin(), m_flipChain.end(), surface) != m_flipChain.end()) {
        return;
    }
    
    if (!m_flipChain.empty()) {
        // Связываем с последним в цепочке
        SurfaceImpl* last = m_flipChain.back();
        last->backBuffer = surface;
        surface->frontBuffer = last;
        surface->backBuffer = nullptr;
    }
    
    m_flipChain.push_back(surface);
}

void FlipManager::RemoveFlipSurface(SurfaceImpl* surface) {
    auto it = std::find(m_flipChain.begin(), m_flipChain.end(), surface);
    if (it != m_flipChain.end()) {
        // Пересвязываем соседей
        if (surface->frontBuffer) {
            surface->frontBuffer->backBuffer = surface->backBuffer;
        }
        if (surface->backBuffer) {
            surface->backBuffer->frontBuffer = surface->frontBuffer;
        }
        
        m_flipChain.erase(it);
    }
}

HRESULT FlipManager::Flip(SurfaceImpl* target, DWORD flags) {
    if (!m_frontBuffer || !m_backBuffer) {
        return DDERR_NOFLIPHW;
    }
    
    // Проверяем, не занята ли цепочка
    if (m_flipPending && !(flags & DDFLIP_WAIT)) {
        return DDERR_WASSTILLDRAWING;
    }
    
    // Ждем если нужно
    if (flags & DDFLIP_WAIT) {
        WaitForFlip();
    }
    
    HRESULT result = DD_OK;
    
    // Выбираем метод флипа
    if (target) {
        // Флип на конкретную поверхность
        result = FlipWithSwap(target);
    } else {
        // Стандартный флип на следующий в цепочке
        result = FlipWithSwap(m_backBuffer);
    }
    
    if (result == DD_OK) {
        m_flipPending = true;
        
        // Если не ждать, то сбрасываем флаг сразу
        if (!(flags & DDFLIP_WAIT)) {
            m_flipPending = false;
        }
    }
    
    return result;
}

bool FlipManager::IsFlipPending() const {
    return m_flipPending;
}

void FlipManager::WaitForFlip() {
    // В реальном коде здесь нужно ждать вертикальной синхронизации
    m_renderer->WaitForVerticalBlank();
    m_flipPending = false;
}

SurfaceImpl* FlipManager::GetNextSurface() const {
    if (m_flipChain.size() < 2) return nullptr;
    
    // Находим текущий front buffer
    for (size_t i = 0; i < m_flipChain.size(); i++) {
        if (m_flipChain[i] == m_frontBuffer) {
            // Следующий в цепочке
            size_t next = (i + 1) % m_flipChain.size();
            return m_flipChain[next];
        }
    }
    
    return nullptr;
}

HRESULT FlipManager::FlipWithSwap(SurfaceImpl* target) {
    if (!target) return DDERR_INVALIDPARAMS;
    
    // Меняем местами front и target
    SurfaceImpl* newFront = target;
    SurfaceImpl* newBack = m_frontBuffer;
    
    // Обновляем связи
    if (newFront) {
        newFront->isPrimary = true;
        newFront->isBackBuffer = false;
    }
    if (newBack) {
        newBack->isPrimary = false;
        newBack->isBackBuffer = true;
    }
    
    // Меняем указатели
    m_frontBuffer = newFront;
    m_backBuffer = newBack;
    
    return DD_OK;
}

HRESULT FlipManager::FlipWithCopy(SurfaceImpl* target) {
    if (!target || !m_frontBuffer || !m_renderer) return DDERR_INVALIDPARAMS;

    return m_renderer->ExecuteUnderSurfaceLock([&]() -> HRESULT {
        if (!m_renderer->IsValidSurface(target) || !m_renderer->IsValidSurface(m_frontBuffer))
            return DDERR_INVALIDOBJECT;
        if (target->mappedData && m_renderer->IsDeferredBuffer(target->mappedData))
            return DDERR_SURFACELOST;
        if (m_frontBuffer->mappedData && m_renderer->IsDeferredBuffer(m_frontBuffer->mappedData))
            return DDERR_SURFACELOST;
        if (target->mappedData && m_frontBuffer->mappedData) {
            size_t size = std::min(target->dataSize, m_frontBuffer->dataSize);
            memcpy(m_frontBuffer->mappedData, target->mappedData, size);
            if (m_frontBuffer->texture) {
                m_frontBuffer->texture->UpdateFromData(m_frontBuffer->mappedData,
                                                        m_frontBuffer->dataSize);
            }
        }
        return DD_OK;
    });
}

HRESULT FlipManager::FlipWithTexture(SurfaceImpl* target) {
    if (!target || !m_frontBuffer) return DDERR_INVALIDPARAMS;
    
    // Меняем текстуры местами
    std::swap(m_frontBuffer->texture, target->texture);
    
    return DD_OK;
}

} // namespace ddvk