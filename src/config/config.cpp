#include "config.hpp"
#include "../module/module_manager.h"
#include "../module/module.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/combat/autorefill.h"
#include "../module/combat/velocity.h"
#include "../module/combat/reach.h"
#include "../module/combat/blink.h"
#include "../module/utility/nojumpdelay.h"
#include "../module/utility/bridgeassist.h"
#include "../module/visual/nametags.h"
#include "../module/visual/esp.h"
#include "../ui/hud/arraylist.h"
#include "../ui/hud/notifications.h"

using namespace ArrayListRenderer;

// Apply default config (used when no config file exists)
void ConfigManager::applyDefaults() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    config_["version"] = CONFIG_VERSION;
    
    // Settings
    config_["settings"]["cloudSync"] = false;
    
    // HUD
    config_["hud"]["arrayList"] = true;
    config_["hud"]["notifications"] = true;
    
    // Default module configs (all disabled with default keybinds)
}

// Apply loaded config to modules
void ConfigManager::applyConfig() {
    if (!moduleManager) return;
    
    // Apply settings
    if (config_.has("settings")) {
        syncEnabled_ = config_["settings"]["cloudSync"].as_bool(false);
    }
    
    // Apply HUD settings
    if (config_.has("hud")) {
        enabled = config_["hud"]["arrayList"].as_bool(true);
        NotificationManager::enabled = config_["hud"]["notifications"].as_bool(true);
    }
    
    // Apply module configs
    if (!config_.has("modules")) return;
    
    const auto& modules = config_["modules"].object();
    
    for (const auto& kv : modules) {
        const std::string& name = kv.first;
        const json::Value& data = kv.second;
        
        // Find module by name
        for (auto* mod : moduleManager->getModules()) {
            if (std::string(mod->name) != name) continue;
            
            // Apply base settings
            mod->keybind = data["keybind"].as_int(0);
            bool shouldEnable = data["enabled"].as_bool(false);
            if (shouldEnable != mod->enabled && mod->isAccessible()) {
                mod->setEnabled(shouldEnable);
            }
            
            // Apply module-specific settings
            if (name == "AutoClicker" && autoClicker) {
                auto& s = autoClicker->settings;
                s.minCPS = data["minCps"].as_float(s.minCPS);
                s.maxCPS = data["maxCps"].as_float(s.maxCPS);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
                s.breakBlocks = data["breakBlocks"].as_bool(s.breakBlocks);
                s.workInInventory = data["workInInventory"].as_bool(s.workInInventory);
                s.stableCPS = data["stableCps"].as_bool(s.stableCPS);
                s.advancedMode = data["advancedMode"].as_bool(s.advancedMode);
                s.dropChance = data["dropChance"].as_float(s.dropChance);
                s.spikeChance = data["spikeChance"].as_float(s.spikeChance);
                s.attackingOnly = data["attackingOnly"].as_bool(s.attackingOnly);
            }
            else if (name == "AimAssist" && aimAssist) {
                auto& s = aimAssist->settings;
                s.speed = data["speed"].as_float(s.speed);
                s.fov = data["fov"].as_int(s.fov);
                s.distance = data["distance"].as_float(s.distance);
                s.targetMode = static_cast<AimAssist::TargetMode>(data["targetMode"].as_int(static_cast<int>(s.targetMode)));
                s.horizontalOnly = data["horizontalOnly"].as_bool(s.horizontalOnly);
                s.breakBlocks = data["breakBlocks"].as_bool(s.breakBlocks);
                s.aimAtClosestPoint = data["aimAtClosestPoint"].as_bool(s.aimAtClosestPoint);
                s.alwaysAim = data["alwaysAim"].as_bool(s.alwaysAim);
                s.invisibles = data["invisibles"].as_bool(s.invisibles);
            }
            else if (name == "Velocity" && velocity) {
                auto& s = velocity->settings;
                s.mode = static_cast<Velocity::Mode>(data["mode"].as_int(static_cast<int>(s.mode)));
                s.horizontal = data["horizontal"].as_float(s.horizontal);
                s.vertical = data["vertical"].as_float(s.vertical);
                s.chance = data["chance"].as_float(s.chance);
            }
            else if (name == "NameTags" && nameTags) {
                auto& s = nameTags->settings;
                s.range = data["range"].as_float(s.range);
                s.scale = data["scale"].as_float(s.scale);
                s.background = data["background"].as_bool(s.background);
                s.showHealth = data["showHealth"].as_bool(s.showHealth);
                s.showDistance = data["showDistance"].as_bool(s.showDistance);
            }
            else if (name == "ESP" && esp) {
                auto& s = esp->settings;
                s.box = data["box"].as_bool(s.box);
                s.box3D = data["box3D"].as_bool(s.box3D);
                s.filled = data["filled"].as_bool(s.filled);
                s.outline = data["outline"].as_bool(s.outline);
                s.healthBar = data["healthBar"].as_bool(s.healthBar);
                s.range = data["range"].as_float(s.range);
            }
            else if (name == "Bridge Assist" && bridgeAssist) {
                auto& s = bridgeAssist->settings;
                s.onlyLookingDown = data["onlyLookingDown"].as_bool(s.onlyLookingDown);
                s.onlyWhenSneaking = data["onlyWhenSneaking"].as_bool(s.onlyWhenSneaking);
            }
            
            break;
        }
    }
}

// Build config from current module state
void ConfigManager::buildConfigFromModules() {
    if (!moduleManager) return;
    
    std::lock_guard<std::mutex> lock(configMutex_);
    
    config_["version"] = CONFIG_VERSION;
        
    // Settings
    config_["settings"]["cloudSync"] = syncEnabled_;
    
    // HUD
    config_["hud"]["arrayList"] = enabled;
    config_["hud"]["notifications"] = NotificationManager::enabled;
    
    // Build modules config
    for (auto* mod : moduleManager->getModules()) {
        std::string name = mod->name;
        
        config_["modules"][name]["enabled"] = mod->enabled;
        config_["modules"][name]["keybind"] = mod->keybind;
        
        // Module-specific settings
        if (name == "AutoClicker" && autoClicker) {
            const auto& s = autoClicker->settings;
            config_["modules"][name]["minCps"] = s.minCPS;
            config_["modules"][name]["maxCps"] = s.maxCPS;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
            config_["modules"][name]["breakBlocks"] = s.breakBlocks;
            config_["modules"][name]["workInInventory"] = s.workInInventory;
            config_["modules"][name]["stableCps"] = s.stableCPS;
            config_["modules"][name]["advancedMode"] = s.advancedMode;
            config_["modules"][name]["dropChance"] = s.dropChance;
            config_["modules"][name]["spikeChance"] = s.spikeChance;
            config_["modules"][name]["attackingOnly"] = s.attackingOnly;
        }
        else if (name == "AimAssist" && aimAssist) {
            const auto& s = aimAssist->settings;
            config_["modules"][name]["speed"] = s.speed;
            config_["modules"][name]["fov"] = s.fov;
            config_["modules"][name]["distance"] = s.distance;
            config_["modules"][name]["targetMode"] = static_cast<int>(s.targetMode);
            config_["modules"][name]["horizontalOnly"] = s.horizontalOnly;
            config_["modules"][name]["breakBlocks"] = s.breakBlocks;
            config_["modules"][name]["aimAtClosestPoint"] = s.aimAtClosestPoint;
            config_["modules"][name]["alwaysAim"] = s.alwaysAim;
            config_["modules"][name]["invisibles"] = s.invisibles;
        }
        else if (name == "Velocity" && velocity) {
            const auto& s = velocity->settings;
            config_["modules"][name]["mode"] = static_cast<int>(s.mode);
            config_["modules"][name]["horizontal"] = s.horizontal;
            config_["modules"][name]["vertical"] = s.vertical;
            config_["modules"][name]["chance"] = s.chance;
        }
        else if (name == "NameTags" && nameTags) {
            const auto& s = nameTags->settings;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["scale"] = s.scale;
            config_["modules"][name]["background"] = s.background;
            config_["modules"][name]["showHealth"] = s.showHealth;
            config_["modules"][name]["showDistance"] = s.showDistance;
        }
        else if (name == "ESP" && esp) {
            const auto& s = esp->settings;
            config_["modules"][name]["box"] = s.box;
            config_["modules"][name]["box3D"] = s.box3D;
            config_["modules"][name]["filled"] = s.filled;
            config_["modules"][name]["outline"] = s.outline;
            config_["modules"][name]["healthBar"] = s.healthBar;
            config_["modules"][name]["range"] = s.range;
        }
        else if (name == "Bridge Assist" && bridgeAssist) {
            const auto& s = bridgeAssist->settings;
            config_["modules"][name]["onlyLookingDown"] = s.onlyLookingDown;
            config_["modules"][name]["onlyWhenSneaking"] = s.onlyWhenSneaking;
        }
    }
}

// Migrate config from older version
void ConfigManager::migrateConfig(int fromVersion) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // Add migrations here as needed
    // if (fromVersion < 2) { ... }
    
    config_["version"] = CONFIG_VERSION;
}
