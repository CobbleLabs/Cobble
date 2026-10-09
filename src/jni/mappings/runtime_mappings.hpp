#pragma once

#include <span>

#include "../../sdk/version.h"

// Name tables for clients that rename every Minecraft class and member (CheatBreaker).
// Class names use slashes. Members are keyed by their declaring MCP class and can be
// looked up by either their MCP or their SRG name.
namespace mappings {

struct ClassMapping {
    const char* mcp;
    const char* runtime;
};

struct MemberMapping {
    const char* owner;
    const char* mcpName;
    const char* srgName;
    const char* desc;
    const char* runtimeName;
    const char* runtimeDesc;
};

struct RuntimeMappings {
    MinecraftVersion version;
    std::span<const ClassMapping> classes;
    std::span<const MemberMapping> members;
};

}  // namespace mappings
