#pragma once

#include "../java.h"

#include <string>

namespace AltManagerGate {

void MaybeSpoofLoginPacket(JNIEnv *env, jobject packet);

// Read/write net.minecraft.util.Session through reflection. Any argument may be
// null to leave that field untouched; empty strings are also skipped unless
// overwriteEmpty is set (needed to restore an account that has no token/uuid).
bool SetSession(JNIEnv* env, const char* username, const char* uuid,
                const char* token, const char* sessionType,
                bool overwriteEmpty = false);
bool GetSession(JNIEnv* env, std::string& username, std::string& uuid,
                std::string& token, std::string& sessionType);

void Shutdown(JNIEnv* env);

} // namespace AltManagerGate
