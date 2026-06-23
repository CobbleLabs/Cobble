#include "network_probe.hpp"
#include <string>
#include <vector>
#include "../../jni/java.h"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../jni/jstring_util.h"
#include "../../util/inject_trace.hpp"

// ============================================================================
// NetworkManager Discovery Probe
//
// This diagnostic tool attempts multiple strategies to find the NetworkManager
// class in obfuscated clients like Lunar Client.
// ============================================================================

namespace NetworkProbe {

// Helper: Check if class can be loaded by name
static bool tryLoadClass(const std::string& name, jclass& out) {
    // Use Java::AssignClass which handles the class loader internally
    return Java::AssignClass(name, out);
}

// Helper: Get field signature
static std::string getFieldSignature(jclass cls, jfieldID field) {
    if (!Java::tiEnv)
        return "<no-jvmti>";

    char* name = nullptr;
    char* sig = nullptr;
    Java::tiEnv->GetFieldName(cls, field, &name, &sig, nullptr);

    std::string result = sig ? sig : "<unknown>";

    if (name)
        Java::tiEnv->Deallocate((unsigned char*)name);
    if (sig)
        Java::tiEnv->Deallocate((unsigned char*)sig);

    return result;
}

// ============================================================================
// METHOD 1: Try standard MCP class names
// ============================================================================
void probeMCPNames() {
    // log_debug("=== PROBE 1: MCP Names ===");

    const std::string classNames[] = {// Standard MCP names
                                       OBFUSCATE_STRING("net.minecraft.network.NetworkManager"),
                                       OBFUSCATE_STRING("net.minecraft.client.network.NetHandlerPlayClient"),
                                       OBFUSCATE_STRING("net.minecraft.client.entity.EntityPlayerSP"),
                                       OBFUSCATE_STRING("net.minecraft.network.Packet"),

                                       // Possible obfuscated patterns (common in Forge/Lunar)
                                       OBFUSCATE_STRING("ave"),  // NetworkManager in some mappings
                                       OBFUSCATE_STRING("bcy"),  // Another possibility
                                       OBFUSCATE_STRING("ej")};  // Packet base

    for (const auto& className : classNames) {
        jclass cls = nullptr;
        bool found = tryLoadClass(className, cls);

        if (found) {
            log_ok("[MCP] FOUND: %s", className.c_str());
            Java::Env()->DeleteLocalRef(cls);
        } else {
            log_debug("[MCP] not found: %s", className.c_str());
        }
    }
}

// ============================================================================
// METHOD 2: Enumerate fields of EntityPlayerSP to find sendQueue
// ============================================================================
void probePlayerFields() {
    log_debug("=== PROBE 2: EntityPlayerSP Fields ===");

    if (!Java::tiEnv) {
        // log_warn("[Fields] JVMTI not available - skipping field enumeration");
        return;
    }

    // Try to get EntityPlayerSP
    jclass entityPlayerSP = nullptr;
    if (!tryLoadClass(OBFUSCATE_STRING("net.minecraft.client.entity.EntityPlayerSP"), entityPlayerSP)) {
        log_warn("[Fields] EntityPlayerSP not found by MCP name");
        return;
    }

    // log_ok("[Fields] Found EntityPlayerSP, enumerating fields...");

    jint fieldCount = 0;
    jfieldID* fields = nullptr;
    jvmtiError err = Java::tiEnv->GetClassFields(entityPlayerSP, &fieldCount, &fields);

    if (err != JVMTI_ERROR_NONE) {
        // log_err("[Fields] GetClassFields failed: %d", err);
        Java::Env()->DeleteLocalRef(entityPlayerSP);
        return;
    }

    log_debug("[Fields] EntityPlayerSP has %d fields", fieldCount);

    for (int i = 0; i < fieldCount; i++) {
        char* name = nullptr;
        char* sig = nullptr;
        Java::tiEnv->GetFieldName(entityPlayerSP, fields[i], &name, &sig, nullptr);

        // Look for fields that might be NetHandlerPlayClient
        // The signature would be Lnet/minecraft/client/network/NetHandlerPlayClient; or Lxxx;
        if (sig) {
            // Print all fields with non-primitive types
            if (sig[0] == 'L') {
                log_debug("[Fields] %s -> %s", name ? name : "?", sig);

                // Highlight potential network handler fields
                if (strstr(sig, OBFUSCATE_STRING("Net").c_str()) ||
                    strstr(sig, OBFUSCATE_STRING("Handler").c_str()) ||
                    strstr(sig, OBFUSCATE_STRING("network").c_str()) ||
                    strstr(sig, OBFUSCATE_STRING("Connection").c_str())) {
                    log_ok("[Fields] POTENTIAL NETWORK HANDLER: %s -> %s", name, sig);
                }
            }
        }

        if (name)
            Java::tiEnv->Deallocate((unsigned char*)name);
        if (sig)
            Java::tiEnv->Deallocate((unsigned char*)sig);
    }

    Java::tiEnv->Deallocate((unsigned char*)fields);
    Java::Env()->DeleteLocalRef(entityPlayerSP);
}

// ============================================================================
// METHOD 3: Search all loaded classes for network-related names
// ============================================================================
void probeNetworkClasses() {
    // log_debug("=== PROBE 3: Network Classes Search ===");

    if (!Java::tiEnv) {
        // log_warn("[Classes] JVMTI not available - skipping class enumeration");
        return;
    }

    jint classCount = 0;
    jclass* classes = nullptr;
    jvmtiError err = Java::tiEnv->GetLoadedClasses(&classCount, &classes);

    if (err != JVMTI_ERROR_NONE) {
        // log_err("[Classes] GetLoadedClasses failed: %d", err);
        return;
    }

    // log_debug("[Classes] Total loaded classes: %d", classCount);

    // Keywords to search for
    const std::vector<std::string> keywords = {
        OBFUSCATE_STRING("Network"),    OBFUSCATE_STRING("network"),
        OBFUSCATE_STRING("Packet"),     OBFUSCATE_STRING("packet"),
        OBFUSCATE_STRING("sendPacket"), OBFUSCATE_STRING("SendPacket"),
        OBFUSCATE_STRING("C03"),        OBFUSCATE_STRING("C0E"),  // Movement packets
        OBFUSCATE_STRING("NetHandler"), OBFUSCATE_STRING("netHandler")};

    // Lunar/Apollo specific patterns
    const std::vector<std::string> lunarKeywords = {
        OBFUSCATE_STRING("lunar"),      OBFUSCATE_STRING("Lunar"),
        OBFUSCATE_STRING("apollo"),     OBFUSCATE_STRING("Apollo"),
        OBFUSCATE_STRING("moonsworth"), OBFUSCATE_STRING("Moonsworth"),
        OBFUSCATE_STRING("team"),       OBFUSCATE_STRING("Team"),
        OBFUSCATE_STRING("TeamMember"), OBFUSCATE_STRING("UpdateTeam")};

    int foundCount = 0;
    int lunarCount = 0;

    for (int i = 0; i < classCount; i++) {
        char* sig = nullptr;
        Java::tiEnv->GetClassSignature(classes[i], &sig, nullptr);

        if (sig) {
            // Check network keywords (limited output)
            if (foundCount < 50) {
                for (const auto& kw : keywords) {
                    if (strstr(sig, kw.c_str())) {
                        // log_ok("[Classes] MATCH '%s': %s", kw.c_str(), sig);
                        foundCount++;
                        break;  // Don't log same class twice
                    }
                }
            }

            // Check Lunar/Apollo keywords (log ALL of these)
            for (const auto& kw : lunarKeywords) {
                if (strstr(sig, kw.c_str())) {
                    // log_ok("[Lunar] FOUND: %s", sig);
                    lunarCount++;
                    break;
                }
            }

            Java::tiEnv->Deallocate((unsigned char*)sig);
        }
    }

    // Cleanup
    for (int i = 0; i < classCount; i++) {
        Java::Env()->DeleteLocalRef(classes[i]);
    }
    Java::tiEnv->Deallocate((unsigned char*)classes);

    // log_debug("[Classes] Found %d network-related classes", foundCount);
    // log_ok("[Lunar] Found %d Lunar/Apollo classes total", lunarCount);
}

// ============================================================================
// METHOD 4: Find Netty ChannelHandlers (NetworkManager extends this)
// ============================================================================
void probeNettyHandlers() {
    // log_debug("=== PROBE 4: Netty Handlers ===");

    if (!Java::tiEnv) {
        // log_warn("[Netty] JVMTI not available - skipping");
        return;
    }

    // Try to find Netty base class
    jclass channelHandler = nullptr;

    const std::vector<std::string> nettyClasses = {OBFUSCATE_STRING("io.netty.channel.ChannelInboundHandler"),
                                                   OBFUSCATE_STRING("io.netty.channel.SimpleChannelInboundHandler"),
                                                   OBFUSCATE_STRING("io.netty.channel.ChannelHandler")};

    for (const auto& nc : nettyClasses) {
        // Convert dot notation to slash for FindClass
        std::string slashName = nc;
        for (char& c : slashName)
            if (c == '.')
                c = '/';

        jclass cls = Java::Env()->FindClass(slashName.c_str());

        if (Java::Env()->ExceptionCheck()) {
            Java::Env()->ExceptionClear();
            continue;
        }

        if (cls) {
            // log_ok("[Netty] Found base class: %s", nc.c_str());
            channelHandler = cls;
            break;
        }
    }

    if (!channelHandler) {
        // log_warn("[Netty] No Netty base classes found - Lunar may use shaded Netty");

        // Try shaded netty paths
        const std::string shadedPaths[] = {OBFUSCATE_STRING("com.lunarclient.libs.netty.channel.ChannelHandler"),
                                             OBFUSCATE_STRING("shaded.netty.channel.ChannelHandler")};

        for (const auto& sp : shadedPaths) {
            if (tryLoadClass(sp, channelHandler)) {
                // log_ok("[Netty] Found shaded: %s", sp.c_str());
                break;
            }
        }
    }

    if (channelHandler) {
        Java::Env()->DeleteLocalRef(channelHandler);
    }
}

// ============================================================================
// PROBE 5: Enumerate NetHandlerPlayClient fields to find netManager
// ============================================================================
static void probeNetHandlerFields() {
    // log_debug("=== PROBE 5: NetHandlerPlayClient Fields ===");

    if (!Java::tiEnv) {
        // log_warn("[NetHandler] JVMTI not available");
        return;
    }

    jclass netHandlerPlayClient = nullptr;
    if (!tryLoadClass(OBFUSCATE_STRING("net.minecraft.client.network.NetHandlerPlayClient"), netHandlerPlayClient)) {
        // log_warn("[NetHandler] NetHandlerPlayClient not found");
        return;
    }

    // log_ok("[NetHandler] Found NetHandlerPlayClient, enumerating fields...");

    jint fieldCount = 0;
    jfieldID* fields = nullptr;
    jvmtiError err = Java::tiEnv->GetClassFields(netHandlerPlayClient, &fieldCount, &fields);

    if (err != JVMTI_ERROR_NONE) {
        // log_err("[NetHandler] GetClassFields failed: %d", err);
        Java::Env()->DeleteLocalRef(netHandlerPlayClient);
        return;
    }

    // log_debug("[NetHandler] NetHandlerPlayClient has %d fields", fieldCount);

    for (int i = 0; i < fieldCount; i++) {
        char* name = nullptr;
        char* sig = nullptr;
        Java::tiEnv->GetFieldName(netHandlerPlayClient, fields[i], &name, &sig, nullptr);

        if (sig && sig[0] == 'L') {
            // Print all object fields
            // log_debug("[NetHandler] %s -> %s", name ? name : "?", sig);

            // Highlight NetworkManager field
            if (strstr(sig, OBFUSCATE_STRING("NetworkManager").c_str()) ||
                strstr(sig, OBFUSCATE_STRING("network/Network").c_str())) {
                // log_ok("[NetHandler] FOUND NETWORK MANAGER FIELD: %s -> %s", name, sig);
            }
        }

        if (name)
            Java::tiEnv->Deallocate((unsigned char*)name);
        if (sig)
            Java::tiEnv->Deallocate((unsigned char*)sig);
    }

    Java::tiEnv->Deallocate((unsigned char*)fields);
    Java::Env()->DeleteLocalRef(netHandlerPlayClient);
}

// ============================================================================
// PROBE 6: Verify NetworkManager.sendPacket() exists
// ============================================================================
static void probeSendPacket() {
    // log_debug("=== PROBE 6: NetworkManager.sendPacket() ===");

    jclass networkManager = nullptr;
    if (!tryLoadClass(OBFUSCATE_STRING("net.minecraft.network.NetworkManager"), networkManager)) {
        // log_warn("[sendPacket] NetworkManager not found");
        return;
    }

    // log_ok("[sendPacket] Found NetworkManager, checking methods...");

    // Try to get sendPacket method
    // Signature: (Lnet/minecraft/network/Packet;)V
    JNIEnv* env = Java::Env();

    jmethodID sendPacket = env->GetMethodID(networkManager, OBFUSCATE_STRING("sendPacket").c_str(),
                                            OBFUSCATE_STRING("(Lnet/minecraft/network/Packet;)V").c_str());

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        // log_warn("[sendPacket] sendPacket(Packet) not found with MCP signature");
    } else if (sendPacket) {
        // log_ok("[sendPacket] FOUND: sendPacket(Lnet/minecraft/network/Packet;)V");
    }

    // Also check for overloaded version with GenericFutureListener
    jmethodID sendPacketFuture = env->GetMethodID(
        networkManager, OBFUSCATE_STRING("sendPacket").c_str(),
        OBFUSCATE_STRING("(Lnet/minecraft/network/Packet;Lio/netty/util/concurrent/GenericFutureListener;)V").c_str());

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (sendPacketFuture) {
        // log_ok("[sendPacket] FOUND: sendPacket(Packet, GenericFutureListener)");
    }

    // Check for scheduleOutboundPacket (alternative entry point)
    jmethodID schedulePacket = env->GetMethodID(
        networkManager, OBFUSCATE_STRING("scheduleOutboundPacket").c_str(),
        OBFUSCATE_STRING("(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V").c_str());

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (schedulePacket) {
        // log_ok("[sendPacket] FOUND: scheduleOutboundPacket(Packet, GenericFutureListener[])");
    }

    // Enumerate all methods with JVMTI for more detail
    if (Java::tiEnv) {
        jint methodCount = 0;
        jmethodID* methods = nullptr;
        jvmtiError err = Java::tiEnv->GetClassMethods(networkManager, &methodCount, &methods);

        if (err == JVMTI_ERROR_NONE) {
            // log_debug("[sendPacket] NetworkManager has %d methods", methodCount);

            for (int i = 0; i < methodCount; i++) {
                char* name = nullptr;
                char* sig = nullptr;
                Java::tiEnv->GetMethodName(methods[i], &name, &sig, nullptr);

                // Only log packet-related methods
                if (name && (strstr(name, OBFUSCATE_STRING("send").c_str()) ||
                             strstr(name, OBFUSCATE_STRING("Packet").c_str()) ||
                             strstr(name, OBFUSCATE_STRING("schedule").c_str()) ||
                             strstr(name, OBFUSCATE_STRING("dispatch").c_str()))) {
                    // log_debug("[sendPacket] Method: %s%s", name, sig ? sig : "");
                }

                if (name)
                    Java::tiEnv->Deallocate((unsigned char*)name);
                if (sig)
                    Java::tiEnv->Deallocate((unsigned char*)sig);
            }

            Java::tiEnv->Deallocate((unsigned char*)methods);
        }
    }

    Java::Env()->DeleteLocalRef(networkManager);
}

// ============================================================================
// PROBE 7: Find lastReportedPos fields in EntityPlayerSP (for Blink)
// ============================================================================
static void probeLastReportedPos() {
    // log_debug("=== PROBE 7: lastReportedPos Fields ===");

    jclass entityPlayerSP = nullptr;
    if (!tryLoadClass(OBFUSCATE_STRING("net.minecraft.client.entity.EntityPlayerSP"), entityPlayerSP)) {
        // log_warn("[lastReportedPos] EntityPlayerSP not found");
        return;
    }

    JNIEnv* env = Java::Env();

    // Try MCP names first
    const std::vector<std::string> fieldNames = {OBFUSCATE_STRING("lastReportedPosX"),
                                                 OBFUSCATE_STRING("lastReportedPosY"),
                                                 OBFUSCATE_STRING("lastReportedPosZ"),
                                                 OBFUSCATE_STRING("lastReportedYaw"),
                                                 OBFUSCATE_STRING("lastReportedPitch"),
                                                 OBFUSCATE_STRING("field_175170_bN"),
                                                 OBFUSCATE_STRING("field_175166_bO"),
                                                 OBFUSCATE_STRING("field_175167_bP")};  // SRG names

    for (const auto& fn : fieldNames) {
        jfieldID field = env->GetFieldID(entityPlayerSP, fn.c_str(), "D");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            // Try float
            field = env->GetFieldID(entityPlayerSP, fn.c_str(), "F");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                continue;
            }
        }
        if (field) {
            // log_ok("[lastReportedPos] FOUND FIELD: %s", fn.c_str());
        }
    }

    // Also enumerate ALL fields looking for position-related ones
    if (Java::tiEnv) {
        jint fieldCount = 0;
        jfieldID* fields = nullptr;
        jvmtiError err = Java::tiEnv->GetClassFields(entityPlayerSP, &fieldCount, &fields);

        if (err == JVMTI_ERROR_NONE) {
            // log_debug("[lastReportedPos] Checking %d fields for position-related names...", fieldCount);

            for (int i = 0; i < fieldCount; i++) {
                char* name = nullptr;
                char* sig = nullptr;
                Java::tiEnv->GetFieldName(entityPlayerSP, fields[i], &name, &sig, nullptr);

                // Only log double (D) or float (F) fields that might be position
                if (name && sig && (sig[0] == 'D' || sig[0] == 'F')) {
                    if (strstr(name, OBFUSCATE_STRING("last").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("Last").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("reported").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("Reported").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("pos").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("Pos").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("prev").c_str()) ||
                        strstr(name, OBFUSCATE_STRING("Prev").c_str())) {
                        // log_ok("[lastReportedPos] POTENTIAL: %s (%s)", name, sig);
                    }
                }

                if (name)
                    Java::tiEnv->Deallocate((unsigned char*)name);
                if (sig)
                    Java::tiEnv->Deallocate((unsigned char*)sig);
            }

            Java::tiEnv->Deallocate((unsigned char*)fields);
        }
    }

    env->DeleteLocalRef(entityPlayerSP);
}

// ============================================================================
// Run all probes
// ============================================================================
void run() {
    COBBLE_TRACE("NetworkProbe::run: enter");
    if (!Java::Initialized) {
        COBBLE_TRACE("NetworkProbe::run: Java not initialized, skipping");
        return;
    }

    COBBLE_TRACE("NetworkProbe::run: probeMCPNames");
    probeMCPNames();
    
    COBBLE_TRACE("NetworkProbe::run: probePlayerFields");
    probePlayerFields();
    
    COBBLE_TRACE("NetworkProbe::run: skipping probeNetworkClasses (prevents local ref overflow)");
    // probeNetworkClasses(); // Disabled: GetLoadedClasses returns local references for all loaded classes, causing JNI local ref table overflow
    
    COBBLE_TRACE("NetworkProbe::run: probeNettyHandlers");
    probeNettyHandlers();
    
    COBBLE_TRACE("NetworkProbe::run: probeNetHandlerFields");
    probeNetHandlerFields();
    
    COBBLE_TRACE("NetworkProbe::run: probeSendPacket");
    probeSendPacket();
    
    COBBLE_TRACE("NetworkProbe::run: probeLastReportedPos");
    probeLastReportedPos();

    COBBLE_TRACE("NetworkProbe::run: exit");
}

}  // namespace NetworkProbe
