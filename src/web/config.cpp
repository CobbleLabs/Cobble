#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "config.h"
#include "config_server.h"
#include "../util/logger.hpp"
#include <sstream>
#include <iomanip>

namespace {
	std::string getValue(const std::string& json, const std::string& key) {
		std::string search = "\"" + key + "\":";
		size_t pos = json.find(search);
		if (pos == std::string::npos) return "";
		pos += search.length();
		while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
		if (json[pos] == '"') {
			pos++;
			size_t end = json.find('"', pos);
			return json.substr(pos, end - pos);
		} else {
			size_t end = json.find_first_of(",}", pos);
			return json.substr(pos, end - pos);
		}
	}

	float getFloat(const std::string& json, const std::string& key, float def = 0.0f) {
		std::string val = getValue(json, key);
		return val.empty() ? def : std::stof(val);
	}

	bool getBool(const std::string& json, const std::string& key, bool def = false) {
		std::string val = getValue(json, key);
		return val.empty() ? def : val == "true";
	}

	int keyStringToVK(const std::string& key) {
		if (key.empty()) return 0;
		if (key.length() == 1) {
			char c = key[0];
			if (c >= 'A' && c <= 'Z') return c;
			if (c >= 'a' && c <= 'z') return c - 32;
			if (c >= '0' && c <= '9') return c;
		}
		if (key == "SHIFT") return 0x10;
		if (key == "CTRL") return 0x11;
		if (key == "ALT") return 0x12;
		if (key == "SPACE") return 0x20;
		return 0x52;
	}

	std::string vkToKeyString(int vk) {
		if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
		if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
		switch (vk) {
			case 0x10: return "SHIFT";
			case 0x11: return "CTRL";
			case 0x12: return "ALT";
			case 0x20: return "SPACE";
			default: return "R";
		}
	}
}

void Config::apply(const std::string& json) {
	std::string type = getValue(json, "type");
	
	if (type == "config") {
		std::string module = getValue(json, "module");
		
		if (module == "aimassist") {
			aimAssist.settings.horizontalSpeed = getFloat(json, "horizontalSpeed", 25.0f);
			aimAssist.settings.verticalSpeed = getFloat(json, "verticalSpeed", 12.0f);
			aimAssist.settings.maxFov = getFloat(json, "maxFov", 180.0f);
			aimAssist.settings.maxDistance = getFloat(json, "maxDistance", 4.5f);
			aimAssist.settings.smoothing = getFloat(json, "smoothing", 0.15f);
			aimAssist.settings.randomization = getFloat(json, "randomization", 0.15f);
			aimAssist.settings.onlyClicking = getBool(json, "onlyClicking", true);
			aimAssist.settings.onlySprinting = getBool(json, "onlySprinting", false);
			aimAssist.settings.verticalAim = getBool(json, "verticalAim", true);
			aimAssist.settings.visibilityCheck = getBool(json, "visibilityCheck", true);
			aimAssist.settings.ignoreInvisible = getBool(json, "ignoreInvisible", true);
			aimAssist.settings.targetLock = getBool(json, "targetLock", true);
			aimAssist.settings.hurtTimeBoost = getBool(json, "hurtTimeBoost", true);
			aimAssist.settings.multipoint = getBool(json, "multipoint", false);
			aimAssist.settings.hitboxY = getFloat(json, "hitboxY", 0.5f);
			
			std::string kb = getValue(json, "keybind");
			if (!kb.empty()) aimAssist.keybind = keyStringToVK(kb);
			
			bool was = aimAssist.enabled;
			bool now = getBool(json, "enabled");
			aimAssist.setEnabled(now);
			if (now != was) log_ok(now ? "AimAssist enabled" : "AimAssist disabled");
		}
		else if (module == "nametags") {
			nameTags.settings.range = getFloat(json, "range", 64.0f);
			nameTags.settings.scale = getFloat(json, "scale", 1.0f);
			nameTags.settings.distanceScale = getBool(json, "distanceScale", false);
			nameTags.settings.background = getBool(json, "background", true);
			nameTags.settings.showHealth = getBool(json, "showHealth", true);
			nameTags.settings.showDistance = getBool(json, "showDistance", true);
			
			std::string kb = getValue(json, "keybind");
			if (!kb.empty()) nameTags.keybind = keyStringToVK(kb);
			
			bool was = nameTags.enabled;
			bool now = getBool(json, "enabled");
			nameTags.setEnabled(now);
			if (now != was) log_ok(now ? "NameTags enabled" : "NameTags disabled");
		}
		else {
			autoClicker.settings.minCPS = getFloat(json, "minCPS", 10.0f);
			autoClicker.settings.maxCPS = getFloat(json, "maxCPS", 14.0f);
			autoClicker.settings.weaponOnly = getBool(json, "weaponOnly");
			autoClicker.settings.ignoreBlocks = getBool(json, "ignoreBlocks", true);
			autoClicker.settings.workInInventory = getBool(json, "workInInventory");
			autoClicker.settings.advancedMode = getBool(json, "advancedMode", true);
			autoClicker.settings.dropChance = getFloat(json, "dropChance", 5.0f);
			autoClicker.settings.spikeChance = getFloat(json, "spikeChance", 10.0f);
			autoClicker.settings.kurtosis = getFloat(json, "kurtosis", 2.0f);
			
			std::string kb = getValue(json, "keybind");
			if (!kb.empty()) autoClicker.keybind = keyStringToVK(kb);
			
			bool was = autoClicker.enabled;
			bool now = getBool(json, "enabled");
			autoClicker.setEnabled(now);
			if (now != was) log_ok(now ? "AutoClicker enabled" : "AutoClicker disabled");
		}
	}
	else if (type == "getConfig") {
		configServer.sendMessage(serialize());
	}
}

std::string Config::serialize() {
	std::ostringstream ss;
	ss << std::fixed << std::setprecision(2);
	
	ss << "{\"type\":\"configAll\",";
	
	// AutoClicker
	ss << "\"autoclicker\":{";
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
	ss << "\"kurtosis\":" << autoClicker.settings.kurtosis << "},";
	
	// AimAssist
	ss << "\"aimassist\":{";
	ss << "\"enabled\":" << (aimAssist.enabled ? "true" : "false") << ",";
	ss << "\"keybind\":\"" << vkToKeyString(aimAssist.keybind) << "\",";
	ss << "\"horizontalSpeed\":" << aimAssist.settings.horizontalSpeed << ",";
	ss << "\"verticalSpeed\":" << aimAssist.settings.verticalSpeed << ",";
	ss << "\"maxFov\":" << aimAssist.settings.maxFov << ",";
	ss << "\"maxDistance\":" << aimAssist.settings.maxDistance << ",";
	ss << "\"smoothing\":" << aimAssist.settings.smoothing << ",";
	ss << "\"randomization\":" << aimAssist.settings.randomization << ",";
	ss << "\"onlyClicking\":" << (aimAssist.settings.onlyClicking ? "true" : "false") << ",";
	ss << "\"onlySprinting\":" << (aimAssist.settings.onlySprinting ? "true" : "false") << ",";
	ss << "\"verticalAim\":" << (aimAssist.settings.verticalAim ? "true" : "false") << ",";
	ss << "\"visibilityCheck\":" << (aimAssist.settings.visibilityCheck ? "true" : "false") << ",";
	ss << "\"ignoreInvisible\":" << (aimAssist.settings.ignoreInvisible ? "true" : "false") << ",";
	ss << "\"targetLock\":" << (aimAssist.settings.targetLock ? "true" : "false") << ",";
	ss << "\"hurtTimeBoost\":" << (aimAssist.settings.hurtTimeBoost ? "true" : "false") << ",";
	ss << "\"multipoint\":" << (aimAssist.settings.multipoint ? "true" : "false") << ",";
	ss << "\"hitboxY\":" << aimAssist.settings.hitboxY << "},";
	
	// NameTags
	ss << "\"nametags\":{";
	ss << "\"enabled\":" << (nameTags.enabled ? "true" : "false") << ",";
	ss << "\"keybind\":\"" << vkToKeyString(nameTags.keybind) << "\",";
	ss << "\"range\":" << nameTags.settings.range << ",";
	ss << "\"scale\":" << nameTags.settings.scale << ",";
	ss << "\"distanceScale\":" << (nameTags.settings.distanceScale ? "true" : "false") << ",";
	ss << "\"background\":" << (nameTags.settings.background ? "true" : "false") << ",";
	ss << "\"showHealth\":" << (nameTags.settings.showHealth ? "true" : "false") << ",";
	ss << "\"showDistance\":" << (nameTags.settings.showDistance ? "true" : "false") << "}}";
	
	return ss.str();
}
