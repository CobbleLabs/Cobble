#include "piercing.h"
#include "antibot.h"
#include "backtrack.h"

#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"
#include "../../util/diagnostics.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

Piercing* piercing = nullptr;

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double EPSILON = 1.0e-8;
constexpr std::uint64_t DUPLICATE_PACKET_GUARD_MS = 250;
// Cap through-hitbox attacks to a human cadence (~5-6/sec) so the C0A swing
// packet stream isn't a regular high-rate pulse Watchdog's "Post animation"
// check flags. Also add ±40 ms jitter (see handleAttackInput) so the interval
// isn't a perfect 180 ms spike.
constexpr std::uint64_t ATTACK_MIN_GAP_MS = 180;
constexpr std::uint64_t ATTACK_JITTER_MS = 40;

bool isWeapon(int itemId) {
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
           itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286;
}

struct Box {
    double minX, minY, minZ, maxX, maxY, maxZ;
};

// Hitbox to ray-test a candidate against: the real AABB when the server has
// synced one, a standing/sneaking approximation otherwise, grown by `expand`.
Box hitboxOf(const PlayerInfo& candidate, double expand) {
    Box box{candidate.hitboxMinX, candidate.hitboxMinY, candidate.hitboxMinZ,
            candidate.hitboxMaxX, candidate.hitboxMaxY, candidate.hitboxMaxZ};

    if (box.maxX <= box.minX || box.maxY <= box.minY || box.maxZ <= box.minZ) {
        box.minX = candidate.posX - 0.3;
        box.maxX = candidate.posX + 0.3;
        box.minY = candidate.posY;
        box.maxY = candidate.posY + (candidate.isSneaking ? 1.62 : 1.8);
        box.minZ = candidate.posZ - 0.3;
        box.maxZ = candidate.posZ + 0.3;
    }

    if (expand > 0.0) {
        box.minX -= expand;
        box.minY -= expand;
        box.minZ -= expand;
        box.maxX += expand;
        box.maxY += expand;
        box.maxZ += expand;
    }
    return box;
}

bool isAttackOrSwingPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return false;

    jclass packetClass = env->GetObjectClass(packet);
    jclass classClass = packetClass ? env->GetObjectClass(packetClass) : nullptr;
    jmethodID getName =
        classClass ? env->GetMethodID(classClass, OBFUSCATE_STRING("getName").c_str(),
                                      OBFUSCATE_STRING("()Ljava/lang/String;").c_str())
                   : nullptr;
    jstring name = getName ? static_cast<jstring>(env->CallObjectMethod(packetClass, getName)) : nullptr;

    bool result = false;
    if (name && !env->ExceptionCheck()) {
        const char* chars = env->GetStringUTFChars(name, nullptr);
        if (chars) {
            result = std::strstr(chars, OBFUSCATE_STRING("C02PacketUseEntity").c_str()) != nullptr ||
                     std::strstr(chars, OBFUSCATE_STRING("C0APacketAnimation").c_str()) != nullptr;
            env->ReleaseStringUTFChars(name, chars);
        }
    }

    if (name)
        env->DeleteLocalRef(name);
    if (classClass)
        env->DeleteLocalRef(classClass);
    if (packetClass)
        env->DeleteLocalRef(packetClass);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return result;
}
}  // namespace

Piercing::Piercing() : Module("Piercing") {
    keybind = 0;
}

void Piercing::onEnable() {
    manualAttackActive_.store(false, std::memory_order_release);
    suppressDuplicatePacketsUntilMs_.store(0, std::memory_order_release);
}

void Piercing::onDisable() {
    manualAttackActive_.store(false, std::memory_order_release);
    suppressDuplicatePacketsUntilMs_.store(0, std::memory_order_release);
}

std::uint64_t Piercing::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

bool Piercing::rayIntersectsBox(double originX, double originY, double originZ,
                                double directionX, double directionY, double directionZ,
                                double minX, double minY, double minZ,
                                double maxX, double maxY, double maxZ,
                                double maxDistance, double& hitDistance) {
    double entry = 0.0;
    double exit = maxDistance;

    const double origins[3] = {originX, originY, originZ};
    const double directions[3] = {directionX, directionY, directionZ};
    const double minimums[3] = {minX, minY, minZ};
    const double maximums[3] = {maxX, maxY, maxZ};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) < EPSILON) {
            if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                return false;
            continue;
        }

        double first = (minimums[axis] - origins[axis]) / directions[axis];
        double second = (maximums[axis] - origins[axis]) / directions[axis];
        if (first > second)
            std::swap(first, second);

        entry = std::max(entry, first);
        exit = std::min(exit, second);
        if (entry > exit)
            return false;
    }

    if (exit < 0.0 || entry > maxDistance)
        return false;

    hitDistance = std::max(0.0, entry);
    return true;
}

bool Piercing::isFriendly(const PlayerInfo& candidate, const std::string& localName,
                          ChatColor localTeamColor) const {
    if (!friendsManager)
        return false;

    if (settings.ignoreFriends) {
        if (!candidate.uuid.empty() && friendsManager->isFriendByUUID(candidate.uuid))
            return true;
        // Legacy name-based match — kicks in for friends added by name whose UUID
        // hasn't been resolved yet (first seconds after login).
        if (friendsManager->isFriend(FriendsManager::stripColorCodes(candidate.name)))
            return true;
    }

    if (settings.ignoreTeams) {
        // Vanilla /team API (isOnSameTeam JNI check), if Teams honours it.
        if (candidate.isOnLocalTeam && friendsManager->honorsVanillaTeams())
            return true;
        // Apollo + Lunar API teammates (auto-detected, not manual friends).
        if (!candidate.uuid.empty() && (friendsManager->isApolloTeammate(candidate.uuid) ||
                                        friendsManager->isLunarTeammate(candidate.uuid)))
            return true;
        // Scoreboard prefix/suffix teammates (color-independent, e.g. the
        // "TEAM_RED" style Bedwars puts every teammate under).
        const std::string& scoreboardName =
            candidate.profileName.empty() ? candidate.name : candidate.profileName;
        if (friendsManager->isScoreboardTeammate(localName, scoreboardName))
            return true;
        // Same chat-color tag as us → treat as teammate.
        if (friendsManager->isTeammate(candidate.uuid, static_cast<ChatColor>(candidate.teamColor),
                                       localTeamColor))
            return true;
    }

    return false;
}

Piercing::TargetPick Piercing::selectTarget(bool hiddenOnly) {
    TargetPick pick;

    JNIEnv* env = Java::Env();
    if (!env)
        return pick;

    if (!enabled || !shouldUpdate() || Menu::isOpen || mc.isInGui() || !player.isValid() || !mc.instance)
        return pick;

    if (settings.weaponsOnly && !isWeapon(player.getHeldItemId()))
        return pick;

    if (hiddenOnly && !settings.throughWalls)
        return pick;

    if (env->ExceptionCheck())
        env->ExceptionClear();

    const double yaw = static_cast<double>(player.getYaw()) * PI / 180.0;
    const double pitch = static_cast<double>(player.getPitch()) * PI / 180.0;
    const double cosPitch = std::cos(pitch);
    const double directionX = -std::sin(yaw) * cosPitch;
    const double directionY = -std::sin(pitch);
    const double directionZ = std::cos(yaw) * cosPitch;

    const double originX = player.getPosX();
    const double originY = player.getPosY() + (player.isSneaking() ? 1.54 : 1.62);
    const double originZ = player.getPosZ();

    auto players = mc.getPlayerList();

    // Resolve any pending friend name → UUID mappings before we filter — the
    // by-UUID checks below depend on it, so friends added by name still match.
    if (friendsManager)
        friendsManager->resolvePending(players);

    // Read local player name + team color once per call. Piercing::selectTarget
    // runs on two threads (module tick + WM_LBUTTONDOWN via handleAttackInput),
    // so any per-call cached-across-calls state races and corrupts. Both JNI
    // reads are ~5 calls each and only hit once per invocation → negligible.
    const std::string localName = mc.getLocalPlayerName();
    const ChatColor localTeamColor = static_cast<ChatColor>(mc.getLocalPlayerTeamColor());

    const double expandVal = static_cast<double>(settings.expand);
    const double visibleRange = static_cast<double>(settings.distance);
    const double hiddenRange = static_cast<double>(settings.throughWallsRange);
    const double pierceRange = settings.pierceTeams ? static_cast<double>(settings.pierceRange) : 0.0;
    // Widest reach any pass can grant. The friendly sweep has to see every body
    // that could be shielding somebody inside that reach, so it scans this far
    // regardless of which range the winning candidate ends up using.
    const double scanRange = std::max({visibleRange, hiddenRange, pierceRange});

    // ---- Pass 1: friends and teammates -------------------------------------
    // Two things come out of this: the ID list the crosshair guard matches
    // against, and how far away the nearest off-limits body sits. The latter is
    // what makes a farther candidate count as "pierced through" rather than as
    // an ordinary out-of-reach target.
    double nearestFriendlyHit = std::numeric_limits<double>::max();
    for (auto& candidate : players) {
        if (!candidate.instance || AntiBot::Filtered(candidate))
            continue;
        if (!isFriendly(candidate, localName, localTeamColor))
            continue;

        pick.friendlyIds.push_back(candidate.entityId);

        const Box box = hitboxOf(candidate, expandVal);
        double friendlyHit = 0.0;
        if (rayIntersectsBox(originX, originY, originZ,
                             directionX, directionY, directionZ,
                             box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ,
                             scanRange, friendlyHit))
            nearestFriendlyHit = std::min(nearestFriendlyHit, friendlyHit);
    }
    const bool friendlyInRay = nearestFriendlyHit < std::numeric_limits<double>::max();

    // ---- Pass 2: attackable candidates -------------------------------------
    PlayerInfo* selectedCandidate = nullptr;
    double closestHit = std::numeric_limits<double>::max();

    Backtrack::HeldTargetSnapshot backtrackSnapshot;
    if (!hiddenOnly && settings.backtrackCheck && backtrack &&
        backtrack->enabled) {
        backtrackSnapshot = backtrack->heldTargetSnapshot();
    }

    for (auto& candidate : players) {
        if (!candidate.instance || AntiBot::Filtered(candidate))
            continue;
        if (candidate.isInvisible && !settings.targetInvisibles)
            continue;
        if (isFriendly(candidate, localName, localTeamColor))
            continue;

        const bool isVisible = mc.canEntityBeSeen(candidate.instance);
        if (hiddenOnly && isVisible)
            continue;
        if (!isVisible && !settings.throughWalls)
            continue;

        const double baseRange = isVisible ? visibleRange : hiddenRange;
        // Extra reach is only on the table while a friend/teammate is in the ray;
        // it exists to cover the depth their body adds, nothing else.
        const double reach =
            (settings.pierceTeams && friendlyInRay) ? std::max(baseRange, pierceRange) : baseRange;

        const Box box = hitboxOf(candidate, expandVal);
        double candidateHit = std::numeric_limits<double>::max();

        if (!hiddenOnly && settings.backtrackCheck &&
            backtrackSnapshot.active && backtrackSnapshot.valid &&
            backtrackSnapshot.pendingPackets > 0 &&
            backtrackSnapshot.entityId == candidate.entityId) {
            const double realX = backtrackSnapshot.x;
            const double realY = backtrackSnapshot.y;
            const double realZ = backtrackSnapshot.z;
            const double realMinX = realX - 0.3 - expandVal;
            const double realMaxX = realX + 0.3 + expandVal;
            const double realMinY = realY - expandVal;
            const double realMaxY =
                realY + (candidate.isSneaking ? 1.62 : 1.8) + expandVal;
            const double realMinZ = realZ - 0.3 - expandVal;
            const double realMaxZ = realZ + 0.3 + expandVal;

            double backtrackHit = 0.0;
            if (rayIntersectsBox(originX, originY, originZ,
                                 directionX, directionY, directionZ,
                                 realMinX, realMinY, realMinZ,
                                 realMaxX, realMaxY, realMaxZ,
                                 reach, backtrackHit)) {
                candidateHit = std::min(candidateHit, backtrackHit);
            }
        }

        double currentHit = 0.0;
        if (rayIntersectsBox(originX, originY, originZ,
                            directionX, directionY, directionZ,
                            box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ,
                            reach, currentHit)) {
            candidateHit = std::min(candidateHit, currentHit);
        }

        if (candidateHit >= std::numeric_limits<double>::max())
            continue;

        // Past the plain reach and *not* behind a friendly body → this is just an
        // out-of-range player, so the pierce budget must not rescue it.
        if (candidateHit > baseRange && candidateHit < nearestFriendlyHit)
            continue;

        if (candidateHit < closestHit) {
            jobject newTarget = env->NewLocalRef(candidate.instance);
            if (newTarget) {
                if (pick.entity)
                    env->DeleteLocalRef(pick.entity);
                pick.entity = newTarget;
                selectedCandidate = &candidate;
                pick.visible = isVisible;
                pick.behindFriendly = friendlyInRay && candidateHit >= nearestFriendlyHit;
                closestHit = candidateHit;
            }
        }
    }

    // Rate-limited pick trace — helps diagnose "why did Piercing hit my friend"
    // by showing what info the filter saw for the selected candidate.
    if (pick.entity && selectedCandidate) {
        static std::atomic<std::uint64_t> s_lastLogMs{0};
        std::uint64_t nowLocal = nowMs();
        std::uint64_t last = s_lastLogMs.load(std::memory_order_relaxed);
        if (nowLocal - last > 1500) {
            s_lastLogMs.store(nowLocal, std::memory_order_relaxed);
            log_debug("[Piercing] pick name=%s prof=%s uuid=%s isOnLocalTeam=%d teamColor=%d "
                      "hidden=%d ignoreF=%d ignoreT=%d friendlies=%zu pierced=%d dist=%.2f",
                      selectedCandidate->name.c_str(), selectedCandidate->profileName.c_str(),
                      selectedCandidate->uuid.empty() ? "?" : selectedCandidate->uuid.substr(0, 8).c_str(),
                      selectedCandidate->isOnLocalTeam ? 1 : 0, selectedCandidate->teamColor,
                      pick.visible ? 0 : 1, settings.ignoreFriends ? 1 : 0,
                      settings.ignoreTeams ? 1 : 0, pick.friendlyIds.size(),
                      pick.behindFriendly ? 1 : 0, closestHit);
        }
    }

    for (auto& candidate : players) {
        if (candidate.instance) {
            env->DeleteGlobalRef(candidate.instance);
            candidate.instance = nullptr;
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (pick.entity) {
            env->DeleteLocalRef(pick.entity);
            pick.entity = nullptr;
        }
    }
    return pick;
}

bool Piercing::applyCrosshairOverride(JNIEnv* env, const TargetPick& pick) {
    if (!env || !mc.instance || !JniCache::fid_objectMouseOver || !JniCache::fid_typeOfHit ||
        !JniCache::fid_entityHit || !JniCache::cls_MovingObjectType || !JniCache::fid_ENTITY)
        return false;

    // Hidden targets are attacked synchronously from the input hook. Leaving
    // them in objectMouseOver would race the renderer and can duplicate hits.
    const bool placeTarget = pick.entity != nullptr && pick.visible;
    const bool guardFriendlies = settings.protectTeams && !pick.friendlyIds.empty();
    if (!placeTarget && !guardFriendlies)
        return false;

    jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    // Does vanilla's own raycast land on somebody we refuse to hit? Matching by
    // entity ID rather than by our ray means a crosshair the two raycasts
    // disagree about (partial-tick interpolation) is still recognised.
    bool crosshairFriendly = false;
    if (guardFriendlies && JniCache::mid_getEntityId) {
        jobject hitEntity = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
        if (hitEntity) {
            const jint hitId = env->CallIntMethod(hitEntity, JniCache::mid_getEntityId);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            } else {
                crosshairFriendly = std::find(pick.friendlyIds.begin(), pick.friendlyIds.end(),
                                              static_cast<int>(hitId)) != pick.friendlyIds.end();
            }
            env->DeleteLocalRef(hitEntity);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    bool blanked = false;

    if (placeTarget) {
        jobject entityType = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_ENTITY);
        if (entityType) {
            const bool typeReady = !env->ExceptionCheck();
            if (typeReady)
                env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, entityType);

            if (typeReady && !env->ExceptionCheck())
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, pick.entity);

            if (typeReady && !env->ExceptionCheck() &&
                JniCache::fid_entityRenderer && JniCache::fid_pointedEntity) {
                jobject renderer = env->GetObjectField(mc.instance, JniCache::fid_entityRenderer);
                if (renderer) {
                    if (!env->ExceptionCheck())
                        env->SetObjectField(renderer, JniCache::fid_pointedEntity, pick.entity);
                    env->DeleteLocalRef(renderer);
                }
            }

            env->DeleteLocalRef(entityType);
        }
    } else if (crosshairFriendly && JniCache::fid_MISS) {
        // A friend/teammate is under the crosshair and there is nobody behind
        // them to pierce to. Blank the hit instead of leaving it: vanilla's MISS
        // branch only swings the item, so the click can't damage them at all.
        jobject missType = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_MISS);
        if (missType) {
            const bool typeReady = !env->ExceptionCheck();
            if (typeReady)
                env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, missType);

            if (typeReady && !env->ExceptionCheck()) {
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, nullptr);
                blanked = !env->ExceptionCheck();
            }

            if (blanked && JniCache::fid_entityRenderer && JniCache::fid_pointedEntity) {
                jobject renderer = env->GetObjectField(mc.instance, JniCache::fid_entityRenderer);
                if (renderer) {
                    if (!env->ExceptionCheck())
                        env->SetObjectField(renderer, JniCache::fid_pointedEntity, nullptr);
                    env->DeleteLocalRef(renderer);
                }
            }

            env->DeleteLocalRef(missType);
        }
    }

    env->DeleteLocalRef(mouseOver);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return blanked;
}

bool Piercing::handleAttackInput() {
    // Avoid heavy JNI work right after a world change — the cache is being
    // re-resolved and Java objects are being GC'd.  Let the click fall
    // through to vanilla instead of blocking the message pump.
    if (Diagnostics::g_inWorldChange.load(std::memory_order_relaxed))
        return false;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    // Pick ANY target in the ray — visible or hidden. selectTarget skips friends
    // and teammates, so "friend in the crosshair with an enemy behind" resolves
    // to the enemy: the friend's ray hit is closer but discarded, and the body
    // farther along the same ray wins (Pierce Teams grants it the reach the
    // friend's own depth costs).
    TargetPick pick = selectTarget(false);

    // Do this on the input thread so the write lands in the same frame as the
    // click about to be processed, rather than whenever the module tick next
    // runs. It either points vanilla at the pierced enemy or blanks a crosshair
    // sitting on a friend.
    const bool blanked = applyCrosshairOverride(env, pick);

    if (!pick.entity) {
        // Swallow the click only when we actually shielded somebody — otherwise
        // this was an ordinary miss and vanilla must handle it normally.
        return blanked;
    }

    // Rate-limit — an external autoclicker sending WM_LBUTTONDOWN at 17 CPS
    // would otherwise spray Piercing attacks at that rate and trip Watchdog's
    // MultiInteractA / BadPacketsE (inhuman use-entity packet cadence).
    const std::uint64_t nowLocal = nowMs();
    const std::uint64_t last = lastAttackMs_.load(std::memory_order_acquire);
    if (last != 0 && nowLocal - last < ATTACK_MIN_GAP_MS) {
        env->DeleteLocalRef(pick.entity);
        return blanked;
    }

    // LWJGL polls mouse state independently from the Windows message. Mark the
    // synchronous attack so its own swing/C02 packets pass, then keep the
    // duplicate-guard window large enough (250 ms) to catch vanilla's next
    // click-loop attack on the same target — otherwise the same input can
    // register a second attack via raw input and MultiInteractA fires.
    manualAttackActive_.store(true, std::memory_order_release);
    const bool attacked = mc.attackEntity(pick.entity);
    if (attacked) {
        suppressDuplicatePacketsUntilMs_.store(nowLocal + DUPLICATE_PACKET_GUARD_MS,
                                               std::memory_order_release);
        mc.setLeftClickCounter(2);
        lastAttackMs_.store(nowLocal, std::memory_order_release);
    }
    manualAttackActive_.store(false, std::memory_order_release);

    env->DeleteLocalRef(pick.entity);
    return attacked || blanked;
}

bool Piercing::shouldSendOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || manualAttackActive_.load(std::memory_order_acquire))
        return true;

    const std::uint64_t deadline = suppressDuplicatePacketsUntilMs_.load(std::memory_order_acquire);
    if (deadline == 0 || nowMs() > deadline)
        return true;

    // A second vanilla pass emits another animation packet and another use-
    // entity attack. Suppress only those packet classes during this short,
    // attack-scoped window; movement and every unrelated packet still pass.
    return !isAttackOrSwingPacket(env, packet);
}

void Piercing::onUpdate() {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Runs unconditionally, not only when a target was found: with no enemy
    // behind them a friend/teammate still has to be lifted out of the crosshair,
    // otherwise vanilla's own raycast keeps them there and the click lands.
    TargetPick pick = selectTarget(false);
    applyCrosshairOverride(env, pick);

    if (pick.entity)
        env->DeleteLocalRef(pick.entity);
}

void Piercing::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ImGui::Spacing();
            ui::SliderFloat("Visible Distance", &settings.distance, 3.0f, 6.0f, "%.2f");
            ui::Checkbox("Through Walls", &settings.throughWalls);
            if (settings.throughWalls) {
                ui::SliderFloat("Wall Distance", &settings.throughWallsRange, 3.0f, 6.0f, "%.2f");
            }
            ImGui::Spacing();
            ui::SliderFloat("Hitbox Expansion", &settings.expand, 0.0f, 1.0f, "%.2f");
            ui::Checkbox("Backtrack Check", &settings.backtrackCheck);
            ui::Checkbox("Weapons Only", &settings.weaponsOnly);
        },
        "Targets",
        [this]() {
            ui::Checkbox("Target Invisibles", &settings.targetInvisibles);
            ui::Checkbox("Ignore Friends", &settings.ignoreFriends);
            ui::Checkbox("Ignore Teams", &settings.ignoreTeams);

            ImGui::Spacing();
            ui::Checkbox("Pierce Teams", &settings.pierceTeams);
            if (settings.pierceTeams)
                ui::SliderFloat("Pierce Range", &settings.pierceRange, 3.0f, 6.0f, "%.2f");
            ui::Checkbox("Protect Teams", &settings.protectTeams);
        });
}

std::string Piercing::getDisplayInfo() {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.2f", settings.distance);
    return buffer;
}



void Piercing::saveSettings(json::Value& node) const {
    node["distance"] = settings.distance;
    node["weaponsOnly"] = settings.weaponsOnly;
    node["targetInvisibles"] = settings.targetInvisibles;
    node["ignoreFriends"] = settings.ignoreFriends;
    node["ignoreTeams"] = settings.ignoreTeams;
    node["throughWalls"] = settings.throughWalls;
    node["throughWallsRange"] = settings.throughWallsRange;
    node["backtrackCheck"] = settings.backtrackCheck;
    node["expand"] = settings.expand;
    node["pierceTeams"] = settings.pierceTeams;
    node["pierceRange"] = settings.pierceRange;
    node["protectTeams"] = settings.protectTeams;
}

void Piercing::loadSettings(const json::Value& node) {
    settings.distance = node["distance"].as_float(settings.distance);
    settings.weaponsOnly = node["weaponsOnly"].as_bool(settings.weaponsOnly);
    settings.targetInvisibles = node["targetInvisibles"].as_bool(settings.targetInvisibles);
    settings.ignoreFriends = node["ignoreFriends"].as_bool(settings.ignoreFriends);
    settings.ignoreTeams = node["ignoreTeams"].as_bool(settings.ignoreTeams);
    settings.throughWalls = node["throughWalls"].as_bool(settings.throughWalls);
    settings.throughWallsRange = node["throughWallsRange"].as_float(settings.throughWallsRange);
    settings.backtrackCheck = node["backtrackCheck"].as_bool(settings.backtrackCheck);
    settings.expand = node["expand"].as_float(settings.expand);
    settings.pierceTeams = node["pierceTeams"].as_bool(settings.pierceTeams);
    settings.pierceRange = node["pierceRange"].as_float(settings.pierceRange);
    settings.protectTeams = node["protectTeams"].as_bool(settings.protectTeams);
}
