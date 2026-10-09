#include "noitemrelease_gate.hpp"

#include <initializer_list>
#include <string>

#include "../../module/utility/noitemrelease.h"
#include "../../util/aes_string.hpp"

namespace {

// C07PacketPlayerDigging carries RELEASE_USE_ITEM as an enum constant on 1.8.9
// and as the int 5 on 1.7.10. Classes and constants are compared by identity so
// clients that rename them (CheatBreaker) match the same way vanilla does.
struct DiggingPacketIds {
    jclass cls = nullptr;
    jfieldID actionField = nullptr;
    jobject releaseUseItem = nullptr;
    jfieldID intStatusField = nullptr;
};

jfieldID firstField(JNIEnv* env, jclass cls, const char* sig, std::initializer_list<std::string> names) {
    for (const auto& name : names) {
        jfieldID id = Java::GetFieldID(env, cls, name.c_str(), sig);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (id)
            return id;
    }
    return nullptr;
}

DiggingPacketIds resolveDiggingPacket(JNIEnv* env) {
    DiggingPacketIds ids;
    jclass local = nullptr;
    if (!Java::AssignClass(OBFUSCATE_STRING("net.minecraft.network.play.client.C07PacketPlayerDigging"), local))
        return ids;
    ids.cls = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);

    if (Java::Version == MinecraftVersion::MC_1_7_10) {
        ids.intStatusField = firstField(env, ids.cls, "I", {OBFUSCATE_STRING("status"), OBFUSCATE_STRING("field_149508_e")});
        return ids;
    }

    const std::string actionSig = OBFUSCATE_STRING("Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;");
    ids.actionField = firstField(env, ids.cls, actionSig.c_str(), {OBFUSCATE_STRING("status"), OBFUSCATE_STRING("field_149508_e")});

    jclass action = nullptr;
    if (!Java::AssignClass(OBFUSCATE_STRING("net.minecraft.network.play.client.C07PacketPlayerDigging$Action"), action))
        return ids;
    jfieldID constant =
        Java::GetStaticFieldID(env, action, OBFUSCATE_STRING("RELEASE_USE_ITEM").c_str(), actionSig.c_str());
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (constant) {
        jobject value = env->GetStaticObjectField(action, constant);
        if (value) {
            ids.releaseUseItem = env->NewGlobalRef(value);
            env->DeleteLocalRef(value);
        }
    }
    env->DeleteLocalRef(action);
    return ids;
}

bool isReleaseUseItem(JNIEnv* env, jobject packet) {
    static const DiggingPacketIds ids = resolveDiggingPacket(env);
    if (!ids.cls || !env->IsInstanceOf(packet, ids.cls))
        return false;

    if (ids.actionField && ids.releaseUseItem) {
        jobject status = env->GetObjectField(packet, ids.actionField);
        const bool release = status && env->IsSameObject(status, ids.releaseUseItem);
        if (status)
            env->DeleteLocalRef(status);
        return release;
    }
    return ids.intStatusField && env->GetIntField(packet, ids.intStatusField) == 5;
}

}  // namespace

namespace NoItemReleaseGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    if (!packet || !::noItemRelease || !::noItemRelease->enabled || !::noItemRelease->wasUsingItem)
        return JNI_TRUE;
    return isReleaseUseItem(env, packet) ? JNI_FALSE : JNI_TRUE;
}

}  // namespace NoItemReleaseGate
