#include "noitemrelease_gate.hpp"

#include <cstring>
#include "../../module/utility/noitemrelease.h"

namespace {

static bool shouldSendPacket(JNIEnv* env, jobject packet) {
    if (!packet)
        return true;

    if (!::noItemRelease || !::noItemRelease->enabled || !::noItemRelease->wasUsingItem) {
        return true;
    }

    // ===== NOITEMRELEASE PACKET BLOCKING =====
    // Check for C07PacketPlayerDigging with RELEASE_USE_ITEM action
    jclass cls = env->GetObjectClass(packet);
    if (!cls) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return true;
    }

    jclass clsCls = env->GetObjectClass(cls);
    if (clsCls) {
        jmethodID mid = env->GetMethodID(clsCls, "getName", "()Ljava/lang/String;");
        if (mid) {
            jstring jname = (jstring)env->CallObjectMethod(cls, mid);
            if (jname) {
                const char* cstr = env->GetStringUTFChars(jname, nullptr);
                if (cstr) {
                    if (strstr(cstr, "C07PacketPlayerDigging") != nullptr) {
                        bool shouldBlock = false;

                        // Try 1.8.9+ method first: enum Action field
                        jfieldID statusField = env->GetFieldID(
                            cls, "status", "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;");
                        if (env->ExceptionCheck())
                            env->ExceptionClear();

                        if (statusField) {
                            jobject statusObj = env->GetObjectField(packet, statusField);
                            if (statusObj) {
                                jclass statusCls = env->GetObjectClass(statusObj);
                                if (statusCls) {
                                    jmethodID nameMethod = env->GetMethodID(statusCls, "name", "()Ljava/lang/String;");
                                    if (nameMethod) {
                                        jstring enumName = (jstring)env->CallObjectMethod(statusObj, nameMethod);
                                        if (enumName) {
                                            const char* statusStr = env->GetStringUTFChars(enumName, nullptr);
                                            if (statusStr) {
                                                shouldBlock = (strcmp(statusStr, "RELEASE_USE_ITEM") == 0);
                                                env->ReleaseStringUTFChars(enumName, statusStr);
                                            }
                                            env->DeleteLocalRef(enumName);
                                        }
                                    }
                                    env->DeleteLocalRef(statusCls);
                                }
                                env->DeleteLocalRef(statusObj);
                            }
                        } else {
                            // 1.7.10 path: int field (field_149508_e), value 5 = RELEASE_USE_ITEM
                            jfieldID intStatusField = env->GetFieldID(cls, "field_149508_e", "I");
                            if (env->ExceptionCheck())
                                env->ExceptionClear();

                            if (!intStatusField) {
                                intStatusField = env->GetFieldID(cls, "status", "I");
                                if (env->ExceptionCheck())
                                    env->ExceptionClear();
                            }

                            if (intStatusField) {
                                jint statusValue = env->GetIntField(packet, intStatusField);
                                shouldBlock = (statusValue == 5);
                            }
                        }

                        env->ReleaseStringUTFChars(jname, cstr);
                        env->DeleteLocalRef(jname);
                        env->DeleteLocalRef(clsCls);
                        env->DeleteLocalRef(cls);

                        if (shouldBlock) {
                            return false;
                        }
                        goto skip_cleanup;
                    }
                    env->ReleaseStringUTFChars(jname, cstr);
                }
                env->DeleteLocalRef(jname);
            }
        }
        env->DeleteLocalRef(clsCls);
    }
skip_cleanup:
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return true;
}

}  // namespace

namespace NoItemReleaseGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    return shouldSendPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

}  // namespace NoItemReleaseGate
