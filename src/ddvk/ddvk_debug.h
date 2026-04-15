#pragma once

#include <windows.h>
#include <cstdio>
#include <cstdint>

namespace ddvk {
namespace debug {

    // Безопасная проверка на чтение с использованием SEH
    inline bool IsBadReadPtrSafe(const void* ptr, size_t size) {
        if (!ptr) return true;
        
        // Проверяем, что ptr не равен маленьким значениям (часто используемым в ошибках)
        uintptr_t uptr = reinterpret_cast<uintptr_t>(ptr);
        if (uptr < 0x10000) {  // Меньше 64KB - скорее всего невалидный указатель
            return true;
        }
        
        __try {
            // Пытаемся прочитать первый байт
            volatile char test = *(volatile char*)ptr;
            (void)test;
            
            // Если size > 1, проверяем последний байт
            if (size > 1) {
                volatile char testLast = *(volatile char*)((char*)ptr + size - 1);
                (void)testLast;
            }
            return false;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return true;
        }
    }

    // Проверка vtbl на валидность
    inline bool IsVtblValid(void* obj) {
        if (!obj) return false;
        
        uintptr_t uobj = reinterpret_cast<uintptr_t>(obj);
        if (uobj < 0x10000) return false;
        
        __try {
            void** vtbl = *(void***)obj;
            if (!vtbl) return false;
            
            uintptr_t uvtbl = reinterpret_cast<uintptr_t>(vtbl);
            if (uvtbl < 0x10000) return false;
            
            // Проверяем первые несколько методов (обычно QueryInterface, AddRef, Release)
            for (int i = 0; i < 3; i++) {
                void* method = vtbl[i];
                if (!method) return false;
                
                uintptr_t umethod = reinterpret_cast<uintptr_t>(method);
                if (umethod < 0x10000) return false;
            }
            
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Логирование контекста ошибки
    inline void LogCrashContext(const char* function, void* obj, void* vtbl) {
        FILE* f = fopen("ddvk_crash.log", "a");
        if (f) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            
            fprintf(f, "[%02d:%02d:%02d.%03d] CRASH in %s: obj=%p, vtbl=%p\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    function ? function : "unknown", obj, vtbl);
            
            // Дополнительная информация
            if (obj) {
                uintptr_t uobj = reinterpret_cast<uintptr_t>(obj);
                fprintf(f, "  obj value: 0x%p (%llu)\n", obj, (unsigned long long)uobj);
                
                __try {
                    void** vtblPtr = *(void***)obj;
                    fprintf(f, "  vtbl pointer from obj: %p\n", vtblPtr);
                    
                    if (vtblPtr && (uintptr_t)vtblPtr >= 0x10000) {
                        for (int i = 0; i < 4; i++) {
                            void* method = vtblPtr[i];
                            fprintf(f, "  method[%d]: %p\n", i, method);
                        }
                    }
                }
                __except(EXCEPTION_EXECUTE_HANDLER) {
                    fprintf(f, "  Cannot read vtbl from obj (exception)\n");
                }
            }
            
            fflush(f);
            fclose(f);
        }
        
        // Также пишем в отладочный вывод
        char buffer[512];
        sprintf(buffer, "[DDVK] CRASH in %s: obj=%p, vtbl=%p\n", 
                function ? function : "unknown", obj, vtbl);
        OutputDebugStringA(buffer);
    }

    // Дамп памяти вокруг указателя (для отладки)
    inline void DumpMemory(const void* ptr, size_t size, const char* label = nullptr) {
        FILE* f = fopen("ddvk_memdump.log", "a");
        if (!f) return;
        
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        if (label) {
            fprintf(f, "\n[%02d:%02d:%02d.%03d] Memory dump: %s at %p, %zu bytes\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    label, ptr, size);
        } else {
            fprintf(f, "\n[%02d:%02d:%02d.%03d] Memory dump at %p, %zu bytes\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    ptr, size);
        }
        
        if (!ptr || IsBadReadPtrSafe(ptr, size)) {
            fprintf(f, "  (invalid pointer or cannot read)\n");
            fclose(f);
            return;
        }
        
        const uint8_t* bytes = (const uint8_t*)ptr;
        for (size_t i = 0; i < size; i += 16) {
            fprintf(f, "  %08zx: ", i);
            for (size_t j = 0; j < 16 && i + j < size; j++) {
                fprintf(f, "%02x ", bytes[i + j]);
            }
            fprintf(f, "\n");
        }
        
        fflush(f);
        fclose(f);
    }

    // Получение имени функции по адресу (упрощенно)
    inline void LogCallstack(const char* label = nullptr) {
        FILE* f = fopen("ddvk_callstack.log", "a");
        if (!f) return;
        
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        if (label) {
            fprintf(f, "\n[%02d:%02d:%02d.%03d] Call stack: %s\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    label);
        } else {
            fprintf(f, "\n[%02d:%02d:%02d.%03d] Call stack:\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        }
        
        // Простой дамп стека (x86)
        #ifdef _M_IX86
        __asm {
            mov eax, ebp
            mov ecx, 10  // максимум 10 кадров
        loop_start:
            cmp ecx, 0
            jz loop_end
            mov eax, [eax]  // предыдущий ebp
            test eax, eax
            jz loop_end
            mov edx, [eax + 4]  // адрес возврата
            push ecx
            push edx
            push offset format_str
            call fprintf
            add esp, 12
            pop ecx
            dec ecx
            jmp loop_start
        loop_end:
        }
        #endif
        
        fflush(f);
        fclose(f);
    }

} // namespace debug
} // namespace ddvk