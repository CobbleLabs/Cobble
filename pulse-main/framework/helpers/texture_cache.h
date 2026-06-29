#pragma once

#include "imgui.h"

struct ID3D11Device;

namespace texture_cache
{
    void init(ID3D11Device* device);
    void shutdown();
    void invalidate(const char* path);

    ImTextureID get(const char* path);
    ImVec2 get_size(const char* path);
}
