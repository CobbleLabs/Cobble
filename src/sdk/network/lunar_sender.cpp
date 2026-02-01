#include "lunar_sender.hpp"
#include "../../util/logger.hpp"
#include "../../jni/java.h"
#include <mutex>

namespace LunarSender {

    static jclass cls_LunarClientAPI = nullptr;
    static jobject obj_LunarClientAPI = nullptr;
    static jmethodID mid_sendPacket = nullptr;
    static jmethodID mid_getInstance = nullptr;

    static jclass cls_WaypointAdd = nullptr;
    static jmethodID mid_ctor_WaypointAdd = nullptr;

    static jclass cls_WaypointRemove = nullptr;
    static jmethodID mid_ctor_WaypointRemove = nullptr;
    
    // We assume world is current world, avoiding world parameter if possible or passing string
    // Signatures vary by version. We will try minimal common valid signature.
    // LCPacketWaypointAdd(String name, int x, int y, int z, int color, boolean forced)
    
    bool Init() {
        if (!Java::Initialized) return false;
        JNIEnv* env = Java::Env();
        if (!env) return false;

        // Find LunarClientAPI
        jclass localAPI = nullptr;
        if (Java::AssignClass("com.lunarclient.bukkitapi.LunarClientAPI", localAPI)) {
            cls_LunarClientAPI = (jclass)env->NewGlobalRef(localAPI);
            env->DeleteLocalRef(localAPI);
            
            mid_getInstance = env->GetStaticMethodID(cls_LunarClientAPI, "getInstance", "()Lcom/lunarclient/bukkitapi/LunarClientAPI;");
            if (mid_getInstance) {
                 obj_LunarClientAPI = env->NewGlobalRef(env->CallStaticObjectMethod(cls_LunarClientAPI, mid_getInstance));
            }
            
            // sendPacket is usually: void sendPacket(LCPacket packet)
            // We need to find LCPacket base class or just "packet"
            // Signature might be (Lcom/lunarclient/bukkitapi/nethandler/LCPacket;)V
            // But checking exact signature requires knowing the package of LCPacket.
            // Usually com.lunarclient.bukkitapi.nethandler.LCPacket
            
            mid_sendPacket = env->GetMethodID(cls_LunarClientAPI, "sendPacket", "(Lcom/lunarclient/bukkitapi/nethandler/LCPacket;)V");
            if (!mid_sendPacket) {
                env->ExceptionClear();
                // Try alternate signature
                mid_sendPacket = env->GetMethodID(cls_LunarClientAPI, "sendPacket", "(Lcom/lunarclient/bukkitapi/nethandler/shared/LCPacket;)V");
            }
        }
        
        // WaypointAdd
        jclass localAdd = nullptr;
        if (Java::AssignClass("com.lunarclient.bukkitapi.nethandler.client.LCPacketWaypointAdd", localAdd)) {
            cls_WaypointAdd = (jclass)env->NewGlobalRef(localAdd);
            env->DeleteLocalRef(localAdd);
            
            // Constructor: Name, World(String?), Color, X, Y, Z, Forced, Visible
            // Or: Name, Color, X, Y, Z, Forced, Visible
            // We will try to find a constructor that matches params we have.
            // (Ljava/lang/String;IIIIZZ)V  -> name, r, g, b, ? No that's not right.
            // Color is int.
            // (Ljava/lang/String;Ljava/lang/String;IIIIZZ)V ? (Name, World, Color, X, Y, Z, Forced, Visible)
            
            // Common: (String name, String world, int color, int x, int y, int z, boolean forced, boolean visible)
            mid_ctor_WaypointAdd = env->GetMethodID(cls_WaypointAdd, "<init>", "(Ljava/lang/String;Ljava/lang/String;IIIIZZ)V");
        }

        // WaypointRemove
        jclass localRemove = nullptr;
        if (Java::AssignClass("com.lunarclient.bukkitapi.nethandler.client.LCPacketWaypointRemove", localRemove)) {
             cls_WaypointRemove = (jclass)env->NewGlobalRef(localRemove);
             env->DeleteLocalRef(localRemove);
             
             // Constructor: Name, World(String)
             mid_ctor_WaypointRemove = env->GetMethodID(cls_WaypointRemove, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
        }
        
        return (obj_LunarClientAPI && mid_sendPacket && cls_WaypointAdd && cls_WaypointRemove);
    }

    void SendWaypoint(const std::string& name, int x, int y, int z, int color, bool forced) {
        JNIEnv* env = Java::Env();
        if (!env || !obj_LunarClientAPI || !mid_sendPacket || !cls_WaypointAdd || !mid_ctor_WaypointAdd) return;

        jstring jName = env->NewStringUTF(name.c_str());
        jstring jWorld = env->NewStringUTF(""); // Empty world often defaults to current or ignored if forced?
        // Actually, we should probably pass current world name if possible, or "" if it works. 
        
        // Object packet = new LCPacketWaypointAdd(...)
        jobject packet = env->NewObject(cls_WaypointAdd, mid_ctor_WaypointAdd, jName, jWorld, color, x, y, z, forced, true);
        
        if (packet) {
            env->CallVoidMethod(obj_LunarClientAPI, mid_sendPacket, packet);
            env->DeleteLocalRef(packet);
        }
        
        env->DeleteLocalRef(jName);
        env->DeleteLocalRef(jWorld);
    }

    void RemoveWaypoint(const std::string& name) {
        JNIEnv* env = Java::Env();
        if (!env || !obj_LunarClientAPI || !mid_sendPacket || !cls_WaypointRemove || !mid_ctor_WaypointRemove) return;
        
        jstring jName = env->NewStringUTF(name.c_str());
        jstring jWorld = env->NewStringUTF(""); 
        
        jobject packet = env->NewObject(cls_WaypointRemove, mid_ctor_WaypointRemove, jName, jWorld);
        
        if (packet) {
            env->CallVoidMethod(obj_LunarClientAPI, mid_sendPacket, packet);
            env->DeleteLocalRef(packet);
        }
        
        env->DeleteLocalRef(jName);
        env->DeleteLocalRef(jWorld);
    }
}
