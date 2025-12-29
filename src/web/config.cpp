#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "config.h"
#include "config_server.h"
#include "../cobble.hpp"
#include "../util/logger.hpp"
#include "../overlay/notifications.h"
#include <sstream>
#include <iomanip>
#include <thread>

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
			// Speed slider 0-100 maps to 0-150 (1.5x multiplier)
			aimAssist.settings.horizontalSpeed = getFloat(json, "horizontalSpeed", 45.0f) * 1.5f;
			aimAssist.settings.verticalSpeed = getFloat(json, "verticalSpeed", 25.0f) * 1.5f;
			aimAssist.settings.maxFov = getFloat(json, "maxFov", 360.0f);
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
		else if (module == "autorefill") {
			autoRefill.settings.refillSoup = getBool(json, "refillSoup", true);
			autoRefill.settings.refillPotions = getBool(json, "refillPotions", true);
			autoRefill.settings.useNonHealthPotions = getBool(json, "useNonHealthPotions", false);
			autoRefill.settings.simulateButton = static_cast<int>(getFloat(json, "simulateButton", 0.0f));
			autoRefill.settings.delayAfterOpen = static_cast<int>(getFloat(json, "delayAfterOpen", 50.0f));
			autoRefill.settings.delayBeforeClose = static_cast<int>(getFloat(json, "delayBeforeClose", 50.0f));
			autoRefill.settings.speed = static_cast<int>(getFloat(json, "speed", 50.0f));
			autoRefill.settings.smartSpeed = getBool(json, "smartSpeed", true);
			
			std::string kb = getValue(json, "keybind");
			if (!kb.empty()) autoRefill.keybind = keyStringToVK(kb);
			
			bool was = autoRefill.enabled;
			bool now = getBool(json, "enabled");
			autoRefill.setEnabled(now);
			if (now != was) log_ok(now ? "AutoRefill enabled" : "AutoRefill disabled");
		}
		else {
			autoClicker.settings.minCPS = getFloat(json, "minCPS", 10.0f);
			autoClicker.settings.maxCPS = getFloat(json, "maxCPS", 14.0f);
			autoClicker.settings.weaponOnly = getBool(json, "weaponOnly");
			autoClicker.settings.breakBlocks = getBool(json, "breakBlocks", true);
			autoClicker.settings.workInInventory = getBool(json, "workInInventory");
			autoClicker.settings.advancedMode = getBool(json, "advancedMode", true);
			autoClicker.settings.dropChance = getFloat(json, "dropChance", 5.0f);
			autoClicker.settings.spikeChance = getFloat(json, "spikeChance", 10.0f);
			autoClicker.settings.kurtosis = getFloat(json, "kurtosis", 2.0f);
			autoClicker.settings.attackingOnly = getBool(json, "attackingOnly");
			
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
	else if (type == "eject") {
		// Show unload notification
		NotificationManager::addSystem("Cobble unloading...");
		
		// Give notification time to show (runs async in overlay)
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
		
		// Signal main loop to stop - will trigger clean shutdown
		cobble::context::is_running = false;
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
	ss << "\"breakBlocks\":" << (autoClicker.settings.breakBlocks ? "true" : "false") << ",";
	ss << "\"workInInventory\":" << (autoClicker.settings.workInInventory ? "true" : "false") << ",";
	ss << "\"advancedMode\":" << (autoClicker.settings.advancedMode ? "true" : "false") << ",";
	ss << "\"dropChance\":" << autoClicker.settings.dropChance << ",";
	ss << "\"spikeChance\":" << autoClicker.settings.spikeChance << ",";
	ss << "\"kurtosis\":" << autoClicker.settings.kurtosis << ",";
	ss << "\"attackingOnly\":" << (autoClicker.settings.attackingOnly ? "true" : "false") << "},";
	
	// AimAssist
	ss << "\"aimassist\":{";
	ss << "\"enabled\":" << (aimAssist.enabled ? "true" : "false") << ",";
	ss << "\"keybind\":\"" << vkToKeyString(aimAssist.keybind) << "\",";
	// Divide by 1.5 to show slider value (100 on slider = 150 actual)
	ss << "\"horizontalSpeed\":" << (aimAssist.settings.horizontalSpeed / 1.5f) << ",";
	ss << "\"verticalSpeed\":" << (aimAssist.settings.verticalSpeed / 1.5f) << ",";
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
	ss << "\"showDistance\":" << (nameTags.settings.showDistance ? "true" : "false") << "},";
	
	// AutoRefill
	ss << "\"autorefill\":{";
	ss << "\"enabled\":" << (autoRefill.enabled ? "true" : "false") << ",";
	ss << "\"keybind\":\"" << vkToKeyString(autoRefill.keybind) << "\",";
	ss << "\"refillSoup\":" << (autoRefill.settings.refillSoup ? "true" : "false") << ",";
	ss << "\"refillPotions\":" << (autoRefill.settings.refillPotions ? "true" : "false") << ",";
	ss << "\"useNonHealthPotions\":" << (autoRefill.settings.useNonHealthPotions ? "true" : "false") << ",";
	ss << "\"simulateButton\":" << autoRefill.settings.simulateButton << ",";
	ss << "\"delayAfterOpen\":" << autoRefill.settings.delayAfterOpen << ",";
	ss << "\"delayBeforeClose\":" << autoRefill.settings.delayBeforeClose << ",";
	ss << "\"speed\":" << autoRefill.settings.speed << ",";
	ss << "\"smartSpeed\":" << (autoRefill.settings.smartSpeed ? "true" : "false") << "}}";
	
	return ss.str();
}
