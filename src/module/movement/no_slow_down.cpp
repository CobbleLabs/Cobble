#include "no_slow_down.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

NoSlowDown* noSlowDown = nullptr;

// ---------------------------------------------------------------------------
// Minecraft 1.7.10 / 1.8.x item ID ranges
//   Swords:  272 (wood), 267 (iron), 276 (diamond), 268 (stone), 283 (gold)
//   Bows:    261
//   Food:    297,320,319,349,350,357,363–366,391–394,400,412,413,423,424,
//            322 (golden apple), 391 (carrot), 393 (baked potato)…
//   Potions: 373
//   Milk:    335
// ---------------------------------------------------------------------------
static bool isSwordId(int id) {
    return id == 267 || id == 268 || id == 269 || id == 272 || id == 276 || id == 283;
}
static bool isBowId(int id) {
    return id == 261;
}
static bool isConsumeId(int id) {
    // Food IDs (common 1.7/1.8 foods)
    switch (id) {
        case 297: case 298: case 299: case 300: case 301: // bread, pork, cooked pork, painting (skip), golden apple
        case 319: case 320:   // raw/cooked pork
        case 322:             // golden apple
        case 335:             // milk bucket
        case 349: case 350:   // raw/cooked fish
        case 357:             // cookie
        case 360:             // melon slice
        case 363: case 364: case 365: case 366: // raw/cooked beef / raw/cooked chicken
        case 373:             // potion
        case 374:             // water bottle (also ItemPotion)
        case 391: case 392: case 393: case 394: // carrot, potato, baked potato, poisonous potato
        case 400:             // pumpkin pie
        case 412: case 413:   // raw/cooked rabbit
        case 423: case 424:   // raw/cooked mutton
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Helpers to build and send C07 / C08 packets via JNI
// ---------------------------------------------------------------------------

// Sends C07PacketPlayerDigging with Action.RELEASE_USE_ITEM
// (BlockPos(-1,-1,-1), EnumFacing.DOWN)
static bool sendC07ReleaseUseItem(JNIEnv* env) {
    // Class / method IDs are looked up once and cached statically
    static jclass    cls_C07        = nullptr;
    static jmethodID mid_C07Init    = nullptr;
    static jclass    cls_Action     = nullptr;
    static jobject   obj_RELEASE    = nullptr;  // GlobalRef
    static jclass    cls_BlockPos   = nullptr;
    static jmethodID mid_BPInit     = nullptr;
    static jclass    cls_EnumFacing = nullptr;
    static jobject   obj_DOWN       = nullptr;  // GlobalRef

    if (!cls_C07) {
        cls_C07 = (jclass)env->NewGlobalRef(
            env->FindClass("net/minecraft/network/play/client/C07PacketPlayerDigging"));
        if (env->ExceptionCheck() || !cls_C07) { env->ExceptionClear(); return false; }

        mid_C07Init = env->GetMethodID(cls_C07, "<init>",
            "(Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)V");
        if (env->ExceptionCheck() || !mid_C07Init) {
            // 1.7.10 alternative - no BlockPos
            env->ExceptionClear();
            mid_C07Init = env->GetMethodID(cls_C07, "<init>", "(I)V");
            if (env->ExceptionCheck() || !mid_C07Init) { env->ExceptionClear(); return false; }

            // 1.7.10 path: C07(5) = RELEASE_USE_ITEM
            jobject packet = env->NewObject(cls_C07, mid_C07Init, 5);
            if (env->ExceptionCheck() || !packet) { env->ExceptionClear(); return false; }
            mc.sendPacket(packet);
            env->DeleteLocalRef(packet);
            return true;
        }
    }

    if (!obj_RELEASE) {
        jclass actionCls = env->FindClass(
            "net/minecraft/network/play/client/C07PacketPlayerDigging$Action");
        if (env->ExceptionCheck() || !actionCls) { env->ExceptionClear(); return false; }
        jfieldID fid = env->GetStaticFieldID(actionCls, "RELEASE_USE_ITEM",
            "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;");
        if (env->ExceptionCheck() || !fid) { env->ExceptionClear(); env->DeleteLocalRef(actionCls); return false; }
        jobject local = env->GetStaticObjectField(actionCls, fid);
        obj_RELEASE = env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
        env->DeleteLocalRef(actionCls);
    }

    if (!cls_BlockPos) {
        cls_BlockPos = (jclass)env->NewGlobalRef(
            env->FindClass("net/minecraft/util/BlockPos"));
        if (env->ExceptionCheck() || !cls_BlockPos) { env->ExceptionClear(); return false; }
        mid_BPInit = env->GetMethodID(cls_BlockPos, "<init>", "(III)V");
        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    }

    if (!cls_EnumFacing) {
        cls_EnumFacing = (jclass)env->NewGlobalRef(
            env->FindClass("net/minecraft/util/EnumFacing"));
        if (env->ExceptionCheck() || !cls_EnumFacing) { env->ExceptionClear(); return false; }
        jfieldID fid = env->GetStaticFieldID(cls_EnumFacing, "DOWN",
            "Lnet/minecraft/util/EnumFacing;");
        if (env->ExceptionCheck() || !fid) { env->ExceptionClear(); return false; }
        jobject local = env->GetStaticObjectField(cls_EnumFacing, fid);
        obj_DOWN = env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
    }

    // Build BlockPos(-1, -1, -1)
    jobject blockPos = env->NewObject(cls_BlockPos, mid_BPInit, -1, -1, -1);
    if (env->ExceptionCheck() || !blockPos) { env->ExceptionClear(); return false; }

    // Build C07(RELEASE_USE_ITEM, blockPos, DOWN)
    jobject packet = env->NewObject(cls_C07, mid_C07Init, obj_RELEASE, blockPos, obj_DOWN);
    env->DeleteLocalRef(blockPos);
    if (env->ExceptionCheck() || !packet) { env->ExceptionClear(); return false; }

    mc.sendPacket(packet);
    env->DeleteLocalRef(packet);
    return true;
}

// Sends C08PacketPlayerBlockPlacement with currently held item
// (equivalent to thePlayer.sendQueue.addToSendQueue(new C08(...getCurrentItem())))
static bool sendC08HeldItem(JNIEnv* env) {
    static jclass    cls_C08     = nullptr;
    static jmethodID mid_C08Init = nullptr;

    if (!cls_C08) {
        cls_C08 = (jclass)env->NewGlobalRef(
            env->FindClass("net/minecraft/network/play/client/C08PacketPlayerBlockPlacement"));
        if (env->ExceptionCheck() || !cls_C08) { env->ExceptionClear(); return false; }

        // Constructor that takes ItemStack (held item)
        mid_C08Init = env->GetMethodID(cls_C08, "<init>",
            "(Lnet/minecraft/item/ItemStack;)V");
        if (env->ExceptionCheck() || !mid_C08Init) {
            env->ExceptionClear();
            // Fallback: no-arg constructor (places "nothing" but still triggers the re-arm)
            mid_C08Init = env->GetMethodID(cls_C08, "<init>", "()V");
            if (env->ExceptionCheck() || !mid_C08Init) { env->ExceptionClear(); return false; }
        }
    }

    jobject packet = nullptr;
    if (mid_C08Init) {
        // Try to get current held ItemStack for the proper constructor
        static jclass    cls_InvPlayer = nullptr;
        static jmethodID mid_getCurItem = nullptr;
        bool usedItemStack = false;

        if (!cls_InvPlayer && JniCache::fid_inventory) {
            jobject inv = env->GetObjectField(player.instance, JniCache::fid_inventory);
            if (!env->ExceptionCheck() && inv) {
                cls_InvPlayer = (jclass)env->NewGlobalRef(env->GetObjectClass(inv));
                mid_getCurItem = env->GetMethodID(cls_InvPlayer, "getCurrentItem",
                    "()Lnet/minecraft/item/ItemStack;");
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    mid_getCurItem = env->GetMethodID(cls_InvPlayer, "func_70448_g",
                        "()Lnet/minecraft/item/ItemStack;");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); mid_getCurItem = nullptr; }
                }
                env->DeleteLocalRef(inv);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (cls_InvPlayer && mid_getCurItem && JniCache::fid_inventory) {
            jobject inv = env->GetObjectField(player.instance, JniCache::fid_inventory);
            if (!env->ExceptionCheck() && inv) {
                jobject heldItem = env->CallObjectMethod(inv, mid_getCurItem);
                env->DeleteLocalRef(inv);
                if (!env->ExceptionCheck() && heldItem) {
                    packet = env->NewObject(cls_C08, mid_C08Init, heldItem);
                    env->DeleteLocalRef(heldItem);
                    usedItemStack = true;
                } else {
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
            } else {
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }

        if (!usedItemStack || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (packet) { env->DeleteLocalRef(packet); packet = nullptr; }
            // Fall back to no-arg
            jmethodID mid_noArg = env->GetMethodID(cls_C08, "<init>", "()V");
            if (!env->ExceptionCheck() && mid_noArg) {
                packet = env->NewObject(cls_C08, mid_noArg);
            }
            if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
        }
    }

    if (!packet) return false;
    mc.sendPacket(packet);
    env->DeleteLocalRef(packet);
    return true;
}

// ---------------------------------------------------------------------------
// NoSlowDown
// ---------------------------------------------------------------------------

NoSlowDown::NoSlowDown() : Module("NoSlowDown") {
    keybind = 0;
    locked = true;
}

bool NoSlowDown::initializeFields() {
    if (movementInputField && moveForwardField && moveStrafeField)
        return true;

    JNIEnv* env = Java::Env();
    if (!env || !player.isValid())
        return false;

    jclass playerClass = env->GetObjectClass(player.instance);
    if (!playerClass)
        return false;

    const auto findField = [env](jclass cls, const char* name, const char* fallback,
                                 const char* signature) -> jfieldID {
        jfieldID field = env->GetFieldID(cls, name, signature);
        if (!field) {
            env->ExceptionClear();
            field = env->GetFieldID(cls, fallback, signature);
        }
        if (!field && env->ExceptionCheck())
            env->ExceptionClear();
        return field;
    };

    movementInputField    = findField(playerClass, "movementInput",  "field_71158_b",  "Lnet/minecraft/util/MovementInput;");
    entityMoveStrafeField = findField(playerClass, "moveStrafing",   "field_70702_br", "F");
    entityMoveForwardField= findField(playerClass, "moveForward",    "field_70701_bs", "F");
    itemInUseCountField   = findField(playerClass, "itemInUseCount", "field_71072_f",  "I");

    if (env->ExceptionCheck() || !movementInputField) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jobject movementInput = env->GetObjectField(player.instance, movementInputField);
    if (env->ExceptionCheck() || !movementInput) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jclass movementInputClass = env->GetObjectClass(movementInput);
    if (movementInputClass) {
        moveStrafeField = findField(movementInputClass, "moveStrafe",  "field_78902_a", "F");
        moveForwardField= findField(movementInputClass, "moveForward", "field_78900_b", "F");
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    if (movementInputClass) env->DeleteLocalRef(movementInputClass);
    env->DeleteLocalRef(movementInput);
    env->DeleteLocalRef(playerClass);

    return moveForwardField && moveStrafeField;
}

bool NoSlowDown::isKeyPhysicallyPressed(jobject gameSettings, jfieldID keyBindField) {
    JNIEnv* env = Java::Env();
    if (!env || !gameSettings || !keyBindField)
        return false;

    jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
    if (!keyBind || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    bool pressed = false;
    bool physicalStateAvailable = false;
    if (JniCache::mid_getKeyCode) {
        const int keyCode = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);
        if (!env->ExceptionCheck()) {
            if (keyCode < 0) {
                const int button = keyCode + 100;
                const int virtualKey = button == 0 ? VK_LBUTTON :
                                       button == 1 ? VK_RBUTTON :
                                       button == 2 ? VK_MBUTTON : 0;
                physicalStateAvailable = virtualKey != 0;
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            } else {
                const UINT virtualKey = MapVirtualKeyA(static_cast<UINT>(keyCode), MAPVK_VSC_TO_VK);
                physicalStateAvailable = virtualKey != 0;
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            }
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!physicalStateAvailable && JniCache::fid_pressed)
        pressed = env->GetBooleanField(keyBind, JniCache::fid_pressed) == JNI_TRUE;
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(keyBind);
    return pressed;
}

bool NoSlowDown::isPlayerUsingItem() {
    JNIEnv* env = Java::Env();
    if (!env || !player.isValid())
        return false;

    bool usingItem = player.isUsingItem();
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        usingItem = false;
    }

    if (!usingItem && itemInUseCountField) {
        usingItem = env->GetIntField(player.instance, itemInUseCountField) > 0;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            usingItem = false;
        }
    }

    return usingItem;
}

NoSlowDown::HeldItemType NoSlowDown::getHeldItemType() {
    const int id = player.getHeldItemId();
    if (isSwordId(id))   return HeldItemType::Sword;
    if (isBowId(id))     return HeldItemType::Bow;
    if (isConsumeId(id)) return HeldItemType::Consume;
    return HeldItemType::None;
}

// Returns [forward, strafe] multipliers for the currently held item
static std::pair<float, float> getMultipliers(NoSlowDown::HeldItemType type,
                                              const NoSlowDown& m) {
    switch (type) {
        case NoSlowDown::HeldItemType::Sword:   return { m.blockForward,   m.blockStrafe   };
        case NoSlowDown::HeldItemType::Bow:     return { m.bowForward,     m.bowStrafe     };
        case NoSlowDown::HeldItemType::Consume: return { m.consumeForward, m.consumeStrafe };
        default:                                return { 0.2f,             0.2f            };
    }
}

void NoSlowDown::handlePacketMode() {
    // Only relevant for Sword (blocking) in NCP / Hypixel / AAC modes
    if (packetMode == PacketMode::Vanilla)
        return;

    const HeldItemType type = getHeldItemType();
    if (type != HeldItemType::Sword)
        return;

    // Check that the player is actually blocking (right mouse held)
    // We use the UseItem keybind physical state as proxy for blocking
    JNIEnv* env = Java::Env();
    if (!env || !player.isValid()) return;

    jobject gameSettings = JniCache::fid_gameSettings
        ? env->GetObjectField(mc.instance, JniCache::fid_gameSettings)
        : nullptr;

    bool isBlocking = false;
    if (gameSettings && JniCache::fid_keyBindUseItem)
        isBlocking = isKeyPhysicallyPressed(gameSettings, JniCache::fid_keyBindUseItem);

    if (gameSettings) env->DeleteLocalRef(gameSettings);

    if (!isBlocking) {
        packetSentPre = false;
        return;
    }

    const bool onGround = player.isOnGround();
    const uint32_t now = static_cast<uint32_t>(GetTickCount());

    bool shouldSend = false;
    switch (packetMode) {
        case PacketMode::NCP:     shouldSend = true;                            break;
        case PacketMode::Hypixel: shouldSend = onGround;                        break;
        case PacketMode::AAC:     shouldSend = (now - packetTimer) >= 80;       break;
        default: break;
    }

    if (!shouldSend) return;

    if (!packetSentPre) {
        // PRE phase: send RELEASE_USE_ITEM
        sendC07ReleaseUseItem(env);
        packetSentPre = true;
    } else {
        // POST phase: send re-arm placement + reset timer
        sendC08HeldItem(env);
        packetTimer   = static_cast<uint32_t>(GetTickCount());
        packetSentPre = false;
    }
}

void NoSlowDown::onUpdate() {
    // Handled in JNI by SlowDownHook / NativeOnSlowDown called from EntityPlayerSP.onLivingUpdate()
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static const char* kModeNames[] = { "Vanilla", "NCP", "Hypixel", "AAC" };

void NoSlowDown::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ImGui::Spacing();

            int modeIdx = static_cast<int>(packetMode);
            if (ui::Combo("Mode", &modeIdx, kModeNames, 4))
                packetMode = static_cast<PacketMode>(modeIdx);
        },
        "Multipliers",
        [this]() {
            ImGui::Text("-- Sword (blocking) --");
            ui::SliderFloat("Fwd##blk",    &blockForward,   0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##blk", &blockStrafe,    0.2f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("-- Consume (food/pot) --");
            ui::SliderFloat("Fwd##con",    &consumeForward, 0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##con", &consumeStrafe,  0.2f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("-- Bow --");
            ui::SliderFloat("Fwd##bow",    &bowForward,     0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##bow", &bowStrafe,      0.2f, 1.0f, "%.2f");
        });
}
