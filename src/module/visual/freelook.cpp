#include "freelook.h"

#include <algorithm>
#include <cmath>

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "../../jni/java.h"

Freelook* freelook = nullptr;

Freelook::Freelook() : Module("Freelook") {}

jobject Freelook::getPlayer(JNIEnv* env) const {
    if (!env || !mc.instance || !JniCache::fid_thePlayer)
        return nullptr;

    jobject localPlayer = env->GetObjectField(mc.instance, JniCache::fid_thePlayer);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (localPlayer)
            env->DeleteLocalRef(localPlayer);
        return nullptr;
    }
    return localPlayer;
}

void Freelook::restoreRotation(JNIEnv* env) {
    if (!swapped_ || !env || !swappedPlayer_)
        return;

    env->SetFloatField(swappedPlayer_, JniCache::fid_rotationYaw, realYaw_);
    env->SetFloatField(swappedPlayer_, JniCache::fid_rotationPitch, realPitch_);
    env->SetFloatField(swappedPlayer_, JniCache::fid_prevRotationYaw, realPrevYaw_);
    env->SetFloatField(swappedPlayer_, JniCache::fid_prevRotationPitch, realPrevPitch_);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    env->DeleteGlobalRef(swappedPlayer_);
    swappedPlayer_ = nullptr;
    swapped_ = false;
}

void Freelook::onEnable() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    previousPerspective_ = mc.getPerspective();
    initialized_ = false;
    active_.store(true, std::memory_order_release);
    mc.setPerspective(settings.perspective);
}

void Freelook::onDisable() {
    active_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(stateMutex_);
    restoreRotation(Java::Env());
    if (previousPerspective_ >= 0)
        mc.setPerspective(previousPerspective_);
    previousPerspective_ = -1;
    initialized_ = false;
}

void Freelook::onWorldChange() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    restoreRotation(Java::Env());
    initialized_ = false;
}

void Freelook::onUpdate() {
    if (active_.load(std::memory_order_acquire) && mc.getPerspective() != settings.perspective)
        mc.setPerspective(settings.perspective);
}

void Freelook::onCameraBegin(JNIEnv* env) {
    if (!active_.load(std::memory_order_acquire) || !env ||
        !JniCache::fid_rotationYaw || !JniCache::fid_rotationPitch ||
        !JniCache::fid_prevRotationYaw || !JniCache::fid_prevRotationPitch)
        return;

    std::lock_guard<std::mutex> lock(stateMutex_);
    restoreRotation(env);
    if (swapped_)
        return;

    jobject localPlayer = getPlayer(env);
    if (!localPlayer)
        return;

    const float observedYaw = env->GetFloatField(localPlayer, JniCache::fid_rotationYaw);
    const float observedPitch = env->GetFloatField(localPlayer, JniCache::fid_rotationPitch);
    const float observedPrevYaw = env->GetFloatField(localPlayer, JniCache::fid_prevRotationYaw);
    const float observedPrevPitch = env->GetFloatField(localPlayer, JniCache::fid_prevRotationPitch);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(localPlayer);
        return;
    }

    if (!initialized_) {
        realYaw_ = observedYaw;
        realPitch_ = observedPitch;
        realPrevYaw_ = observedPrevYaw;
        realPrevPitch_ = observedPrevPitch;
        cameraYaw_ = observedYaw;
        cameraPitch_ = observedPitch;
        initialized_ = true;
    } else {
        cameraYaw_ += std::remainder(observedYaw - realYaw_, 360.0f);
        cameraPitch_ = std::clamp(cameraPitch_ + observedPitch - realPitch_, -90.0f, 90.0f);
        realPrevYaw_ = observedPrevYaw;
        realPrevPitch_ = observedPrevPitch;
    }

    jobject retainedPlayer = env->NewGlobalRef(localPlayer);
    if (env->ExceptionCheck() || !retainedPlayer) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(localPlayer);
        return;
    }

    env->SetFloatField(localPlayer, JniCache::fid_rotationYaw, cameraYaw_);
    env->SetFloatField(localPlayer, JniCache::fid_rotationPitch, cameraPitch_);
    env->SetFloatField(localPlayer, JniCache::fid_prevRotationYaw, cameraYaw_);
    env->SetFloatField(localPlayer, JniCache::fid_prevRotationPitch, cameraPitch_);
    env->DeleteLocalRef(localPlayer);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->SetFloatField(retainedPlayer, JniCache::fid_rotationYaw, realYaw_);
        env->SetFloatField(retainedPlayer, JniCache::fid_rotationPitch, realPitch_);
        env->SetFloatField(retainedPlayer, JniCache::fid_prevRotationYaw, realPrevYaw_);
        env->SetFloatField(retainedPlayer, JniCache::fid_prevRotationPitch, realPrevPitch_);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteGlobalRef(retainedPlayer);
        return;
    }
    swappedPlayer_ = retainedPlayer;
    swapped_ = true;
}

void Freelook::onCameraReady(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    restoreRotation(env);
}

void Freelook::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);
            if (ui::Checkbox("Hold Key", &settings.holdKey) && settings.holdKey && enabled)
                setEnabled(false);
        },
        "Camera",
        [this]() {
            static const char* perspectives[] = {"Back", "Front"};
            int selected = settings.perspective - 1;
            if (ui::Combo("Perspective", &selected, perspectives, 2)) {
                settings.perspective = selected + 1;
                if (enabled)
                    mc.setPerspective(settings.perspective);
            }
        });
}

std::string Freelook::getDisplayInfo() {
    if (!enabled)
        return "Disabled";
    return settings.perspective == 2 ? "Front" : "Back";
}

void Freelook::saveSettings(json::Value& node) const {
    node["perspective"] = settings.perspective;
    node["holdKey"] = settings.holdKey;
}

void Freelook::loadSettings(const json::Value& node) {
    settings.perspective = std::clamp(node["perspective"].as_int(settings.perspective), 1, 2);
    settings.holdKey = node["holdKey"].as_bool(settings.holdKey);
    if (settings.holdKey && enabled)
        setEnabled(false);
}
