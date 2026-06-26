#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace obf {

// Compile-time seed generator from __TIME__ (HH:MM:SS)
constexpr uint32_t compile_seed() {
    return (__TIME__[7] - '0') * 1 + (__TIME__[6] - '0') * 10 + (__TIME__[4] - '0') * 60 + (__TIME__[3] - '0') * 600 +
           (__TIME__[1] - '0') * 3600 + (__TIME__[0] - '0') * 36000;
}

// Obfuscated string container
template <size_t N>
class ObfString {
private:
    std::array<char, N> data_;

    // Encrypt character at compile-time
    static constexpr char encrypt(char c, size_t i, uint32_t seed) {
        // Linear Congruential Generator for position-dependent key
        uint32_t k = (seed + static_cast<uint32_t>(i)) * 0x41C64E6D + 0x3039;
        return c ^ static_cast<char>((k & 0xFF) ^ (i & 0xFF));
    }

public:
    // Constructor - encrypts at compile-time using index_sequence
    template <size_t... I>
    constexpr ObfString(const char* str, std::index_sequence<I...>) : data_{encrypt(str[I], I, compile_seed())...} {}

    // Decrypt on-demand (stack allocation, no heap)
    inline std::string decrypt() const {
        std::string result;
        result.reserve(N - 1);

        uint32_t seed = compile_seed();
        for (size_t i = 0; i < N - 1; i++) {
            // Same LCG formula as encrypt
            uint32_t k = (seed + static_cast<uint32_t>(i)) * 0x41C64E6D + 0x3039;
            result += data_[i] ^ static_cast<char>((k & 0xFF) ^ (i & 0xFF));
        }

        return result;
    }

    // Helper for C-string operations
    inline const char* c_str() const {
        static thread_local std::string cached;
        cached = decrypt();
        return cached.c_str();
    }
};

}  // namespace obf

// Macro for easy usage
#define OBFS(str)                                                                                     \
    [] {                                                                                              \
        constexpr auto s = obf::ObfString<sizeof(str)>(str, std::make_index_sequence<sizeof(str)>{}); \
        return s.decrypt();                                                                           \
    }()

// Macro for C-string usage (careful - uses thread_local cache)
#define OBFS_CSTR(str)                                                                                       \
    [] {                                                                                                     \
        constexpr static auto s = obf::ObfString<sizeof(str)>(str, std::make_index_sequence<sizeof(str)>{}); \
        return s.c_str();                                                                                    \
    }()
