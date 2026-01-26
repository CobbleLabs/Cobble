#include "apollo_handshake.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../util/logger.hpp"
#include "../java.h"
#include "apollo_packet_observer.hpp"

namespace ApolloHandshake {

// ============================================================================
// State
// ============================================================================

static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_debugLogging{true};  // Start with debug ON
static std::atomic<uint64_t> g_worldSerial{0};
static std::atomic<uint64_t> g_lastHandshakeSerial{0};
static std::mutex g_mutex;
static std::atomic<int64_t> g_lastWorldChangeMs{0};  // debounces repeated world-change triggers

static Stats g_stats;

// Pending handshake - for when mc isn't ready yet
static std::atomic<bool> g_pendingHandshake{false};
static std::atomic<int64_t> g_pendingHandshakeTimeMs{0};
static constexpr int64_t kHandshakeRetryDelayMs = 100;  // Retry every 100ms
static constexpr int kMaxHandshakeRetries = 50;         // Max 5 seconds of retries
static std::atomic<int> g_handshakeRetryCount{0};

// JNI cached references
static jclass g_clsNetHandler = nullptr;
static jmethodID g_midAddToSendQueue = nullptr;
static jclass g_clsC17Packet = nullptr;
static jmethodID g_midC17Ctor = nullptr;
static jclass g_clsPacketBuffer = nullptr;
static jmethodID g_midPacketBufferCtor = nullptr;
static jclass g_clsUnpooled = nullptr;
static jmethodID g_midWrappedBuffer = nullptr;

// ============================================================================
// Protobuf Encoding Helpers
// ============================================================================

// Write a varint to a buffer
static void writeVarint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value > 0x7F) {
        buf.push_back((uint8_t)((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back((uint8_t)value);
}

// Write a length-delimited field (string or bytes)
static void writeLengthDelimited(std::vector<uint8_t>& buf, int fieldNum, const uint8_t* data, size_t len) {
    // Field tag: (fieldNum << 3) | 2 (wire type 2 = length-delimited)
    writeVarint(buf, (fieldNum << 3) | 2);
    writeVarint(buf, len);
    buf.insert(buf.end(), data, data + len);
}

// Write a string field
static void writeString(std::vector<uint8_t>& buf, int fieldNum, const std::string& str) {
    writeLengthDelimited(buf, fieldNum, (const uint8_t*)str.data(), str.size());
}

// Write a varint field
static void writeVarintField(std::vector<uint8_t>& buf, int fieldNum, uint64_t value) {
    // Field tag: (fieldNum << 3) | 0 (wire type 0 = varint)
    writeVarint(buf, (fieldNum << 3) | 0);
    writeVarint(buf, value);
}

// Write an embedded message field
static void writeEmbeddedMessage(std::vector<uint8_t>& buf, int fieldNum, const std::vector<uint8_t>& msg) {
    writeLengthDelimited(buf, fieldNum, msg.data(), msg.size());
}

// ============================================================================
// Apollo Message Builders
// ============================================================================

// Build a MinecraftVersion message
// message MinecraftVersion { string enum = 1; }
static std::vector<uint8_t> buildMinecraftVersion(const std::string& version) {
    std::vector<uint8_t> msg;
    writeString(msg, 1, version);  // enum field = "V1_8"
    return msg;
}

// Build a LunarClientVersion message
// message LunarClientVersion { string git_branch = 1; string git_commit = 2; string semver = 3; }
static std::vector<uint8_t> buildLunarClientVersion() {
    std::vector<uint8_t> msg;
    writeString(msg, 1, "master");   // git_branch
    writeString(msg, 2, "abc1234");  // git_commit (fake but plausible)
    writeString(msg, 3, "3.2.0");    // semver (recent version)
    return msg;
}

// Build a ModMessage
// message ModMessage { string id = 1; string name = 2; string version = 3; Type type = 4; }
// enum Type { FABRIC = 1; FORGE = 2; LUNAR = 3; UNKNOWN = 4; }
static std::vector<uint8_t> buildModMessage(const std::string& id, const std::string& name, const std::string& version,
                                            int type) {
    std::vector<uint8_t> msg;
    writeString(msg, 1, id);
    writeString(msg, 2, name);
    writeString(msg, 3, version);
    writeVarintField(msg, 4, type);  // Type enum
    return msg;
}

// Build PlayerHandshakeMessage
// message PlayerHandshakeMessage {
//   MinecraftVersion minecraft_version = 1;
//   LunarClientVersion lunar_client_version = 2;
//   repeated ModMessage installed_mods = 3;
//   EmbeddedCheckoutSupport embedded_checkout_support = 4;  // optional
//   map<string, Value> mod_status = 5;  // optional
// }
static std::vector<uint8_t> buildPlayerHandshakeMessage() {
    std::vector<uint8_t> msg;

    // Field 1: minecraft_version
    auto mcVersion = buildMinecraftVersion("V1_8");
    writeEmbeddedMessage(msg, 1, mcVersion);

    // Field 2: lunar_client_version
    auto lcVersion = buildLunarClientVersion();
    writeEmbeddedMessage(msg, 2, lcVersion);

    // Field 3: installed_mods (add a few fake Lunar mods)
    auto mod1 = buildModMessage("freelook", "Freelook", "1.0.0", 3);  // LUNAR = 3
    writeEmbeddedMessage(msg, 3, mod1);

    auto mod2 = buildModMessage("teamview", "Team View", "1.0.0", 3);
    writeEmbeddedMessage(msg, 3, mod2);

    auto mod3 = buildModMessage("nametag", "Nametag", "1.0.0", 3);
    writeEmbeddedMessage(msg, 3, mod3);

    // Field 4: embedded_checkout_support = UNSUPPORTED (1)
    writeVarintField(msg, 4, 1);

    return msg;
}

// Build a google.protobuf.Any wrapper
// message Any { string type_url = 1; bytes value = 2; }
static std::vector<uint8_t> buildAnyWrapper(const std::string& typeUrl, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> msg;
    writeString(msg, 1, typeUrl);
    writeLengthDelimited(msg, 2, payload.data(), payload.size());
    return msg;
}

// ============================================================================
// Nametag Request Messages
// ============================================================================

// Build a ConfigurableSettings message for a specific module
// message ConfigurableSettings {
//   bool enable = 1;
//   map<string, Value> properties = 2;  // We leave this empty
// }
static std::vector<uint8_t> buildConfigurableSettings(bool enable) {
    std::vector<uint8_t> msg;
    writeVarintField(msg, 1, enable ? 1 : 0);  // enable = true/false
    // No properties needed
    return msg;
}

// Build OverrideConfigurableSettingsMessage
// message OverrideConfigurableSettingsMessage {
//   map<string, ConfigurableSettings> configurable_settings = 1;
// }
// Map entry: key = module name (e.g., "nametag"), value = ConfigurableSettings
static std::vector<uint8_t> buildOverrideConfigurableSettingsMessage() {
    std::vector<uint8_t> msg;

    // In protobuf, map<string, Message> is encoded as repeated MapEntry
    // message MapEntry { string key = 1; ConfigurableSettings value = 2; }

    auto settings = buildConfigurableSettings(true);

    // Entry 1: nametag
    {
        std::vector<uint8_t> entry;
        writeString(entry, 1, "nametag");
        writeEmbeddedMessage(entry, 2, settings);
        writeEmbeddedMessage(msg, 1, entry);
    }

    // Entry 2: team
    {
        std::vector<uint8_t> entry;
        writeString(entry, 1, "team");
        writeEmbeddedMessage(entry, 2, settings);
        writeEmbeddedMessage(msg, 1, entry);
    }

    // Entry 3: team_view
    {
        std::vector<uint8_t> entry;
        writeString(entry, 1, "team_view");
        writeEmbeddedMessage(entry, 2, settings);
        writeEmbeddedMessage(msg, 1, entry);
    }

    return msg;
}

// ============================================================================
// Packet Sending
// ============================================================================

static jobject getNetHandler(JNIEnv* env) {
    if (!env || !mc.isValid() || !JniCache::mid_getNetHandler)
        return nullptr;

    jobject netHandler = env->CallObjectMethod(mc.instance, JniCache::mid_getNetHandler);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (netHandler)
            env->DeleteLocalRef(netHandler);
        return nullptr;
    }
    return netHandler;
}

static bool initSendPacketMethod(JNIEnv* env, jobject netHandler) {
    if (g_midAddToSendQueue)
        return true;

    jclass cls = env->GetObjectClass(netHandler);
    if (!cls)
        return false;

    // Try different method names
    g_midAddToSendQueue = env->GetMethodID(cls, "addToSendQueue", "(Lnet/minecraft/network/Packet;)V");
    if (!g_midAddToSendQueue) {
        env->ExceptionClear();
        g_midAddToSendQueue = env->GetMethodID(cls, "func_147297_a", "(Lnet/minecraft/network/Packet;)V");
    }
    if (!g_midAddToSendQueue) {
        env->ExceptionClear();
        g_midAddToSendQueue = env->GetMethodID(cls, "sendPacket", "(Lnet/minecraft/network/Packet;)V");
    }

    env->DeleteLocalRef(cls);
    return g_midAddToSendQueue != nullptr;
}

static bool initPacketClasses(JNIEnv* env) {
    if (g_clsC17Packet)
        return true;

    // Find C17PacketCustomPayload
    jclass c17Local = env->FindClass("net/minecraft/network/play/client/C17PacketCustomPayload");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] C17PacketCustomPayload class not found");
        return false;
    }
    if (!c17Local) {
        log_warn("[ApolloHandshake] C17PacketCustomPayload class is null");
        return false;
    }

    g_clsC17Packet = (jclass)env->NewGlobalRef(c17Local);
    env->DeleteLocalRef(c17Local);

    // Get constructor: C17PacketCustomPayload(String channel, PacketBuffer data)
    g_midC17Ctor =
        env->GetMethodID(g_clsC17Packet, "<init>", "(Ljava/lang/String;Lnet/minecraft/network/PacketBuffer;)V");
    if (!g_midC17Ctor) {
        env->ExceptionClear();
        // Try obfuscated name or different signature
        g_midC17Ctor = env->GetMethodID(g_clsC17Packet, "<init>", "(Ljava/lang/String;Lio/netty/buffer/ByteBuf;)V");
    }
    if (!g_midC17Ctor) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] C17PacketCustomPayload constructor not found");
        return false;
    }

    // Find PacketBuffer
    jclass pbLocal = env->FindClass("net/minecraft/network/PacketBuffer");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] PacketBuffer class not found");
        return false;
    }
    if (pbLocal) {
        g_clsPacketBuffer = (jclass)env->NewGlobalRef(pbLocal);
        env->DeleteLocalRef(pbLocal);

        // Constructor: PacketBuffer(ByteBuf wrapped)
        g_midPacketBufferCtor = env->GetMethodID(g_clsPacketBuffer, "<init>", "(Lio/netty/buffer/ByteBuf;)V");
        if (!g_midPacketBufferCtor) {
            env->ExceptionClear();
        }
    }

    // Find Unpooled.wrappedBuffer for creating ByteBuf from bytes
    jclass unpooledLocal = env->FindClass("io/netty/buffer/Unpooled");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] Unpooled class not found");
        return false;
    }
    if (unpooledLocal) {
        g_clsUnpooled = (jclass)env->NewGlobalRef(unpooledLocal);
        env->DeleteLocalRef(unpooledLocal);

        // Static method: wrappedBuffer(byte[])
        g_midWrappedBuffer = env->GetStaticMethodID(g_clsUnpooled, "wrappedBuffer", "([B)Lio/netty/buffer/ByteBuf;");
        if (!g_midWrappedBuffer) {
            env->ExceptionClear();
            log_warn("[ApolloHandshake] Unpooled.wrappedBuffer not found");
        }
    }

    //log_ok("[ApolloHandshake] Packet classes initialized successfully");
    return true;
}

// Create a C17PacketCustomPayload with the given channel and data
static jobject createCustomPayloadPacket(JNIEnv* env, const char* channel, const std::vector<uint8_t>& data) {
    if (!initPacketClasses(env)) {
        return nullptr;
    }

    // Create byte array from data
    jbyteArray byteArray = env->NewByteArray((jsize)data.size());
    if (!byteArray) {
        log_warn("[ApolloHandshake] Failed to create byte array");
        return nullptr;
    }
    env->SetByteArrayRegion(byteArray, 0, (jsize)data.size(), (const jbyte*)data.data());

    // Create ByteBuf using Unpooled.wrappedBuffer(byte[])
    jobject byteBuf = nullptr;
    if (g_clsUnpooled && g_midWrappedBuffer) {
        byteBuf = env->CallStaticObjectMethod(g_clsUnpooled, g_midWrappedBuffer, byteArray);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            byteBuf = nullptr;
        }
    }

    if (!byteBuf) {
        log_warn("[ApolloHandshake] Failed to create ByteBuf");
        env->DeleteLocalRef(byteArray);
        return nullptr;
    }

    // Create PacketBuffer wrapping the ByteBuf
    jobject packetBuffer = nullptr;
    if (g_clsPacketBuffer && g_midPacketBufferCtor) {
        packetBuffer = env->NewObject(g_clsPacketBuffer, g_midPacketBufferCtor, byteBuf);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            packetBuffer = nullptr;
        }
    }

    // Use byteBuf directly if PacketBuffer creation failed
    jobject bufferArg = packetBuffer ? packetBuffer : byteBuf;

    // Create channel string
    jstring channelStr = env->NewStringUTF(channel);
    if (!channelStr) {
        log_warn("[ApolloHandshake] Failed to create channel string");
        if (packetBuffer)
            env->DeleteLocalRef(packetBuffer);
        env->DeleteLocalRef(byteBuf);
        env->DeleteLocalRef(byteArray);
        return nullptr;
    }

    // Create the packet
    jobject packet = env->NewObject(g_clsC17Packet, g_midC17Ctor, channelStr, bufferArg);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] Failed to create C17PacketCustomPayload");
        packet = nullptr;
    }

    // Cleanup
    env->DeleteLocalRef(channelStr);
    if (packetBuffer && packetBuffer != bufferArg)
        env->DeleteLocalRef(packetBuffer);
    env->DeleteLocalRef(byteBuf);
    env->DeleteLocalRef(byteArray);

    return packet;
}

static bool sendPacketToServer(JNIEnv* env, jobject packet) {
    if (!packet)
        return false;

    jobject netHandler = getNetHandler(env);
    if (!netHandler) {
        log_warn("[ApolloHandshake] Failed to get NetHandler");
        env->DeleteLocalRef(packet);
        return false;
    }

    if (!initSendPacketMethod(env, netHandler)) {
        log_warn("[ApolloHandshake] Failed to get sendPacket method");
        env->DeleteLocalRef(netHandler);
        env->DeleteLocalRef(packet);
        return false;
    }

    env->CallVoidMethod(netHandler, g_midAddToSendQueue, packet);
    bool success = !env->ExceptionCheck();
    if (!success) {
        env->ExceptionClear();
        log_warn("[ApolloHandshake] Exception while sending packet");
    }

    env->DeleteLocalRef(netHandler);
    env->DeleteLocalRef(packet);
    return success;
}

// ============================================================================
// Public API
// ============================================================================

bool Init() {
    if (g_initialized.load())
        return true;

    g_initialized.store(true);
    log_ok("[ApolloHandshake] Initialized");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);

    // Note: Can't delete global refs here without JNIEnv
    // They will be cleaned up when the JVM unloads

    g_initialized.store(false);
    log_debug("[ApolloHandshake] Shutdown");
}

bool IsReady() {
    // Just check if mc is valid - no need for explicit Init()
    return mc.isValid();
}

uint64_t GetWorldSerial() {
    return g_worldSerial.load();
}

void SetDebugLogging(bool enabled) {
    g_debugLogging.store(enabled);
}

bool IsDebugLogging() {
    return g_debugLogging.load();
}

Stats GetStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_stats;
}

void OnApolloResponse() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stats.responsesReceived++;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    g_stats.lastResponseMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool SendHandshake(JNIEnv* env) {
    if (!env || !IsReady()) {
        log_warn("[ApolloHandshake] Not ready to send handshake");
        return false;
    }

    bool debug = g_debugLogging.load();

    // Build PlayerHandshakeMessage
    auto handshakePayload = buildPlayerHandshakeMessage();
    /*
    if (debug) {
        log_debug("[ApolloHandshake] PlayerHandshakeMessage built: %zu bytes", handshakePayload.size());
    }*/

    // Wrap in google.protobuf.Any
    const char* typeUrl = "type.googleapis.com/lunarclient.apollo.player.v1.PlayerHandshakeMessage";
    auto anyWrapper = buildAnyWrapper(typeUrl, handshakePayload);
    /*
    if (debug) {
        log_debug("[ApolloHandshake] Any wrapper built: %zu bytes (type=%s)", anyWrapper.size(), typeUrl);
    }*/

    // Create and send the packet
    jobject packet = createCustomPayloadPacket(env, "lunar:apollo", anyWrapper);
    if (!packet) {
        log_warn("[ApolloHandshake] Failed to create handshake packet");
        return false;
    }

    if (!sendPacketToServer(env, packet)) {
        log_warn("[ApolloHandshake] Failed to send handshake packet");
        return false;
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.handshakesSent++;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        g_stats.lastHandshakeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    log_ok("[ApolloHandshake] Handshake sent on lunar:apollo channel (%zu bytes)", anyWrapper.size());
    return true;
}

bool SendPingRequest(JNIEnv* env) {
    if (!env || !IsReady()) {
        return false;
    }

    bool debug = g_debugLogging.load();

    // Build empty PingRequest message (just for roundtrip validation)
    // message PingRequest { }
    std::vector<uint8_t> pingPayload;  // Empty message

    // Wrap in Any
    const char* typeUrl = "type.googleapis.com/lunarclient.apollo.common.v1.PingRequest";
    auto anyWrapper = buildAnyWrapper(typeUrl, pingPayload);

    /*if (debug) {
        log_debug("[ApolloHandshake] PingRequest built: %zu bytes", anyWrapper.size());
    }*/

    // Create and send
    jobject packet = createCustomPayloadPacket(env, "lunar:apollo", anyWrapper);
    if (!packet) {
        return false;
    }

    if (!sendPacketToServer(env, packet)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.pingsSent++;
    }

    /*if (debug) {
        log_debug("[ApolloHandshake] PingRequest sent");
    }*/

    return true;
}

void OnWorldChange(JNIEnv* env) {
    // Debounce repeated world-change triggers that can happen back-to-back
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t lastMs = g_lastWorldChangeMs.load();
    if (nowMs - lastMs < 800) {
        //log_debug("[ApolloHandshake] World change debounce hit, skipping extra trigger");
        return;
    }
    g_lastWorldChangeMs.store(nowMs);

    uint64_t newSerial = g_worldSerial.fetch_add(1) + 1;
    uint64_t lastHandshake = g_lastHandshakeSerial.load();

    if (newSerial == lastHandshake) {
        // Already sent handshake for this world
        //log_debug("[ApolloHandshake] Handshake already sent for world serial %llu", newSerial);
        return;
    }

    //log_ok("[ApolloHandshake] World change detected (serial=%llu), queuing handshake...", newSerial);

    // Try to send immediately if ready
    if (mc.isValid() && SendHandshake(env)) {
        g_lastHandshakeSerial.store(newSerial);
        SendPingRequest(env);
        return;
    }

    // Not ready - queue for later
    g_pendingHandshakeTimeMs.store(nowMs);
    g_handshakeRetryCount.store(0);
    g_pendingHandshake.store(true);
    //log_debug("[ApolloHandshake] Handshake queued (mc not ready), will retry...");
}

void Tick() {
    // Check if we have a pending handshake to send
    if (!g_pendingHandshake.load()) {
        return;
    }

    // Check if enough time has passed since last attempt
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t lastAttempt = g_pendingHandshakeTimeMs.load();

    if (nowMs - lastAttempt < kHandshakeRetryDelayMs) {
        return;  // Not time yet
    }

    // Check retry count
    int retries = g_handshakeRetryCount.fetch_add(1);
    if (retries >= kMaxHandshakeRetries) {
        //log_warn("[ApolloHandshake] Max retries reached, giving up on handshake");
        g_pendingHandshake.store(false);
        return;
    }

    // Check if mc is ready now
    if (!mc.isValid()) {
        g_pendingHandshakeTimeMs.store(nowMs);
        if (retries % 10 == 0) {  // Log every 10 retries
            log_debug("[ApolloHandshake] Still waiting for mc... (retry %d/%d)", retries, kMaxHandshakeRetries);
        }
        return;
    }

    // Get JNIEnv
    JNIEnv* env = nullptr;
    if (Java::vm) {
        Java::vm->GetEnv((void**)&env, JNI_VERSION_1_8);
        if (!env) {
            Java::vm->AttachCurrentThread((void**)&env, nullptr);
        }
    }

    if (!env) {
        g_pendingHandshakeTimeMs.store(nowMs);
        return;
    }

    // Try to send
    uint64_t currentSerial = g_worldSerial.load();
    if (SendHandshake(env)) {
        g_lastHandshakeSerial.store(currentSerial);
        SendPingRequest(env);
        g_pendingHandshake.store(false);
        //log_ok("[ApolloHandshake] Pending handshake sent successfully (after %d retries)", retries);
    } else {
        g_pendingHandshakeTimeMs.store(nowMs);
    }
}

bool HasPendingHandshake() {
    return g_pendingHandshake.load();
}

void RequestNametagRefresh() {
    // Force a new handshake to refresh nametags
    // Reset the last handshake serial to force re-send
    g_lastHandshakeSerial.store(0);

    // Queue the handshake
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    g_pendingHandshakeTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    g_handshakeRetryCount.store(0);
    g_pendingHandshake.store(true);

    //log_ok("[ApolloHandshake] Nametag refresh requested - handshake queued");
}

bool SendConfigurableSettings(JNIEnv* env) {
    if (!env || !IsReady()) {
        //log_warn("[ApolloHandshake] Not ready to send config settings");
        return false;
    }

    bool debug = g_debugLogging.load();

    // Build OverrideConfigurableSettingsMessage
    auto configPayload = buildOverrideConfigurableSettingsMessage();
    if (debug) {
        //log_debug("[ApolloHandshake] OverrideConfigurableSettingsMessage built: %zu bytes", configPayload.size());
    }

    // Wrap in google.protobuf.Any
    const char* typeUrl = "type.googleapis.com/lunarclient.apollo.configurable.v1.OverrideConfigurableSettingsMessage";
    auto anyWrapper = buildAnyWrapper(typeUrl, configPayload);
    if (debug) {
        //log_debug("[ApolloHandshake] Any wrapper built: %zu bytes", anyWrapper.size());
    }

    // Create and send the packet
    jobject packet = createCustomPayloadPacket(env, "lunar:apollo", anyWrapper);
    if (!packet) {
        //log_warn("[ApolloHandshake] Failed to create config packet");
        return false;
    }

    if (!sendPacketToServer(env, packet)) {
        //log_warn("[ApolloHandshake] Failed to send config packet");
        return false;
    }

    //log_ok("[ApolloHandshake] Sent OverrideConfigurableSettingsMessage (nametag/team/team_view enabled)");
    return true;
}

bool RequestNametagData() {
    if (!IsReady()) {
        //log_warn("[ApolloHandshake] Not ready to request nametag data");
        return false;
    }

    // Get JNIEnv
    JNIEnv* env = nullptr;
    if (Java::vm) {
        Java::vm->GetEnv((void**)&env, JNI_VERSION_1_8);
        if (!env) {
            Java::vm->AttachCurrentThread((void**)&env, nullptr);
        }
    }

    if (!env) {
        //log_warn("[ApolloHandshake] Failed to get JNIEnv");
        return false;
    }

    //log_ok("[ApolloHandshake] Requesting nametag data from server...");

    // Boost Apollo payload logging so user sees incoming types/colors after the request
    ApolloPacketObserver::BoostLogBudget(100);
    ApolloPacketObserver::ForceLogNametags(100);

    // Step 1: Send handshake first
    bool handshakeOk = SendHandshake(env);

    // Step 2: Send config settings to enable nametag module
    bool configOk = SendConfigurableSettings(env);

    // Step 3: Send ping to trigger response
    bool pingOk = SendPingRequest(env);

    if (handshakeOk || configOk) {
        //log_ok("[ApolloHandshake] Nametag request sent (handshake=%s, config=%s, ping=%s)", handshakeOk ? "OK" : "FAIL",
        //       configOk ? "OK" : "FAIL", pingOk ? "OK" : "FAIL");
        return true;
    }

    return false;
}

}  // namespace ApolloHandshake
