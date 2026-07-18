#include "aimbow.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>

AimBow* aimBow = nullptr;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;
constexpr double kArrowSpeed = 3.0;   // fully-charged arrow speed (blocks/tick)
constexpr double kGravity = 0.05;     // arrow gravity (blocks/tick^2)
constexpr int kBowItemId = 261;       // Minecraft 1.8 bow

// Where posY sits on a Player varies by version:
//   1.7.10: posY is at EYE level (~1.62 above feet) because EntityPlayer sets
//           yOffset=1.62 in its ctor. So feet = posY-1.62, neck ≈ posY-0.12.
//   1.8+:   posY is at FEET (yOffset removed). Neck ≈ posY+1.5.
// AimAssist confirms this: it builds the target hitbox as [posY-1.6, posY+0.2]
// which only makes sense with posY at eye/head level.
// aimHeightAbovePosY() returns the offset to add to the target's posY so we
// end up aiming near the neck regardless of MC version.
inline double aimHeightAbovePosY() {
    return (Java::Version == MinecraftVersion::MC_1_7_10) ? -0.12 : 1.50;
}
}  // namespace

AimBow::AimBow() : Module("AimBow") {
    keybind = 0;
}

void AimBow::onEnable() {
    chargeStartMs = 0;
    wasDrawing = false;
    hasTarget = false;
    hasOverride = false;
}

void AimBow::onDisable() {
    wasDrawing = false;
    hasTarget = false;
    hasOverride = false;
}

float AimBow::wrapDeg(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f)
        a += 360.0f;
    return a - 180.0f;
}

std::string AimBow::getDisplayInfo() {
    return hasTarget ? "locked" : "";
}

void AimBow::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        wasDrawing = false;
        hasTarget = false;
        hasOverride = false;
        return;
    }

    // Armor filter keybind: toggle the filter on/off on a key press (edge).
    if (settings.armorFilterKeybind != 0) {
        bool keyDown = (GetAsyncKeyState(settings.armorFilterKeybind) & 0x8000) != 0;
        if (keyDown && !armorFilterKeyPrev_) {
            settings.armorFilterEnabled = !settings.armorFilterEnabled;
            NotificationManager::add("Bow Armor Filter", settings.armorFilterEnabled);
        }
        armorFilterKeyPrev_ = keyDown;
    }

    // Must be holding a bow.
    const int heldId = player.getHeldItemId();
    if (heldId != kBowItemId) {
        wasDrawing = false;
        hasTarget = false;
        hasOverride = false;
        static int s_lastLoggedHeld = -1;
        if (s_lastLoggedHeld != heldId) {
            s_lastLoggedHeld = heldId;
            log_ok("[AimBow] bail: not holding bow (heldId=%d, expected=%d)", heldId, kBowItemId);
        }
        return;
    }

    // "Drawing" = right mouse held (charging the bow).
    const bool drawing = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const unsigned long now = GetTickCount();
    if (drawing && !wasDrawing)
        chargeStartMs = now;
    wasDrawing = drawing;

    if (settings.onlyWhileDrawing && !drawing) {
        hasTarget = false;
        hasOverride = false;
        return;
    }

    // Always solve the arc for a FULL-charge shot. Solving for the instantaneous
    // charge makes the crosshair lob up and then settle as the bow charges (the
    // "goes up then comes down" behaviour). Assuming full charge keeps the aim
    // stable and points where a properly-charged shot actually lands.
    (void)chargeStartMs;
    const double v = kArrowSpeed;

    // Local eye position.
    const double lx = player.getPosX();
    const double ly = player.getPosY() + (player.isSneaking() ? 1.54 : 1.62);
    const double lz = player.getPosZ();
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();

    // Track user's manual crosshair rotation
    if (wasDrawing && hasOverride) {
        float deltaYaw = wrapDeg(curYaw - lastOverrideYaw);
        float deltaPitch = curPitch - lastOverridePitch;

        manualYaw = wrapDeg(manualYaw + deltaYaw);
        manualPitch = std::clamp(manualPitch + deltaPitch, -90.0f, 90.0f);
    } else {
        manualYaw = curYaw;
        manualPitch = curPitch;
        hasOverride = false;
    }

    // Pick the valid player closest to our current aim (smallest yaw delta) in range/FOV.
    std::vector<PlayerInfo> players = mc.getPlayerList();
    if (friendsManager) {
        friendsManager->resolvePending(players);
    }
    const PlayerInfo* best = nullptr;
    float bestAngle = (float)settings.fov * 0.5f;
    double bestDistSq = (double)settings.range * settings.range;

    float referenceYaw = curYaw;
    if (settings.mode == AimMode::ClosestToCrosshair) {
        referenceYaw = manualYaw;
    }

    for (const auto& p : players) {
        if (p.isNPC)
            continue;
        if (!settings.targetInvisible && p.isInvisible)
            continue;

        if (friendsManager) {
            // Friend verification: never target friends
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName)
                continue;

            // Teammate verification: skip if targeting teammates is disabled
            if (!settings.targetTeammates) {
                if (p.isOnLocalTeam)
                    continue;

                bool isApolloTeam = !p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid);
                if (isApolloTeam)
                    continue;

                bool isApolloNametag = !p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid);
                if (isApolloNametag)
                    continue;

                // Scoreboard teams & color matching
                static int localTeamColor = -1;
                static unsigned long lastColorRefresh = 0;
                unsigned long nowTick = GetTickCount();
                if (nowTick - lastColorRefresh > 1000) {
                    localTeamColor = mc.getLocalPlayerTeamColor();
                    lastColorRefresh = nowTick;
                }

                ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
                ChatColor localColor = static_cast<ChatColor>(localTeamColor);
                static std::string localPlayerName;
                static unsigned long lastNameRefresh = 0;
                if (localPlayerName.empty() || nowTick - lastNameRefresh > 1000) {
                    localPlayerName = mc.getLocalPlayerName();
                    lastNameRefresh = nowTick;
                }
                bool isScoreboardTeammate = false;
                if (playerColor != ChatColor::WHITE && playerColor != ChatColor::GRAY && playerColor != ChatColor::UNKNOWN) {
                    isScoreboardTeammate = friendsManager->isScoreboardTeammate(localPlayerName,
                                                                         p.profileName.empty() ? p.name : p.profileName);
                }
                isScoreboardTeammate = isScoreboardTeammate || friendsManager->isTeammate(p.uuid, playerColor, localColor);
                if (isScoreboardTeammate)
                    continue;
            }
        } else {
            if (!settings.targetTeammates && p.isOnLocalTeam)
                continue;
        }

        if (settings.onlyVisible && !mc.canEntityBeSeen(p.instance))
            continue;

        // Armor-tier ("class") filter: only aim at players wearing a checked tier.
        if (settings.armorFilterEnabled && settings.armorTierMask != 0) {
            auto tierBit = [](int id) -> int {
                if (id >= 298 && id <= 301) return ARMOR_LEATHER;
                if (id >= 302 && id <= 305) return ARMOR_CHAIN;
                if (id >= 306 && id <= 309) return ARMOR_IRON;
                if (id >= 310 && id <= 313) return ARMOR_DIAMOND;
                if (id >= 314 && id <= 317) return ARMOR_GOLD;
                return 0;
            };
            bool match = false;
            bool hasAnyArmor = false;
            for (const auto& piece : p.armor) {
                if (piece.id > 0) {
                    hasAnyArmor = true;
                    int b = tierBit(piece.id);
                    if (b && (settings.armorTierMask & b)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match && (settings.armorTierMask & ARMOR_NO_ARMOR) && !hasAnyArmor)
                match = true;
            if (!match)
                continue;
        }

        double dx = p.posX - lx;
        double dz = p.posZ - lz;
        double distSq = dx * dx + dz * dz;
        if (distSq > bestDistSq)
            continue;

        float yawTo = (float)(std::atan2(-dx, dz) * kDeg);
        float yawDelta = std::fabs(wrapDeg(yawTo - referenceYaw));
        if (yawDelta > bestAngle)
            continue;

        bestAngle = yawDelta;
        best = &p;
    }

    // Copy the target's data, then release EVERY player GlobalRef that
    // getPlayerList() allocated (one per player). These are NOT auto-freed —
    // leaking them every tick overflows the JVM global-ref table and crashes.
    bool found = best != nullptr;
    double tX = 0, tY = 0, tZ = 0, tLX = 0, tLY = 0, tLZ = 0;
    if (found) {
        tX = best->posX;
        tY = best->posY;
        tZ = best->posZ;
        tLX = best->lastPosX;
        tLY = best->lastPosY;
        tLZ = best->lastPosZ;
    }
    if (JNIEnv* env = Java::Env()) {
        for (auto& pp : players) {
            if (pp.instance) {
                env->DeleteGlobalRef(pp.instance);
                pp.instance = nullptr;
            }
        }
    }

    if (!found) {
        hasTarget = false;
        hasOverride = false;
        return;
    }
    hasTarget = true;

    // Throttled diagnostic — the logger's printf doesn't handle %f, so we
    // scale by 100 and print ints (so tY=64.12 shows as tY_x100=6412).
    static unsigned long s_lastAimLog = 0;
    const unsigned long nowTickLog = GetTickCount();
    if (nowTickLog - s_lastAimLog > 2000) {
        s_lastAimLog = nowTickLog;
        log_ok("[AimBow] tY_x100=%d tLY_x100=%d localY_x100=%d eyeY_x100=%d aimY_x100=%d kAimBody_x100=%d",
               (int)(tY * 100.0), (int)(tLY * 100.0),
               (int)(player.getPosY() * 100.0), (int)(ly * 100.0),
               (int)((tY + aimHeightAbovePosY()) * 100.0), (int)(aimHeightAbovePosY() * 100.0));
    }

    // Movement prediction (horizontal only — vertical lead jerks the camera).
    // Raw per-tick deltas are 0 on most ticks and spike when the server sends a
    // position update, which makes naive prediction useless. Reject teleport/
    // target-switch spikes and EMA the rest so the lead is smooth and accurate:
    // motion frames pull the estimate faster, idle frames decay it slowly (so it
    // persists between the server's sparse updates).
    {
        const double rawVx = tX - tLX;
        const double rawVz = tZ - tLZ;
        if (std::fabs(rawVx) > 1.5 || std::fabs(rawVz) > 1.5) {
            smoothVx_ = 0.0;  // teleport / switch / server correction — don't lead on it
            smoothVz_ = 0.0;
        } else {
            const double a = (std::fabs(rawVx) + std::fabs(rawVz) > 0.002) ? 0.35 : 0.06;
            smoothVx_ += (rawVx - smoothVx_) * a;
            smoothVz_ += (rawVz - smoothVz_) * a;
        }
    }
    const double vx = smoothVx_;
    const double vz = smoothVz_;
    const double pstr = settings.predict ? (double)settings.predictStrength : 0.0;

    // Iteratively solve: predict target position at time-of-flight, then re-solve
    // the ballistic arc, a few passes to converge.
    const double aimBodyOffset = aimHeightAbovePosY();
    double aimX = tX, aimY = tY + aimBodyOffset, aimZ = tZ;
    double theta = 0.0;  // launch elevation (radians, + = up)
    for (int iter = 0; iter < 4; ++iter) {
        double dx = aimX - lx;
        double dz = aimZ - lz;
        double dy = aimY - ly;
        double d = std::sqrt(dx * dx + dz * dz);

        double v2 = v * v;
        double root = v2 * v2 - kGravity * (kGravity * d * d + 2.0 * dy * v2);
        if (root < 0.0 || d < 0.5) {
            // Out of ballistic reach (or too close): aim straight at the point.
            theta = std::atan2(dy, d);
        } else {
            double tanTheta = (v2 - std::sqrt(root)) / (kGravity * d);
            theta = std::atan(tanTheta);
        }

        if (pstr <= 0.0)
            break;

        double horizV = v * std::cos(theta);
        double tof = horizV > 0.01 ? d / horizV : 0.0;  // ticks
        aimX = tX + vx * tof * pstr;
        aimY = tY + aimBodyOffset;  // no vertical lead
        aimZ = tZ + vz * tof * pstr;
    }

    double fdx = aimX - lx;
    double fdz = aimZ - lz;
    float targetYaw = (float)(std::atan2(-fdx, fdz) * kDeg);
    float targetPitch = (float)(-theta * kDeg);
    targetPitch = std::clamp(targetPitch, -90.0f, 90.0f);

    // Smooth toward the solution. Ease out as the crosshair closes in so it
    // settles instead of snapping/jittering (that's the "gaga" stiffness).
    float yawErr = wrapDeg(targetYaw - curYaw);
    float pitchErr = targetPitch - curPitch;
    float base = std::clamp(settings.speed / 100.0f, 0.02f, 1.0f);
    float mag = std::max(std::fabs(yawErr), std::fabs(pitchErr));
    float ease = std::clamp(mag / 22.0f, 0.08f, 1.0f);  // wide slow-down zone -> buttery settle
    float step = base * ease;

    float newYaw = curYaw + yawErr * step;
    float newPitch = std::clamp(curPitch + pitchErr * step, -90.0f, 90.0f);

    // Set yaw/pitch AND their prev values, but set prev slightly BEHIND the new
    // value (lerp) so the render interpolates smoothly instead of teleporting
    // each tick. Setting prev == new is what made it look stiff/janky.
    player.setYaw(newYaw);
    player.setPrevYaw(std::lerp(curYaw, newYaw, 0.85f));
    player.setPitch(newPitch);
    player.setPrevPitch(std::lerp(curPitch, newPitch, 0.85f));

    lastOverrideYaw = newYaw;
    lastOverridePitch = newPitch;
    hasOverride = true;
}

void AimBow::onRenderMenu() {
    const char* bowModes[] = {"Automatic", "Closest to Crosshair"};
    int currentBowMode = static_cast<int>(settings.mode);
    bool enabledValue = enabled;

    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ui::SliderFloat("Range", &settings.range, 5.0f, 120.0f, "%.0f blocks");
            ui::SliderInt("FOV", &settings.fov, 10, 180, "%d");
            ui::SliderFloat("Speed", &settings.speed, 1.0f, 100.0f, "%.0f%%");
        },
        "Targeting",
        [this, &currentBowMode, &bowModes]() {
            ui::Checkbox("Predict Movement", &settings.predict);
            if (settings.predict)
                ui::SliderFloat("Strength", &settings.predictStrength, 0.0f, 2.0f, "%.2fx");
            ui::Checkbox("Only While Drawing", &settings.onlyWhileDrawing);
            ui::Checkbox("Only Visible", &settings.onlyVisible);
            ui::Checkbox("Target Invisibles", &settings.targetInvisible);
            ui::Checkbox("Target Teammates", &settings.targetTeammates);

            if (ui::Combo("Aim Mode", &currentBowMode, bowModes, IM_ARRAYSIZE(bowModes))) {
                settings.mode = static_cast<AimMode>(currentBowMode);
            }

            // Armor-tier ("class") filter — only aim at these. None checked = all.
            ImGui::Spacing();
            ui::Checkbox("Armor Filter", &settings.armorFilterEnabled);
            ui::Binder("Armor Filter Key", &settings.armorFilterKeybind);
            auto tierCheckbox = [this](const char* label, int bit) {
                bool on = (settings.armorTierMask & bit) != 0;
                if (ui::Checkbox(label, &on)) {
                    if (on)
                        settings.armorTierMask |= bit;
                    else
                        settings.armorTierMask &= ~bit;
                }
            };
            tierCheckbox("No Armor", ARMOR_NO_ARMOR);
            tierCheckbox("Leather", ARMOR_LEATHER);
            tierCheckbox("Chainmail", ARMOR_CHAIN);
            tierCheckbox("Iron", ARMOR_IRON);
            tierCheckbox("Gold", ARMOR_GOLD);
            tierCheckbox("Diamond", ARMOR_DIAMOND);
            if (settings.armorTierMask == 0)
                ImGui::TextDisabled("(targeting everyone)");
        });
}

void AimBow::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["range"] = settings.range;
    node["fov"] = settings.fov;
    node["speed"] = settings.speed;
    node["predict"] = settings.predict;
    node["predictStrength"] = settings.predictStrength;
    node["targetInvisible"] = settings.targetInvisible;
    node["targetTeammates"] = settings.targetTeammates;
    node["onlyWhileDrawing"] = settings.onlyWhileDrawing;
    node["onlyVisible"] = settings.onlyVisible;
    node["armorTierMask"] = settings.armorTierMask;
    node["armorFilterEnabled"] = settings.armorFilterEnabled;
    node["armorFilterKeybind"] = settings.armorFilterKeybind;
}

void AimBow::loadSettings(const json::Value& node) {
    if (node.has("mode")) {
        settings.mode = static_cast<AimMode>(std::clamp(node["mode"].as_int(static_cast<int>(settings.mode)), 0, 1));
    }
    settings.range = std::clamp(node["range"].as_float(settings.range), 5.0f, 120.0f);
    settings.fov = std::clamp(node["fov"].as_int(settings.fov), 10, 180);
    settings.speed = std::clamp(node["speed"].as_float(settings.speed), 1.0f, 100.0f);
    settings.predict = node["predict"].as_bool(settings.predict);
    settings.predictStrength = std::clamp(node["predictStrength"].as_float(settings.predictStrength), 0.0f, 2.0f);
    settings.targetInvisible = node["targetInvisible"].as_bool(settings.targetInvisible);
    settings.targetTeammates = node["targetTeammates"].as_bool(settings.targetTeammates);
    settings.onlyWhileDrawing = node["onlyWhileDrawing"].as_bool(settings.onlyWhileDrawing);
    settings.onlyVisible = node["onlyVisible"].as_bool(settings.onlyVisible);
    settings.armorTierMask = node["armorTierMask"].as_int(settings.armorTierMask);
    settings.armorFilterEnabled = node["armorFilterEnabled"].as_bool(settings.armorFilterEnabled);
    settings.armorFilterKeybind = node["armorFilterKeybind"].as_int(settings.armorFilterKeybind);
}
