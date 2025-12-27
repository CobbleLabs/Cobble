#pragma once

#include "../module/autoclicker.h"
#include "../module/aimassist.h"
#include "../module/nametags.h"
#include <string>

// Simple JSON-like config parsing and applying
// No external JSON library - just basic string parsing for now
namespace Config
{
	// Parse incoming config message and apply to modules
	void apply(const std::string& json);

	// Generate current config as JSON for sending to client
	std::string serialize();
}
