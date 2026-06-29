#pragma once
#include <string>
#include <array>
#include <cstddef>
#include <immintrin.h>

namespace PulseCrypt {
    // Generador pseudo-aleatorio en tiempo de compilación basado en macros de compilador
    constexpr unsigned int RandomSeed() {
        return (__TIME__[7] - '0') * 1 +
               (__TIME__[6] - '0') * 10 +
               (__TIME__[4] - '0') * 60 +
               (__TIME__[3] - '0') * 600 +
               __LINE__ * 1337;
    }

    template <std::size_t N, int K>
    struct XorString {
        std::array<char, N> encrypted{};

        constexpr XorString(const char(&str)[N]) {
            for (std::size_t i = 0; i < N; ++i) {
                encrypted[i] = str[i] ^ static_cast<char>(K + i);
            }
        }

        std::string decrypt() const {
            std::string s;
            s.resize(N - 1);
            for (std::size_t i = 0; i < N - 1; ++i) {
                s[i] = encrypted[i] ^ static_cast<char>(K + i);
            }
            return s;
        }

        std::wstring wdecrypt() const {
            std::string s = decrypt();
            return std::wstring(s.begin(), s.end());
        }
    };
}

#define OBFUSCATE_SEED ((__LINE__ * 16777619u) ^ 0x811C9DC5u)
#define XOR(str) (PulseCrypt::XorString<sizeof(str), (__LINE__ % 250 + 1)>(str).decrypt())
#define XORW(str) (PulseCrypt::XorString<sizeof(str), (__LINE__ % 250 + 1)>(str).wdecrypt())

// Macro de Aplanamiento de Flujo y Predicado Opaco Anti-IDA Pro F5
#define OPAQUE_JUNK() \
    do { \
        volatile int _j1 = __LINE__ * 13; \
        volatile int _j2 = _j1 + 7; \
        if ((_j1 * _j1 + _j1) % 2 != 0) { \
            _j2 ^= 0xDEADBEEF; \
            __debugbreak(); \
        } \
    } while (0)
