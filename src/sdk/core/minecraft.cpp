// Minecraft SDK - Core module
// Contains SDK_Init and basic Minecraft instance methods

#include "minecraft.h"
#include "../../util/aes_string.hpp"
#include "../../util/inventory/inventory_hooks.h"
#include "../../util/logger.hpp"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"
#include "../player/effect_tracker.h"
#include "../player/render_data.h"
#include "../player/visual_cache.h"
#include "../../util/state.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

Minecraft mc;

void SDK_Init() {
    if (!Java::Initialized) {
        log_err("SDK_Init: java not initialized");
        return;
    }

    // Get Minecraft class
    if (mc.cls) {
        Java::Env()->DeleteGlobalRef(mc.cls);
        mc.cls = nullptr;
    }
    jclass localCls = nullptr;
    if (!Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.Minecraft"), localCls)) {
        log_err("SDK_Init: failed to find Minecraft class");
        return;
    }
    mc.cls = (jclass)Java::Env()->NewGlobalRef(localCls);
    Java::Env()->DeleteLocalRef(localCls);

    // getMinecraft() -> Minecraft instance
    mc.mid_getMinecraft = Java::Env()->GetStaticMethodID(mc.cls, OBFUSCATE_STRING("getMinecraft").c_str(),
                                                         OBFUSCATE_STRING("()Lnet/minecraft/client/Minecraft;").c_str());
    if (!mc.mid_getMinecraft && Java::RuntimeObfuscated) {
        Java::Env()->ExceptionClear();
        mc.mid_getMinecraft = Java::Env()->GetStaticMethodID(mc.cls, "A", "()Lave;");
    }
    if (!mc.mid_getMinecraft) {
        log_err("SDK_Init: failed to find getMinecraft");
        return;
    }

    if (mc.instance) {
        Java::Env()->DeleteGlobalRef(mc.instance);
        mc.instance = nullptr;
    }
    jobject localInst = Java::Env()->CallStaticObjectMethod(mc.cls, mc.mid_getMinecraft);
    if (!localInst) {
        log_err("SDK_Init: getMinecraft returned null");
        return;
    }
    mc.instance = Java::Env()->NewGlobalRef(localInst);
    Java::Env()->DeleteLocalRef(localInst);

    // thePlayer field - signature depends on version
    std::string playerSig = (Java::Version == MinecraftVersion::MC_1_7_10)
                                ? OBFUSCATE_STRING("Lnet/minecraft/client/entity/EntityClientPlayerMP;")
                                : OBFUSCATE_STRING("Lnet/minecraft/client/entity/EntityPlayerSP;");
    mc.fid_thePlayer = Java::Env()->GetFieldID(mc.cls, OBFUSCATE_STRING("thePlayer").c_str(), playerSig.c_str());
    if (!mc.fid_thePlayer && Java::RuntimeObfuscated) {
        Java::Env()->ExceptionClear();
        mc.fid_thePlayer = Java::Env()->GetFieldID(mc.cls, "h", "Lbew;");
    }

    // theWorld field
    mc.fid_theWorld = Java::Env()->GetFieldID(mc.cls, OBFUSCATE_STRING("theWorld").c_str(),
                                              OBFUSCATE_STRING("Lnet/minecraft/client/multiplayer/WorldClient;").c_str());
    if (!mc.fid_theWorld && Java::RuntimeObfuscated) {
        Java::Env()->ExceptionClear();
        mc.fid_theWorld = Java::Env()->GetFieldID(mc.cls, "f", "Lbdb;");
    }

    // currentScreen field (to detect if GUI is open)
    mc.fid_currentScreen = Java::Env()->GetFieldID(mc.cls, OBFUSCATE_STRING("currentScreen").c_str(),
                                                   OBFUSCATE_STRING("Lnet/minecraft/client/gui/GuiScreen;").c_str());
    if (!mc.fid_currentScreen && Java::RuntimeObfuscated) {
        Java::Env()->ExceptionClear();
        mc.fid_currentScreen = Java::Env()->GetFieldID(mc.cls, "m", "Laxu;");
    }

    // clickMouse method
    mc.mid_clickMouse = Java::Env()->GetMethodID(mc.cls, OBFUSCATE_STRING("clickMouse").c_str(),
                                                 OBFUSCATE_STRING("()V").c_str());
    mc.mid_rightClickMouse = Java::Env()->GetMethodID(mc.cls, OBFUSCATE_STRING("rightClickMouse").c_str(),
                                                      OBFUSCATE_STRING("()V").c_str());
    if (!mc.mid_rightClickMouse) {
        Java::Env()->ExceptionClear();
        mc.mid_rightClickMouse = Java::Env()->GetMethodID(mc.cls, OBFUSCATE_STRING("func_147121_ag").c_str(),
                                                          OBFUSCATE_STRING("()V").c_str());
        if (!mc.mid_rightClickMouse)
            Java::Env()->ExceptionClear();
    }

    // Click delay fields
    mc.fid_rightClickDelayTimer =
        Java::Env()->GetFieldID(mc.cls, OBFUSCATE_STRING("rightClickDelayTimer").c_str(), OBFUSCATE_STRING("I").c_str());
    mc.fid_leftClickCounter =
        Java::Env()->GetFieldID(mc.cls, OBFUSCATE_STRING("leftClickCounter").c_str(), OBFUSCATE_STRING("I").c_str());

    // KeyBinding cache for releaseMovementKeys (legacy, kept for compatibility)
    jclass kbCls = nullptr;
    if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.settings.KeyBinding"), kbCls)) {
        mc.cls_keyBinding = (jclass)Java::Env()->NewGlobalRef(kbCls);

        mc.mid_unPressAllKeys = Java::Env()->GetStaticMethodID(
            mc.cls_keyBinding, OBFUSCATE_STRING("unPressAllKeys").c_str(), OBFUSCATE_STRING("()V").c_str());
        if (!mc.mid_unPressAllKeys) {
            Java::Env()->ExceptionClear();
            mc.mid_unPressAllKeys = Java::Env()->GetStaticMethodID(
                mc.cls_keyBinding, OBFUSCATE_STRING("func_74505_d").c_str(), OBFUSCATE_STRING("()V").c_str());
        }

        mc.mid_setKeyBindState = Java::Env()->GetStaticMethodID(
            mc.cls_keyBinding, OBFUSCATE_STRING("setKeyBindState").c_str(), OBFUSCATE_STRING("(IZ)V").c_str());
        mc.mid_getKeyCode = Java::Env()->GetMethodID(mc.cls_keyBinding, OBFUSCATE_STRING("getKeyCode").c_str(),
                                                     OBFUSCATE_STRING("()I").c_str());
        mc.fid_pressed = Java::Env()->GetFieldID(mc.cls_keyBinding, OBFUSCATE_STRING("pressed").c_str(),
                                                 OBFUSCATE_STRING("Z").c_str());
    }

    // Initialize JNI cache (new centralized system)
    if (!JniCache::Init()) {
        log_warn("JniCache::Init failed - some features may not work");
    }

    // Initialize player (will lazy-load fields on first refresh)
    player.refresh();

    // Initialize inventory hooks for potion detection
    if (!InventoryHooks::install()) {
        log_warn("[SDK] Inventory hooks installation failed - potion detection disabled");
    }

    log_ok("SDK initialized");
}

// =============================================================================
// Core Minecraft instance methods
// =============================================================================

jobject Minecraft::getPlayer() {
    if (!isValid() || !fid_thePlayer)
        return nullptr;
    return Java::Env()->GetObjectField(instance, fid_thePlayer);
}

jobject Minecraft::getWorld() {
    if (!isValid() || !fid_theWorld)
        return nullptr;
    return Java::Env()->GetObjectField(instance, fid_theWorld);
}

bool Minecraft::isInGui() {
    if (!isValid() || !fid_currentScreen)
        return false;
    jobject screen = Java::Env()->GetObjectField(instance, fid_currentScreen);
    bool result = (screen != nullptr);
    if (screen)
        Java::Env()->DeleteLocalRef(screen);
    return result;
}

// =============================================================================
// getItemEntityList - Get all item entities
// =============================================================================

std::vector<ItemEntityInfo> Minecraft::getItemEntityList() {
    std::vector<ItemEntityInfo> result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    jobject world = getWorld();
    if (!world)
        return result;

    // Bail if required IDs are not yet resolved.
    if (!JniCache::fid_loadedEntityList || !JniCache::mid_listSize || !JniCache::mid_listGet) {
        env->DeleteLocalRef(world);
        return result;
    }

    jobject entityList = env->GetObjectField(world, JniCache::fid_loadedEntityList);
    env->DeleteLocalRef(world);

    if (!entityList)
        return result;

    int size = env->CallIntMethod(entityList, JniCache::mid_listSize);
    if (size <= 0) {
        env->DeleteLocalRef(entityList);
        return result;
    }

    result.reserve(size / 4); // Heuristic

    for (int i = 0; i < size; i++) {
        jobject entity = env->CallObjectMethod(entityList, JniCache::mid_listGet, i);
        if (!entity)
            continue;

        // Check if EntityItem
        if (JniCache::cls_EntityItem && JniCache::mid_getEntityItem &&
            env->IsInstanceOf(entity, JniCache::cls_EntityItem)) {
             jobject itemStack = env->CallObjectMethod(entity, JniCache::mid_getEntityItem);
             if (itemStack) {
                 ItemEntityInfo info;
                 // We extract everything we need below and never use the entity
                 // ref afterwards, so don't keep it — holding the local ref every
                 // tick overflows the JNI local reference table (~512) within ~1s
                 // and makes getItemEntityList start returning empty, which shows
                 // up as the ESP box flickering on/off. Deleted before continue.
                 info.instance = nullptr;

                 // Position
                 if (JniCache::fid_posX) info.posX = env->GetDoubleField(entity, JniCache::fid_posX);
                 if (JniCache::fid_posY) info.posY = env->GetDoubleField(entity, JniCache::fid_posY);
                 if (JniCache::fid_posZ) info.posZ = env->GetDoubleField(entity, JniCache::fid_posZ);

                 // Stack Size
                 if (JniCache::fid_stackSize) {
                     info.stackSize = env->GetIntField(itemStack, JniCache::fid_stackSize);
                 }

                 // Item ID and Max Stack
                 if (JniCache::mid_getItem) {
                     jobject item = env->CallObjectMethod(itemStack, JniCache::mid_getItem);
                     if (item) {
                         if (JniCache::mid_getIdFromItem && JniCache::cls_Item) {
                             info.itemId = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
                         }
                         if (JniCache::mid_getItemStackLimit) {
                             info.maxStackSize = env->CallIntMethod(item, JniCache::mid_getItemStackLimit);
                         }
                         env->DeleteLocalRef(item);
                     }
                 }
                 
                 // Damage/Metadata
                 if (JniCache::mid_getItemDamage) {
                     info.damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
                 }

                 // Custom display name (server-set NBT name). Only read the string
                 // when hasDisplayName() is true so normal drops don't pay the
                 // GetStringUTFChars cost every tick. Keeps the raw § color codes;
                 // the renderer parses them.
                 if (JniCache::mid_itemStackHasDisplayName && JniCache::mid_itemStackGetDisplayName) {
                     if (env->CallBooleanMethod(itemStack, JniCache::mid_itemStackHasDisplayName)) {
                         jobject nameObj = env->CallObjectMethod(itemStack, JniCache::mid_itemStackGetDisplayName);
                         if (nameObj) {
                             info.customName = jstringToString((jstring)nameObj);
                             info.hasCustomName = !info.customName.empty();
                             env->DeleteLocalRef(nameObj);
                         }
                     }
                 }

                 env->DeleteLocalRef(itemStack);
                 env->DeleteLocalRef(entity); // done with it — avoid local ref leak
                 result.push_back(info);
                 continue;
             }
        }

        // If we reached here, we didn't use the entity
        env->DeleteLocalRef(entity);
    }

    env->DeleteLocalRef(entityList);
    return result;
}

void Minecraft::clickMouse() {
    if (!isValid() || !mid_clickMouse)
        return;
    Java::Env()->CallVoidMethod(instance, mid_clickMouse);
}

bool Minecraft::attackEntity(jobject target) {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !target || !JniCache::fid_playerController)
        return false;

    if (env->ExceptionCheck())
        env->ExceptionClear();

    jobject controller = env->GetObjectField(instance, JniCache::fid_playerController);
    jobject localPlayer = getPlayer();
    if (!controller || !localPlayer) {
        if (controller)
            env->DeleteLocalRef(controller);
        if (localPlayer)
            env->DeleteLocalRef(localPlayer);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jclass controllerClass = env->GetObjectClass(controller);
    jclass playerClass = env->GetObjectClass(localPlayer);
    if (!controllerClass || !playerClass) {
        if (controllerClass)
            env->DeleteLocalRef(controllerClass);
        if (playerClass)
            env->DeleteLocalRef(playerClass);
        env->DeleteLocalRef(localPlayer);
        env->DeleteLocalRef(controller);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    const std::string attackSignature =
        OBFUSCATE_STRING("(Lnet/minecraft/entity/player/EntityPlayer;Lnet/minecraft/entity/Entity;)V");
    jmethodID attackMethod = env->GetMethodID(
        controllerClass, OBFUSCATE_STRING("attackEntity").c_str(), attackSignature.c_str());
    if (!attackMethod) {
        env->ExceptionClear();
        attackMethod = env->GetMethodID(
            controllerClass, OBFUSCATE_STRING("func_78764_a").c_str(), attackSignature.c_str());
    }
    if (!attackMethod && env->ExceptionCheck())
        env->ExceptionClear();

    jmethodID swingMethod =
        env->GetMethodID(playerClass, OBFUSCATE_STRING("swingItem").c_str(), OBFUSCATE_STRING("()V").c_str());
    if (!swingMethod) {
        env->ExceptionClear();
        swingMethod =
            env->GetMethodID(playerClass, OBFUSCATE_STRING("func_71038_i").c_str(), OBFUSCATE_STRING("()V").c_str());
    }
    if (!swingMethod && env->ExceptionCheck())
        env->ExceptionClear();

    bool attacked = false;
    if (attackMethod) {
        if (swingMethod) {
            env->CallVoidMethod(localPlayer, swingMethod);
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }

        env->CallVoidMethod(controller, attackMethod, localPlayer, target);
        attacked = !env->ExceptionCheck();
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(playerClass);
    env->DeleteLocalRef(controllerClass);
    env->DeleteLocalRef(localPlayer);
    env->DeleteLocalRef(controller);
    return attacked;
}

jobject Minecraft::getNetHandler() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !JniCache::mid_getNetHandler)
        return nullptr;
    jobject nh = env->CallObjectMethod(instance, JniCache::mid_getNetHandler);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return nh;
}

void Minecraft::rightClickMouse() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !mid_rightClickMouse || !env)
        return;
    env->CallVoidMethod(instance, mid_rightClickMouse);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void Minecraft::updateMouseOver() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !JniCache::fid_entityRenderer || !JniCache::mid_getMouseOver)
        return;
    jobject renderer = env->GetObjectField(instance, JniCache::fid_entityRenderer);
    if (!renderer)
        return;
    env->CallVoidMethod(renderer, JniCache::mid_getMouseOver, getPartialTicks());
    env->DeleteLocalRef(renderer);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

int Minecraft::getRightClickDelay() {
    if (!isValid() || !fid_rightClickDelayTimer)
        return 0;
    return Java::Env()->GetIntField(instance, fid_rightClickDelayTimer);
}

void Minecraft::setRightClickDelay(int value) {
    if (!isValid() || !fid_rightClickDelayTimer)
        return;
    Java::Env()->SetIntField(instance, fid_rightClickDelayTimer, value);
}

void Minecraft::setLeftClickCounter(int value) {
    if (!isValid() || !fid_leftClickCounter || !Java::Env())
        return;
    Java::Env()->SetIntField(instance, fid_leftClickCounter, value);
    if (Java::Env()->ExceptionCheck()) {
        Java::Env()->ExceptionClear();
    }
}

void Minecraft::setTimerSpeed(float speed) {
    if (!isValid())
        return;
    if (!JniCache::fid_timer || !JniCache::fid_timerSpeed)
        return;

    jobject timer = Java::Env()->GetObjectField(instance, JniCache::fid_timer);
    if (!timer)
        return;

    Java::Env()->SetFloatField(timer, JniCache::fid_timerSpeed, speed);
    Java::Env()->DeleteLocalRef(timer);
}

int Minecraft::getGameFps() {
    if (!isValid() || !cls)
        return 0;
    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    // Static int Minecraft.debugFPS. Resolve once; SRG fallback for non-MCP maps.
    static jfieldID fid_debugFPS = nullptr;
    static bool triedResolve = false;
    if (!fid_debugFPS && !triedResolve) {
        triedResolve = true;
        fid_debugFPS = env->GetStaticFieldID(cls, OBFUSCATE_STRING("debugFPS").c_str(), OBFUSCATE_STRING("I").c_str());
        if (!fid_debugFPS) {
            env->ExceptionClear();
            fid_debugFPS =
                env->GetStaticFieldID(cls, OBFUSCATE_STRING("field_71470_ab").c_str(), OBFUSCATE_STRING("I").c_str());
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!fid_debugFPS)
        return 0;

    int fps = env->GetStaticIntField(cls, fid_debugFPS);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return fps;
}

void Minecraft::sendPacket(jobject packet) {
    if (!isValid() || !packet)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Bail if IDs are not yet resolved (inject-in-menu or poisoned cache).
    if (!JniCache::mid_getNetHandler || !JniCache::mid_addToSendQueue)
        return;

    if (env->ExceptionCheck())
        env->ExceptionClear();

    // Get NetHandlerPlayClient connection
    jobject netHandler = env->CallObjectMethod(instance, JniCache::mid_getNetHandler);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!netHandler)
        return;

    // Call addToSendQueue(packet)
    env->CallVoidMethod(netHandler, JniCache::mid_addToSendQueue, packet);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    env->DeleteLocalRef(netHandler);
}

int Minecraft::getLocalPlayerPing() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !JniCache::mid_getNetHandler || !JniCache::mid_getUniqueID ||
        !JniCache::mid_getPlayerInfoByUUID || !JniCache::mid_getResponseTime)
        return -1;

    if (env->ExceptionCheck())
        env->ExceptionClear();

    jobject localPlayer = getPlayer();
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return -1;
    }
    if (!localPlayer)
        return -1;

    jobject netHandler = env->CallObjectMethod(instance, JniCache::mid_getNetHandler);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(localPlayer);
        if (netHandler)
            env->DeleteLocalRef(netHandler);
        return -1;
    }
    if (!netHandler) {
        env->DeleteLocalRef(localPlayer);
        return -1;
    }

    jobject uuid = env->CallObjectMethod(localPlayer, JniCache::mid_getUniqueID);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (uuid)
            env->DeleteLocalRef(uuid);
        env->DeleteLocalRef(netHandler);
        env->DeleteLocalRef(localPlayer);
        return -1;
    }

    jobject playerInfo = uuid ? env->CallObjectMethod(netHandler, JniCache::mid_getPlayerInfoByUUID, uuid) : nullptr;

    int ping = -1;
    if (!env->ExceptionCheck() && playerInfo)
        ping = env->CallIntMethod(playerInfo, JniCache::mid_getResponseTime);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ping = -1;
    }

    if (playerInfo)
        env->DeleteLocalRef(playerInfo);
    if (uuid)
        env->DeleteLocalRef(uuid);
    env->DeleteLocalRef(netHandler);
    env->DeleteLocalRef(localPlayer);
    return ping;
}

bool Minecraft::isFirstPerson() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !JniCache::fid_gameSettings || !JniCache::fid_thirdPersonView)
        return true;

    jobject gameSettings = env->GetObjectField(instance, JniCache::fid_gameSettings);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (gameSettings)
            env->DeleteLocalRef(gameSettings);
        return true;
    }
    if (!gameSettings)
        return true;

    const jint view = env->GetIntField(gameSettings, JniCache::fid_thirdPersonView);
    env->DeleteLocalRef(gameSettings);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return view == 0;
}

void Minecraft::sendInventoryOpenPacket() {
    JNIEnv* env = Java::Env();
    if (!env || !JniCache::cls_C16PacketClientStatus || !JniCache::fid_C16EnumState_OPEN_INVENTORY_ACHIEVEMENT) {
        log_debug("Cannot send open packet: Missing classes/IDs");
        return;
    }

    // Get EnumState.OPEN_INVENTORY_ACHIEVEMENT
    jobject enumState =
        env->GetStaticObjectField(JniCache::cls_C16EnumState, JniCache::fid_C16EnumState_OPEN_INVENTORY_ACHIEVEMENT);
    if (!enumState)
        return;

    // New C16PacketClientStatus(enumState)
    jobject packet =
        env->NewObject(JniCache::cls_C16PacketClientStatus, JniCache::mid_C16PacketClientStatus_init, enumState);
    if (packet) {
        sendPacket(packet);
        env->DeleteLocalRef(packet);
    }

    env->DeleteLocalRef(enumState);
}

void Minecraft::sendInventoryClosePacket() {
    JNIEnv* env = Java::Env();
    if (!env || !JniCache::cls_C0DPacketCloseWindow) {
        log_debug("Cannot send close packet: Missing classes");
        return;
    }

    // We can use windowId 0 (player inventory) or try to get actual open container id?
    // Usually for client inventory loop, sending close for window 0 is correct if we pretended to open it.
    // However, C0D constructor takes int windowId.

    jobject packet = env->NewObject(JniCache::cls_C0DPacketCloseWindow, JniCache::mid_C0DPacketCloseWindow_init, 0);
    if (packet) {
        sendPacket(packet);
        env->DeleteLocalRef(packet);
    }
}

void Minecraft::cleanup() {
    if (cls_keyBinding && Java::Initialized) {
        JNIEnv* env = Java::Env();
        if (env) {
            env->DeleteGlobalRef(cls_keyBinding);
            cls_keyBinding = nullptr;
        }
    }
}

// =============================================================================
// World / server change detection (main JNI thread only)
// =============================================================================
bool SDK_PollWorldChange() {
    JNIEnv* env = Java::Env();
    if (!env || !Java::Initialized)
        return false;

    // Track the current world by GlobalRef identity across ticks.
    static jobject s_lastWorldRef = nullptr;

    jobject currentWorld = mc.getWorld();  // fresh local ref (null while in a menu)

    bool changed = false;
    if (s_lastWorldRef == nullptr && currentWorld != nullptr) {
        changed = true;  // menu -> world (first join / reconnect)
    } else if (s_lastWorldRef != nullptr && currentWorld == nullptr) {
        changed = true;  // world -> menu (disconnect)
    } else if (s_lastWorldRef != nullptr && currentWorld != nullptr) {
        if (env->IsSameObject(s_lastWorldRef, currentWorld) == JNI_FALSE)
            changed = true;  // server -> server / dimension change
    }

    if (changed) {
        // Refresh our GlobalRef to point at the current world.
        if (s_lastWorldRef) {
            env->DeleteGlobalRef(s_lastWorldRef);
            s_lastWorldRef = nullptr;
        }
        if (currentWorld)
            s_lastWorldRef = env->NewGlobalRef(currentWorld);

        // Re-resolve JNI IDs that were unavailable at inject time and
        // re-baseline the checksum so validateState() doesn't flag it.
        if (sdk::internal::isOperational()) {
            JniCache::Reinit();
            sdk::internal::rebaselineChecksum();
        }

        // Drop stale cross-world caches.
        VisualCache::Reset();
        EffectTracker::Clear();
        RenderData::initialized = false;
        RenderData::Reset();
    }

    if (currentWorld)
        env->DeleteLocalRef(currentWorld);

    return changed;
}

std::string Minecraft::getCurrentServerIP() {
    if (!isValid())
        return "none";
    JNIEnv* env = Java::Env();

    jfieldID fid_currentServerData = env->GetFieldID(cls, OBFUSCATE_STRING("currentServerData").c_str(),
                                                     OBFUSCATE_STRING("Lnet/minecraft/client/multiplayer/ServerData;").c_str());
    if (!fid_currentServerData) {
        env->ExceptionClear();
        fid_currentServerData = env->GetFieldID(cls, OBFUSCATE_STRING("field_71422_O").c_str(),
                                                 OBFUSCATE_STRING("Lnet/minecraft/client/multiplayer/ServerData;").c_str());
        if (!fid_currentServerData) {
            env->ExceptionClear();
            return "singleplayer";
        }
    }

    jobject serverData = env->GetObjectField(instance, fid_currentServerData);
    if (!serverData) {
        return "singleplayer";
    }

    jclass cls_ServerData = env->GetObjectClass(serverData);
    jfieldID fid_serverIP = env->GetFieldID(cls_ServerData, OBFUSCATE_STRING("serverIP").c_str(),
                                            OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    if (!fid_serverIP) {
        env->ExceptionClear();
        fid_serverIP = env->GetFieldID(cls_ServerData, OBFUSCATE_STRING("field_78845_b").c_str(),
                                        OBFUSCATE_STRING("Ljava/lang/String;").c_str());
        if (!fid_serverIP) {
            env->ExceptionClear();
            env->DeleteLocalRef(cls_ServerData);
            env->DeleteLocalRef(serverData);
            return "singleplayer";
        }
    }

    jstring jip = (jstring)env->GetObjectField(serverData, fid_serverIP);
    std::string ip = "singleplayer";
    if (jip) {
        ip = jstringToString(jip);
        env->DeleteLocalRef(jip);
    }

    env->DeleteLocalRef(cls_ServerData);
    env->DeleteLocalRef(serverData);
    return ip;
}
