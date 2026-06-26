#include "altmanager_gate.hpp"

#include "../../module/utility/altmanager.h"
#include "../../util/aes_string.hpp"

namespace {

static jclass g_clsC00 = nullptr;
static jfieldID g_fidProfile = nullptr;
static jclass g_clsProfile = nullptr;
static jfieldID g_fidId = nullptr;
static jfieldID g_fidName = nullptr;
static bool g_initTried = false;
static bool g_initOk = false;

static bool ensureInit(JNIEnv* env) {
    if (g_initOk)
        return true;
    if (g_initTried)
        return false;
    g_initTried = true;

    jclass localPacket = nullptr;
    if (!Java::AssignClass(OBFUSCATE_STRING("net.minecraft.network.login.client.C00PacketLoginStart"), localPacket) ||
        !localPacket) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }
    g_clsC00 = (jclass)env->NewGlobalRef(localPacket);
    env->DeleteLocalRef(localPacket);

    g_fidProfile = env->GetFieldID(g_clsC00, OBFUSCATE_STRING("profile").c_str(),
                                   OBFUSCATE_STRING("Lcom/mojang/authlib/GameProfile;").c_str());
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!g_fidProfile) {
        g_fidProfile = env->GetFieldID(g_clsC00, OBFUSCATE_STRING("field_149304_a").c_str(),
                                       OBFUSCATE_STRING("Lcom/mojang/authlib/GameProfile;").c_str());
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!g_fidProfile)
        return false;

    jclass localProfile = env->FindClass(OBFUSCATE_STRING("com/mojang/authlib/GameProfile").c_str());
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!localProfile)
        return false;
    g_clsProfile = (jclass)env->NewGlobalRef(localProfile);
    env->DeleteLocalRef(localProfile);

    g_fidId = env->GetFieldID(g_clsProfile, OBFUSCATE_STRING("id").c_str(), OBFUSCATE_STRING("Ljava/util/UUID;").c_str());
    if (env->ExceptionCheck())
        env->ExceptionClear();
    g_fidName = env->GetFieldID(g_clsProfile, OBFUSCATE_STRING("name").c_str(), OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (!g_fidId || !g_fidName)
        return false;

    g_initOk = true;
    return true;
}

}  // namespace

namespace AltManagerGate {

void MaybeSpoofLoginPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return;
    if (!::altManager || !::altManager->isActive())
        return;

    if (!ensureInit(env))
        return;

    if (!env->IsInstanceOf(packet, g_clsC00))
        return;

    jobject profile = env->GetObjectField(packet, g_fidProfile);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (profile)
            env->DeleteLocalRef(profile);
        return;
    }
    if (!profile)
        return;

    env->SetObjectField(profile, g_fidId, nullptr);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    jstring newName = env->NewStringUTF(::altManager->settings.spoofedName);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(profile);
        return;
    }
    if (newName) {
        env->SetObjectField(profile, g_fidName, newName);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(newName);
    }

    env->DeleteLocalRef(profile);
}

}  // namespace AltManagerGate
