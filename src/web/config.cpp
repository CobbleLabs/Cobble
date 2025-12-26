#include "config.h"
#include "config_server.h"
#include "../util/logger.hpp"
#include <sstream>

// Simple JSON value extraction - no external deps
namespace {
	std::string getValue(const std::string& json, const std::string& key) {
		std::string search = "\"" + key + "\":";
		size_t pos = json.find(search);
		if (pos == std::string::npos) return "";
		
		pos += search.length();
		while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
		
		if (json[pos] == '"') {
			// String value
			pos++;
			size_t end = json.find('"', pos);
			return json.substr(pos, end - pos);
		} else {
			// Number or boolean
			size_t end = json.find_first_of(",}", pos);
			return json.substr(pos, end - pos);
		}
	}

	float getFloat(const std::string& json, const std::string& key, float def = 0.0f) {
		std::string val = getValue(json, key);
		if (val.empty()) return def;
		return std::stof(val);
	}

	bool getBool(const std::string& json, const std::string& key, bool def = false) {
		std::string val = getValue(json, key);
		if (val.empty()) return def;
		return val == "true";
	}

	// Convert key string to virtual key code
	int keyStringToVK(const std::string& key) {
		if (key.empty()) return 0;
		if (key.length() == 1) {
			char c = key[0];
			if (c >= 'A' && c <= 'Z') return c; // A-Z
			if (c >= 'a' && c <= 'z') return c - 32; // a-z -> A-Z
			if (c >= '0' && c <= '9') return c; // 0-9
		}
		// Special keys
		if (key == "SHIFT") return 0x10;
		if (key == "CONTROL" || key == "CTRL") return 0x11;
		if (key == "ALT") return 0x12;
		if (key == "SPACE") return 0x20;
		if (key == "TAB") return 0x09;
		if (key == "CAPSLOCK") return 0x14;
		return 0x52; // Default to R
	}

	// Convert virtual key code to key string
	std::string vkToKeyString(int vk) {
		if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
		if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
		switch (vk) {
			case 0x10: return "SHIFT";
			case 0x11: return "CTRL";
			case 0x12: return "ALT";
			case 0x20: return "SPACE";
			case 0x09: return "TAB";
			case 0x14: return "CAPSLOCK";
			default: return "R";
		}
	}
}

void Config::apply(const std::string& json) {
	log_debug("Config received: " + json.substr(0, 100) + "...");
	
	// Check message type
	std::string type = getValue(json, "type");
	
	if (type == "config") {
		// Apply AutoClicker settings
		autoClicker.settings.minCPS = getFloat(json, "minCPS", 10.0f);
		autoClicker.settings.maxCPS = getFloat(json, "maxCPS", 14.0f);
		autoClicker.settings.weaponOnly = getBool(json, "weaponOnly");
		autoClicker.settings.ignoreBlocks = getBool(json, "ignoreBlocks", true);
		autoClicker.settings.workInInventory = getBool(json, "workInInventory");
		autoClicker.settings.advancedMode = getBool(json, "advancedMode", true);
		autoClicker.settings.dropChance = getFloat(json, "dropChance", 5.0f);
		autoClicker.settings.spikeChance = getFloat(json, "spikeChance", 10.0f);
		autoClicker.settings.kurtosis = getFloat(json, "kurtosis", 2.0f);
		
		// Keybind
		std::string keybindStr = getValue(json, "keybind");
		if (!keybindStr.empty()) {
			autoClicker.keybind = keyStringToVK(keybindStr);
		}
		
		bool enabled = getBool(json, "enabled");
		autoClicker.setEnabled(enabled);
		
		log_ok("Config applied: CPS=" + std::to_string((int)autoClicker.settings.minCPS) + 
			   "-" + std::to_string((int)autoClicker.settings.maxCPS) + 
			   ", key=" + vkToKeyString(autoClicker.keybind) +
			   ", enabled=" + (enabled ? "true" : "false"));
	}
	else if (type == "getConfig") {
		// Send current config to client
		log_debug("Sending current config to client");
		configServer.sendMessage(serialize());
	}
	else {
		log_err("Unknown config type: " + type);
	}
}

std::string Config::serialize() {
	std::ostringstream ss;
	ss << "{";
	ss << "\"type\":\"config\",";
	ss << "\"enabled\":" << (autoClicker.enabled ? "true" : "false") << ",";
	ss << "\"keybind\":\"" << vkToKeyString(autoClicker.keybind) << "\",";
	ss << "\"minCPS\":" << autoClicker.settings.minCPS << ",";
	ss << "\"maxCPS\":" << autoClicker.settings.maxCPS << ",";
	ss << "\"weaponOnly\":" << (autoClicker.settings.weaponOnly ? "true" : "false") << ",";
	ss << "\"ignoreBlocks\":" << (autoClicker.settings.ignoreBlocks ? "true" : "false") << ",";
	ss << "\"workInInventory\":" << (autoClicker.settings.workInInventory ? "true" : "false") << ",";
	ss << "\"advancedMode\":" << (autoClicker.settings.advancedMode ? "true" : "false") << ",";
	ss << "\"dropChance\":" << autoClicker.settings.dropChance << ",";
	ss << "\"spikeChance\":" << autoClicker.settings.spikeChance << ",";
	ss << "\"kurtosis\":" << autoClicker.settings.kurtosis;
	ss << "}";
	return ss.str();
}
