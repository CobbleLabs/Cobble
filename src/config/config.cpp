#include "config.hpp"
#include "../module/combat/aimassist.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/backtrack.h"
#include "../module/combat/hitselect.h"
#include "../module/combat/nohitdelay.h"
#include "../module/combat/criticals.h"
#include "../module/combat/piercing.h"
#include "../module/combat/wtap.h"

#include "../module/combat/velocity.h"
#include "../module/combat/reach.h"
#include "../module/module.h"
#include "../module/module_manager.h"
#include "../module/movement/nojumpdelay.h"
#include "../module/movement/no_slow_down.h"
#include "../module/movement/timer.h"
#include "../module/movement/timerrange.h"

#include "../module/movement/blink.h"
#include "../module/movement/fakelag.h"
#include "../module/utility/armorswitch.h"
#include "../module/utility/autorefill.h"
#include "../module/utility/autotool.h"
#include "../module/utility/breaker.h"
#include "../module/utility/bridgeassist.h"
#include "../module/utility/clutch.h"
#include "../module/utility/fastplace.h"
#include "../module/utility/noitemrelease.h"
#include "../module/utility/inventorymanager.h"
#include "../module/utility/altmanager.h"
#include "../module/visual/blockesp.h"
#include "../module/visual/esp.h"
#include "../module/visual/item_esp.h"
#include "../module/visual/nametags.h"
#include "../module/visual/anti_debuff.h"
#include "../module/visual/tracers.h"
#include "../module/visual/trajectories.h"
#include "../module/visual/dropped_info.h"
#include "../ui/hud/arraylist.h"
#include "../ui/hud/notifications.h"
#include "../ui/menu/menu.h"
#include "../ui/widgets/theme.hpp"
#include "../util/team/friends_manager.h"

#include <algorithm>
#include <cstring>
#include <cctype>

using namespace ArrayListRenderer;

namespace {
static std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string sanitize_user_config_name(const std::string& in) {
    std::string s = trim_copy(in);
    std::string out;
    out.reserve(s.size());

    // Keep this intentionally strict to avoid path traversal and weird FS edge cases.
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
            c == '-') {
            out.push_back((char)c);
        }
    }

    out = trim_copy(out);
    if (out.size() > 48)  // keep filenames manageable
        out.resize(48);

    return out;
}

static bool ends_with_case_insensitive(const std::string& s, const char* suffix) {
    size_t n = s.size();
    size_t m = strlen(suffix);
    if (m > n)
        return false;
    for (size_t i = 0; i < m; i++) {
        char a = (char)std::tolower((unsigned char)s[n - m + i]);
        char b = (char)std::tolower((unsigned char)suffix[i]);
        if (a != b)
            return false;
    }
    return true;
}
}  // namespace

std::string ConfigManager::getUserConfigsDir() const {
    if (configPath_.empty())
        return "";

    std::string configDir = configPath_.substr(0, configPath_.find_last_of("\\/"));
    std::string userDir = configDir + "\\configs";

    CreateDirectoryA(userDir.c_str(), nullptr);  // Ensure directory exists
    return userDir;
}

std::vector<std::string> ConfigManager::listUserConfigs() const {
    std::vector<std::string> out;
    std::string dir = getUserConfigsDir();
    if (dir.empty())
        return out;

    std::string pattern = dir + "\\*.json";

    WIN32_FIND_DATAA ffd{};
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return out;

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::string file = ffd.cFileName;
        if (!ends_with_case_insensitive(file, ".json"))
            continue;

        // Strip extension for display
        file.resize(file.size() - 5);
        if (file.empty())
            continue;

        // Only surface configs we can actually address with our name sanitizer.
        if (sanitize_user_config_name(file) != file)
            continue;

        out.push_back(file);
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);

    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        auto lower = [](unsigned char c) { return (unsigned char)std::tolower(c); };
        size_t n = (std::min)(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            unsigned char ca = lower((unsigned char)a[i]);
            unsigned char cb = lower((unsigned char)b[i]);
            if (ca < cb)
                return true;
            if (ca > cb)
                return false;
        }
        return a.size() < b.size();
    });

    return out;
}

bool ConfigManager::saveUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";

    // Snapshot current state into config_ and serialize.
    buildConfigFromModules();

    std::string content;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        content = config_.serialize();
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        if (err)
            *err = "Failed to open file for write";
        return false;
    }

    file << content;
    file.close();

    return true;
}

bool ConfigManager::loadUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (err)
            *err = "Config not found";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    json::Value loaded;
    if (!json::Parser::parse(content, loaded)) {
        if (err)
            *err = "Failed to parse JSON";
        return false;
    }

    int version = loaded["version"].as_int(0);

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_ = loaded;
    }

    if (version < CONFIG_VERSION) {
        migrateConfig(version);
    }

    applyConfig();

    // Persist and (if enabled) sync like any other setting change.
    onSettingChanged();

    return true;
}

bool ConfigManager::deleteUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";
    if (!DeleteFileA(path.c_str())) {
        if (err)
            *err = "Failed to delete file";
        return false;
    }

    return true;
}

// Apply default config (used when no config file exists)
void ConfigManager::applyDefaults() {
    COBBLE_TRACE("applyDefaults:enter");
    std::lock_guard<std::mutex> lock(configMutex_);
    COBBLE_TRACE("applyDefaults:got mutex");

    // Reset to a clean object. A failed parse can leave config_ with partial
    // state whose destructor faults; sehResetValue catches that and
    // placement-new's a fresh Value over the corrupt bytes (intentional
    // small leak rather than tearing down javaw).
    sehResetValue(config_);
    COBBLE_TRACE("applyDefaults:config_ reset");

    config_["version"] = CONFIG_VERSION;
    COBBLE_TRACE("applyDefaults:version set");

    // Settings
    config_["settings"]["cloudSync"] = false;
    COBBLE_TRACE("applyDefaults:cloudSync set");

    // HUD
    config_["hud"]["arrayList"] = true;
    config_["hud"]["notifications"] = true;
    COBBLE_TRACE("applyDefaults:hud set");

    // Default module configs (all disabled with default keybinds)
    COBBLE_TRACE("applyDefaults:exit");
}

// Apply loaded config to modules
void ConfigManager::applyConfig() {
    if (!moduleManager)
        return;

    // Apply settings
    if (config_.has("settings")) {
        syncEnabled_ = config_["settings"]["cloudSync"].as_bool(false);
        Menu::menuKey = config_["settings"]["menuKey"].as_int(0x2D);  // VK_INSERT
        // Load accent color
        if (config_["settings"].has("accentColor")) {
            ui::theme::accentColor[0] = config_["settings"]["accentColor"]["r"].as_float(0.9412f);
            ui::theme::accentColor[1] = config_["settings"]["accentColor"]["g"].as_float(0.4667f);
            ui::theme::accentColor[2] = config_["settings"]["accentColor"]["b"].as_float(0.0549f);
            ui::theme::accentColor[3] = config_["settings"]["accentColor"]["a"].as_float(1.0f);
        }
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
        // Load dynamic friend records. This preserves every entry and its UUID.
        if (config_["friends"].has("entries")) {
            friendsManager->clearFriends();
            const auto& entries = config_["friends"]["entries"].object();
            for (const auto& entry : entries) {
                const json::Value& data = entry.second;
                friendsManager->restoreFriend(data["uuid"].as_string(""), data["name"].as_string(""));
            }
        } else if (config_["friends"].has("list")) {
            // Backwards compatibility with older comma-separated configs.
            friendsManager->clearFriends();
            std::string friendsList = config_["friends"]["list"].as_string("");
            if (!friendsList.empty()) {
                size_t start = 0, end;
                while ((end = friendsList.find(',', start)) != std::string::npos) {
                    std::string name = friendsList.substr(start, end - start);
                    if (!name.empty())
                        friendsManager->addFriend(name);
                    start = end + 1;
                }
                std::string last = friendsList.substr(start);
                if (!last.empty())
                    friendsManager->addFriend(last);
            }
        }
        // Load team settings
        friendsManager->teams.enabled = config_["friends"]["teamsEnabled"].as_bool(true);
        friendsManager->teams.matchOwnNametag = config_["friends"]["matchOwnNametag"].as_bool(true);
        friendsManager->applyToCombat = config_["friends"]["applyToCombat"].as_bool(true);
        friendsManager->applyToVisual = config_["friends"]["applyToVisual"].as_bool(false);
        // Quick-add settings
        friendsManager->quickAddEnabled = config_["friends"]["quickAddEnabled"].as_bool(true);
        friendsManager->quickAddKey = config_["friends"]["quickAddKey"].as_int(0x04);  // VK_MBUTTON
    }

    // Apply module configs
    if (!config_.has("modules"))
        return;

    if (!config_["modules"].has("AntiDebuff") && config_["modules"].has("NoVisualEffects"))
        config_["modules"]["AntiDebuff"] = config_["modules"]["NoVisualEffects"];

    const auto& modules = config_["modules"].object();

    for (const auto& kv : modules) {
        const std::string& name = kv.first;
        const json::Value& data = kv.second;

        // Find module by name
        for (auto* mod : moduleManager->getModules()) {
            if (std::string(mod->name) != name)
                continue;

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
                s.stableCPSValue = data["stableCpsValue"].as_float(s.stableCPSValue);
                s.advancedMode = data["advancedMode"].as_bool(s.advancedMode);
                s.dropChance = data["dropChance"].as_float(s.dropChance);
                s.spikeChance = data["spikeChance"].as_float(s.spikeChance);
                s.spikeMultiplier = data["spikeMultiplier"].as_float(s.spikeMultiplier);
                s.kurtosis = data["kurtosis"].as_float(s.kurtosis);
                s.attackingOnly = data["attackingOnly"].as_bool(s.attackingOnly);
            } else if (name == "AimAssist" && aimAssist) {
                auto& s = aimAssist->settings;
                s.horizontalOnly = data["horizontalOnly"].as_bool(s.horizontalOnly);
                s.speed = data["speed"].as_float(s.speed);
                s.fov = data["fov"].as_int(s.fov);
                s.distance = data["distance"].as_float(s.distance);
                s.reactionTime = data["reactionTime"].as_int(s.reactionTime);
                s.targetMode =
                    static_cast<AimAssist::TargetMode>(data["targetMode"].as_int(static_cast<int>(s.targetMode)));
                s.switchSortBy =
                    static_cast<AimAssist::SortBy>(data["switchSortBy"].as_int(static_cast<int>(s.switchSortBy)));
                s.hurtTimeLock = data["hurtTimeLock"].as_bool(s.hurtTimeLock);
                s.breakBlocks = data["breakBlocks"].as_bool(s.breakBlocks);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
                s.aimAtClosestPoint = data["aimAtClosestPoint"].as_bool(s.aimAtClosestPoint);
                s.alwaysAim = data["alwaysAim"].as_bool(s.alwaysAim);
                s.invisibles = data["invisibles"].as_bool(s.invisibles);
                if (data.has("entityLock"))
                    s.entityLock = data["entityLock"].as_bool(s.entityLock);
                else
                    s.entityLock = data["lockAim"].as_bool(s.entityLock);
                s.stopOnHitbox = data["stopOnHitbox"].as_bool(s.stopOnHitbox);
            } else if (name == "Velocity" && velocity) {
                auto& s = velocity->settings;
                s.mode = static_cast<Velocity::Mode>(data["mode"].as_int(static_cast<int>(s.mode)));
                s.horizontal = data["horizontal"].as_float(s.horizontal);
                s.vertical = data["vertical"].as_float(s.vertical);
                s.chance = data["chance"].as_float(s.chance);
                s.fov = data["fov"].as_float(s.fov);
                s.reduceDelay = data["reduceDelay"].as_int(s.reduceDelay);
                s.jumpChance = data["jumpChance"].as_float(s.jumpChance);
                s.allowDoubleClicks = data["allowDoubleClicks"].as_bool(s.allowDoubleClicks);
                s.verticalMode =
                    static_cast<Velocity::VerticalMode>(data["verticalMode"].as_int(static_cast<int>(s.verticalMode)));
                s.onlyLookingAtPlayer = data["onlyLookingAtPlayer"].as_bool(s.onlyLookingAtPlayer);
                s.onlyMousePressed = data["onlyMousePressed"].as_bool(s.onlyMousePressed);
                s.onlyMovingForward = data["onlyMovingForward"].as_bool(s.onlyMovingForward);
                s.onlyHoldingWeapon = data["onlyHoldingWeapon"].as_bool(s.onlyHoldingWeapon);
                s.onlySprinting = data["onlySprinting"].as_bool(s.onlySprinting);
                s.percentageVariance = data["percentageVariance"].as_float(s.percentageVariance);
                s.tickSpread = data["tickSpread"].as_int(s.tickSpread);
                s.microDelayMs = data["microDelayMs"].as_int(s.microDelayMs);
            } else if (name == "NameTags" && nameTags) {
                auto& s = nameTags->settings;
                s.range = data["range"].as_float(s.range);
                s.scale = data["scale"].as_float(s.scale);
                s.background = data["background"].as_bool(s.background);
                s.showHealth = data["showHealth"].as_bool(s.showHealth);
                s.showDistance = data["showDistance"].as_bool(s.showDistance);
                s.showArmor = data["showArmor"].as_bool(s.showArmor);
                s.showEffects = data["showEffects"].as_bool(s.showEffects);
                if (data.has("normalColor")) {
                    s.normalColor[0] = data["normalColor"]["r"].as_float(s.normalColor[0]);
                    s.normalColor[1] = data["normalColor"]["g"].as_float(s.normalColor[1]);
                    s.normalColor[2] = data["normalColor"]["b"].as_float(s.normalColor[2]);
                    s.normalColor[3] = data["normalColor"]["a"].as_float(s.normalColor[3]);
                }
                if (data.has("friendColor")) {
                    s.friendColor[0] = data["friendColor"]["r"].as_float(s.friendColor[0]);
                    s.friendColor[1] = data["friendColor"]["g"].as_float(s.friendColor[1]);
                    s.friendColor[2] = data["friendColor"]["b"].as_float(s.friendColor[2]);
                    s.friendColor[3] = data["friendColor"]["a"].as_float(s.friendColor[3]);
                }
            } else if (name == "AntiDebuff" && antiDebuff) {
                auto& s = antiDebuff->settings;
                s.blindness = data["blindness"].as_bool(s.blindness);
                s.nausea = data["nausea"].as_bool(s.nausea);
            } else if (name == "ESP" && esp) {
                auto& s = esp->settings;
                s.box = data["box"].as_bool(s.box);
                s.box3D = data["box3D"].as_bool(s.box3D);
                s.filled = data["filled"].as_bool(s.filled);
                s.outline = data["outline"].as_bool(s.outline);
                s.healthBar = data["healthBar"].as_bool(s.healthBar);
                s.teamColors = data["teamColors"].as_bool(s.teamColors);
                s.range = data["range"].as_float(s.range);
                // Load color arrays
                if (data.has("boxColor")) {
                    s.boxColor[0] = data["boxColor"]["r"].as_float(s.boxColor[0]);
                    s.boxColor[1] = data["boxColor"]["g"].as_float(s.boxColor[1]);
                    s.boxColor[2] = data["boxColor"]["b"].as_float(s.boxColor[2]);
                    s.boxColor[3] = data["boxColor"]["a"].as_float(s.boxColor[3]);
                }
                if (data.has("filledColor")) {
                    s.filledColor[0] = data["filledColor"]["r"].as_float(s.filledColor[0]);
                    s.filledColor[1] = data["filledColor"]["g"].as_float(s.filledColor[1]);
                    s.filledColor[2] = data["filledColor"]["b"].as_float(s.filledColor[2]);
                    s.filledColor[3] = data["filledColor"]["a"].as_float(s.filledColor[3]);
                }
                if (data.has("outlineColor")) {
                    s.outlineColor[0] = data["outlineColor"]["r"].as_float(s.outlineColor[0]);
                    s.outlineColor[1] = data["outlineColor"]["g"].as_float(s.outlineColor[1]);
                    s.outlineColor[2] = data["outlineColor"]["b"].as_float(s.outlineColor[2]);
                    s.outlineColor[3] = data["outlineColor"]["a"].as_float(s.outlineColor[3]);
                }
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
            } else if (name == "ItemESP" && itemEsp) {
                auto& s = itemEsp->settings;
                s.box = data["box"].as_bool(s.box);
                s.box3D = data["box3D"].as_bool(s.box3D);
                s.filled = data["filled"].as_bool(s.filled);
                s.outline = data["outline"].as_bool(s.outline);
                s.showQuantity = data["showQuantity"].as_bool(s.showQuantity);
                s.range = data["range"].as_float(s.range);
                if (data.has("boxColor")) {
                    s.boxColor[0] = data["boxColor"]["r"].as_float(s.boxColor[0]);
                    s.boxColor[1] = data["boxColor"]["g"].as_float(s.boxColor[1]);
                    s.boxColor[2] = data["boxColor"]["b"].as_float(s.boxColor[2]);
                    s.boxColor[3] = data["boxColor"]["a"].as_float(s.boxColor[3]);
                }
                if (data.has("filledColor")) {
                    s.filledColor[0] = data["filledColor"]["r"].as_float(s.filledColor[0]);
                    s.filledColor[1] = data["filledColor"]["g"].as_float(s.filledColor[1]);
                    s.filledColor[2] = data["filledColor"]["b"].as_float(s.filledColor[2]);
                    s.filledColor[3] = data["filledColor"]["a"].as_float(s.filledColor[3]);
                }
                if (data.has("outlineColor")) {
                    s.outlineColor[0] = data["outlineColor"]["r"].as_float(s.outlineColor[0]);
                    s.outlineColor[1] = data["outlineColor"]["g"].as_float(s.outlineColor[1]);
                    s.outlineColor[2] = data["outlineColor"]["b"].as_float(s.outlineColor[2]);
                    s.outlineColor[3] = data["outlineColor"]["a"].as_float(s.outlineColor[3]);
                }
            } else if (name == "Bridge Assist" && bridgeAssist) {
                auto& s = bridgeAssist->settings;
                s.onlyLookingDown = data["onlyLookingDown"].as_bool(s.onlyLookingDown);
                s.onlyWhenSneaking = data["onlyWhenSneaking"].as_bool(s.onlyWhenSneaking);
            } else if (name == "Reach" && reach) {
                auto& s = reach->settings;
                s.distance = data["distance"].as_float(s.distance);
                s.weaponsOnly = data["weaponsOnly"].as_bool(s.weaponsOnly);
                s.sprintingOnly = data["sprintingOnly"].as_bool(s.sprintingOnly);
                s.movingOnly = data["movingOnly"].as_bool(s.movingOnly);
                s.hitChance = data["hitChance"].as_int(s.hitChance);
                s.tickDelay = data["tickDelay"].as_int(s.tickDelay);
            } else if (name == "Piercing" && piercing) {
                auto& s = piercing->settings;
                s.distance = data["distance"].as_float(s.distance);
                s.weaponsOnly = data["weaponsOnly"].as_bool(s.weaponsOnly);
                s.targetInvisibles = data["targetInvisibles"].as_bool(s.targetInvisibles);
                s.ignoreFriends = data["ignoreFriends"].as_bool(s.ignoreFriends);
                s.ignoreTeams = data["ignoreTeams"].as_bool(s.ignoreTeams);
            } else if (name == "HitSelect" && hitSelect) {
                auto& s = hitSelect->settings;
                s.minComboHits = data["minComboHits"].as_int(s.minComboHits);
                s.comboDetectionTime = data["comboDetectionTime"].as_int(s.comboDetectionTime);
                s.waitForFirstHit = data["waitForFirstHit"].as_bool(s.waitForFirstHit);
                s.waitDuration = data["waitDuration"].as_int(s.waitDuration);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
            } else if (name == "ArmorSwitch" && armorSwitch) {
                auto& s = armorSwitch->settings;
                s.set1Material = data["set1Material"].as_int(s.set1Material);
                s.set2Material = data["set2Material"].as_int(s.set2Material);
                s.switchDelay = data["switchDelay"].as_int(s.switchDelay);
                s.retryMinDelay = data["retryMinDelay"].as_int(s.retryMinDelay);
                s.preferProtection = data["preferProtection"].as_bool(s.preferProtection);
                s.onInventoryOpen = data["onInventoryOpen"].as_bool(s.onInventoryOpen);
                s.retryOnFail = data["retryOnFail"].as_bool(s.retryOnFail);
                s.silentMode = data["silentMode"].as_bool(s.silentMode);
                s.autoBalance = data["autoBalance"].as_bool(s.autoBalance);
                s.maxDurabilityDiff = data["maxDurabilityDiff"].as_int(s.maxDurabilityDiff);
                s.checkInterval = data["checkInterval"].as_int(s.checkInterval);
            } else if (name == "AutoRefill" && autoRefill) {
                auto& s = autoRefill->settings;
                s.refillSoup = data["refillSoup"].as_bool(s.refillSoup);
                s.refillPotions = data["refillPotions"].as_bool(s.refillPotions);
                s.speed = data["speed"].as_int(s.speed);
                s.onInventoryOpen = data["onInventoryOpen"].as_bool(s.onInventoryOpen);
                s.silentMode = data["silentMode"].as_bool(s.silentMode);
            } else if (name == "BlockESP" && blockESP) {
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
                s.range = (std::clamp)(s.range, 8.0f, 32.0f);
                s.opacity = (std::clamp)(s.opacity, 10, 100);
                s.fadeWithDistance = data["fadeWithDistance"].as_bool(s.fadeWithDistance);
            } else if (name == "Tracers" && tracers) {
                auto& s = tracers->settings;
                s.colorByDistance = data["colorByDistance"].as_bool(s.colorByDistance);
                s.colorByHealth = data["colorByHealth"].as_bool(s.colorByHealth);
                s.colorByTeam = data["colorByTeam"].as_bool(s.colorByTeam);
                s.thickness = data["thickness"].as_float(s.thickness);
                s.range = data["range"].as_float(s.range);
                s.lineFrom = data["lineFrom"].as_int(s.lineFrom);
                if (data.has("tracerColor")) {
                    s.tracerColor[0] = data["tracerColor"]["r"].as_float(s.tracerColor[0]);
                    s.tracerColor[1] = data["tracerColor"]["g"].as_float(s.tracerColor[1]);
                    s.tracerColor[2] = data["tracerColor"]["b"].as_float(s.tracerColor[2]);
                    s.tracerColor[3] = data["tracerColor"]["a"].as_float(s.tracerColor[3]);
                }
            } else if (name == "Trajectories" && trajectories) {
                auto& s = trajectories->settings;
                s.thickness = data["thickness"].as_float(s.thickness);
                if (data.has("lineColor")) {
                    s.lineColor[0] = data["lineColor"]["r"].as_float(s.lineColor[0]);
                    s.lineColor[1] = data["lineColor"]["g"].as_float(s.lineColor[1]);
                    s.lineColor[2] = data["lineColor"]["b"].as_float(s.lineColor[2]);
                    s.lineColor[3] = data["lineColor"]["a"].as_float(s.lineColor[3]);
                }
                // Projectile toggles
                s.showBow = data["showBow"].as_bool(s.showBow);
                s.showEnderPearl = data["showEnderPearl"].as_bool(s.showEnderPearl);
                s.showEgg = data["showEgg"].as_bool(s.showEgg);
                s.showSnowball = data["showSnowball"].as_bool(s.showSnowball);
                s.showPotionHealing = data["showPotionHealing"].as_bool(s.showPotionHealing);
                s.showPotionPoison = data["showPotionPoison"].as_bool(s.showPotionPoison);
                s.showPotionSlowness = data["showPotionSlowness"].as_bool(s.showPotionSlowness);
                s.showExpBottle = data["showExpBottle"].as_bool(s.showExpBottle);
            } else if (name == "NoItemRelease" && noItemRelease) {
                noItemRelease->settings.mode =
                    static_cast<ItemReleaseMode>(data["mode"].as_int(static_cast<int>(noItemRelease->settings.mode)));
            } else if (name == "AltManager" && altManager) {
                std::string n = data["spoofedName"].as_string(altManager->settings.spoofedName);
                size_t copyLen = (std::min)(n.size(), (size_t)AltManager::MAX_NAME_LEN);
                memcpy(altManager->settings.spoofedName, n.data(), copyLen);
                altManager->settings.spoofedName[copyLen] = '\0';
            } else if (name == "FastPlace" && fastPlace) {
                fastPlace->settings.blocksOnly = data["blocksOnly"].as_bool(fastPlace->settings.blocksOnly);
            } else if (name == "Clutch" && clutch) {
                auto& s = clutch->settings;
                s.predictionMs = (std::clamp)(data["predictionMs"].as_int(s.predictionMs), 100, 600);
                s.searchRadius = (std::clamp)(data["searchRadius"].as_int(s.searchRadius), 2, 8);
                s.reach = (std::clamp)(data["reach"].as_float(s.reach), 3.0f, 4.5f);
                s.placeDelayMs = (std::clamp)(data["placeDelayMs"].as_int(s.placeDelayMs), 40, 200);
                s.restoreSlot = data["restoreSlot"].as_bool(s.restoreSlot);
            } else if (name == "Breaker" && breaker) {
                auto& s = breaker->settings;
                s.range = (std::clamp)(data["range"].as_float(s.range), 2.0f, 6.0f);
                s.legit = data["legit"].as_bool(s.legit);
            } else if (name == "Auto Tool" && autoTool) {
                auto& s = autoTool->settings;
                s.switchBack = data["switchBack"].as_bool(s.switchBack);
                s.autoSword = data["autoSword"].as_bool(s.autoSword);
            } else if (name == "NoHitDelay" && noHitDelay) {
                noHitDelay->settings.weaponOnly = data["weaponOnly"].as_bool(noHitDelay->settings.weaponOnly);
            } else if (name == "Criticals" && criticals) {
                auto& s = criticals->settings;
                s.maxDelayMs = data["maxDelayMs"].as_int(s.maxDelayMs);
                s.chance = data["chance"].as_int(s.chance);
                s.onlyWhileAttacking = data["onlyWhileAttacking"].as_bool(s.onlyWhileAttacking);
                s.lookingAtPlayer = data["lookingAtPlayer"].as_bool(s.lookingAtPlayer);
                s.onlyWithSword = data["onlyWithSword"].as_bool(s.onlyWithSword);
            } else if (name == "WTap" && wTap) {
                auto& s = wTap->settings;
                s.mode = static_cast<WTap::Mode>((std::clamp)(data["mode"].as_int(static_cast<int>(s.mode)), 0, 2));
                s.chance = (std::clamp)(data["chance"].as_int(s.chance), 0, 100);
                s.delayMs = (std::clamp)(data["delayMs"].as_int(s.delayMs), 0, 120);
                s.releaseMs = (std::clamp)(data["releaseMs"].as_int(s.releaseMs), 20, 180);
                s.targetOnly = data["targetOnly"].as_bool(s.targetOnly);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
            } else if (name == "Backtrack" && backtrack) {
                auto& s = backtrack->settings;
                s.mode = static_cast<BacktrackMode>(data["mode"].as_int(static_cast<int>(s.mode)));
                s.ticks = data["ticks"].as_int(s.ticks);
                s.latency = data["latency"].as_int(s.latency);
                s.renderPreviousTicks = data["renderPreviousTicks"].as_bool(s.renderPreviousTicks);
                s.renderServerPos = data["renderServerPos"].as_bool(s.renderServerPos);
                s.minDistance = data["minDistance"].as_float(s.minDistance);
                s.maxDistance = data["maxDistance"].as_float(s.maxDistance);
                s.maxHurtTime = data["maxHurtTime"].as_int(s.maxHurtTime);
                s.cooldown = data["cooldown"].as_float(s.cooldown);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
                s.showBacktrackBox = data["showBacktrackBox"].as_bool(s.showBacktrackBox);
                s.filledBox = data["filledBox"].as_bool(s.filledBox);
                if (data.has("boxColor")) {
                    s.boxColor[0] = data["boxColor"]["r"].as_float(s.boxColor[0]);
                    s.boxColor[1] = data["boxColor"]["g"].as_float(s.boxColor[1]);
                    s.boxColor[2] = data["boxColor"]["b"].as_float(s.boxColor[2]);
                    s.boxColor[3] = data["boxColor"]["a"].as_float(s.boxColor[3]);
                }
                if (data.has("shadowColor")) {
                    s.shadowColor[0] = data["shadowColor"]["r"].as_float(s.shadowColor[0]);
                    s.shadowColor[1] = data["shadowColor"]["g"].as_float(s.shadowColor[1]);
                    s.shadowColor[2] = data["shadowColor"]["b"].as_float(s.shadowColor[2]);
                    s.shadowColor[3] = data["shadowColor"]["a"].as_float(s.shadowColor[3]);
                }
            } else if (name == "Blink" && blink) {
                auto& s = blink->settings;
                s.timerLimit = data["timerLimit"].as_float(s.timerLimit);
                s.showTimerHud = data["showTimerHud"].as_bool(s.showTimerHud);
                s.showBreadcrumbs = data["showBreadcrumbs"].as_bool(s.showBreadcrumbs);
                if (data.has("blinkColor")) {
                    s.blinkColor[0] = data["blinkColor"]["r"].as_float(s.blinkColor[0]);
                    s.blinkColor[1] = data["blinkColor"]["g"].as_float(s.blinkColor[1]);
                    s.blinkColor[2] = data["blinkColor"]["b"].as_float(s.blinkColor[2]);
                    s.blinkColor[3] = data["blinkColor"]["a"].as_float(s.blinkColor[3]);
                }
            } else if (name == "FakeLag" && fakeLag) {
                auto& s = fakeLag->settings;
                s.mode = static_cast<FakeLag::Mode>((std::clamp)(data["mode"].as_int(static_cast<int>(s.mode)), 0, 3));
                s.minDelayMs = data["minDelayMs"].as_int(s.minDelayMs);
                s.maxDelayMs = data["maxDelayMs"].as_int(s.maxDelayMs);
                s.pulseHoldMs = data["pulseHoldMs"].as_int(s.pulseHoldMs);
                s.pulseReleaseMs = data["pulseReleaseMs"].as_int(s.pulseReleaseMs);
                s.maxDistance = data["maxDistance"].as_float(s.maxDistance);
                s.adaptiveStrength = data["adaptiveStrength"].as_float(s.adaptiveStrength);
                s.maxPackets = data["maxPackets"].as_int(s.maxPackets);
                s.releaseRate = data["releaseRate"].as_int(s.releaseRate);
                s.minDelayMs = (std::clamp)(s.minDelayMs, 20, 250);
                s.maxDelayMs = (std::clamp)(s.maxDelayMs, (std::max)(40, s.minDelayMs), 400);
                s.pulseHoldMs = (std::clamp)(s.pulseHoldMs, 40, 1000);
                s.pulseReleaseMs = (std::clamp)(s.pulseReleaseMs, 20, 500);
                s.maxDistance = (std::clamp)(s.maxDistance, 0.5f, 4.0f);
                s.adaptiveStrength = (std::clamp)(s.adaptiveStrength, 0.0f, 1.0f);
                s.maxPackets = (std::clamp)(s.maxPackets, 8, 60);
                s.releaseRate = (std::clamp)(s.releaseRate, 1, 12);
                s.movingOnly = data["movingOnly"].as_bool(s.movingOnly);
                s.smoothRelease = data["smoothRelease"].as_bool(s.smoothRelease);
                s.flushOnAttack = data["flushOnAttack"].as_bool(s.flushOnAttack);
                s.flushOnDamage = data["flushOnDamage"].as_bool(s.flushOnDamage);
                s.flushOnItemUse = data["flushOnItemUse"].as_bool(s.flushOnItemUse);
                s.renderServerPlayer = data["renderServerPlayer"].as_bool(s.renderServerPlayer);
                s.filledRender = data["filledRender"].as_bool(s.filledRender);
                s.renderLine = data["renderLine"].as_bool(s.renderLine);
                s.showStatus = data["showStatus"].as_bool(s.showStatus);
                if (data.has("color")) {
                    s.color[0] = data["color"]["r"].as_float(s.color[0]);
                    s.color[1] = data["color"]["g"].as_float(s.color[1]);
                    s.color[2] = data["color"]["b"].as_float(s.color[2]);
                    s.color[3] = data["color"]["a"].as_float(s.color[3]);
                }
            } else if (name == "NoSlowDown" && noSlowDown) {
                auto& s = *noSlowDown;
                s.packetMode = static_cast<NoSlowDown::PacketMode>(
                    (std::clamp)(data["packetMode"].as_int(static_cast<int>(s.packetMode)), 0, 3));
                s.blockForward = (std::clamp)(data["blockForward"].as_float(s.blockForward), 0.2f, 1.0f);
                s.blockStrafe = (std::clamp)(data["blockStrafe"].as_float(s.blockStrafe), 0.2f, 1.0f);
                s.consumeForward = (std::clamp)(data["consumeForward"].as_float(s.consumeForward), 0.2f, 1.0f);
                s.consumeStrafe = (std::clamp)(data["consumeStrafe"].as_float(s.consumeStrafe), 0.2f, 1.0f);
                s.bowForward = (std::clamp)(data["bowForward"].as_float(s.bowForward), 0.2f, 1.0f);
                s.bowStrafe = (std::clamp)(data["bowStrafe"].as_float(s.bowStrafe), 0.2f, 1.0f);
            } else if (name == "Timer" && timerModule) {
                auto& s = timerModule->settings;
                s.speed = (double)data["speed"].as_float((float)s.speed);
                s.mousePressed = data["mousePressed"].as_bool(s.mousePressed);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
            } else if (name == "TimerRange" && timerRange) {
                auto& s = timerRange->settings;
                const int mode = (std::clamp)(data["mode"].as_int(static_cast<int>(s.mode)), 0, 3);
                s.mode = static_cast<TimerRange::TriggerMode>(mode);
                s.minSpeed = data["minSpeed"].as_float(s.minSpeed);
                s.maxSpeed = data["maxSpeed"].as_float(s.maxSpeed);
                s.offsetMs = (std::clamp)(data["offsetMs"].as_int(s.offsetMs), 0, 500);
                const int legacyDuration = data["intervalTicks"].as_int(0) * 50;
                s.durationMs = (std::clamp)(data["durationMs"].as_int(legacyDuration > 0 ? legacyDuration : s.durationMs),
                                            10, 1000);
                s.minSpeed = (std::clamp)(s.minSpeed, 0.05f, 3.0f);
                s.maxSpeed = (std::clamp)(s.maxSpeed, s.minSpeed, 3.0f);
                s.targetOnly = data["targetOnly"].as_bool(s.targetOnly);
                s.weaponOnly = data["weaponOnly"].as_bool(s.weaponOnly);
            } else if (name == "DroppedInfo" && droppedInfo) {
                auto& s = droppedInfo->settings;
                s.x = data["x"].as_float(s.x);
                s.y = data["y"].as_float(s.y);
                s.scale = data["scale"].as_float(s.scale);
                s.background = data["background"].as_bool(s.background);
            } else if (name == "InvManager" && inventoryManager) {
                inventoryManager->settings.swapDelay = data["swapDelay"].as_int(inventoryManager->settings.swapDelay);
                inventoryManager->settings.retryOnFail =
                    data["retryOnFail"].as_bool(inventoryManager->settings.retryOnFail);
                inventoryManager->settings.activePreset = data["activePreset"].as_int(0);
                if (data.has("presets")) {
                    for (int p = 0; p < InventoryManager::MAX_PRESETS; p++) {
                        std::string presetKey = "p" + std::to_string(p);
                        if (data["presets"].has(presetKey)) {
                            inventoryManager->presets[p].saved = data["presets"][presetKey]["saved"].as_bool(false);
                            for (int i = 0; i < 9; i++) {
                                std::string slotKey = std::to_string(i);
                                if (data["presets"][presetKey].has(slotKey)) {
                                    inventoryManager->presets[p].slots[i].itemId =
                                        data["presets"][presetKey][slotKey]["id"].as_int(0);
                                    inventoryManager->presets[p].slots[i].damage =
                                        data["presets"][presetKey][slotKey]["dmg"].as_int(0);
                                }
                            }
                        }
                    }
                }
            }

            break;
        }
    }
}

// Build config from current module state
void ConfigManager::buildConfigFromModules() {
    if (!moduleManager)
        return;

    std::lock_guard<std::mutex> lock(configMutex_);

    config_["version"] = CONFIG_VERSION;

    // Settings
    config_["settings"]["cloudSync"] = syncEnabled_;
    config_["settings"]["menuKey"] = Menu::menuKey;
    // Save accent color
    config_["settings"]["accentColor"]["r"] = ui::theme::accentColor[0];
    config_["settings"]["accentColor"]["g"] = ui::theme::accentColor[1];
    config_["settings"]["accentColor"]["b"] = ui::theme::accentColor[2];
    config_["settings"]["accentColor"]["a"] = ui::theme::accentColor[3];

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
        auto friendRecords = friendsManager->getFriends();
        config_["friends"]["entries"] = json::Value();
        for (size_t i = 0; i < friendRecords.size(); ++i) {
            const std::string key = std::to_string(i);
            config_["friends"]["entries"][key]["uuid"] = friendRecords[i].uuid;
            config_["friends"]["entries"][key]["name"] = friendRecords[i].displayName;
        }

        auto friends = friendsManager->getFriendNames();
        std::string friendsList;
        for (size_t i = 0; i < friends.size(); ++i) {
            if (i > 0)
                friendsList += ",";
            friendsList += friends[i];
        }
        config_["friends"]["list"] = friendsList;
        config_["friends"]["teamsEnabled"] = friendsManager->teams.enabled;
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
            config_["modules"][name]["stableCpsValue"] = s.stableCPSValue;
            config_["modules"][name]["advancedMode"] = s.advancedMode;
            config_["modules"][name]["dropChance"] = s.dropChance;
            config_["modules"][name]["spikeChance"] = s.spikeChance;
            config_["modules"][name]["spikeMultiplier"] = s.spikeMultiplier;
            config_["modules"][name]["kurtosis"] = s.kurtosis;
            config_["modules"][name]["attackingOnly"] = s.attackingOnly;
        } else if (name == "AimAssist" && aimAssist) {
            const auto& s = aimAssist->settings;
            config_["modules"][name]["horizontalOnly"] = s.horizontalOnly;
            config_["modules"][name]["speed"] = s.speed;
            config_["modules"][name]["fov"] = s.fov;
            config_["modules"][name]["distance"] = s.distance;
            config_["modules"][name]["reactionTime"] = s.reactionTime;
            config_["modules"][name]["targetMode"] = static_cast<int>(s.targetMode);
            config_["modules"][name]["switchSortBy"] = static_cast<int>(s.switchSortBy);
            config_["modules"][name]["hurtTimeLock"] = s.hurtTimeLock;
            config_["modules"][name]["breakBlocks"] = s.breakBlocks;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
            config_["modules"][name]["aimAtClosestPoint"] = s.aimAtClosestPoint;
            config_["modules"][name]["alwaysAim"] = s.alwaysAim;
            config_["modules"][name]["invisibles"] = s.invisibles;
            config_["modules"][name]["entityLock"] = s.entityLock;
            config_["modules"][name]["stopOnHitbox"] = s.stopOnHitbox;
        } else if (name == "Velocity" && velocity) {
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
        } else if (name == "NameTags" && nameTags) {
            const auto& s = nameTags->settings;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["scale"] = s.scale;
            config_["modules"][name]["background"] = s.background;
            config_["modules"][name]["showHealth"] = s.showHealth;
            config_["modules"][name]["showDistance"] = s.showDistance;
            config_["modules"][name]["showArmor"] = s.showArmor;
            config_["modules"][name]["showEffects"] = s.showEffects;
            config_["modules"][name]["normalColor"]["r"] = s.normalColor[0];
            config_["modules"][name]["normalColor"]["g"] = s.normalColor[1];
            config_["modules"][name]["normalColor"]["b"] = s.normalColor[2];
            config_["modules"][name]["normalColor"]["a"] = s.normalColor[3];
            config_["modules"][name]["friendColor"]["r"] = s.friendColor[0];
            config_["modules"][name]["friendColor"]["g"] = s.friendColor[1];
            config_["modules"][name]["friendColor"]["b"] = s.friendColor[2];
            config_["modules"][name]["friendColor"]["a"] = s.friendColor[3];
        } else if (name == "AntiDebuff" && antiDebuff) {
            const auto& s = antiDebuff->settings;
            config_["modules"][name]["blindness"] = s.blindness;
            config_["modules"][name]["nausea"] = s.nausea;
        } else if (name == "ESP" && esp) {
            const auto& s = esp->settings;
            config_["modules"][name]["box"] = s.box;
            config_["modules"][name]["box3D"] = s.box3D;
            config_["modules"][name]["filled"] = s.filled;
            config_["modules"][name]["outline"] = s.outline;
            config_["modules"][name]["healthBar"] = s.healthBar;
            config_["modules"][name]["teamColors"] = s.teamColors;
            config_["modules"][name]["range"] = s.range;
            // Save color arrays
            config_["modules"][name]["boxColor"]["r"] = s.boxColor[0];
            config_["modules"][name]["boxColor"]["g"] = s.boxColor[1];
            config_["modules"][name]["boxColor"]["b"] = s.boxColor[2];
            config_["modules"][name]["boxColor"]["a"] = s.boxColor[3];

            config_["modules"][name]["filledColor"]["r"] = s.filledColor[0];
            config_["modules"][name]["filledColor"]["g"] = s.filledColor[1];
            config_["modules"][name]["filledColor"]["b"] = s.filledColor[2];
            config_["modules"][name]["filledColor"]["a"] = s.filledColor[3];

            config_["modules"][name]["outlineColor"]["r"] = s.outlineColor[0];
            config_["modules"][name]["outlineColor"]["g"] = s.outlineColor[1];
            config_["modules"][name]["outlineColor"]["b"] = s.outlineColor[2];
            config_["modules"][name]["outlineColor"]["a"] = s.outlineColor[3];

            config_["modules"][name]["friendColor"]["r"] = s.friendColor[0];
            config_["modules"][name]["friendColor"]["g"] = s.friendColor[1];
            config_["modules"][name]["friendColor"]["b"] = s.friendColor[2];
            config_["modules"][name]["friendColor"]["a"] = s.friendColor[3];
            config_["modules"][name]["enemyColor"]["r"] = s.enemyColor[0];
            config_["modules"][name]["enemyColor"]["g"] = s.enemyColor[1];
            config_["modules"][name]["enemyColor"]["b"] = s.enemyColor[2];
            config_["modules"][name]["enemyColor"]["a"] = s.enemyColor[3];
        } else if (name == "ItemESP" && itemEsp) {
            const auto& s = itemEsp->settings;
            config_["modules"][name]["box"] = s.box;
            config_["modules"][name]["box3D"] = s.box3D;
            config_["modules"][name]["filled"] = s.filled;
            config_["modules"][name]["outline"] = s.outline;
            config_["modules"][name]["showQuantity"] = s.showQuantity;
            config_["modules"][name]["range"] = s.range;

            config_["modules"][name]["boxColor"]["r"] = s.boxColor[0];
            config_["modules"][name]["boxColor"]["g"] = s.boxColor[1];
            config_["modules"][name]["boxColor"]["b"] = s.boxColor[2];
            config_["modules"][name]["boxColor"]["a"] = s.boxColor[3];

            config_["modules"][name]["filledColor"]["r"] = s.filledColor[0];
            config_["modules"][name]["filledColor"]["g"] = s.filledColor[1];
            config_["modules"][name]["filledColor"]["b"] = s.filledColor[2];
            config_["modules"][name]["filledColor"]["a"] = s.filledColor[3];

            config_["modules"][name]["outlineColor"]["r"] = s.outlineColor[0];
            config_["modules"][name]["outlineColor"]["g"] = s.outlineColor[1];
            config_["modules"][name]["outlineColor"]["b"] = s.outlineColor[2];
            config_["modules"][name]["outlineColor"]["a"] = s.outlineColor[3];
        } else if (name == "Bridge Assist" && bridgeAssist) {
            const auto& s = bridgeAssist->settings;
            config_["modules"][name]["onlyLookingDown"] = s.onlyLookingDown;
            config_["modules"][name]["onlyWhenSneaking"] = s.onlyWhenSneaking;
        } else if (name == "Reach" && reach) {
            const auto& s = reach->settings;
            config_["modules"][name]["distance"] = s.distance;
            config_["modules"][name]["weaponsOnly"] = s.weaponsOnly;
            config_["modules"][name]["sprintingOnly"] = s.sprintingOnly;
            config_["modules"][name]["movingOnly"] = s.movingOnly;
            config_["modules"][name]["hitChance"] = s.hitChance;
            config_["modules"][name]["tickDelay"] = s.tickDelay;
        } else if (name == "Piercing" && piercing) {
            const auto& s = piercing->settings;
            config_["modules"][name]["distance"] = s.distance;
            config_["modules"][name]["weaponsOnly"] = s.weaponsOnly;
            config_["modules"][name]["targetInvisibles"] = s.targetInvisibles;
            config_["modules"][name]["ignoreFriends"] = s.ignoreFriends;
            config_["modules"][name]["ignoreTeams"] = s.ignoreTeams;
        } else if (name == "HitSelect" && hitSelect) {
            const auto& s = hitSelect->settings;
            config_["modules"][name]["minComboHits"] = s.minComboHits;
            config_["modules"][name]["comboDetectionTime"] = s.comboDetectionTime;
            config_["modules"][name]["waitForFirstHit"] = s.waitForFirstHit;
            config_["modules"][name]["waitDuration"] = s.waitDuration;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
        } else if (name == "ArmorSwitch" && armorSwitch) {
            const auto& s = armorSwitch->settings;
            config_["modules"][name]["set1Material"] = s.set1Material;
            config_["modules"][name]["set2Material"] = s.set2Material;
            config_["modules"][name]["switchDelay"] = s.switchDelay;
            config_["modules"][name]["retryMinDelay"] = s.retryMinDelay;
            config_["modules"][name]["preferProtection"] = s.preferProtection;
            config_["modules"][name]["onInventoryOpen"] = s.onInventoryOpen;
            config_["modules"][name]["retryOnFail"] = s.retryOnFail;
            config_["modules"][name]["silentMode"] = s.silentMode;
            config_["modules"][name]["autoBalance"] = s.autoBalance;
            config_["modules"][name]["maxDurabilityDiff"] = s.maxDurabilityDiff;
            config_["modules"][name]["checkInterval"] = s.checkInterval;
        } else if (name == "AutoRefill" && autoRefill) {
            const auto& s = autoRefill->settings;
            config_["modules"][name]["refillSoup"] = s.refillSoup;
            config_["modules"][name]["refillPotions"] = s.refillPotions;
            config_["modules"][name]["speed"] = s.speed;
            config_["modules"][name]["onInventoryOpen"] = s.onInventoryOpen;
            config_["modules"][name]["silentMode"] = s.silentMode;
        } else if (name == "BlockESP" && blockESP) {
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
        } else if (name == "Tracers" && tracers) {
            const auto& s = tracers->settings;
            config_["modules"][name]["colorByDistance"] = s.colorByDistance;
            config_["modules"][name]["colorByHealth"] = s.colorByHealth;
            config_["modules"][name]["colorByTeam"] = s.colorByTeam;
            config_["modules"][name]["thickness"] = s.thickness;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["lineFrom"] = s.lineFrom;
            config_["modules"][name]["tracerColor"]["r"] = s.tracerColor[0];
            config_["modules"][name]["tracerColor"]["g"] = s.tracerColor[1];
            config_["modules"][name]["tracerColor"]["b"] = s.tracerColor[2];
            config_["modules"][name]["tracerColor"]["a"] = s.tracerColor[3];
        } else if (name == "Trajectories" && trajectories) {
            const auto& s = trajectories->settings;
            config_["modules"][name]["thickness"] = s.thickness;
            config_["modules"][name]["lineColor"]["r"] = s.lineColor[0];
            config_["modules"][name]["lineColor"]["g"] = s.lineColor[1];
            config_["modules"][name]["lineColor"]["b"] = s.lineColor[2];
            config_["modules"][name]["lineColor"]["a"] = s.lineColor[3];
            // Projectile toggles
            config_["modules"][name]["showBow"] = s.showBow;
            config_["modules"][name]["showEnderPearl"] = s.showEnderPearl;
            config_["modules"][name]["showEgg"] = s.showEgg;
            config_["modules"][name]["showSnowball"] = s.showSnowball;
            config_["modules"][name]["showPotionHealing"] = s.showPotionHealing;
            config_["modules"][name]["showPotionPoison"] = s.showPotionPoison;
            config_["modules"][name]["showPotionSlowness"] = s.showPotionSlowness;
            config_["modules"][name]["showExpBottle"] = s.showExpBottle;
        } else if (name == "NoItemRelease" && noItemRelease) {
            config_["modules"][name]["mode"] = static_cast<int>(noItemRelease->settings.mode);
        } else if (name == "AltManager" && altManager) {
            config_["modules"][name]["spoofedName"] = std::string(altManager->settings.spoofedName);
        } else if (name == "FastPlace" && fastPlace) {
            config_["modules"][name]["blocksOnly"] = fastPlace->settings.blocksOnly;
        } else if (name == "Clutch" && clutch) {
            const auto& s = clutch->settings;
            config_["modules"][name]["predictionMs"] = s.predictionMs;
            config_["modules"][name]["searchRadius"] = s.searchRadius;
            config_["modules"][name]["reach"] = s.reach;
            config_["modules"][name]["placeDelayMs"] = s.placeDelayMs;
            config_["modules"][name]["restoreSlot"] = s.restoreSlot;
        } else if (name == "Breaker" && breaker) {
            const auto& s = breaker->settings;
            config_["modules"][name]["range"] = s.range;
            config_["modules"][name]["legit"] = s.legit;
        } else if (name == "Auto Tool" && autoTool) {
            const auto& s = autoTool->settings;
            config_["modules"][name]["switchBack"] = s.switchBack;
            config_["modules"][name]["autoSword"] = s.autoSword;
        } else if (name == "NoHitDelay" && noHitDelay) {
            config_["modules"][name]["weaponOnly"] = noHitDelay->settings.weaponOnly;
        } else if (name == "Criticals" && criticals) {
            const auto& s = criticals->settings;
            config_["modules"][name]["maxDelayMs"] = s.maxDelayMs;
            config_["modules"][name]["chance"] = s.chance;
            config_["modules"][name]["onlyWhileAttacking"] = s.onlyWhileAttacking;
            config_["modules"][name]["lookingAtPlayer"] = s.lookingAtPlayer;
            config_["modules"][name]["onlyWithSword"] = s.onlyWithSword;
        } else if (name == "WTap" && wTap) {
            const auto& s = wTap->settings;
            config_["modules"][name]["mode"] = static_cast<int>(s.mode);
            config_["modules"][name]["chance"] = s.chance;
            config_["modules"][name]["delayMs"] = s.delayMs;
            config_["modules"][name]["releaseMs"] = s.releaseMs;
            config_["modules"][name]["targetOnly"] = s.targetOnly;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
        } else if (name == "Backtrack" && backtrack) {
            const auto& s = backtrack->settings;
            config_["modules"][name]["mode"] = static_cast<int>(s.mode);
            config_["modules"][name]["ticks"] = s.ticks;
            config_["modules"][name]["latency"] = s.latency;
            config_["modules"][name]["renderPreviousTicks"] = s.renderPreviousTicks;
            config_["modules"][name]["renderServerPos"] = s.renderServerPos;
            config_["modules"][name]["minDistance"] = s.minDistance;
            config_["modules"][name]["maxDistance"] = s.maxDistance;
            config_["modules"][name]["maxHurtTime"] = s.maxHurtTime;
            config_["modules"][name]["cooldown"] = s.cooldown;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
            config_["modules"][name]["showBacktrackBox"] = s.showBacktrackBox;
            config_["modules"][name]["filledBox"] = s.filledBox;
            config_["modules"][name]["boxColor"]["r"] = s.boxColor[0];
            config_["modules"][name]["boxColor"]["g"] = s.boxColor[1];
            config_["modules"][name]["boxColor"]["b"] = s.boxColor[2];
            config_["modules"][name]["boxColor"]["a"] = s.boxColor[3];
            config_["modules"][name]["shadowColor"]["r"] = s.shadowColor[0];
            config_["modules"][name]["shadowColor"]["g"] = s.shadowColor[1];
            config_["modules"][name]["shadowColor"]["b"] = s.shadowColor[2];
            config_["modules"][name]["shadowColor"]["a"] = s.shadowColor[3];
        } else if (name == "Blink" && blink) {
            const auto& s = blink->settings;
            config_["modules"][name]["timerLimit"] = s.timerLimit;
            config_["modules"][name]["showTimerHud"] = s.showTimerHud;
            config_["modules"][name]["showBreadcrumbs"] = s.showBreadcrumbs;
            config_["modules"][name]["blinkColor"]["r"] = s.blinkColor[0];
            config_["modules"][name]["blinkColor"]["g"] = s.blinkColor[1];
            config_["modules"][name]["blinkColor"]["b"] = s.blinkColor[2];
            config_["modules"][name]["blinkColor"]["a"] = s.blinkColor[3];
        } else if (name == "FakeLag" && fakeLag) {
            const auto& s = fakeLag->settings;
            config_["modules"][name]["mode"] = static_cast<int>(s.mode);
            config_["modules"][name]["minDelayMs"] = s.minDelayMs;
            config_["modules"][name]["maxDelayMs"] = s.maxDelayMs;
            config_["modules"][name]["pulseHoldMs"] = s.pulseHoldMs;
            config_["modules"][name]["pulseReleaseMs"] = s.pulseReleaseMs;
            config_["modules"][name]["maxDistance"] = s.maxDistance;
            config_["modules"][name]["adaptiveStrength"] = s.adaptiveStrength;
            config_["modules"][name]["maxPackets"] = s.maxPackets;
            config_["modules"][name]["releaseRate"] = s.releaseRate;
            config_["modules"][name]["movingOnly"] = s.movingOnly;
            config_["modules"][name]["smoothRelease"] = s.smoothRelease;
            config_["modules"][name]["flushOnAttack"] = s.flushOnAttack;
            config_["modules"][name]["flushOnDamage"] = s.flushOnDamage;
            config_["modules"][name]["flushOnItemUse"] = s.flushOnItemUse;
            config_["modules"][name]["renderServerPlayer"] = s.renderServerPlayer;
            config_["modules"][name]["filledRender"] = s.filledRender;
            config_["modules"][name]["renderLine"] = s.renderLine;
            config_["modules"][name]["showStatus"] = s.showStatus;
            config_["modules"][name]["color"]["r"] = s.color[0];
            config_["modules"][name]["color"]["g"] = s.color[1];
            config_["modules"][name]["color"]["b"] = s.color[2];
            config_["modules"][name]["color"]["a"] = s.color[3];
        } else if (name == "NoSlowDown" && noSlowDown) {
            const auto& s = *noSlowDown;
            config_["modules"][name]["packetMode"] = static_cast<int>(s.packetMode);
            config_["modules"][name]["blockForward"] = s.blockForward;
            config_["modules"][name]["blockStrafe"] = s.blockStrafe;
            config_["modules"][name]["consumeForward"] = s.consumeForward;
            config_["modules"][name]["consumeStrafe"] = s.consumeStrafe;
            config_["modules"][name]["bowForward"] = s.bowForward;
            config_["modules"][name]["bowStrafe"] = s.bowStrafe;
        } else if (name == "Timer" && timerModule) {
            const auto& s = timerModule->settings;
            config_["modules"][name]["speed"] = (float)s.speed;
            config_["modules"][name]["mousePressed"] = s.mousePressed;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
        } else if (name == "TimerRange" && timerRange) {
            const auto& s = timerRange->settings;
            config_["modules"][name]["mode"] = static_cast<int>(s.mode);
            config_["modules"][name]["minSpeed"] = s.minSpeed;
            config_["modules"][name]["maxSpeed"] = s.maxSpeed;
            config_["modules"][name]["offsetMs"] = s.offsetMs;
            config_["modules"][name]["durationMs"] = s.durationMs;
            config_["modules"][name]["targetOnly"] = s.targetOnly;
            config_["modules"][name]["weaponOnly"] = s.weaponOnly;
        } else if (name == "DroppedInfo" && droppedInfo) {
            const auto& s = droppedInfo->settings;
            config_["modules"][name]["x"] = s.x;
            config_["modules"][name]["y"] = s.y;
            config_["modules"][name]["scale"] = s.scale;
            config_["modules"][name]["background"] = s.background;
        } else if (name == "InvManager" && inventoryManager) {
            config_["modules"][name]["swapDelay"] = inventoryManager->settings.swapDelay;
            config_["modules"][name]["retryOnFail"] = inventoryManager->settings.retryOnFail;
            config_["modules"][name]["activePreset"] = inventoryManager->settings.activePreset;
            for (int p = 0; p < InventoryManager::MAX_PRESETS; p++) {
                std::string presetKey = "p" + std::to_string(p);
                config_["modules"][name]["presets"][presetKey]["saved"] = inventoryManager->presets[p].saved;
                for (int i = 0; i < 9; i++) {
                    std::string slotKey = std::to_string(i);
                    config_["modules"][name]["presets"][presetKey][slotKey]["id"] =
                        inventoryManager->presets[p].slots[i].itemId;
                    config_["modules"][name]["presets"][presetKey][slotKey]["dmg"] =
                        inventoryManager->presets[p].slots[i].damage;
                }
            }
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
