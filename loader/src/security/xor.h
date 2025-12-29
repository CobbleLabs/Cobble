#pragma once
#include <array>
#include <cstddef>
#include <utility>

// XorString - Compile time string encryption
// ------------------------------------------

template <size_t N, int K>
struct XorString {
private:
    const char _key;
    std::array<char, N + 1> _encrypted;

    constexpr char enc(char c) const {
        return c ^ _key ^ K;
    }

    char dec(char c) const {
        return c ^ _key ^ K;
    }

public:
    template <size_t... Is>
    constexpr __forceinline XorString(const char* str, std::index_sequence<Is...>)
        : _key((char)(N % 255 + 1)), _encrypted{ enc(str[Is])... } {}

    __forceinline const char* decrypt() {
        for (size_t i = 0; i < N; ++i) {
            _encrypted[i] = dec(_encrypted[i]);
        }
        _encrypted[N] = '\0';
        return _encrypted.data();
    }
};

// Macro helper to deduce size and generate indices
#define _xor_(str) (XorString<sizeof(str) - 1, __COUNTER__>(str, std::make_index_sequence<sizeof(str) - 1>()).decrypt())
