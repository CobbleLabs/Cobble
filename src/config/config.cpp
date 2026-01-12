#include "config.hpp"
#include "../module/module_manager.h"
#include "../module/module.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/combat/autorefill.h"
#include "../module/combat/velocity.h"
#include "../module/combat/reach.h"
#include "../module/combat/blink.h"
#include "../module/combat/criticals.h"
#include "../module/utility/nojumpdelay.h"
#include "../module/utility/bridgeassist.h"
#include "../module/utility/sprint.h"
#include "../module/utility/fastplace.h"
#include "../module/utility/noitemrelease.h"
#include "../module/visual/nametags.h"
#include "../module/visual/esp.h"
#include "../module/visual/blockesp.h"
#include "../module/visual/tracers.h"
#include "../module/visual/trajectories.h"
#include "../ui/hud/arraylist.h"
#include "../ui/hud/notifications.h"
#include "../ui/menu/menu.h"
#include "../util/team/friends_manager.h"

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
        Menu::menuKey = config_["settings"]["menuKey"].as_int(0x2D); // VK_INSERT
    }
    
    // Apply HUD settings
    if (config_.has("hud")) {
        enabled = config_["hud"]["arrayList"].as_bool(true);
        rainbow = config_["hud"]["arrayListRainbow"].as_bool(true);
        colorBar = config_["hud"]["arrayListColorBar"].as_bool(true);
        showBackground = config_["hud"]["arrayListBackground"].as_bool(true);
        showInfo = config_["hud"]["arrayListShowInfo"].as_bool(true);
        sortByLength = config_["hud"]["arrayListSortByLength"].as_bool(true);
        scale = config_["hud"]["arrayListScale"].as_float(1.0f);
        rainbowSaturation = config_["hud"]["arrayListSaturation"].as_float(0.7f);
        anchor = config_["hud"]["arrayListAnchor"].as_int(0);
        NotificationManager::enabled = config_["hud"]["notifications"].as_bool(true);
    }
    
    // Apply friends settings
    if (config_.has("friends") && friendsManager) {
        // Load friends list (comma-separated)
        if (config_["friends"].has("list")) {
            friendsManager->clearFriends();
            std::string friendsList = config_["friends"]["list"].as_string("");
            if (!friendsList.empty()) {
                size_t start = 0, end;
                while ((end = friendsList.find(',', start)) != std::string::npos) {
                    std::string name = friendsList.substr(start, end - start);
                    if (!name.empty()) friendsManager->addFriend(name);
                    start = end + 1;
                }
                std::string last = friendsList.substr(start);
                if (!last.empty()) friendsManager->addFriend(last);
            }
        }
        // Load team settings
        friendsManager->teams.matchOwnNametag = config_["friends"]["matchOwnNametag"].as_bool(true);
        friendsManager->applyToCombat = config_["friends"]["applyToCombat"].as_bool(true);
        friendsManager->applyToVisual = config_["friends"]["applyToVisual"].as_bool(false);
        // Quick-add settings
        friendsManager->quickAddEnabled = config_["friends"]["quickAddEnabled"].as_bool(true);
        friendsManager->quickAddKey = config_["friends"]["quickAddKey"].as_int(0x04); // VK_MBUTTON
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
                s.reactionTime = data["reactionTime"].as_int(s.reactionTime);
                s.targetMode = static_cast<AimAssist::TargetMode>(data["targetMode"].as_int(static_cast<int>(s.targetMode)));
                s.breakBlocks = data["breakBlocks"].as_bool(s.breakBlocks);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
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
                s.fov = data["fov"].as_float(s.fov);
                s.reduceDelay = data["reduceDelay"].as_int(s.reduceDelay);
                s.jumpChance = data["jumpChance"].as_float(s.jumpChance);
                s.allowDoubleClicks = data["allowDoubleClicks"].as_bool(s.allowDoubleClicks);
                s.verticalMode = static_cast<Velocity::VerticalMode>(data["verticalMode"].as_int(static_cast<int>(s.verticalMode)));
                s.onlyLookingAtPlayer = data["onlyLookingAtPlayer"].as_bool(s.onlyLookingAtPlayer);
                s.onlyMousePressed = data["onlyMousePressed"].as_bool(s.onlyMousePressed);
                s.onlyMovingForward = data["onlyMovingForward"].as_bool(s.onlyMovingForward);
                s.onlyHoldingWeapon = data["onlyHoldingWeapon"].as_bool(s.onlyHoldingWeapon);
                s.onlySprinting = data["onlySprinting"].as_bool(s.onlySprinting);
                s.percentageVariance = data["percentageVariance"].as_float(s.percentageVariance);
                s.tickSpread = data["tickSpread"].as_int(s.tickSpread);
                s.microDelayMs = data["microDelayMs"].as_int(s.microDelayMs);
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
                s.teamColors = data["teamColors"].as_bool(s.teamColors);
                s.range = data["range"].as_float(s.range);
                // Load color arrays
                if (data.has("friendColor")) {
                    s.friendColor[0] = data["friendColor"]["r"].as_float(s.friendColor[0]);
                    s.friendColor[1] = data["friendColor"]["g"].as_float(s.friendColor[1]);
                    s.friendColor[2] = data["friendColor"]["b"].as_float(s.friendColor[2]);
                    s.friendColor[3] = data["friendColor"]["a"].as_float(s.friendColor[3]);
                }
                if (data.has("enemyColor")) {
                    s.enemyColor[0] = data["enemyColor"]["r"].as_float(s.enemyColor[0]);
                    s.enemyColor[1] = data["enemyColor"]["g"].as_float(s.enemyColor[1]);
                    s.enemyColor[2] = data["enemyColor"]["b"].as_float(s.enemyColor[2]);
                    s.enemyColor[3] = data["enemyColor"]["a"].as_float(s.enemyColor[3]);
                }
            }
            else if (name == "Bridge Assist" && bridgeAssist) {
                auto& s = bridgeAssist->settings;
                s.onlyLookingDown = data["onlyLookingDown"].as_bool(s.onlyLookingDown);
                s.onlyWhenSneaking = data["onlyWhenSneaking"].as_bool(s.onlyWhenSneaking);
            }
            else if (name == "Reach" && reach) {
                auto& s = reach->settings;
                s.distance = data["distance"].as_float(s.distance);
                s.minDistance = data["minDistance"].as_float(s.minDistance);
                s.maxDistance = data["maxDistance"].as_float(s.maxDistance);
                s.randomize = data["randomize"].as_bool(s.randomize);
                s.weaponsOnly = data["weaponsOnly"].as_bool(s.weaponsOnly);
                s.sprintingOnly = data["sprintingOnly"].as_bool(s.sprintingOnly);
                s.movingOnly = data["movingOnly"].as_bool(s.movingOnly);
            }
            else if (name == "Blink" && blink) {
                auto& s = blink->settings;
                s.maxDuration = data["maxDuration"].as_float(s.maxDuration);
                s.disableOnAttack = data["disableOnAttack"].as_bool(s.disableOnAttack);
                s.showDebug = data["showDebug"].as_bool(s.showDebug);
            }
            else if (name == "Criticals" && criticals) {
                auto& s = criticals->settings;
                s.duration = data["duration"].as_int(s.duration);
                s.mousePressed = data["mousePressed"].as_bool(s.mousePressed);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
                s.lookingAtPlayer = data["lookingAtPlayer"].as_bool(s.lookingAtPlayer);
                s.movingOnly = data["movingOnly"].as_bool(s.movingOnly);
            }
            else if (name == "AutoRefill" && autoRefill) {
                auto& s = autoRefill->settings;
                s.refillSoup = data["refillSoup"].as_bool(s.refillSoup);
                s.refillPotions = data["refillPotions"].as_bool(s.refillPotions);
                s.speed = data["speed"].as_int(s.speed);
                s.onInventoryOpen = data["onInventoryOpen"].as_bool(s.onInventoryOpen);
            }
            else if (name == "BlockESP" && blockESP) {
                auto& s = blockESP->settings;
                s.drawDiamond = data["drawDiamond"].as_bool(s.drawDiamond);
                s.drawEmerald = data["drawEmerald"].as_bool(s.drawEmerald);
                s.drawIron = data["drawIron"].as_bool(s.drawIron);
                s.drawGold = data["drawGold"].as_bool(s.drawGold);
                s.drawLapis = data["drawLapis"].as_bool(s.drawLapis);
                s.drawRedstone = data["drawRedstone"].as_bool(s.drawRedstone);
                s.drawCoal = data["drawCoal"].as_bool(s.drawCoal);
                s.drawChests = data["drawChests"].as_bool(s.drawChests);
                s.range = data["range"].as_float(s.range);
                s.opacity = data["opacity"].as_int(s.opacity);
                s.fadeWithDistance = data["fadeWithDistance"].as_bool(s.fadeWithDistance);
            }
            else if (name == "Tracers" && tracers) {
                auto& s = tracers->settings;
                s.colorByDistance = data["colorByDistance"].as_bool(s.colorByDistance);
                s.colorByHealth = data["colorByHealth"].as_bool(s.colorByHealth);
                s.colorByTeam = data["colorByTeam"].as_bool(s.colorByTeam);
                s.thickness = data["thickness"].as_float(s.thickness);
                s.range = data["range"].as_float(s.range);
                s.lineFrom = data["lineFrom"].as_int(s.lineFrom);
            }
            else if (name == "Trajectories" && trajectories) {
                auto& s = trajectories->settings;
                s.thickness = data["thickness"].as_float(s.thickness);
                s.circleRadius = data["circleRadius"].as_float(s.circleRadius);
                s.circleThickness = data["circleThickness"].as_float(s.circleThickness);
                if (data.has("lineColor")) {
                    s.lineColor[0] = data["lineColor"]["r"].as_float(s.lineColor[0]);
                    s.lineColor[1] = data["lineColor"]["g"].as_float(s.lineColor[1]);
                    s.lineColor[2] = data["lineColor"]["b"].as_float(s.lineColor[2]);
                    s.lineColor[3] = data["lineColor"]["a"].as_float(s.lineColor[3]);
                }
                if (data.has("circleColor")) {
                    s.circleColor[0] = data["circleColor"]["r"].as_float(s.circleColor[0]);
                    s.circleColor[1] = data["circleColor"]["g"].as_float(s.circleColor[1]);
                    s.circleColor[2] = data["circleColor"]["b"].as_float(s.circleColor[2]);
                    s.circleColor[3] = data["circleColor"]["a"].as_float(s.circleColor[3]);
                }
            }
            else if (name == "NoItemRelease" && noItemRelease) {
                noItemRelease->settings.mode = static_cast<ItemReleaseMode>(data["mode"].as_int(static_cast<int>(noItemRelease->settings.mode)));
            }
            else if (name == "FastPlace" && fastPlace) {
                fastPlace->settings.blocksOnly = data["blocksOnly"].as_bool(fastPlace->settings.blocksOnly);
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
    config_["settings"]["menuKey"] = Menu::menuKey;
    
    // HUD
    config_["hud"]["arrayList"] = enabled;
    config_["hud"]["arrayListRainbow"] = rainbow;
    config_["hud"]["arrayListColorBar"] = colorBar;
    config_["hud"]["arrayListBackground"] = showBackground;
    config_["hud"]["arrayListShowInfo"] = showInfo;
    config_["hud"]["arrayListSortByLength"] = sortByLength;
    config_["hud"]["arrayListScale"] = scale;
    config_["hud"]["arrayListSaturation"] = rainbowSaturation;
    config_["hud"]["arrayListAnchor"] = anchor;
    config_["hud"]["notifications"] = NotificationManager::enabled;
    
    // Friends
    if (friendsManager) {
        auto friends = friendsManager->getFriendNames();  // Legacy: save names for config
        std::string friendsList;
        for (size_t i = 0; i < friends.size(); ++i) {
            if (i > 0) friendsList += ",";
            friendsList += friends[i];
        }
        config_["friends"]["list"] = friendsList;
        config_["friends"]["matchOwnNametag"] = friendsManager->teams.matchOwnNametag;
        config_["friends"]["applyToCombat"] = friendsManager->applyToCombat;
        config_["friends"]["applyToVisual"] = friendsManager->applyToVisual;
        // Quick-add settings
        config_["friends"]["quickAddEnabled"] = friendsManager->quickAddEnabled;
        config_["friends"]["quickAddKey"] = friendsManager->quickAddKey;
    }
    
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
            config_["modules"][name]["reactionTime"] = s.reactionTime;
            config_["modules"][name]["targetMode"] = static_cast<int>(s.targetMode);
            config_["modules"][name]["breakBlocks"] = s.breakBlocks;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
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
            config_["modules"][name]["fov"] = s.fov;
            config_["modules"][name]["reduceDelay"] = s.reduceDelay;
            config_["modules"][name]["jumpChance"] = s.jumpChance;
            config_["modules"][name]["allowDoubleClicks"] = s.allowDoubleClicks;
            config_["modules"][name]["verticalMode"] = static_cast<int>(s.verticalMode);
            config_["modules"][name]["onlyLookingAtPlayer"] = s.onlyLookingAtPlayer;
            config_["modules"][name]["onlyMousePressed"] = s.onlyMousePressed;
            config_["modules"][name]["onlyMovingForward"] = s.onlyMovingForward;
            config_["modules"][name]["onlyHoldingWeapon"] = s.onlyHoldingWeapon;
            config_["modules"][name]["onlySprinting"] = s.onlySprinting;
            config_["modules"][name]["percentageVariance"] = s.percentageVariance;
            config_["modules"][name]["tickSpread"] = s.tickSpread;
            config_["modules"][name]["microDelayMs"] = s.microDelayMs;
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
            config_["modules"][name]["teamColors"] = s.teamColors;
            config_["modules"][name]["range"] = s.range;
            // Save color arrays
            config_["modules"][name]["friendColor"]["r"] = s.friendColor[0];
            config_["modules"][name]["friendColor"]["g"] = s.friendColor[1];
            config_["modules"][name]["friendColor"]["b"] = s.friendColor[2];
            config_["modules"][name]["friendColor"]["a"] = s.friendColor[3];
            config_["modules"][name]["enemyColor"]["r"] = s.enemyColor[0];
            config_["modules"][name]["enemyColor"]["g"] = s.enemyColor[1];
            config_["modules"][name]["enemyColor"]["b"] = s.enemyColor[2];
            config_["modules"][name]["enemyColor"]["a"] = s.enemyColor[3];
        }
        else if (name == "Bridge Assist" && bridgeAssist) {
            const auto& s = bridgeAssist->settings;
            config_["modules"][name]["onlyLookingDown"] = s.onlyLookingDown;
            config_["modules"][name]["onlyWhenSneaking"] = s.onlyWhenSneaking;
        }
        else if (name == "Reach" && reach) {
            const auto& s = reach->settings;
            config_["modules"][name]["distance"] = s.distance;
            config_["modules"][name]["minDistance"] = s.minDistance;
            config_["modules"][name]["maxDistance"] = s.maxDistance;
            config_["modules"][name]["randomize"] = s.randomize;
            config_["modules"][name]["weaponsOnly"] = s.weaponsOnly;
            config_["modules"][name]["sprintingOnly"] = s.sprintingOnly;
            config_["modules"][name]["movingOnly"] = s.movingOnly;
        }
        else if (name == "Blink" && blink) {
            const auto& s = blink->settings;
            config_["modules"][name]["maxDuration"] = s.maxDuration;
            config_["modules"][name]["disableOnAttack"] = s.disableOnAttack;
            config_["modules"][name]["showDebug"] = s.showDebug;
        }
        else if (name == "Criticals" && criticals) {
            const auto& s = criticals->settings;
            config_["modules"][name]["duration"] = s.duration;
            config_["modules"][name]["mousePressed"] = s.mousePressed;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
            config_["modules"][name]["lookingAtPlayer"] = s.lookingAtPlayer;
            config_["modules"][name]["movingOnly"] = s.movingOnly;
        }
        else if (name == "AutoRefill" && autoRefill) {
            const auto& s = autoRefill->settings;
            config_["modules"][name]["refillSoup"] = s.refillSoup;
            config_["modules"][name]["refillPotions"] = s.refillPotions;
            config_["modules"][name]["speed"] = s.speed;
            config_["modules"][name]["onInventoryOpen"] = s.onInventoryOpen;
        }
        else if (name == "BlockESP" && blockESP) {
            const auto& s = blockESP->settings;
            config_["modules"][name]["drawDiamond"] = s.drawDiamond;
            config_["modules"][name]["drawEmerald"] = s.drawEmerald;
            config_["modules"][name]["drawIron"] = s.drawIron;
            config_["modules"][name]["drawGold"] = s.drawGold;
            config_["modules"][name]["drawLapis"] = s.drawLapis;
            config_["modules"][name]["drawRedstone"] = s.drawRedstone;
            config_["modules"][name]["drawCoal"] = s.drawCoal;
            config_["modules"][name]["drawChests"] = s.drawChests;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["opacity"] = s.opacity;
            config_["modules"][name]["fadeWithDistance"] = s.fadeWithDistance;
        }
        else if (name == "Tracers" && tracers) {
            const auto& s = tracers->settings;
            config_["modules"][name]["colorByDistance"] = s.colorByDistance;
            config_["modules"][name]["colorByHealth"] = s.colorByHealth;
            config_["modules"][name]["colorByTeam"] = s.colorByTeam;
            config_["modules"][name]["thickness"] = s.thickness;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["lineFrom"] = s.lineFrom;
        }
        else if (name == "Trajectories" && trajectories) {
            const auto& s = trajectories->settings;
            config_["modules"][name]["thickness"] = s.thickness;
            config_["modules"][name]["circleRadius"] = s.circleRadius;
            config_["modules"][name]["circleThickness"] = s.circleThickness;
            config_["modules"][name]["lineColor"]["r"] = s.lineColor[0];
            config_["modules"][name]["lineColor"]["g"] = s.lineColor[1];
            config_["modules"][name]["lineColor"]["b"] = s.lineColor[2];
            config_["modules"][name]["lineColor"]["a"] = s.lineColor[3];
            config_["modules"][name]["circleColor"]["r"] = s.circleColor[0];
            config_["modules"][name]["circleColor"]["g"] = s.circleColor[1];
            config_["modules"][name]["circleColor"]["b"] = s.circleColor[2];
            config_["modules"][name]["circleColor"]["a"] = s.circleColor[3];
        }
        else if (name == "NoItemRelease" && noItemRelease) {
            config_["modules"][name]["mode"] = static_cast<int>(noItemRelease->settings.mode);
        }
        else if (name == "FastPlace" && fastPlace) {
            config_["modules"][name]["blocksOnly"] = fastPlace->settings.blocksOnly;
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
