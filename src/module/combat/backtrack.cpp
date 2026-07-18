#include "backtrack.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

Backtrack* backtrack = nullptr;

static constexpr int HISTORY_LIMIT = 20;

Backtrack::Backtrack() : Module("Backtrack") {
    keybind = 0;
    renders = true;
}

int Backtrack::getHoldMs() const {
    if (settings.mode == Mode::Lag)
        return std::clamp(settings.latencyMs, 50, 500);
    return std::clamp(settings.ticks, 1, 8) * 50;
}

void Backtrack::onEnable() {
    clearHistory();
    realTrail_.clear();
    currentTargetUUID = "";
    holdTargetId_.store(-1, std::memory_order_release);
    blocking_.store(false, std::memory_order_release);
    heldShown_.store(0, std::memory_order_release);
}

void Backtrack::onDisable() {
    holdTargetId_.store(-1, std::memory_order_release);
    blocking_.store(false, std::memory_order_release);
    heldShown_.store(0, std::memory_order_release);
    // Nothing to flush: we never held packets. Any target that was frozen resyncs
    // to its real position on the next incoming movement packet.
    realTrail_.clear();
    clearHistory();
}

bool Backtrack::isHoldingWeapon() {
    int id = player.getHeldItemId();
    return (id == 268 || id == 272 || id == 267 || id == 276 || id == 283 ||  // swords
            id == 271 || id == 275 || id == 258 || id == 279 || id == 286);    // axes
}

void Backtrack::clearHistory() {
    entityHistory.clear();
}

void Backtrack::recordPosition(const PlayerInfo& target) {
    PositionSnapshot snap;
    snap.x = target.posX;
    snap.y = target.posY;
    snap.z = target.posZ;
    snap.yaw = 0.0f;
    snap.pitch = 0.0f;
    snap.hurtTime = target.hurtTime;
    snap.tickAge = 0;
    snap.timestamp = std::chrono::steady_clock::now();
    auto& h = entityHistory[target.uuid];
    h.push_back(snap);
    while (h.size() > static_cast<size_t>(HISTORY_LIMIT))
        h.pop_front();
    for (auto& s : h)
        s.tickAge++;
}

const std::deque<PositionSnapshot>* Backtrack::getPositionHistory(const std::string& uuid) const {
    auto it = entityHistory.find(uuid);
    return it != entityHistory.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Position-rollback JNI plumbing.
//
// We move the target entity backwards in time entirely on the client: relocate its
// posX/Y/Z + bounding box to a delayed position and stop its interpolation. Real
// (server) positions come from otherPlayerMP* — the interpolation destination the
// incoming movement packets write and that we never touch — so the recorded trail
// stays real even while we hold the visible entity behind it. No packets are held
// or dropped, so this cannot desync or drop the connection.
// ---------------------------------------------------------------------------
namespace {
bool s_btResolved = false;
jclass s_clsOtherPlayer = nullptr;  // EntityOtherPlayerMP (global ref)
jfieldID s_fidPrevX = nullptr, s_fidPrevY = nullptr, s_fidPrevZ = nullptr;
jfieldID s_fidOtherX = nullptr, s_fidOtherY = nullptr, s_fidOtherZ = nullptr;
jfieldID s_fidIncr = nullptr;
jmethodID s_midSetPos = nullptr;
}  // namespace

void Backtrack::resolveHandles(JNIEnv* env) {
    if (s_btResolved || !env)
        return;
    s_btResolved = true;

    if (JniCache::cls_Entity) {
        s_fidPrevX = env->GetFieldID(JniCache::cls_Entity, "prevPosX", "D");
        if (!s_fidPrevX) env->ExceptionClear();
        s_fidPrevY = env->GetFieldID(JniCache::cls_Entity, "prevPosY", "D");
        if (!s_fidPrevY) env->ExceptionClear();
        s_fidPrevZ = env->GetFieldID(JniCache::cls_Entity, "prevPosZ", "D");
        if (!s_fidPrevZ) env->ExceptionClear();

        s_midSetPos = env->GetMethodID(JniCache::cls_Entity, "setPosition", "(DDD)V");
        if (!s_midSetPos) {
            env->ExceptionClear();
            s_midSetPos = env->GetMethodID(JniCache::cls_Entity, "func_70107_b", "(DDD)V");  // obf
            if (!s_midSetPos) env->ExceptionClear();
        }
    }

    jclass local = nullptr;
    if (Java::AssignClass("net.minecraft.client.entity.EntityOtherPlayerMP", local)) {
        s_clsOtherPlayer = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        s_fidOtherX = env->GetFieldID(s_clsOtherPlayer, "otherPlayerMPX", "D");
        if (!s_fidOtherX) env->ExceptionClear();
        s_fidOtherY = env->GetFieldID(s_clsOtherPlayer, "otherPlayerMPY", "D");
        if (!s_fidOtherY) env->ExceptionClear();
        s_fidOtherZ = env->GetFieldID(s_clsOtherPlayer, "otherPlayerMPZ", "D");
        if (!s_fidOtherZ) env->ExceptionClear();
        s_fidIncr = env->GetFieldID(s_clsOtherPlayer, "otherPlayerMPPosRotationIncrements", "I");
        if (!s_fidIncr) env->ExceptionClear();
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    log_debug("[Backtrack] handles: setPos=%d otherX=%d incr=%d prevX=%d", s_midSetPos ? 1 : 0,
              s_fidOtherX ? 1 : 0, s_fidIncr ? 1 : 0, s_fidPrevX ? 1 : 0);
}

void Backtrack::freezeTarget(JNIEnv* env, jobject entity, int entityId, int depth) {
    if (!env || !entity)
        return;
    resolveHandles(env);
    if (!s_midSetPos || !s_clsOtherPlayer || !s_fidOtherX || !s_fidOtherY || !s_fidOtherZ)
        return;  // graceful no-op if the entity model isn't what we expect
    if (env->IsInstanceOf(entity, s_clsOtherPlayer) != JNI_TRUE) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;  // not a real remote player (NPC/mob) — leave it alone
    }

    // 1) Record the real (server) position: the interpolation destination the
    //    incoming movement packets keep fresh and that we never overwrite.
    const double rx = env->GetDoubleField(entity, s_fidOtherX);
    const double ry = env->GetDoubleField(entity, s_fidOtherY);
    const double rz = env->GetDoubleField(entity, s_fidOtherZ);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    auto& trail = realTrail_[entityId];
    trail.push_back({rx, ry, rz});
    while (trail.size() > 20)
        trail.pop_front();

    // Drop trails for any other entity so the map cannot grow across targets.
    for (auto it = realTrail_.begin(); it != realTrail_.end();) {
        if (it->first != entityId)
            it = realTrail_.erase(it);
        else
            ++it;
    }

    // 2) Choose the delayed position (depth already clamped to the server's rewind
    //    window by the caller).
    const int d = std::clamp(depth, 1, 8);
    const size_t idx =
        trail.size() > static_cast<size_t>(d) ? trail.size() - 1 - static_cast<size_t>(d) : 0;
    const Vec3d past = trail[idx];

    // 3) Relocate the visible entity (posX/Y/Z + bounding box) to the past position.
    //    Position rewind ALWAYS happens (that's the hit trick). What's optional is
    //    zeroing the render smear + interpolation increment: doing so makes the target
    //    "snap" back and forth (the classic visible lag look); leaving them alone lets
    //    MC's renderer interpolate the entity into place smoothly.
    env->CallVoidMethod(entity, s_midSetPos, past.x, past.y, past.z);

    if (settings.visualLag) {
        if (s_fidPrevX) env->SetDoubleField(entity, s_fidPrevX, past.x);
        if (s_fidPrevY) env->SetDoubleField(entity, s_fidPrevY, past.y);
        if (s_fidPrevZ) env->SetDoubleField(entity, s_fidPrevZ, past.z);
        if (JniCache::fid_lastTickPosX) env->SetDoubleField(entity, JniCache::fid_lastTickPosX, past.x);
        if (JniCache::fid_lastTickPosY) env->SetDoubleField(entity, JniCache::fid_lastTickPosY, past.y);
        if (JniCache::fid_lastTickPosZ) env->SetDoubleField(entity, JniCache::fid_lastTickPosZ, past.z);
        if (s_fidIncr) env->SetIntField(entity, s_fidIncr, 0);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void Backtrack::onClientTick(JNIEnv* env) {
    if (!enabled || !env)
        return;

    auto disengage = [&]() {
        blocking_.store(false, std::memory_order_release);
        holdTargetId_.store(-1, std::memory_order_release);
        heldShown_.store(0, std::memory_order_release);
        realTrail_.clear();  // frozen targets resync on their next movement packet
    };

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        disengage();
        return;
    }
    if (settings.weaponOnly && !isHoldingWeapon()) {
        disengage();
        return;
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();

    const double px = player.getPosX();
    const double py = player.getPosY();
    const double pz = player.getPosZ();

    std::vector<PlayerInfo> players = mc.getPlayerList();

    static int localTeamColor = -1;
    static unsigned long lastColorRefresh = 0;
    unsigned long nowTick = GetTickCount();
    if (nowTick - lastColorRefresh > 1000) {
        localTeamColor = mc.getLocalPlayerTeamColor();
        lastColorRefresh = nowTick;
    }
    static std::string localName;
    static unsigned long lastNameRefresh = 0;
    if (localName.empty() || nowTick - lastNameRefresh > 1000) {
        localName = mc.getLocalPlayerName();
        lastNameRefresh = nowTick;
    }

    double closest = 1e9;
    PlayerInfo* tgt = nullptr;
    for (auto& p : players) {
        if (p.isNPC || p.isInvisible)
            continue;
        if (friendsManager) {
            bool fUuid = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool fName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (fUuid || fName)
                continue;
            if (p.isOnLocalTeam)
                continue;
            if (!p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid))
                continue;
            if (!p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid))
                continue;
            ChatColor pc = static_cast<ChatColor>(p.teamColor);
            ChatColor lc = static_cast<ChatColor>(localTeamColor);
            if (pc != ChatColor::WHITE && pc != ChatColor::GRAY && pc != ChatColor::UNKNOWN) {
                if (friendsManager->isScoreboardTeammate(localName, p.profileName.empty() ? p.name : p.profileName))
                    continue;
            }
            if (friendsManager->isTeammate(p.uuid, pc, lc))
                continue;
        }
        double dx = p.posX - px, dy = p.posY - py, dz = p.posZ - pz;
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < closest && dist < settings.maxDistance) {
            closest = dist;
            tgt = &p;
        }
    }

    const bool haveTarget = tgt != nullptr;
    const int tid = haveTarget ? tgt->entityId : -1;
    if (haveTarget) {
        recordPosition(*tgt);
        currentTargetUUID = tgt->uuid;
    }

    const bool attacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    // Lag mode = continuous hold like Strong. Normal respects Only On Hit.
    const bool isContinuous = (settings.mode == Mode::Strong || settings.mode == Mode::Lag);
    const bool engage = haveTarget && (isContinuous || !settings.onlyOnHit || attacking);

    // Clamp the rollback depth to what the server can actually rewind (~your ping).
    // Asking for more latency than the anticheat's lag-comp window means the rewound
    // hitbox never reaches the frozen position → the hit silently misses. Refresh
    // ping ~1/s to avoid a JNI lookup every tick.
    static int s_pingTicks = 8;
    static unsigned long s_lastPingRefresh = 0;
    if (nowTick - s_lastPingRefresh > 1000) {
        s_lastPingRefresh = nowTick;
        const int pingMs = mc.getLocalPlayerPing();
        s_pingTicks = pingMs > 0 ? std::clamp(pingMs / 50, 1, 8) : 8;  // unknown -> don't clamp
    }
    // Requested depth: ticks-based for Normal/Strong, ms-based for Lag.
    const int requestedDepth =
        (settings.mode == Mode::Lag) ? std::clamp(settings.latencyMs, 50, 500) / 50
                                      : settings.ticks;
    const int depth = std::clamp(requestedDepth, 1, s_pingTicks);

    holdTargetId_.store(tid, std::memory_order_release);
    blocking_.store(engage, std::memory_order_release);

    // Roll the target's client-side state back `depth` ticks. Must run BEFORE we drop
    // the entity GlobalRefs below (freezeTarget uses tgt->instance).
    if (engage && tgt && tgt->instance) {
        freezeTarget(env, tgt->instance, tid, depth);
        heldShown_.store(depth, std::memory_order_release);
    } else {
        heldShown_.store(0, std::memory_order_release);
        realTrail_.clear();
    }

    // Log whenever engagement flips (e.g. when you get near a target). Avoid %f — the
    // trace formatter doesn't handle it; report distance in centimetres as an int.
    static int s_lastEngage = -1;
    if (static_cast<int>(engage) != s_lastEngage) {
        s_lastEngage = static_cast<int>(engage);
        log_debug("[Backtrack] engage=%d targetId=%d distCm=%d depth=%d", engage ? 1 : 0, tid,
                  haveTarget ? static_cast<int>(closest * 100.0) : -1, depth);
    }

    for (auto& p : players) {
        if (p.instance) {
            env->DeleteGlobalRef(p.instance);
            p.instance = nullptr;
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void Backtrack::onUpdate() {
    // All work happens on the client thread in onClientTick (driven by the pre-motion
    // hook), so this async loop is intentionally idle.
}

void Backtrack::onRender() {
    if (!enabled || !settings.showStatus || !player.isValid())
        return;
    if (!blocking_.load(std::memory_order_acquire))
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 ss = ImGui::GetIO().DisplaySize;
    ImFont* f = font(5) ? font(5) : font(2);
    if (!f)
        return;
    char buf[48];
    wsprintfA(buf, "backtrack  %d", heldShown_.load(std::memory_order_acquire));
    float fs = ui::s(12.0f);
    ImVec2 tsz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
    float x = ss.x * 0.5f - tsz.x * 0.5f;
    float y = ss.y * 0.5f + ui::s(28.0f);
    draw->AddText(f, fs, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 150), buf);
    draw->AddText(f, fs, ImVec2(x, y), IM_COL32(120, 230, 140, 220), buf);
}

void Backtrack::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();

            const char* modeItems[] = {"Normal", "Strong", "LagBased"};
            int mode = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &mode, modeItems, IM_ARRAYSIZE(modeItems)))
                settings.mode = static_cast<Mode>(mode);

            if (settings.mode == Mode::Lag) {
                // Lag mode: latency slider in ms (like the classic Lag Based backtrack).
                ui::SliderInt("Latency", &settings.latencyMs, 50, 500, "%d ms");
            } else {
                ui::SliderInt("Ticks", &settings.ticks, 1, 8, "%d");
            }
            ui::SliderFloat("Range", &settings.maxDistance, 2.0f, 6.0f, "%.1f blocks");
            ImGui::Spacing();
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            if (settings.mode == Mode::Normal)
                ui::Checkbox("Only On Hit", &settings.onlyOnHit);
            ui::Checkbox("Visual Lag", &settings.visualLag);
        },
        "Status",
        [this]() {
            ui::Checkbox("Show Status", &settings.showStatus);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (blocking_.load(std::memory_order_acquire))
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Rolling back (%d ticks)",
                                   heldShown_.load(std::memory_order_acquire));
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Idle");
        });
}

std::string Backtrack::getDisplayInfo() {
    switch (settings.mode) {
        case Mode::Lag:
            return "LagBased " + std::to_string(settings.latencyMs) + "ms";
        case Mode::Strong:
            return "Strong " + std::to_string(settings.ticks) + " ticks";
        case Mode::Normal:
        default:
            return std::to_string(settings.ticks) + " ticks";
    }
}

void Backtrack::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["ticks"] = settings.ticks;
    node["latencyMs"] = settings.latencyMs;
    node["maxDistance"] = settings.maxDistance;
    node["weaponOnly"] = settings.weaponOnly;
    node["onlyOnHit"] = settings.onlyOnHit;
    node["showStatus"] = settings.showStatus;
    node["visualLag"] = settings.visualLag;
}

void Backtrack::loadSettings(const json::Value& node) {
    settings.mode = static_cast<Mode>(std::clamp(node["mode"].as_int(static_cast<int>(settings.mode)), 0, 2));
    settings.ticks = std::clamp(node["ticks"].as_int(settings.ticks), 1, 8);
    settings.latencyMs = std::clamp(node["latencyMs"].as_int(settings.latencyMs), 50, 500);
    settings.maxDistance = node["maxDistance"].as_float(settings.maxDistance);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.onlyOnHit = node["onlyOnHit"].as_bool(settings.onlyOnHit);
    settings.showStatus = node["showStatus"].as_bool(settings.showStatus);
    settings.visualLag = node["visualLag"].as_bool(settings.visualLag);
}
