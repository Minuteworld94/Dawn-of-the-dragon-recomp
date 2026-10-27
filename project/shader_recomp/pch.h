#pragma once

// [Spyro] SPYRO_SHADER_IN_GAME: the game compiles shader_recompiler.cpp itself to translate shaders
// missing from the cache at runtime (WORK_LOG 121); it needs none of the tool-only libraries.
#ifndef SPYRO_SHADER_IN_GAME
#ifdef _WIN32
#include <Windows.h>
#endif

#include <dxcapi.h>
#include <execution>
#include <smolv.h>
#include <xxhash.h>
#include <zstd.h>
#endif

#include <bit>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <map>
#include <fmt/core.h>
#include <fmt/format.h>
#include <string>
#include <unordered_map>

template<typename T>
static T byteSwap(T value)
{
    if constexpr (sizeof(T) == 1)
        return value;
    else if constexpr (sizeof(T) == 2)
        return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
    else if constexpr (sizeof(T) == 4)
        return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
    else if constexpr (sizeof(T) == 8) 
        return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));

    assert(false && "Unexpected byte size.");
    return value;
}

template<typename T>
struct be
{
    T value;

    T get() const
    {
        if constexpr (std::is_enum_v<T>)
            return T(byteSwap(std::underlying_type_t<T>(value)));
        else
            return byteSwap(value);
    }

    operator T() const
    {
        return get();
    }
};  
