#include "apollo_packet_observer.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../sdk/network/lunar_packet_listener.hpp"
#include "../../util/logger.hpp"
#include "../../util/team/friends_manager.h"
#include "../../module/movement/blink.h"

namespace {

// Safety limits for protobuf parsing to prevent infinite loops on corrupted packets
constexpr int kMaxVarintBytes = 10;       // Max bytes for a 64-bit varint
constexpr int kMaxParseIterations = 500;  // Max iterations per parsing loop
constexpr int kMaxNestedDepth = 10;       // Max nesting depth for embedded messages

// Safe varint reader: returns false if varint is malformed
inline bool safeReadVarint(const uint8_t* data, int len, int& pos, int& outValue) {
    outValue = 0;
    int shift = 0;
    int bytesRead = 0;
    while (pos < len && bytesRead < kMaxVarintBytes) {
        uint8_t b = data[pos++];
        outValue |= (b & 0x7F) << shift;
        shift += 7;
        bytesRead++;
        if (!(b & 0x80))
            return true;
    }
    return false;  // Malformed varint
}

// Safe varint skipper: returns false if varint is malformed
inline bool safeSkipVarint(const uint8_t* data, int len, int& pos) {
    int bytesRead = 0;
    while (pos < len && bytesRead < kMaxVarintBytes) {
        if (!(data[pos++] & 0x80))
            return true;
        bytesRead++;
    }
    return false;  // Malformed varint
}

inline bool shouldCheckWorldChange() {
    static std::atomic<int64_t> lastCheckMs{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t last = lastCheckMs.load();
    if (nowMs - last < 200) {
        return false;
    }
    lastCheckMs.store(nowMs);
    return true;
}

inline bool checkWorldChangeIfNeeded() {
    if (friendsManager && shouldCheckWorldChange()) {
        return friendsManager->checkWorldChange();
    }
    return false;
}

inline void logWorldChangeSkip(const char* msg) {
    static std::atomic<int64_t> lastLogMs{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t last = lastLogMs.load();
    if (nowMs - last < 500) {
        return;
    }
    lastLogMs.store(nowMs);
    log_debug("%s", msg);
}

inline bool isNameChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

inline std::string extractNameFromJsonLine(const std::string& line) {
    std::string best;
    std::string fallback;
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '"') {
            ++i;
            continue;
        }
        size_t start = i + 1;
        size_t end = start;
        bool escaped = false;
        while (end < line.size()) {
            char ch = line[end];
            if (ch == '\\' && !escaped) {
                escaped = true;
                ++end;
                continue;
            }
            if (ch == '"' && !escaped) {
                break;
            }
            escaped = false;
            ++end;
        }
        if (end >= line.size()) {
            break;
        }
        std::string value = line.substr(start, end - start);
        size_t after = end + 1;
        while (after < line.size() && std::isspace(static_cast<unsigned char>(line[after])))
            ++after;
        if (after < line.size() && line[after] == ':') {
            i = end + 1;
            continue;  // key, not a value
        }

        if (!value.empty() && value[0] == '#' && value.size() >= 4) {
            i = end + 1;
            continue;  // color hex
        }

        std::string cleaned;
        cleaned.reserve(value.size());
        for (unsigned char c : value) {
            if (isNameChar(c))
                cleaned.push_back(static_cast<char>(c));
        }
        if (!cleaned.empty()) {
            if (cleaned.size() >= 2 && best.empty())
                best = cleaned;
            if (fallback.empty())
                fallback = cleaned;
        }
        i = end + 1;
    }
    return best.empty() ? fallback : best;
}

inline std::string extractNameFromJsonLines(const std::vector<std::string>& lines) {
    std::string fallback;
    for (const auto& line : lines) {
        std::string name = extractNameFromJsonLine(line);
        if (!name.empty()) {
            if (name.size() >= 2)
                return name;
            if (fallback.empty())
                fallback = name;
        }
    }
    return fallback;
}

inline bool isWorldChangePacket(JNIEnv* env, jobject packet) {
    static jclass cls_S01PacketJoinGame = nullptr;
    static jclass cls_S07PacketRespawn = nullptr;
    static bool triedInit = false;

    if (!triedInit) {
        triedInit = true;
        jclass local = env->FindClass("net/minecraft/network/play/server/S01PacketJoinGame");
        if (local) {
            cls_S01PacketJoinGame = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
        local = env->FindClass("net/minecraft/network/play/server/S07PacketRespawn");
        if (local) {
            cls_S07PacketRespawn = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    if (cls_S01PacketJoinGame && env->IsInstanceOf(packet, cls_S01PacketJoinGame)) {
        return true;
    }
    if (cls_S07PacketRespawn && env->IsInstanceOf(packet, cls_S07PacketRespawn)) {
        return true;
    }
    return false;
}

inline bool isServerPosLookPacket(JNIEnv* env, jobject packet) {
    static jclass cls_S08PacketPlayerPosLook = nullptr;
    static bool triedInit = false;

    if (!triedInit) {
        triedInit = true;
        jclass local = nullptr;
        if (Java::AssignClass("net.minecraft.network.play.server.S08PacketPlayerPosLook", local)) {
            cls_S08PacketPlayerPosLook = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    return cls_S08PacketPlayerPosLook && env->IsInstanceOf(packet, cls_S08PacketPlayerPosLook);
}

}  // namespace

namespace ApolloPacketObserver {
void JNICALL ApolloPacketObserver::NativeOnReceive(JNIEnv* env, jclass, jobject packet) {
    if (!packet)
        return;

    static std::atomic<int> g_typeLogBudget{0};
    // Removed g_seenApolloSinceWorldChange - serial validation handles stale data

    if (friendsManager && isWorldChangePacket(env, packet)) {
        friendsManager->notifyWorldChangePacket();
        g_typeLogBudget.store(5);
        return;
    }

    if (::blink && ::blink->enabled && isServerPosLookPacket(env, packet)) {
        static std::atomic<int64_t> lastLogMs{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        int64_t last = lastLogMs.load();
        if (nowMs - last > 500) {
            lastLogMs.store(nowMs);
            log_warn("[Blink] Server correction (S08PacketPlayerPosLook) received while blinking");
        }

        ::blink->setEnabled(false);
    }

    bool blockActive = false;

    // Cached JNI references for S3FPacketCustomPayload
    static jclass cls_S3FPacketCustomPayload = nullptr;
    static jmethodID mid_getChannelName = nullptr;
    static jmethodID mid_getBufferData = nullptr;
    static jmethodID mid_getChannelName_legacy = nullptr;
    static jmethodID mid_getPayloadBytes_legacy = nullptr;
    static jclass cls_PacketBuffer = nullptr;
    static jmethodID mid_readableBytes = nullptr;
    static jmethodID mid_readBytes = nullptr;
    static jmethodID mid_array = nullptr;
    static jmethodID mid_readerIndex = nullptr;
    static bool useLegacyCustomPayload = false;
    static bool triedInit = false;

    if (!triedInit) {
        triedInit = true;
        jclass local = env->FindClass("net/minecraft/network/play/server/S3FPacketCustomPayload");
        if (local) {
            cls_S3FPacketCustomPayload = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            mid_getChannelName = env->GetMethodID(cls_S3FPacketCustomPayload, "getChannelName", "()Ljava/lang/String;");
            mid_getBufferData =
                env->GetMethodID(cls_S3FPacketCustomPayload, "getBufferData", "()Lnet/minecraft/network/PacketBuffer;");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_getChannelName = nullptr;
                mid_getBufferData = nullptr;
            }
        }
        // 1.7.10 fallback: obfuscated methods returning byte[]
        if (cls_S3FPacketCustomPayload && (!mid_getChannelName || !mid_getBufferData)) {
            mid_getChannelName_legacy =
                env->GetMethodID(cls_S3FPacketCustomPayload, "func_149169_c", "()Ljava/lang/String;");
            mid_getPayloadBytes_legacy = env->GetMethodID(cls_S3FPacketCustomPayload, "func_149168_d", "()[B");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_getChannelName_legacy = nullptr;
                mid_getPayloadBytes_legacy = nullptr;
            } else if (mid_getChannelName_legacy && mid_getPayloadBytes_legacy) {
                useLegacyCustomPayload = true;
            }
        }
        // PacketBuffer class for reading bytes
        jclass pbLocal = env->FindClass("net/minecraft/network/PacketBuffer");
        if (pbLocal) {
            cls_PacketBuffer = (jclass)env->NewGlobalRef(pbLocal);
            env->DeleteLocalRef(pbLocal);
            mid_readableBytes = env->GetMethodID(cls_PacketBuffer, "readableBytes", "()I");
            mid_readBytes = env->GetMethodID(cls_PacketBuffer, "readBytes", "(I)Lio/netty/buffer/ByteBuf;");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            mid_array = env->GetMethodID(cls_PacketBuffer, "array", "()[B");
            mid_readerIndex = env->GetMethodID(cls_PacketBuffer, "readerIndex", "()I");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_array = nullptr;
                mid_readerIndex = nullptr;
            }
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    // If this is a CustomPayload packet, check for lunar:apollo channel
    if (cls_S3FPacketCustomPayload && env->IsInstanceOf(packet, cls_S3FPacketCustomPayload)) {
        jmethodID channelMethod = mid_getChannelName ? mid_getChannelName : mid_getChannelName_legacy;
        if (channelMethod) {
            jstring channelStr = (jstring)env->CallObjectMethod(packet, channelMethod);
            if (channelStr) {
                const char* channel = env->GetStringUTFChars(channelStr, nullptr);
                if (channel) {
                    // Debug: log all unique channels received
                    static std::unordered_set<std::string> loggedChannels;
                    if (loggedChannels.find(channel) == loggedChannels.end()) {
                        log_debug("[PacketInterceptor] CustomPayload channel: %s", channel);
                        loggedChannels.insert(channel);
                    }

                    // Process lunar:apollo and apollo:json packets
                    if (strcmp(channel, "lunar:apollo") == 0 || strcmp(channel, "apollo:json") == 0) {
                        jbyteArray byteArray = nullptr;
                        jbyte* bytes = nullptr;
                        const uint8_t* data = nullptr;
                        int len = 0;
                        jobject packetBuffer = nullptr;

                        // 1.8+: PacketBuffer -> array() + readerIndex()
                        if (mid_getBufferData && !useLegacyCustomPayload) {
                            packetBuffer = env->CallObjectMethod(packet, mid_getBufferData);
                            if (packetBuffer && !env->ExceptionCheck() && mid_readableBytes && mid_array &&
                                mid_readerIndex) {
                                jint readableBytes = env->CallIntMethod(packetBuffer, mid_readableBytes);
                                if (readableBytes > 0 && readableBytes < 65536) {
                                    jint readerIdx = env->CallIntMethod(packetBuffer, mid_readerIndex);
                                    if (!env->ExceptionCheck()) {
                                        byteArray = (jbyteArray)env->CallObjectMethod(packetBuffer, mid_array);
                                        if (byteArray && !env->ExceptionCheck()) {
                                            jint arrayLen = env->GetArrayLength(byteArray);
                                            if (readerIdx >= 0 && readerIdx + readableBytes <= arrayLen) {
                                                bytes = env->GetByteArrayElements(byteArray, nullptr);
                                                if (bytes) {
                                                    data = (const uint8_t*)(bytes + readerIdx);
                                                    len = readableBytes;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 1.7.10: byte[] payload via func_149168_d
                        if (!data && mid_getPayloadBytes_legacy) {
                            byteArray = (jbyteArray)env->CallObjectMethod(packet, mid_getPayloadBytes_legacy);
                            if (byteArray && !env->ExceptionCheck()) {
                                jint arrayLen = env->GetArrayLength(byteArray);
                                if (arrayLen > 0 && arrayLen < 65536) {
                                    bytes = env->GetByteArrayElements(byteArray, nullptr);
                                    if (bytes) {
                                        data = (const uint8_t*)bytes;
                                        len = arrayLen;
                                    }
                                }
                            }
                        }

                        if (data && len > 0) {
                            // Parse protobuf Any wrapper to get type_url
                            // Protobuf wire format: field 1 (type_url) = tag 0x0A (field 1, wire type 2)
                            // Field 2 (value) = tag 0x12 (field 2, wire type 2)
                            std::string typeUrl;
                            int pos = 0;
                            int iterations = 0;
                            while (pos < len && iterations++ < kMaxParseIterations) {
                                uint8_t tag = data[pos++];
                                int fieldNum = tag >> 3;
                                int wireType = tag & 0x07;

                                if (wireType == 2) {  // Length-delimited
                                    // Read varint length safely
                                    int strLen = 0;
                                    if (!safeReadVarint(data, len, pos, strLen) || strLen < 0 || pos + strLen > len) {
                                        break;  // Malformed packet, abort parsing
                                    }
                                    if (fieldNum == 1 && strLen > 0) {
                                        // type_url field
                                        typeUrl = std::string((const char*)(data + pos), strLen);
                                    }
                                    pos += strLen;
                                } else if (wireType == 0) {  // Varint
                                    if (!safeSkipVarint(data, len, pos)) {
                                        break;  // Malformed varint, abort
                                    }
                                } else {
                                    break;  // Unknown wire type
                                }
                            }

                            // Log if we found a nametag message
                            if (!typeUrl.empty()) {
                                int budget = g_typeLogBudget.load();
                                if (budget > 0) {
                                    if (g_typeLogBudget.compare_exchange_strong(budget, budget - 1)) {
                                        log_debug("[Apollo] Apollo payload type: %s (block=%d)", typeUrl.c_str(),
                                                  blockActive ? 1 : 0);
                                    }
                                }
                                static std::atomic<int> teamCount{0};

                                // Detect and parse Team messages
                                if (typeUrl.find("UpdateTeamMembers") != std::string::npos) {
                                    // Parse value field (field 2) from Any wrapper
                                    const uint8_t* valueData = nullptr;
                                    int valueLen = 0;
                                    int parsePos = 0;
                                    int parseIter = 0;
                                    while (parsePos < len && parseIter++ < kMaxParseIterations) {
                                        uint8_t tag = data[parsePos++];
                                        int fieldNum = tag >> 3;
                                        int wireType = tag & 0x07;
                                        if (wireType == 2) {
                                            int fLen = 0;
                                            if (!safeReadVarint(data, len, parsePos, fLen) || fLen < 0 ||
                                                parsePos + fLen > len) {
                                                break;
                                            }
                                            if (fieldNum == 2 && fLen > 0) {
                                                valueData = data + parsePos;
                                                valueLen = fLen;
                                            }
                                            parsePos += fLen;
                                        } else if (wireType == 0) {
                                            if (!safeSkipVarint(data, len, parsePos))
                                                break;
                                        } else
                                            break;
                                    }

                                    // Parse UpdateTeamMembersMessage: field 1 = members (repeated TeamMember)
                                    if (valueData && valueLen > 0) {
                                        std::vector<std::pair<uint64_t, uint64_t>> teammateUuids;

                                        // Debug: log valueLen once
                                        static std::atomic<int> parseDebugCount{0};
                                        bool shouldDebug = parseDebugCount.load() < 3;
                                        if (shouldDebug) {
                                            log_debug("[Apollo] Team value len: %d bytes", valueLen);
                                        }

                                        int memberCount = 0;
                                        int vPos = 0;
                                        int vIter = 0;
                                        while (vPos < valueLen && vIter++ < kMaxParseIterations) {
                                            uint8_t vTag = valueData[vPos++];
                                            int vFieldNum = vTag >> 3;
                                            int vWireType = vTag & 0x07;

                                            if (vWireType == 2) {  // Length-delimited
                                                int vLen = 0;
                                                if (!safeReadVarint(valueData, valueLen, vPos, vLen) || vLen < 0 ||
                                                    vPos + vLen > valueLen) {
                                                    break;
                                                }
                                                if (vFieldNum == 1 && vLen > 0) {
                                                    memberCount++;
                                                    // TeamMember message: field 1 = player_uuid
                                                    const uint8_t* memberData = valueData + vPos;
                                                    int mPos = 0;
                                                    uint64_t uuidHigh = 0, uuidLow = 0;

                                                    // Debug: log first member's fields
                                                    if (memberCount == 1 && shouldDebug) {
                                                        log_debug(
                                                            "[Apollo]   Member1 len=%d, first bytes: %02x %02x %02x "
                                                            "%02x %02x",
                                                            vLen, vLen > 0 ? memberData[0] : 0,
                                                            vLen > 1 ? memberData[1] : 0, vLen > 2 ? memberData[2] : 0,
                                                            vLen > 3 ? memberData[3] : 0, vLen > 4 ? memberData[4] : 0);
                                                    }
                                                    int mIter = 0;
                                                    while (mPos < vLen && mIter++ < kMaxParseIterations) {
                                                        uint8_t mTag = memberData[mPos++];
                                                        int mFieldNum = mTag >> 3;
                                                        int mWireType = mTag & 0x07;
                                                        if (mWireType == 2) {
                                                            int mLen = 0;
                                                            if (!safeReadVarint(memberData, vLen, mPos, mLen) ||
                                                                mLen < 0 || mPos + mLen > vLen) {
                                                                break;
                                                            }
                                                            if (mFieldNum == 1 && mLen > 0) {
                                                                // player_uuid (Uuid proto) - uses fixed64 for
                                                                // high64/low64
                                                                const uint8_t* uuidData = memberData + mPos;
                                                                int uPos = 0;
                                                                int uIter = 0;
                                                                while (uPos < mLen && uIter++ < kMaxParseIterations) {
                                                                    uint8_t uTag = uuidData[uPos++];
                                                                    int uFieldNum = uTag >> 3;
                                                                    int uWireType = uTag & 0x07;

                                                                    if (uWireType == 1 && uPos + 8 <= mLen) {
                                                                        // Wire type 1 = fixed64 (8 bytes little-endian)
                                                                        uint64_t val = 0;
                                                                        for (int i = 0; i < 8; i++) {
                                                                            val |= (uint64_t)uuidData[uPos + i]
                                                                                   << (i * 8);
                                                                        }
                                                                        uPos += 8;
                                                                        if (uFieldNum == 1)
                                                                            uuidHigh = val;
                                                                        else if (uFieldNum == 2)
                                                                            uuidLow = val;
                                                                    } else if (uWireType == 0) {
                                                                        // Wire type 0 = varint (skip safely)
                                                                        if (!safeSkipVarint(uuidData, mLen, uPos))
                                                                            break;
                                                                    } else
                                                                        break;
                                                                }
                                                                if (uuidHigh != 0 || uuidLow != 0) {
                                                                    teammateUuids.push_back({uuidHigh, uuidLow});
                                                                }
                                                            }
                                                            mPos += mLen;
                                                        } else if (mWireType == 0) {
                                                            while (mPos < vLen && (memberData[mPos++] & 0x80))
                                                                ;
                                                        } else
                                                            break;
                                                    }
                                                }
                                                vPos += vLen;
                                            } else if (vWireType == 0) {
                                                while (vPos < valueLen && (valueData[vPos++] & 0x80))
                                                    ;
                                            } else
                                                break;
                                        }

                                        if (shouldDebug) {
                                            log_debug("[Apollo] Team: found %d members, extracted %zu UUIDs",
                                                      memberCount, teammateUuids.size());
                                            parseDebugCount++;
                                        }

                                        // Log team update once
                                        if (!teammateUuids.empty() && teamCount.load() < 5) {
                                            log_ok("[Apollo] Team update: %d teammates", (int)teammateUuids.size());
                                            for (size_t i = 0; i < teammateUuids.size(); i++) {
                                                char uuidStr[64];
                                                uint64_t hi = teammateUuids[i].first;
                                                uint64_t lo = teammateUuids[i].second;
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(hi >> 32), (uint16_t)(hi >> 16), (uint16_t)hi,
                                                         (uint16_t)(lo >> 48),
                                                         (unsigned long long)(lo & 0xFFFFFFFFFFFFULL));
                                                log_debug("  Teammate[%d]: %s", (int)i, uuidStr);
                                            }
                                            teamCount++;
                                        }

                                        // Register teammates with FriendsManager
                                        // Use diff-based sync to properly handle teammate removal
                                        if (friendsManager) {
                                            // Build new UUID set from packet
                                            std::set<std::string> newTeammates;
                                            for (size_t i = 0; i < teammateUuids.size(); i++) {
                                                char uuidStr[64];
                                                uint64_t hi = teammateUuids[i].first;
                                                uint64_t lo = teammateUuids[i].second;
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(hi >> 32), (uint16_t)(hi >> 16), (uint16_t)hi,
                                                         (uint16_t)(lo >> 48),
                                                         (unsigned long long)(lo & 0xFFFFFFFFFFFFULL));
                                                newTeammates.insert(uuidStr);
                                            }

                                            friendsManager->syncLunarTeammates(newTeammates);

                                            // Only log when teammate count changes significantly
                                            static size_t lastTeammateCount = 0;
                                            size_t currentCount = newTeammates.size();
                                            if (currentCount != lastTeammateCount) {
                                                log_ok("[Apollo] Team synced: %d teammate(s)", (int)currentCount);
                                                lastTeammateCount = currentCount;
                                            }
                                        }
                                    }
                                } else if (!blockActive && typeUrl.find("OverrideNametag") != std::string::npos) {
                                    // Parse value field (field 2) from Any wrapper
                                    const uint8_t* valueData = nullptr;
                                    int valueLen = 0;
                                    int parsePos = 0;
                                    int parseIter = 0;
                                    while (parsePos < len && parseIter++ < kMaxParseIterations) {
                                        uint8_t tag = data[parsePos++];
                                        int fieldNum = tag >> 3;
                                        int wireType = tag & 0x07;
                                        if (wireType == 2) {
                                            int fLen = 0;
                                            if (!safeReadVarint(data, len, parsePos, fLen) || fLen < 0 ||
                                                parsePos + fLen > len) {
                                                break;
                                            }
                                            if (fieldNum == 2 && fLen > 0) {
                                                valueData = data + parsePos;
                                                valueLen = fLen;
                                            }
                                            parsePos += fLen;
                                        } else if (wireType == 0) {
                                            if (!safeSkipVarint(data, len, parsePos))
                                                break;
                                        } else
                                            break;
                                    }

                                    // Parse OverrideNametagMessage:
                                    // field 1 = player_uuid (Uuid)
                                    // field 3 = adventure_json_lines (repeated string)
                                    if (valueData && valueLen > 0) {
                                        uint64_t uuidHigh = 0, uuidLow = 0;
                                        std::vector<std::string> jsonLines;

                                        int vPos = 0;
                                        int fieldCount = 0;
                                        int vIter = 0;
                                        while (vPos < valueLen && vIter++ < kMaxParseIterations) {
                                            uint8_t vTag = valueData[vPos++];
                                            int vFieldNum = vTag >> 3;
                                            int vWireType = vTag & 0x07;

                                            if (vWireType == 2) {
                                                int vLen = 0;
                                                if (!safeReadVarint(valueData, valueLen, vPos, vLen) || vLen < 0 ||
                                                    vPos + vLen > valueLen) {
                                                    break;
                                                }
                                                if (vFieldNum == 1 && vLen > 0) {
                                                    const uint8_t* uuidData = valueData + vPos;
                                                    int uPos = 0;
                                                    int uIter = 0;
                                                    while (uPos < vLen && uIter++ < kMaxParseIterations) {
                                                        uint8_t uTag = uuidData[uPos++];
                                                        int uFieldNum = uTag >> 3;
                                                        int uWireType = uTag & 0x07;

                                                        if (uWireType == 1 && uPos + 8 <= vLen) {
                                                            // Wire type 1 = fixed64
                                                            uint64_t val = 0;
                                                            for (int i = 0; i < 8; i++) {
                                                                val |= (uint64_t)uuidData[uPos + i] << (i * 8);
                                                            }
                                                            uPos += 8;
                                                            if (uFieldNum == 1)
                                                                uuidHigh = val;
                                                            else if (uFieldNum == 2)
                                                                uuidLow = val;
                                                        } else if (uWireType == 0) {
                                                            if (!safeSkipVarint(uuidData, vLen, uPos))
                                                                break;
                                                        } else
                                                            break;
                                                    }
                                                } else if (vFieldNum == 3 && vLen > 0) {
                                                    jsonLines.push_back(
                                                        std::string((const char*)(valueData + vPos), vLen));
                                                }
                                                vPos += vLen;
                                            } else if (vWireType == 0) {
                                                if (!safeSkipVarint(valueData, valueLen, vPos))
                                                    break;
                                            } else
                                                break;
                                            fieldCount++;
                                        }

                                        // Debug parsing results (limited)
                                        static std::atomic<int> nametagDebug{0};
                                        if (nametagDebug.load() < 3) {
                                            log_debug("[Apollo] Nametag parsed: uuidHigh=%llx uuidLow=%llx lines=%zu",
                                                      (unsigned long long)uuidHigh, (unsigned long long)uuidLow,
                                                      jsonLines.size());
                                            nametagDebug++;
                                        }

                                        // Debug: show a few distinct first-line previews
                                        static std::atomic<int> jsonPreviewCount{0};
                                        static std::string lastJsonPreview;
                                        if (!jsonLines.empty() && jsonPreviewCount.load() < 10) {
                                            std::string preview = jsonLines[0];
                                            if (preview.length() > 80)
                                                preview = preview.substr(0, 80) + "...";
                                            if (preview != lastJsonPreview) {
                                                // log_debug("[Apollo]   JSON[0]: %s", preview.c_str());
                                                lastJsonPreview = preview;
                                                jsonPreviewCount++;
                                            }
                                        }

                                        // Detect color from JSON lines (prefer explicit Adventure color, fallback to
                                        // format codes)
                                        std::string detectedColor = "unknown";
                                        bool foundGreen = false;  // GREEN takes priority

                                        // Check if JSON has "extra" array - used as teammate tag in HCTRanked
                                        bool hasExtra = false;
                                        for (const auto& line : jsonLines) {
                                            if (line.find("\"extra\"") != std::string::npos) {
                                                hasExtra = true;
                                                break;
                                            }
                                        }

                                        // Scan ALL color values in the JSON (including nested "extra" arrays)
                                        // GREEN takes priority over other colors since it indicates teammate
                                        if (!jsonLines.empty()) {
                                            const std::string colorKey = "\"color\":\"#";

                                            static const std::unordered_map<std::string, std::string> kHexToColor = {
                                                // Vanilla chat/motd palette
                                                {"000000", "BLACK"},
                                                {"0000AA", "DARK_BLUE"},
                                                {"00AA00", "DARK_GREEN"},
                                                {"00AAAA", "DARK_AQUA"},
                                                {"AA0000", "DARK_RED"},
                                                {"AA00AA", "DARK_PURPLE"},
                                                {"FFAA00", "GOLD"},
                                                {"AAAAAA", "GRAY"},
                                                {"555555", "DARK_GRAY"},
                                                {"5555FF", "BLUE"},
                                                {"55FF55", "GREEN"},
                                                {"55FFFF", "AQUA"},
                                                {"FF5555", "RED"},
                                                {"FF55FF", "LIGHT_PURPLE"},
                                                {"FFFF55", "YELLOW"},
                                                {"FFFFFF", "WHITE"},
                                                // Extra hex observed in Apollo nametags
                                                {"55FF5F", "GREEN"},  // bright green
                                                {"FF5551", "RED"},    // bright red variant
                                                {"AA0003", "DARK_RED"},
                                                {"FF55FA", "LIGHT_PURPLE"},
                                                {"AA00AC", "DARK_PURPLE"},
                                                {"00AAAC", "DARK_AQUA"},
                                                {"5555FE", "BLUE"},
                                                {"FFFFFA", "WHITE"},
                                                {"AAAAAC", "GRAY"},
                                                {"55FFFA", "AQUA"},
                                                {"FFAA02", "GOLD"},
                                            };

                                            for (const auto& line : jsonLines) {
                                                size_t searchPos = 0;
                                                while ((searchPos = line.find(colorKey, searchPos)) !=
                                                       std::string::npos) {
                                                    if (searchPos + colorKey.size() + 6 <= line.size()) {
                                                        std::string hex = line.substr(searchPos + colorKey.size(), 6);
                                                        for (auto& ch : hex)
                                                            ch = (char)std::toupper((unsigned char)ch);

                                                        auto it = kHexToColor.find(hex);
                                                        if (it != kHexToColor.end()) {
                                                            // GREEN/DARK_GREEN always takes priority (teammate
                                                            // indicator)
                                                            if (it->second == "GREEN" || it->second == "DARK_GREEN") {
                                                                detectedColor = it->second;
                                                                foundGreen = true;
                                                                break;
                                                            }
                                                            if (detectedColor == "unknown") {
                                                                detectedColor = it->second;
                                                            }
                                                        }
                                                    }
                                                    searchPos += colorKey.size();
                                                }
                                                if (foundGreen)
                                                    break;
                                            }
                                        }

                                        // Fallback: Minecraft format codes (B'X) - only if Adventure color wasn't found
                                        if (detectedColor == "unknown") {
                                            for (const auto& line : jsonLines) {
                                                size_t pos = line.find(
                                                    "\xc2"
                                                    "\xa7");
                                                while (pos != std::string::npos && pos + 2 < line.size()) {
                                                    char code = (char)std::tolower((unsigned char)line[pos + 2]);
                                                    switch (code) {
                                                        case '0':
                                                            detectedColor = "BLACK";
                                                            break;
                                                        case '1':
                                                            detectedColor = "DARK_BLUE";
                                                            break;
                                                        case '2':
                                                            detectedColor = "DARK_GREEN";
                                                            break;
                                                        case '3':
                                                            detectedColor = "DARK_AQUA";
                                                            break;
                                                        case '4':
                                                            detectedColor = "DARK_RED";
                                                            break;
                                                        case '5':
                                                            detectedColor = "DARK_PURPLE";
                                                            break;
                                                        case '6':
                                                            detectedColor = "GOLD";
                                                            break;
                                                        case '7':
                                                            detectedColor = "GRAY";
                                                            break;
                                                        case '8':
                                                            detectedColor = "DARK_GRAY";
                                                            break;
                                                        case '9':
                                                            detectedColor = "BLUE";
                                                            break;
                                                        case 'a':
                                                            detectedColor = "GREEN";
                                                            break;
                                                        case 'b':
                                                            detectedColor = "AQUA";
                                                            break;
                                                        case 'c':
                                                            detectedColor = "RED";
                                                            break;
                                                        case 'd':
                                                            detectedColor = "LIGHT_PURPLE";
                                                            break;
                                                        case 'e':
                                                            detectedColor = "YELLOW";
                                                            break;
                                                        case 'f':
                                                            detectedColor = "WHITE";
                                                            break;
                                                        default:
                                                            break;
                                                    }
                                                    if (detectedColor != "unknown")
                                                        break;
                                                    pos = line.find(
                                                        "\xc2"
                                                        "\xa7",
                                                        pos + 3);
                                                }
                                                if (detectedColor != "unknown")
                                                    break;
                                            }
                                        }  // end fallback if

                                        // If still unknown, log a limited preview to diagnose color mapping
                                        if (detectedColor == "unknown" && !jsonLines.empty()) {
                                            static std::unordered_set<std::string> unknownLogged;
                                            std::string preview = jsonLines[0];
                                            if (preview.length() > 80)
                                                preview = preview.substr(0, 80) + "...";
                                            if (unknownLogged.size() < 20 &&
                                                unknownLogged.find(preview) == unknownLogged.end()) {
                                                char uuidStr[64];
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(uuidHigh >> 32), (uint16_t)(uuidHigh >> 16),
                                                         (uint16_t)uuidHigh, (uint16_t)(uuidLow >> 48),
                                                         (unsigned long long)(uuidLow & 0xFFFFFFFFFFFFULL));
                                                log_debug("[Apollo] Nametag unknown color: UUID=%s JSON[0]=%s", uuidStr,
                                                          preview.c_str());
                                                unknownLogged.insert(preview);
                                            }
                                        }

                                        // Log the parsed nametag with color
                                        if ((uuidHigh != 0 || uuidLow != 0) && !jsonLines.empty()) {
                                            // Extract player name from JSON lines (skip formatting-only tokens)
                                            std::string playerName = extractNameFromJsonLines(jsonLines);
                                            if (playerName.empty())
                                                playerName = "?";

                                            static std::unordered_map<std::string, std::string> lastNametagColor;
                                            std::string uuidKey =
                                                std::to_string(uuidHigh) + ":" + std::to_string(uuidLow);
                                            auto itColor = lastNametagColor.find(uuidKey);
                                            bool isColorChange = (itColor != lastNametagColor.end()) &&
                                                                 (itColor->second != detectedColor);
                                            bool shouldLog = (itColor == lastNametagColor.end()) ||
                                                             (itColor->second != detectedColor);

                                            // Verbose debug: log raw JSON when color changes (teammate signals only)
                                            if (isColorChange && (detectedColor == "GREEN" ||
                                                                  detectedColor == "DARK_GREEN" || hasExtra)) {
                                                char uuidStr[64];
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(uuidHigh >> 32), (uint16_t)(uuidHigh >> 16),
                                                         (uint16_t)uuidHigh, (uint16_t)(uuidLow >> 48),
                                                         (unsigned long long)(uuidLow & 0xFFFFFFFFFFFFULL));
                                                log_warn("[Apollo] COLOR CHANGE for %s: %s -> %s", uuidStr,
                                                         itColor->second.c_str(), detectedColor.c_str());
                                                // Log all JSON lines for debugging
                                                for (size_t jIdx = 0; jIdx < jsonLines.size() && jIdx < 3; jIdx++) {
                                                    std::string jsonPreview = jsonLines[jIdx];
                                                    if (jsonPreview.length() > 120)
                                                        jsonPreview = jsonPreview.substr(0, 120) + "...";
                                                    log_debug("[Apollo]   JSON[%zu]: %s", jIdx, jsonPreview.c_str());
                                                }
                                            }

                                            if (shouldLog) {
                                                lastNametagColor[uuidKey] = detectedColor;
                                                bool isTeammateSignal = (detectedColor == "GREEN" ||
                                                                         detectedColor == "DARK_GREEN" || foundGreen);
                                                if (isTeammateSignal || detectedColor == "unknown") {
                                                    log_ok("[Apollo] Nametag: \"%s\" -> %s", playerName.c_str(),
                                                           detectedColor.c_str());
                                                }
                                            }

                                            // Register nametag color with FriendsManager for team detection
                                            if (friendsManager && detectedColor != "unknown") {
                                                char uuidStr[64];
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(uuidHigh >> 32), (uint16_t)(uuidHigh >> 16),
                                                         (uint16_t)uuidHigh, (uint16_t)(uuidLow >> 48),
                                                         (unsigned long long)(uuidLow & 0xFFFFFFFFFFFFULL));
                                                bool hasTeammateExtra = foundGreen;
                                                friendsManager->setApolloNametagColor(uuidStr, detectedColor,
                                                                                      hasTeammateExtra);

                                                // Focused teammate debug (throttled per UUID)
                                                if (foundGreen) {
                                                    static std::unordered_map<std::string, int64_t> lastSignalLogMs;
                                                    auto now = std::chrono::steady_clock::now().time_since_epoch();
                                                    int64_t nowMs =
                                                        std::chrono::duration_cast<std::chrono::milliseconds>(now)
                                                            .count();
                                                    int64_t& last = lastSignalLogMs[uuidStr];
                                                    if (nowMs - last > 2000) {
                                                        log_debug(
                                                            "[Apollo] Teammate signal: %s name=%s color=%s extra=%d "
                                                            "green=%d lines=%zu",
                                                            uuidStr, playerName.c_str(), detectedColor.c_str(),
                                                            hasExtra ? 1 : 0, foundGreen ? 1 : 0, jsonLines.size());
                                                        for (size_t jIdx = 0; jIdx < jsonLines.size() && jIdx < 3;
                                                             jIdx++) {
                                                            std::string jsonPreview = jsonLines[jIdx];
                                                            if (jsonPreview.length() > 200)
                                                                jsonPreview = jsonPreview.substr(0, 200) + "...";
                                                            log_debug("[Apollo]   RAW[%zu]: %s", jIdx,
                                                                      jsonPreview.c_str());
                                                        }
                                                        last = nowMs;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (byteArray && bytes) {
                            env->ReleaseByteArrayElements(byteArray, bytes, JNI_ABORT);
                        }
                        if (packetBuffer) {
                            env->DeleteLocalRef(packetBuffer);
                        }
                        if (env->ExceptionCheck())
                            env->ExceptionClear();
                    }
                    env->ReleaseStringUTFChars(channelStr, channel);
                }
                env->DeleteLocalRef(channelStr);
            }
        }
    }
    // Check for S3EPacketTeams (Minecraft scoreboard teams)
    static jclass cls_S3EPacketTeams = nullptr;
    static jmethodID mid_getAction = nullptr;
    static jmethodID mid_getName = nullptr;
    static jmethodID mid_getPlayers = nullptr;
    static bool triedTeamsInit = false;

    if (!triedTeamsInit) {
        triedTeamsInit = true;
        jclass local = env->FindClass("net/minecraft/network/play/server/S3EPacketTeams");
        if (local) {
            cls_S3EPacketTeams = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            mid_getAction = env->GetMethodID(cls_S3EPacketTeams, "getAction", "()I");
            mid_getName = env->GetMethodID(cls_S3EPacketTeams, "getName", "()Ljava/lang/String;");
            mid_getPlayers = env->GetMethodID(cls_S3EPacketTeams, "getPlayers", "()Ljava/util/Collection;");
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    if (cls_S3EPacketTeams && env->IsInstanceOf(packet, cls_S3EPacketTeams)) {
        static std::atomic<int> teamPacketCount{0};

        // Get action type: 0=create, 1=remove, 2=update, 3=add players, 4=remove players
        jint action = mid_getAction ? env->CallIntMethod(packet, mid_getAction) : -1;

        // Get team name
        jstring nameStr = mid_getName ? (jstring)env->CallObjectMethod(packet, mid_getName) : nullptr;
        const char* teamName = nameStr ? env->GetStringUTFChars(nameStr, nullptr) : nullptr;

        // Get players collection
        jobject playersCollection = mid_getPlayers ? env->CallObjectMethod(packet, mid_getPlayers) : nullptr;
        int playersCount = 0;
        if (playersCollection) {
            jclass cls_Collection = env->FindClass("java/util/Collection");
            jmethodID mid_size = env->GetMethodID(cls_Collection, "size", "()I");
            if (mid_size) {
                playersCount = env->CallIntMethod(playersCollection, mid_size);
            }
            env->DeleteLocalRef(cls_Collection);
            env->DeleteLocalRef(playersCollection);
        }

        // Only log meaningful team packets (not visual formatting)
        // Skip UPDATE with 0 players (these are scoreboard sidebar formatting)
        bool isRelevant = (action == 0 || action == 3 || action == 4 || playersCount > 0);

        if (isRelevant && teamPacketCount.load() < 20) {
            const char* actionStr = "";
            switch (action) {
                case 0:
                    actionStr = "CREATE";
                    break;
                case 1:
                    actionStr = "REMOVE";
                    break;
                case 2:
                    actionStr = "UPDATE";
                    break;
                case 3:
                    actionStr = "ADD_PLAYERS";
                    break;
                case 4:
                    actionStr = "REMOVE_PLAYERS";
                    break;
                default:
                    actionStr = "UNKNOWN";
                    break;
            }

            // log_ok("[Scoreboard] Team packet: action=%s team='%s' players=%d",
            //        actionStr, teamName ? teamName : "?", playersCount);

            teamPacketCount++;
        }

        if (teamName)
            env->ReleaseStringUTFChars(nameStr, teamName);
        if (nameStr)
            env->DeleteLocalRef(nameStr);
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    // Process Lunar Client nametag packets (legacy path)
    if (!blockActive && LunarPacketListener::IsReady()) {
        LunarPacketListener::processIncomingPacket(env, packet);
    }
}

}  // namespace ApolloPacketObserver
