#pragma once
#include <cstdint>
#include <utility>

namespace obf {

// Generador de estados aleatorios en compile-time
constexpr uint32_t state_seed(const char* str, uint32_t offset = 0) {
    return str[offset] == '\0' ? 0x5A5A5A5A : str[offset] + 0x9E3779B9 + state_seed(str, offset + 1);
}

// Macro para obfuscar bloques de código crítico
#define OBFUSCATE_BEGIN                                                                     \
    {                                                                                       \
        volatile int __obf_state = (obf::state_seed(__FILE__ __FUNCTION__) ^ __LINE__) % 5; \
        while (true) {                                                                      \
            switch (__obf_state) {
#define OBFUSCATE_CASE(n)          \
    case n:                        \
        __obf_state = (n + 1) % 5; \
        break;

#define OBFUSCATE_CASE_EXEC(n, code) \
    case n: {                        \
        code;                        \
        __obf_state = (n + 1) % 5;   \
    } break;

#define OBFUSCATE_CASE_FINAL(n, code) \
    case n: {                         \
        code;                         \
        goto __obf_exit;              \
    } break;

#define OBFUSCATE_END       \
    default:                \
        __asm { int 3 }       \
        ; /* Anti-tamper */ \
        }                   \
        }                   \
    __obf_exit:;            \
        }

// Template para dispatcher opaco (inline obfuscation)
template <auto Func, uint32_t Seed = state_seed(__FILE__)>
struct OpaqueDispatcher {
    template <typename... Args>
    static inline auto call(Args&&... args) {
        // State machine con estados pseudo-aleatorios
        volatile int state = (Seed ^ 0x5A5A5A5A) % 7;

        for (int i = 0; i < 10; i++) {
            switch (state) {
                case 0:
                    state = 4;
                    break;
                case 1:
                    state = 5;
                    break;
                case 2:
                    return Func(std::forward<Args>(args)...);
                case 3:
                    state = 1;
                    break;
                case 4:
                    state = 3;
                    break;
                case 5:
                    state = 2;
                    break;
                case 6:
                    state = (Seed & 1) ? 3 : 0;
                    break;
                default:
                    __debugbreak();  // Anti-tamper
            }
        }

        // Fallback (nunca debería alcanzarse)
        return Func(std::forward<Args>(args)...);
    }
};

// Macro para llamadas ofuscadas
#define OBFUSCATED_CALL(func, ...) obf::OpaqueDispatcher<&func>::call(__VA_ARGS__)

// Junk code injection (genera instrucciones basura)
#define JUNK_CODE_1               \
    {                             \
        volatile int __junk = 0;  \
        __junk = __junk * 2 + 1;  \
        if (__junk == 0xDEADBEEF) \
            __debugbreak();       \
    }

#define JUNK_CODE_2                           \
    {                                         \
        volatile uint64_t __junk = __rdtsc(); \
        __junk ^= 0x5A5A5A5A5A5A5A5A;         \
        if (__junk == 0)                      \
            __debugbreak();                   \
    }

}  // namespace obf
