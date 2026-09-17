#include "settings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"

namespace {

std::atomic<bool> active{false};
std::mutex stateMutex;
int previousPerspective = -1;
bool initialized = false;
bool swapped = false;
jobject swappedPlayer = nullptr;
float realYaw = 0.0f;
float realPitch = 0.0f;
float realPrevYaw = 0.0f;
float realPrevPitch = 0.0f;
float cameraYaw = 0.0f;
float cameraPitch = 0.0f;

jobject getPlayer(JNIEnv* env) {
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

void restoreRotation(JNIEnv* env) {
    if (!swapped || !env || !swappedPlayer)
        return;

    env->SetFloatField(swappedPlayer, JniCache::fid_rotationYaw, realYaw);
    env->SetFloatField(swappedPlayer, JniCache::fid_rotationPitch, realPitch);
    env->SetFloatField(swappedPlayer, JniCache::fid_prevRotationYaw, realPrevYaw);
    env->SetFloatField(swappedPlayer, JniCache::fid_prevRotationPitch, realPrevPitch);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    env->DeleteGlobalRef(swappedPlayer);
    swappedPlayer = nullptr;
    swapped = false;
}

}  // namespace

namespace Settings {
namespace Freelook {

bool enabled = false;
bool holdKey = false;
int keybind = 0;
int perspective = 1;

void setEnabled(bool value) {
    if (enabled == value)
        return;

    enabled = value;
    if (enabled) {
        std::lock_guard<std::mutex> lock(stateMutex);
        previousPerspective = mc.getPerspective();
        initialized = false;
        active.store(true, std::memory_order_release);
        mc.setPerspective(perspective);
        return;
    }

    active.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(stateMutex);
    restoreRotation(Java::Env());
    if (previousPerspective >= 0)
        mc.setPerspective(previousPerspective);
    previousPerspective = -1;
    initialized = false;
}

void update() {
    if (active.load(std::memory_order_acquire) && mc.getPerspective() != perspective)
        mc.setPerspective(perspective);
}

void onWorldChange() {
    std::lock_guard<std::mutex> lock(stateMutex);
    restoreRotation(Java::Env());
    initialized = false;
}

void shutdown() {
    setEnabled(false);
}

void onCameraBegin(JNIEnv* env) {
    if (!active.load(std::memory_order_acquire) || !env || !JniCache::fid_rotationYaw ||
        !JniCache::fid_rotationPitch || !JniCache::fid_prevRotationYaw || !JniCache::fid_prevRotationPitch)
        return;

    std::lock_guard<std::mutex> lock(stateMutex);
    restoreRotation(env);
    if (swapped)
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

    if (!initialized) {
        realYaw = observedYaw;
        realPitch = observedPitch;
        realPrevYaw = observedPrevYaw;
        realPrevPitch = observedPrevPitch;
        cameraYaw = observedYaw;
        cameraPitch = observedPitch;
        initialized = true;
    } else {
        cameraYaw += std::remainder(observedYaw - realYaw, 360.0f);
        cameraPitch = std::clamp(cameraPitch + observedPitch - realPitch, -90.0f, 90.0f);
        realPrevYaw = observedPrevYaw;
        realPrevPitch = observedPrevPitch;
    }

    jobject retainedPlayer = env->NewGlobalRef(localPlayer);
    if (env->ExceptionCheck() || !retainedPlayer) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(localPlayer);
        return;
    }

    env->SetFloatField(localPlayer, JniCache::fid_rotationYaw, cameraYaw);
    env->SetFloatField(localPlayer, JniCache::fid_rotationPitch, cameraPitch);
    env->SetFloatField(localPlayer, JniCache::fid_prevRotationYaw, cameraYaw);
    env->SetFloatField(localPlayer, JniCache::fid_prevRotationPitch, cameraPitch);
    env->DeleteLocalRef(localPlayer);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->SetFloatField(retainedPlayer, JniCache::fid_rotationYaw, realYaw);
        env->SetFloatField(retainedPlayer, JniCache::fid_rotationPitch, realPitch);
        env->SetFloatField(retainedPlayer, JniCache::fid_prevRotationYaw, realPrevYaw);
        env->SetFloatField(retainedPlayer, JniCache::fid_prevRotationPitch, realPrevPitch);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteGlobalRef(retainedPlayer);
        return;
    }
    swappedPlayer = retainedPlayer;
    swapped = true;
}

void onCameraReady(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(stateMutex);
    restoreRotation(env);
}

}  // namespace Freelook
}  // namespace Settings
