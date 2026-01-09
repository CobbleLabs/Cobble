#include "network_probe.hpp"
#include "../../jni/java.h"
#include "../../util/logger.hpp"
#include "../jni/jstring_util.h"
#include <vector>
#include <string>

// ============================================================================
// NetworkManager Discovery Probe
// 
// This diagnostic tool attempts multiple strategies to find the NetworkManager
// class in obfuscated clients like Lunar Client.
// ============================================================================

namespace NetworkProbe {

// Helper: Check if class can be loaded by name
static bool tryLoadClass(const char* name, jclass& out) {
    // Use Java::AssignClass which handles the class loader internally
    return Java::AssignClass(name, out);
}


// Helper: Get field signature
static std::string getFieldSignature(jclass cls, jfieldID field) {
    if (!Java::tiEnv) return "<no-jvmti>";
    
    char* name = nullptr;
    char* sig = nullptr;
    Java::tiEnv->GetFieldName(cls, field, &name, &sig, nullptr);
    
    std::string result = sig ? sig : "<unknown>";
    
    if (name) Java::tiEnv->Deallocate((unsigned char*)name);
    if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
    
    return result;
}

// ============================================================================
// METHOD 1: Try standard MCP class names
// ============================================================================
void probeMCPNames() {
    //log_debug("=== PROBE 1: MCP Names ===");
    
    const char* classNames[] = {
        // Standard MCP names
        "net.minecraft.network.NetworkManager",
        "net.minecraft.client.network.NetHandlerPlayClient",
        "net.minecraft.client.entity.EntityPlayerSP",
        "net.minecraft.network.Packet",
        
        // Possible obfuscated patterns (common in Forge/Lunar)
        "ave",  // NetworkManager in some mappings
        "bcy",  // Another possibility
        "ej",   // Packet base
        
        nullptr
    };
    
    for (int i = 0; classNames[i] != nullptr; i++) {
        jclass cls = nullptr;
        bool found = tryLoadClass(classNames[i], cls);
        
        if (found) {
            log_ok("[MCP] FOUND: %s", classNames[i]);
            Java::Env->DeleteLocalRef(cls);
        } else {
            log_debug("[MCP] not found: %s", classNames[i]);
        }
    }
}

// ============================================================================
// METHOD 2: Enumerate fields of EntityPlayerSP to find sendQueue
// ============================================================================
void probePlayerFields() {
    log_debug("=== PROBE 2: EntityPlayerSP Fields ===");
    
    if (!Java::tiEnv) {
        //log_warn("[Fields] JVMTI not available - skipping field enumeration");
        return;
    }
    
    // Try to get EntityPlayerSP
    jclass entityPlayerSP = nullptr;
    if (!tryLoadClass("net.minecraft.client.entity.EntityPlayerSP", entityPlayerSP)) {
        log_warn("[Fields] EntityPlayerSP not found by MCP name");
        return;
    }
    
    //log_ok("[Fields] Found EntityPlayerSP, enumerating fields...");
    
    jint fieldCount = 0;
    jfieldID* fields = nullptr;
    jvmtiError err = Java::tiEnv->GetClassFields(entityPlayerSP, &fieldCount, &fields);
    
    if (err != JVMTI_ERROR_NONE) {
        //log_err("[Fields] GetClassFields failed: %d", err);
        Java::Env->DeleteLocalRef(entityPlayerSP);
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
                if (strstr(sig, "Net") || strstr(sig, "Handler") || 
                    strstr(sig, "network") || strstr(sig, "Connection")) {
                    log_ok("[Fields] POTENTIAL NETWORK HANDLER: %s -> %s", name, sig);
                }
            }
        }
        
        if (name) Java::tiEnv->Deallocate((unsigned char*)name);
        if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
    }
    
    Java::tiEnv->Deallocate((unsigned char*)fields);
    Java::Env->DeleteLocalRef(entityPlayerSP);
}

// ============================================================================
// METHOD 3: Search all loaded classes for network-related names
// ============================================================================
void probeNetworkClasses() {
    //log_debug("=== PROBE 3: Network Classes Search ===");
    
    if (!Java::tiEnv) {
        //log_warn("[Classes] JVMTI not available - skipping class enumeration");
        return;
    }
    
    jint classCount = 0;
    jclass* classes = nullptr;
    jvmtiError err = Java::tiEnv->GetLoadedClasses(&classCount, &classes);
    
    if (err != JVMTI_ERROR_NONE) {
        //log_err("[Classes] GetLoadedClasses failed: %d", err);
        return;
    }
    
    //log_debug("[Classes] Total loaded classes: %d", classCount);
    
    // Keywords to search for
    const char* keywords[] = {
        "Network", "network",
        "Packet", "packet", 
        "sendPacket", "SendPacket",
        "C03", "C0E",  // Movement packets
        "NetHandler", "netHandler",
        nullptr
    };
    
    // Lunar/Apollo specific patterns
    const char* lunarKeywords[] = {
        "lunar", "Lunar",
        "apollo", "Apollo", 
        "moonsworth", "Moonsworth",
        "team", "Team",
        "TeamMember", "UpdateTeam",
        nullptr
    };
    
    int foundCount = 0;
    int lunarCount = 0;
    
    for (int i = 0; i < classCount; i++) {
        char* sig = nullptr;
        Java::tiEnv->GetClassSignature(classes[i], &sig, nullptr);
        
        if (sig) {
            // Check network keywords (limited output)
            if (foundCount < 50) {
                for (int k = 0; keywords[k] != nullptr; k++) {
                    if (strstr(sig, keywords[k])) {
                        //log_ok("[Classes] MATCH '%s': %s", keywords[k], sig);
                        foundCount++;
                        break;  // Don't log same class twice
                    }
                }
            }
            
            // Check Lunar/Apollo keywords (log ALL of these)
            for (int k = 0; lunarKeywords[k] != nullptr; k++) {
                if (strstr(sig, lunarKeywords[k])) {
                    //log_ok("[Lunar] FOUND: %s", sig);
                    lunarCount++;
                    break;
                }
            }
            
            Java::tiEnv->Deallocate((unsigned char*)sig);
        }
    }
    
    // Cleanup
    for (int i = 0; i < classCount; i++) {
        Java::Env->DeleteLocalRef(classes[i]);
    }
    Java::tiEnv->Deallocate((unsigned char*)classes);
    
    //log_debug("[Classes] Found %d network-related classes", foundCount);
    //log_ok("[Lunar] Found %d Lunar/Apollo classes total", lunarCount);
}

// ============================================================================
// METHOD 4: Find Netty ChannelHandlers (NetworkManager extends this)
// ============================================================================
void probeNettyHandlers() {
    //log_debug("=== PROBE 4: Netty Handlers ===");
    
    if (!Java::tiEnv) {
        //log_warn("[Netty] JVMTI not available - skipping");
        return;
    }
    
    // Try to find Netty base class
    jclass channelHandler = nullptr;
    
    const char* nettyClasses[] = {
        "io.netty.channel.ChannelInboundHandler",
        "io.netty.channel.SimpleChannelInboundHandler",
        "io.netty.channel.ChannelHandler",
        nullptr
    };
    
    for (int i = 0; nettyClasses[i] != nullptr; i++) {
        // Try standard FindClass for Netty (not obfuscated)
        jclass cls = Java::Env->FindClass(
            // Convert dot notation to slash for FindClass
            [](const char* name) -> std::string {
                std::string result = name;
                for (char& c : result) if (c == '.') c = '/';
                return result;
            }(nettyClasses[i]).c_str()
        );
        
        if (Java::Env->ExceptionCheck()) {
            Java::Env->ExceptionClear();
            continue;
        }
        
        if (cls) {
            //log_ok("[Netty] Found base class: %s", nettyClasses[i]);
            channelHandler = cls;
            break;
        }
    }
    
    if (!channelHandler) {
        //log_warn("[Netty] No Netty base classes found - Lunar may use shaded Netty");
        
        // Try shaded netty paths
        const char* shadedPaths[] = {
            "com.lunarclient.libs.netty.channel.ChannelHandler",
            "shaded.netty.channel.ChannelHandler",
            nullptr
        };
        
        for (int i = 0; shadedPaths[i] != nullptr; i++) {
            if (tryLoadClass(shadedPaths[i], channelHandler)) {
                //log_ok("[Netty] Found shaded: %s", shadedPaths[i]);
                break;
            }
        }
    }
    
    if (channelHandler) {
        Java::Env->DeleteLocalRef(channelHandler);
    }
}

// ============================================================================
// PROBE 5: Enumerate NetHandlerPlayClient fields to find netManager
// ============================================================================
static void probeNetHandlerFields() {
    //log_debug("=== PROBE 5: NetHandlerPlayClient Fields ===");
    
    if (!Java::tiEnv) {
        //log_warn("[NetHandler] JVMTI not available");
        return;
    }
    
    jclass netHandlerPlayClient = nullptr;
    if (!tryLoadClass("net.minecraft.client.network.NetHandlerPlayClient", netHandlerPlayClient)) {
        //log_warn("[NetHandler] NetHandlerPlayClient not found");
        return;
    }
    
    //log_ok("[NetHandler] Found NetHandlerPlayClient, enumerating fields...");
    
    jint fieldCount = 0;
    jfieldID* fields = nullptr;
    jvmtiError err = Java::tiEnv->GetClassFields(netHandlerPlayClient, &fieldCount, &fields);
    
    if (err != JVMTI_ERROR_NONE) {
        //log_err("[NetHandler] GetClassFields failed: %d", err);
        Java::Env->DeleteLocalRef(netHandlerPlayClient);
        return;
    }
    
    //log_debug("[NetHandler] NetHandlerPlayClient has %d fields", fieldCount);
    
    for (int i = 0; i < fieldCount; i++) {
        char* name = nullptr;
        char* sig = nullptr;
        Java::tiEnv->GetFieldName(netHandlerPlayClient, fields[i], &name, &sig, nullptr);
        
        if (sig && sig[0] == 'L') {
            // Print all object fields
            //log_debug("[NetHandler] %s -> %s", name ? name : "?", sig);
            
            // Highlight NetworkManager field
            if (strstr(sig, "NetworkManager") || strstr(sig, "network/Network")) {
                //log_ok("[NetHandler] FOUND NETWORK MANAGER FIELD: %s -> %s", name, sig);
            }
        }
        
        if (name) Java::tiEnv->Deallocate((unsigned char*)name);
        if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
    }
    
    Java::tiEnv->Deallocate((unsigned char*)fields);
    Java::Env->DeleteLocalRef(netHandlerPlayClient);
}

// ============================================================================
// PROBE 6: Verify NetworkManager.sendPacket() exists
// ============================================================================
static void probeSendPacket() {
    //log_debug("=== PROBE 6: NetworkManager.sendPacket() ===");
    
    jclass networkManager = nullptr;
    if (!tryLoadClass("net.minecraft.network.NetworkManager", networkManager)) {
        //log_warn("[sendPacket] NetworkManager not found");
        return;
    }
    
    //log_ok("[sendPacket] Found NetworkManager, checking methods...");
    
    // Try to get sendPacket method
    // Signature: (Lnet/minecraft/network/Packet;)V
    JNIEnv* env = Java::Env;
    
    jmethodID sendPacket = env->GetMethodID(networkManager, "sendPacket", 
        "(Lnet/minecraft/network/Packet;)V");
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        //log_warn("[sendPacket] sendPacket(Packet) not found with MCP signature");
    } else if (sendPacket) {
        //log_ok("[sendPacket] FOUND: sendPacket(Lnet/minecraft/network/Packet;)V");
    }
    
    // Also check for overloaded version with GenericFutureListener
    jmethodID sendPacketFuture = env->GetMethodID(networkManager, "sendPacket", 
        "(Lnet/minecraft/network/Packet;Lio/netty/util/concurrent/GenericFutureListener;)V");
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (sendPacketFuture) {
        //log_ok("[sendPacket] FOUND: sendPacket(Packet, GenericFutureListener)");
    }
    
    // Check for scheduleOutboundPacket (alternative entry point)
    jmethodID schedulePacket = env->GetMethodID(networkManager, "scheduleOutboundPacket", 
        "(Lnet/minecraft/network/Packet;[Lio/netty/util/concurrent/GenericFutureListener;)V");
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (schedulePacket) {
        //log_ok("[sendPacket] FOUND: scheduleOutboundPacket(Packet, GenericFutureListener[])");
    }
    
    // Enumerate all methods with JVMTI for more detail
    if (Java::tiEnv) {
        jint methodCount = 0;
        jmethodID* methods = nullptr;
        jvmtiError err = Java::tiEnv->GetClassMethods(networkManager, &methodCount, &methods);
        
        if (err == JVMTI_ERROR_NONE) {
            //log_debug("[sendPacket] NetworkManager has %d methods", methodCount);
            
            for (int i = 0; i < methodCount; i++) {
                char* name = nullptr;
                char* sig = nullptr;
                Java::tiEnv->GetMethodName(methods[i], &name, &sig, nullptr);
                
                // Only log packet-related methods
                if (name && (strstr(name, "send") || strstr(name, "Packet") || 
                            strstr(name, "schedule") || strstr(name, "dispatch"))) {
                    //log_debug("[sendPacket] Method: %s%s", name, sig ? sig : "");
                }
                
                if (name) Java::tiEnv->Deallocate((unsigned char*)name);
                if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
            }
            
            Java::tiEnv->Deallocate((unsigned char*)methods);
        }
    }
    
    Java::Env->DeleteLocalRef(networkManager);
}

// ============================================================================
// PROBE 7: Find lastReportedPos fields in EntityPlayerSP (for Blink)
// ============================================================================
static void probeLastReportedPos() {
    //log_debug("=== PROBE 7: lastReportedPos Fields ===");
    
    jclass entityPlayerSP = nullptr;
    if (!tryLoadClass("net.minecraft.client.entity.EntityPlayerSP", entityPlayerSP)) {
        //log_warn("[lastReportedPos] EntityPlayerSP not found");
        return;
    }
    
    JNIEnv* env = Java::Env;
    
    // Try MCP names first
    const char* fieldNames[] = {
        "lastReportedPosX", "lastReportedPosY", "lastReportedPosZ",
        "lastReportedYaw", "lastReportedPitch",
        "field_175170_bN", "field_175166_bO", "field_175167_bP",  // SRG names
        nullptr
    };
    
    for (int i = 0; fieldNames[i] != nullptr; i++) {
        jfieldID field = env->GetFieldID(entityPlayerSP, fieldNames[i], "D");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            // Try float
            field = env->GetFieldID(entityPlayerSP, fieldNames[i], "F");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                continue;
            }
        }
        if (field) {
            //log_ok("[lastReportedPos] FOUND FIELD: %s", fieldNames[i]);
        }
    }
    
    // Also enumerate ALL fields looking for position-related ones
    if (Java::tiEnv) {
        jint fieldCount = 0;
        jfieldID* fields = nullptr;
        jvmtiError err = Java::tiEnv->GetClassFields(entityPlayerSP, &fieldCount, &fields);
        
        if (err == JVMTI_ERROR_NONE) {
            //log_debug("[lastReportedPos] Checking %d fields for position-related names...", fieldCount);
            
            for (int i = 0; i < fieldCount; i++) {
                char* name = nullptr;
                char* sig = nullptr;
                Java::tiEnv->GetFieldName(entityPlayerSP, fields[i], &name, &sig, nullptr);
                
                // Only log double (D) or float (F) fields that might be position
                if (name && sig && (sig[0] == 'D' || sig[0] == 'F')) {
                    if (strstr(name, "last") || strstr(name, "Last") ||
                        strstr(name, "reported") || strstr(name, "Reported") ||
                        strstr(name, "pos") || strstr(name, "Pos") ||
                        strstr(name, "prev") || strstr(name, "Prev")) {
                        //log_ok("[lastReportedPos] POTENTIAL: %s (%s)", name, sig);
                    }
                }
                
                if (name) Java::tiEnv->Deallocate((unsigned char*)name);
                if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
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
    //log_debug("=========================================");
    //log_debug("   NetworkManager Discovery Probe");
    //log_debug("=========================================");
    
    if (!Java::Initialized) {
        //log_err("Java not initialized - cannot run probe");
        return;
    }
    
    probeMCPNames();
    probePlayerFields();
    probeNetworkClasses();
    probeNettyHandlers();
    probeNetHandlerFields();
    probeSendPacket();
    probeLastReportedPos();  // NEW: Find position freeze fields
    
    //log_debug("=========================================");
    //log_debug("   Probe Complete");
    //log_debug("=========================================");
}


} // namespace NetworkProbe

