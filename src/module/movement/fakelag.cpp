#include "fakelag.h"

#include "blink.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"

#include <Windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <vector>

FakeLag* fakeLag = nullptr;

namespace {

static bool packetClassContains(JNIEnv* env, jobject packet, const char* needle) {
    if (!env || !packet || !needle)
        return false;

    jclass cls = env->GetObjectClass(packet);
    if (!cls) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jclass classClass = env->GetObjectClass(cls);
    jmethodID getName = classClass
                            ? env->GetMethodID(classClass, OBFUSCATE_STRING("getName").c_str(),
                                               OBFUSCATE_STRING("()Ljava/lang/String;").c_str())
                            : nullptr;
    jstring name = getName ? static_cast<jstring>(env->CallObjectMethod(cls, getName)) : nullptr;
    bool result = false;
    if (name) {
        const char* chars = env->GetStringUTFChars(name, nullptr);
        if (chars) {
            result = std::strstr(chars, needle) != nullptr;
            env->ReleaseStringUTFChars(name, chars);
        }
        env->DeleteLocalRef(name);
    }
    if (classClass)
        env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(cls);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return result;
}

}  // namespace

FakeLag::FakeLag() : Module("FakeLag") {
    renders = true;
}

FakeLag::~FakeLag() {
    clearQueuedPackets();
}

std::uint64_t FakeLag::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

void FakeLag::resetAnchor() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!player.isValid()) {
        hasServerPosition_ = false;
        return;
    }
    serverX_ = player.getPosX();
    serverY_ = player.getPosY();
    serverZ_ = player.getPosZ();
    hasServerPosition_ = true;
}

void FakeLag::onEnable() {
    if (blink && blink->enabled)
        blink->setEnabled(false);

    clearQueuedPackets();
    active_.store(true, std::memory_order_release);
    cycleStartMs_ = nowMs();
    currentDelayMs_ = settings.minDelayMs;
    previousHurtTime_ = player.isValid() ? player.getHurtTime() : 0;
    resetAnchor();
    log_debug("[FakeLag] Enabled (mode=%d, delay=%d-%dms)", static_cast<int>(settings.mode),
              settings.minDelayMs, settings.maxDelayMs);
}

void FakeLag::onDisable() {
    active_.store(false, std::memory_order_release);
    flushQueuedPackets(true);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        hasServerPosition_ = false;
    }
}

bool FakeLag::isMovementPacket(JNIEnv* env, jobject packet) const {
    return packetClassContains(env, packet, OBFUSCATE_STRING("C03PacketPlayer").c_str());
}

bool FakeLag::isAttackPacket(JNIEnv* env, jobject packet) const {
    return packetClassContains(env, packet, OBFUSCATE_STRING("C02PacketUseEntity").c_str());
}

bool FakeLag::shouldQueueMovement() const {
    if (!enabled || !active_.load(std::memory_order_acquire) || isReleasing() || !player.isValid())
        return false;

    const double motion = std::hypot(player.getMotionX(), player.getMotionZ());
    if (settings.movingOnly && motion < 0.012)
        return false;

    if (settings.mode == Mode::Pulse) {
        const int hold = (std::max)(settings.pulseHoldMs, 20);
        const int release = (std::max)(settings.pulseReleaseMs, 20);
        const std::uint64_t phase = (nowMs() - cycleStartMs_) % static_cast<std::uint64_t>(hold + release);
        return phase < static_cast<std::uint64_t>(hold);
    }
    return true;
}

bool FakeLag::handleOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || !active_.load(std::memory_order_acquire) || isReleasing())
        return true;

    if (settings.flushOnAttack && isAttackPacket(env, packet)) {
        flushQueuedPackets(true);
        return true;
    }

    if (!isMovementPacket(env, packet))
        return true;

    if (!shouldQueueMovement()) {
        flushQueuedPackets(true);
        if (player.isValid()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            serverX_ = player.getPosX();
            serverY_ = player.getPosY();
            serverZ_ = player.getPosZ();
            hasServerPosition_ = true;
        }
        return true;
    }

    queuePacket(env, packet);
    return false;
}

void FakeLag::queuePacket(JNIEnv* env, jobject packet) {
    jobject global = env ? env->NewGlobalRef(packet) : nullptr;
    if (!global)
        return;

    QueuedPacket entry;
    entry.packet = global;
    entry.queuedAtMs = nowMs();
    if (player.isValid()) {
        entry.x = player.getPosX();
        entry.y = player.getPosY();
        entry.z = player.getPosZ();
    }

    bool overLimit = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queuedPackets_.push_back(entry);
        overLimit = static_cast<int>(queuedPackets_.size()) > (std::max)(settings.maxPackets, 5);
    }
    if (overLimit)
        flushQueuedPackets(false);
}

void FakeLag::releaseEligible(std::uint64_t ageMs, int limit) {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    std::vector<QueuedPacket> send;
    const std::uint64_t now = nowMs();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!queuedPackets_.empty() && static_cast<int>(send.size()) < limit) {
            if (now - queuedPackets_.front().queuedAtMs < ageMs)
                break;
            send.push_back(queuedPackets_.front());
            queuedPackets_.pop_front();
        }
    }

    if (send.empty())
        return;

    releasing_.store(true, std::memory_order_release);
    for (const auto& entry : send) {
        mc.sendPacket(entry.packet);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            serverX_ = entry.x;
            serverY_ = entry.y;
            serverZ_ = entry.z;
            hasServerPosition_ = true;
        }
        env->DeleteGlobalRef(entry.packet);
    }
    releasing_.store(false, std::memory_order_release);
}

void FakeLag::flushQueuedPackets(bool all) {
    const int limit = all ? INT_MAX : (std::max)(settings.releaseRate, 1);
    releaseEligible(0, limit);
}

void FakeLag::clearQueuedPackets() {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    std::deque<QueuedPacket> discarded;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        discarded.swap(queuedPackets_);
    }
    for (const auto& entry : discarded) {
        if (entry.packet)
            env->DeleteGlobalRef(entry.packet);
    }
}

float FakeLag::serverDistance() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!hasServerPosition_ || !player.isValid())
        return 0.0f;
    const double dx = player.getPosX() - serverX_;
    const double dy = player.getPosY() - serverY_;
    const double dz = player.getPosZ() - serverZ_;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

int FakeLag::calculateAdaptiveDelay() {
    const int minimum = (std::min)(settings.minDelayMs, settings.maxDelayMs);
    const int maximum = (std::max)(settings.minDelayMs, settings.maxDelayMs);
    if (!player.isValid())
        return minimum;

    const float speed = static_cast<float>(std::hypot(player.getMotionX(), player.getMotionZ()));
    float factor = std::clamp(speed / 0.30f, 0.0f, 1.0f) * 0.42f;
    if (player.isSprinting())
        factor += 0.08f;
    if (!player.isOnGround())
        factor += 0.06f;
    if (mc.isTargetingEntity())
        factor -= 0.25f;
    if (player.getHurtTime() > 0 || player.isUsingItem())
        factor = 0.0f;

    std::size_t queueSize = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queueSize = queuedPackets_.size();
    }
    const float pressure = static_cast<float>(queueSize) / static_cast<float>((std::max)(settings.maxPackets, 5));
    factor -= std::clamp(pressure - 0.45f, 0.0f, 0.55f);
    factor -= std::clamp(serverDistance() / (std::max)(settings.maxDistance, 0.5f) - 0.50f, 0.0f, 0.55f);
    factor = std::clamp(factor * settings.adaptiveStrength, 0.0f, 1.0f);

    const int target = minimum + static_cast<int>((maximum - minimum) * factor);
    currentDelayMs_ += static_cast<int>((target - currentDelayMs_) * 0.24f);
    return std::clamp(currentDelayMs_, minimum, maximum);
}

void FakeLag::onUpdate() {
    if (!enabled)
        return;
    JNIEnv* env = Java::Env();
    jobject world = env ? mc.getWorld() : nullptr;
    const bool hasWorld = world != nullptr;
    if (env && world)
        env->DeleteLocalRef(world);
    if (!player.isValid() || !hasWorld) {
        clearQueuedPackets();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hasServerPosition_ = false;
        }
        return;
    }

    const int hurtTime = player.getHurtTime();
    if (settings.flushOnDamage && hurtTime > previousHurtTime_)
        flushQueuedPackets(true);
    previousHurtTime_ = hurtTime;

    if (settings.flushOnItemUse && player.isUsingItem()) {
        flushQueuedPackets(true);
        return;
    }

    const float distance = serverDistance();
    if (settings.smoothRelease && distance >= (std::max)(settings.maxDistance, 0.5f) * 0.65f)
        flushQueuedPackets(false);
    if (distance >= (std::max)(settings.maxDistance, 0.5f)) {
        flushQueuedPackets(settings.smoothRelease ? false : true);
        return;
    }

    switch (settings.mode) {
        case Mode::Pulse: {
            const int hold = (std::max)(settings.pulseHoldMs, 20);
            const int release = (std::max)(settings.pulseReleaseMs, 20);
            const std::uint64_t phase = (nowMs() - cycleStartMs_) % static_cast<std::uint64_t>(hold + release);
            if (phase >= static_cast<std::uint64_t>(hold))
                flushQueuedPackets(settings.smoothRelease ? false : true);
            break;
        }
        case Mode::Constant:
            currentDelayMs_ = std::clamp(settings.maxDelayMs, 20, 1000);
            releaseEligible(static_cast<std::uint64_t>(currentDelayMs_),
                            settings.smoothRelease ? (std::max)(settings.releaseRate, 1) : INT_MAX);
            break;
        case Mode::Distance:
            currentDelayMs_ = settings.maxDelayMs;
            releaseEligible(static_cast<std::uint64_t>((std::max)(settings.maxDelayMs, 20)),
                            settings.smoothRelease ? (std::max)(settings.releaseRate, 1) : INT_MAX);
            break;
        case Mode::Adaptive:
            currentDelayMs_ = calculateAdaptiveDelay();
            releaseEligible(static_cast<std::uint64_t>(currentDelayMs_),
                            settings.smoothRelease ? (std::max)(settings.releaseRate, 1) : INT_MAX);
            break;
    }
}

void FakeLag::renderPlayerBox(double x, double y, double z, unsigned int outline, unsigned int fill) {
    if (!RenderData::updated)
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    RenderPos render = mc.getRenderPos();
    constexpr float half = 0.3f;
    constexpr float height = 1.8f;
    Vector3 corners[8] = {
        {(float)(x - half - render.x), (float)(y - render.y), (float)(z - half - render.z)},
        {(float)(x + half - render.x), (float)(y - render.y), (float)(z - half - render.z)},
        {(float)(x + half - render.x), (float)(y - render.y), (float)(z + half - render.z)},
        {(float)(x - half - render.x), (float)(y - render.y), (float)(z + half - render.z)},
        {(float)(x - half - render.x), (float)(y + height - render.y), (float)(z - half - render.z)},
        {(float)(x + half - render.x), (float)(y + height - render.y), (float)(z - half - render.z)},
        {(float)(x + half - render.x), (float)(y + height - render.y), (float)(z + half - render.z)},
        {(float)(x - half - render.x), (float)(y + height - render.y), (float)(z + half - render.z)}};
    Vector2 screen[8]{};
    bool visible[8]{};
    int visibleCount = 0;
    for (int i = 0; i < 8; ++i) {
        visible[i] = CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection,
                                                   RenderData::screenWidth, RenderData::screenHeight, screen[i]);
        visibleCount += visible[i] ? 1 : 0;
    }
    if (visibleCount < 3)
        return;

    static constexpr int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                        {2, 3, 7, 6}, {0, 3, 7, 4}, {1, 2, 6, 5}};
    if (settings.filledRender) {
        for (const auto& face : faces) {
            if (!visible[face[0]] || !visible[face[1]] || !visible[face[2]] || !visible[face[3]])
                continue;
            ImVec2 points[4] = {{screen[face[0]].x, screen[face[0]].y}, {screen[face[1]].x, screen[face[1]].y},
                                {screen[face[2]].x, screen[face[2]].y}, {screen[face[3]].x, screen[face[3]].y}};
            draw->AddConvexPolyFilled(points, 4, fill);
        }
    }
    static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : edges) {
        if (visible[edge[0]] && visible[edge[1]])
            draw->AddLine({screen[edge[0]].x, screen[edge[0]].y}, {screen[edge[1]].x, screen[edge[1]].y},
                          outline, 2.0f);
    }
}

void FakeLag::onRender() {
    if (!enabled)
        return;
    double serverX = 0.0;
    double serverY = 0.0;
    double serverZ = 0.0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!hasServerPosition_)
            return;
        serverX = serverX_;
        serverY = serverY_;
        serverZ = serverZ_;
    }
    std::size_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queued = queuedPackets_.size();
    }
    if (queued == 0)
        return;

    const ImVec4 base(settings.color[0], settings.color[1], settings.color[2], settings.color[3]);
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(base);
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * 0.20f));
    if (settings.renderServerPlayer)
        renderPlayerBox(serverX, serverY, serverZ, outline, fill);

    if (settings.renderLine && RenderData::updated) {
        RenderPos render = mc.getRenderPos();
        Vector2 a{}, b{};
        const bool va = CWorldToScreen::WorldToScreen(
            {(float)(serverX - render.x), (float)(serverY + 0.9 - render.y), (float)(serverZ - render.z)},
            RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, a);
        const bool vb = CWorldToScreen::WorldToScreen(
            {(float)(player.getPosX() - render.x), (float)(player.getPosY() + 0.9 - render.y),
             (float)(player.getPosZ() - render.z)},
            RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, b);
        if (va && vb)
            ImGui::GetBackgroundDrawList()->AddLine({a.x, a.y}, {b.x, b.y}, outline, 1.5f);
    }

    if (settings.showStatus) {
        const char* modes[] = {"Pulse", "Constant", "Distance", "Adaptive"};
        char text[96];
        std::snprintf(text, sizeof(text), "FakeLag %s  |  %d ms  |  %zu packets", modes[(int)settings.mode],
                      currentDelayMs_, queued);
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        ImVec2 size = ImGui::CalcTextSize(text);
        const float x = (ImGui::GetIO().DisplaySize.x - size.x) * 0.5f;
        draw->AddRectFilled({x - 8.0f, 48.0f}, {x + size.x + 8.0f, 70.0f}, IM_COL32(10, 12, 16, 175), 5.0f);
        draw->AddText({x, 52.0f}, outline, text);
    }
}

void FakeLag::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Fake Lag",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);
            int mode = static_cast<int>(settings.mode);
            const char* modes[] = {"Pulse", "Constant", "Distance", "Adaptive"};
            if (ui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes)))
                settings.mode = static_cast<Mode>(mode);
            ui::SliderInt("Minimum Delay", &settings.minDelayMs, 20, 250, "%d ms");
            ui::SliderInt("Maximum Delay", &settings.maxDelayMs, 40, 400, "%d ms");
            if (settings.mode == Mode::Pulse) {
                ui::SliderInt("Hold Time", &settings.pulseHoldMs, 40, 1000, "%d ms");
                ui::SliderInt("Release Time", &settings.pulseReleaseMs, 20, 500, "%d ms");
            }
            ui::SliderFloat("Maximum Distance", &settings.maxDistance, 0.5f, 4.0f, "%.1f blocks");
            if (settings.mode == Mode::Adaptive)
                ui::SliderFloat("Adaptive Strength", &settings.adaptiveStrength, 0.0f, 1.0f, "%.2f");
            ui::Checkbox("Moving Only", &settings.movingOnly);
            ui::Checkbox("Flush On Attack", &settings.flushOnAttack);
            ui::Checkbox("Flush On Damage", &settings.flushOnDamage);
            ui::Checkbox("Flush On Item Use", &settings.flushOnItemUse);
            ui::Checkbox("Smooth Release", &settings.smoothRelease);
            if (settings.smoothRelease)
                ui::SliderInt("Release Rate", &settings.releaseRate, 1, 12, "%d packets/tick");
            ui::SliderInt("Queue Limit", &settings.maxPackets, 8, 60, "%d packets");
        },
        "Visuals",
        [this]() {
            ui::Checkbox("Render Server Player", &settings.renderServerPlayer);
            ui::Checkbox("Filled Render", &settings.filledRender);
            ui::Checkbox("Render Line", &settings.renderLine);
            ui::Checkbox("Show Status", &settings.showStatus);
            ImGui::Spacing();
            ui::color("Color", settings.color);
        });
}

std::string FakeLag::getDisplayInfo() {
    static const char* names[] = {"Pulse", "Constant", "Distance", "Adaptive"};
    const int mode = std::clamp(static_cast<int>(settings.mode), 0, 3);
    if (settings.mode == Mode::Adaptive || settings.mode == Mode::Constant)
        return std::string(names[mode]) + " " + std::to_string(currentDelayMs_) + "ms";
    return names[mode];
}
