#include "altmanager_gate.hpp"

#include "../../module/utility/altmanager.h"
#include "../../util/logger.hpp"

#include <atomic>
#include <mutex>

namespace AltManagerGate {
namespace {

constexpr const char* kLoginStartClass =
    "net.minecraft.network.login.client.C00PacketLoginStart";
constexpr const char* kGameProfileClass = "com.mojang.authlib.GameProfile";
constexpr const char* kGameProfileSignature = "Lcom/mojang/authlib/GameProfile;";

std::mutex g_cacheMutex;
std::atomic<bool> g_cacheReady{false};
jclass g_loginStartClass = nullptr;
jclass g_gameProfileClass = nullptr;
jfieldID g_profileField = nullptr;
jmethodID g_profileConstructor = nullptr;
jmethodID g_getProfileId = nullptr;

void clearPendingException(JNIEnv* env) {
    if (env && env->ExceptionCheck())
        env->ExceptionClear();
}

jfieldID findProfileField(JNIEnv* env, jclass loginStartClass) {
    jfieldID field =
        env->GetFieldID(loginStartClass, "profile", kGameProfileSignature);
    if (field)
        return field;

    clearPendingException(env);
    field = env->GetFieldID(loginStartClass, "field_149305_a",
                            kGameProfileSignature);
    clearPendingException(env);
    return field;
}

void clearCache(JNIEnv* env) {
    g_cacheReady.store(false, std::memory_order_release);
    g_profileField = nullptr;
    g_profileConstructor = nullptr;
    g_getProfileId = nullptr;

    if (g_gameProfileClass) {
        env->DeleteGlobalRef(g_gameProfileClass);
        g_gameProfileClass = nullptr;
    }
    if (g_loginStartClass) {
        env->DeleteGlobalRef(g_loginStartClass);
        g_loginStartClass = nullptr;
    }
}

bool ensureCache(JNIEnv* env) {
    if (g_cacheReady.load(std::memory_order_acquire))
        return true;

    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (g_cacheReady.load(std::memory_order_relaxed))
        return true;

    jclass loginStartLocal = nullptr;
    if (!Java::AssignClass(kLoginStartClass, loginStartLocal) ||
        !loginStartLocal) {
        clearPendingException(env);
        return false;
    }
    g_loginStartClass =
        static_cast<jclass>(env->NewGlobalRef(loginStartLocal));
    env->DeleteLocalRef(loginStartLocal);

    jclass gameProfileLocal = nullptr;
    if (!Java::AssignClass(kGameProfileClass, gameProfileLocal) ||
        !gameProfileLocal) {
        clearPendingException(env);
        clearCache(env);
        return false;
    }
    g_gameProfileClass =
        static_cast<jclass>(env->NewGlobalRef(gameProfileLocal));
    env->DeleteLocalRef(gameProfileLocal);

    if (!g_loginStartClass || !g_gameProfileClass) {
        clearCache(env);
        return false;
    }

    g_profileField = findProfileField(env, g_loginStartClass);
    g_profileConstructor = env->GetMethodID(
        g_gameProfileClass, "<init>",
        "(Ljava/util/UUID;Ljava/lang/String;)V");
    if (!g_profileConstructor)
        clearPendingException(env);

    g_getProfileId = env->GetMethodID(
        g_gameProfileClass, "getId", "()Ljava/util/UUID;");
    if (!g_getProfileId)
        clearPendingException(env);

    if (!g_profileField || !g_profileConstructor) {
        log_warn("[AltManager] Login packet mappings could not be resolved");
        clearCache(env);
        return false;
    }

    g_cacheReady.store(true, std::memory_order_release);
    return true;
}

}  // namespace

void MaybeSpoofLoginPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet || !altManager || !altManager->isActive())
        return;

    if (!ensureCache(env))
        return;

    const bool isLoginStart =
        env->IsInstanceOf(packet, g_loginStartClass) == JNI_TRUE;
    clearPendingException(env);
    if (!isLoginStart)
        return;

    jobject oldProfile = env->GetObjectField(packet, g_profileField);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        oldProfile = nullptr;
    }

    jobject uuid = nullptr;
    if (oldProfile && g_getProfileId) {
        uuid = env->CallObjectMethod(oldProfile, g_getProfileId);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            uuid = nullptr;
        }
    }

    jstring spoofedName = env->NewStringUTF(altManager->settings.spoofedName);
    jobject newProfile = spoofedName
                             ? env->NewObject(g_gameProfileClass,
                                              g_profileConstructor, uuid,
                                              spoofedName)
                             : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        newProfile = nullptr;
    }

    if (newProfile) {
        env->SetObjectField(packet, g_profileField, newProfile);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            log_warn("[AltManager] Failed to replace login GameProfile");
        } else {
            log_debug("[AltManager] Login username changed to '%s'",
                      altManager->settings.spoofedName);
        }
    }

    if (newProfile)
        env->DeleteLocalRef(newProfile);
    if (spoofedName)
        env->DeleteLocalRef(spoofedName);
    if (uuid)
        env->DeleteLocalRef(uuid);
    if (oldProfile)
        env->DeleteLocalRef(oldProfile);
}

void Shutdown(JNIEnv* env) {
    if (!env)
        return;

    std::lock_guard<std::mutex> lock(g_cacheMutex);
    clearCache(env);
}

}  // namespace AltManagerGate
