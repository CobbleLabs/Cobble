#pragma once

#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/visual/nametags.h"
#include "../sdk/render_data.h"
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
