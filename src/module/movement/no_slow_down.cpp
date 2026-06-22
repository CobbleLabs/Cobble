#include "no_slow_down.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "sprint.h"

#include <algorithm>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

NoSlowDown* noSlowDown = nullptr;

namespace {

bool clearJniException(JNIEnv* env) {
    if (!env || !env->ExceptionCheck())
        return false;
    env->ExceptionClear();
    return true;
}

jclass assignGlobalClass(JNIEnv* env, const char* name) {
    jclass local = nullptr;
    if (!env || !Java::AssignClass(name, local) || !local) {
        clearJniException(env);
        return nullptr;
    }

    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!global)
        clearJniException(env);
    return global;
}

bool isSwordId(int id) {
    return id == 267 || id == 268 || id == 272 || id == 276 || id == 283;
}

bool isBowId(int id) {
    return id == 261;
}

bool isConsumeId(int id) {
    switch (id) {
        case 260:  // Apple
        case 282:  // Mushroom stew
        case 297:  // Bread
        case 319:  // Raw porkchop
        case 320:  // Cooked porkchop
        case 322:  // Golden apple
        case 335:  // Milk bucket
        case 349:  // Raw fish
        case 350:  // Cooked fish
        case 357:  // Cookie
        case 360:  // Melon
        case 363:  // Raw beef
        case 364:  // Steak
        case 365:  // Raw chicken
        case 366:  // Cooked chicken
        case 367:  // Rotten flesh
        case 373:  // Potion
        case 391:  // Carrot
        case 392:  // Potato
        case 393:  // Baked potato
        case 394:  // Poisonous potato
        case 396:  // Golden carrot
        case 400:  // Pumpkin pie
        case 411:  // Raw rabbit
        case 412:  // Cooked rabbit
        case 413:  // Rabbit stew
        case 423:  // Raw mutton
        case 424:  // Cooked mutton
            return true;
        default:
            return false;
    }
}

std::pair<float, float> getMultipliers(NoSlowDown::HeldItemType type, const NoSlowDown& module) {
    switch (type) {
        case NoSlowDown::HeldItemType::Sword:
            return {module.blockForward, module.blockStrafe};
        case NoSlowDown::HeldItemType::Bow:
            return {module.bowForward, module.bowStrafe};
        case NoSlowDown::HeldItemType::Consume:
            return {module.consumeForward, module.consumeStrafe};
        default:
            return {0.2f, 0.2f};
    }
}

enum class C07Constructor {
    Unresolved,
    Modern,
    Legacy
};

struct C07Cache {
    jclass packetClass = nullptr;
    jmethodID constructor = nullptr;
    C07Constructor type = C07Constructor::Unresolved;
    jclass blockPosClass = nullptr;
    jmethodID blockPosConstructor = nullptr;
    jobject releaseAction = nullptr;
    jobject downFacing = nullptr;
};

C07Cache& c07Cache() {
    static C07Cache cache;
    return cache;
}

bool initializeModernC07(JNIEnv* env, C07Cache& cache) {
    cache.constructor = env->GetMethodID(
        cache.packetClass,
        "<init>",
        "(Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;"
        "Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)V");
    if (!cache.constructor) {
        clearJniException(env);
        return false;
    }

    jclass actionClass =
        assignGlobalClass(env, "net.minecraft.network.play.client.C07PacketPlayerDigging$Action");
    cache.blockPosClass = assignGlobalClass(env, "net.minecraft.util.BlockPos");
    jclass facingClass = assignGlobalClass(env, "net.minecraft.util.EnumFacing");
    if (!actionClass || !cache.blockPosClass || !facingClass)
        return false;

    cache.blockPosConstructor = env->GetMethodID(cache.blockPosClass, "<init>", "(III)V");
    jfieldID releaseField = env->GetStaticFieldID(
        actionClass,
        "RELEASE_USE_ITEM",
        "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;");
    jfieldID downField =
        env->GetStaticFieldID(facingClass, "DOWN", "Lnet/minecraft/util/EnumFacing;");
    if (!cache.blockPosConstructor || !releaseField || !downField) {
        clearJniException(env);
        return false;
    }

    jobject localRelease = env->GetStaticObjectField(actionClass, releaseField);
    jobject localDown = env->GetStaticObjectField(facingClass, downField);
    if (!localRelease || !localDown || clearJniException(env))
        return false;

    cache.releaseAction = env->NewGlobalRef(localRelease);
    cache.downFacing = env->NewGlobalRef(localDown);
    env->DeleteLocalRef(localRelease);
    env->DeleteLocalRef(localDown);
    if (!cache.releaseAction || !cache.downFacing)
        return false;

    cache.type = C07Constructor::Modern;
    return true;
}

bool initializeLegacyC07(JNIEnv* env, C07Cache& cache) {
    cache.constructor = env->GetMethodID(cache.packetClass, "<init>", "(IIIII)V");
    if (!cache.constructor) {
        clearJniException(env);
        return false;
    }

    cache.type = C07Constructor::Legacy;
    return true;
}

bool sendC07ReleaseUseItem(JNIEnv* env) {
    C07Cache& cache = c07Cache();
    if (!cache.packetClass) {
        cache.packetClass =
            assignGlobalClass(env, "net.minecraft.network.play.client.C07PacketPlayerDigging");
        if (!cache.packetClass)
            return false;
    }

    if (cache.type == C07Constructor::Unresolved) {
        const bool preferLegacy = Java::Version == MinecraftVersion::MC_1_7_10;
        const bool initialized =
            preferLegacy ? (initializeLegacyC07(env, cache) || initializeModernC07(env, cache))
                         : (initializeModernC07(env, cache) || initializeLegacyC07(env, cache));
        if (!initialized)
            return false;
    }

    jobject packet = nullptr;
    if (cache.type == C07Constructor::Modern) {
        jobject blockPos = env->NewObject(cache.blockPosClass, cache.blockPosConstructor, -1, -1, -1);
        if (!blockPos || clearJniException(env))
            return false;

        packet = env->NewObject(
            cache.packetClass, cache.constructor, cache.releaseAction, blockPos, cache.downFacing);
        env->DeleteLocalRef(blockPos);
    } else {
        // 1.7.10: status 5 is RELEASE_USE_ITEM, face 255 means no block face.
        packet = env->NewObject(cache.packetClass, cache.constructor, 5, 0, 0, 0, 255);
    }

    if (!packet || clearJniException(env))
        return false;

    mc.sendPacket(packet);
    env->DeleteLocalRef(packet);
    clearJniException(env);
    return true;
}

struct C08Cache {
    jclass packetClass = nullptr;
    jmethodID itemStackConstructor = nullptr;
    jmethodID emptyConstructor = nullptr;
    jmethodID getCurrentItem = nullptr;
};

C08Cache& c08Cache() {
    static C08Cache cache;
    return cache;
}

jobject getHeldItemStack(JNIEnv* env, C08Cache& cache) {
    if (!player.isValid() || !JniCache::fid_inventory)
        return nullptr;

    jobject inventory = env->GetObjectField(player.instance, JniCache::fid_inventory);
    if (!inventory || clearJniException(env))
        return nullptr;

    if (!cache.getCurrentItem) {
        jclass inventoryClass = env->GetObjectClass(inventory);
        if (inventoryClass) {
            cache.getCurrentItem = env->GetMethodID(
                inventoryClass, "getCurrentItem", "()Lnet/minecraft/item/ItemStack;");
            if (!cache.getCurrentItem) {
                clearJniException(env);
                cache.getCurrentItem = env->GetMethodID(
                    inventoryClass, "func_70448_g", "()Lnet/minecraft/item/ItemStack;");
            }
            clearJniException(env);
            env->DeleteLocalRef(inventoryClass);
        }
    }

    jobject heldItem = nullptr;
    if (cache.getCurrentItem)
        heldItem = env->CallObjectMethod(inventory, cache.getCurrentItem);
    env->DeleteLocalRef(inventory);
    if (clearJniException(env))
        return nullptr;
    return heldItem;
}

bool sendC08HeldItem(JNIEnv* env) {
    C08Cache& cache = c08Cache();
    if (!cache.packetClass) {
        cache.packetClass =
            assignGlobalClass(env, "net.minecraft.network.play.client.C08PacketPlayerBlockPlacement");
        if (!cache.packetClass)
            return false;

        cache.itemStackConstructor = env->GetMethodID(
            cache.packetClass, "<init>", "(Lnet/minecraft/item/ItemStack;)V");
        clearJniException(env);
        cache.emptyConstructor = env->GetMethodID(cache.packetClass, "<init>", "()V");
        clearJniException(env);
    }

    jobject heldItem = getHeldItemStack(env, cache);
    jobject packet = nullptr;
    if (cache.itemStackConstructor && heldItem)
        packet = env->NewObject(cache.packetClass, cache.itemStackConstructor, heldItem);
    else if (cache.emptyConstructor)
        packet = env->NewObject(cache.packetClass, cache.emptyConstructor);

    if (heldItem)
        env->DeleteLocalRef(heldItem);
    if (!packet || clearJniException(env))
        return false;

    mc.sendPacket(packet);
    env->DeleteLocalRef(packet);
    clearJniException(env);
    return true;
}

}  // namespace

NoSlowDown::NoSlowDown() : Module("NoSlowDown") {
    keybind = 0;
}

bool NoSlowDown::initializeFields(JNIEnv* env, jobject playerInstance) {
    if (movementInputField && moveForwardField && moveStrafeField)
        return true;
    if (!env || !playerInstance)
        return false;

    jclass playerClass = env->GetObjectClass(playerInstance);
    if (!playerClass)
        return false;

    const auto findField = [env](jclass cls, const char* name, const char* fallback,
                                 const char* signature) -> jfieldID {
        jfieldID field = env->GetFieldID(cls, name, signature);
        if (!field) {
            clearJniException(env);
            field = env->GetFieldID(cls, fallback, signature);
        }
        clearJniException(env);
        return field;
    };

    movementInputField =
        findField(playerClass, "movementInput", "field_71158_b", "Lnet/minecraft/util/MovementInput;");
    entityMoveStrafeField =
        findField(playerClass, "moveStrafing", "field_70702_br", "F");
    entityMoveForwardField =
        findField(playerClass, "moveForward", "field_70701_bs", "F");
    itemInUseCountField =
        findField(playerClass, "itemInUseCount", "field_71072_f", "I");

    if (!movementInputField) {
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jobject movementInput = env->GetObjectField(playerInstance, movementInputField);
    if (!movementInput || clearJniException(env)) {
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jclass movementInputClass = env->GetObjectClass(movementInput);
    if (movementInputClass) {
        moveStrafeField =
            findField(movementInputClass, "moveStrafe", "field_78902_a", "F");
        moveForwardField =
            findField(movementInputClass, "moveForward", "field_78900_b", "F");
        env->DeleteLocalRef(movementInputClass);
    }

    env->DeleteLocalRef(movementInput);
    env->DeleteLocalRef(playerClass);
    return moveForwardField && moveStrafeField;
}

bool NoSlowDown::isKeyPressed(JNIEnv* env, jobject gameSettings, jfieldID keyBindField) {
    if (!env || !gameSettings || !keyBindField)
        return false;

    jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
    if (!keyBind || clearJniException(env))
        return false;

    bool pressed = false;
    if (JniCache::fid_pressed) {
        pressed = env->GetBooleanField(keyBind, JniCache::fid_pressed) == JNI_TRUE;
        clearJniException(env);
    }

    if (!pressed && JniCache::mid_getKeyCode) {
        const int keyCode = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);
        if (!clearJniException(env)) {
            if (keyCode < 0) {
                const int button = keyCode + 100;
                const int virtualKey = button == 0 ? VK_LBUTTON :
                                       button == 1 ? VK_RBUTTON :
                                       button == 2 ? VK_MBUTTON : 0;
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            } else {
                const UINT virtualKey =
                    MapVirtualKeyA(static_cast<UINT>(keyCode), MAPVK_VSC_TO_VK);
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            }
        }
    }

    env->DeleteLocalRef(keyBind);
    return pressed;
}

bool NoSlowDown::isPlayerUsingItem(JNIEnv* env, jobject playerInstance) {
    if (!env || !playerInstance)
        return false;

    bool usingItem = player.isUsingItem();
    if (clearJniException(env))
        usingItem = false;

    if (!usingItem && itemInUseCountField) {
        usingItem = env->GetIntField(playerInstance, itemInUseCountField) > 0;
        if (clearJniException(env))
            usingItem = false;
    }
    return usingItem;
}

NoSlowDown::HeldItemType NoSlowDown::getHeldItemType() {
    const int id = player.getHeldItemId();
    if (isSwordId(id))
        return HeldItemType::Sword;
    if (isBowId(id))
        return HeldItemType::Bow;
    if (isConsumeId(id))
        return HeldItemType::Consume;
    return HeldItemType::None;
}

void NoSlowDown::applyMovement(JNIEnv* env, jobject playerInstance) {
    if (!enabled || !env || !playerInstance || !mc.instance || mc.isInGui())
        return;
    if (!initializeFields(env, playerInstance))
        return;

    const HeldItemType type = getHeldItemType();
    if (type == HeldItemType::None)
        return;

    jobject gameSettings = JniCache::fid_gameSettings
        ? env->GetObjectField(mc.instance, JniCache::fid_gameSettings)
        : nullptr;
    if (!gameSettings || clearJniException(env))
        return;

    const bool usePressed =
        isKeyPressed(env, gameSettings, JniCache::fid_keyBindUseItem);
    if (!usePressed && !isPlayerUsingItem(env, playerInstance)) {
        env->DeleteLocalRef(gameSettings);
        return;
    }

    float forward = 0.0f;
    float strafe = 0.0f;
    if (isKeyPressed(env, gameSettings, JniCache::fid_keyBindForward))
        forward += 1.0f;
    if (isKeyPressed(env, gameSettings, JniCache::fid_keyBindBack))
        forward -= 1.0f;
    if (isKeyPressed(env, gameSettings, JniCache::fid_keyBindLeft))
        strafe += 1.0f;
    if (isKeyPressed(env, gameSettings, JniCache::fid_keyBindRight))
        strafe -= 1.0f;

    const bool sprintPressed =
        isKeyPressed(env, gameSettings, JniCache::fid_keyBindSprint) || (sprint && sprint->enabled);

    const bool sneaking =
        player.isSneaking() || isKeyPressed(env, gameSettings, JniCache::fid_keyBindSneak);
    env->DeleteLocalRef(gameSettings);
    if (clearJniException(env))
        return;

    if (sprintPressed && forward > 0.0f && !sneaking) {
        player.setSprinting(true);
    }

    if (sneaking) {
        forward *= 0.3f;
        strafe *= 0.3f;
    }

    const auto [forwardMultiplier, strafeMultiplier] = getMultipliers(type, *this);
    forward *= (std::clamp)(forwardMultiplier, 0.2f, 1.0f);
    strafe *= (std::clamp)(strafeMultiplier, 0.2f, 1.0f);

    jobject movementInput = env->GetObjectField(playerInstance, movementInputField);
    if (!movementInput || clearJniException(env))
        return;

    env->SetFloatField(movementInput, moveForwardField, forward);
    env->SetFloatField(movementInput, moveStrafeField, strafe);
    if (entityMoveForwardField)
        env->SetFloatField(playerInstance, entityMoveForwardField, forward);
    if (entityMoveStrafeField)
        env->SetFloatField(playerInstance, entityMoveStrafeField, strafe);

    env->DeleteLocalRef(movementInput);
    clearJniException(env);
}

void NoSlowDown::handlePacketMode() {
    if (packetMode == PacketMode::Vanilla || !player.isValid() || !mc.instance) {
        packetTimer = 0;
        return;
    }
    if (getHeldItemType() != HeldItemType::Sword) {
        packetTimer = 0;
        return;
    }

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject gameSettings = JniCache::fid_gameSettings
        ? env->GetObjectField(mc.instance, JniCache::fid_gameSettings)
        : nullptr;
    if (!gameSettings || clearJniException(env))
        return;

    const bool blocking =
        isKeyPressed(env, gameSettings, JniCache::fid_keyBindUseItem) ||
        isPlayerUsingItem(env, player.instance);
    env->DeleteLocalRef(gameSettings);
    if (!blocking) {
        packetTimer = 0;
        return;
    }

    if (packetMode == PacketMode::Hypixel && !player.isOnGround())
        return;

    const uint32_t now = static_cast<uint32_t>(GetTickCount());
    const uint32_t interval = packetMode == PacketMode::AAC ? 80u : 50u;
    if (packetTimer != 0 && now - packetTimer < interval)
        return;
    packetTimer = now;

    // Release and re-arm in one logical tick. Splitting this over the 2 ms
    // module update loop floods the connection and leaves blocking desynced.
    sendC07ReleaseUseItem(env);
    sendC08HeldItem(env);
}

void NoSlowDown::onUpdate() {
    JNIEnv* env = Java::Env();
    if (!env || !player.isValid() || !mc.instance || Menu::isOpen || mc.isInGui())
        return;

    applyMovement(env, player.instance);
    handlePacketMode();
}

static const char* kModeNames[] = {"Vanilla", "NCP", "Hypixel", "AAC"};

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
            ui::SliderFloat("Fwd##blk", &blockForward, 0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##blk", &blockStrafe, 0.2f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("-- Consume (food/pot) --");
            ui::SliderFloat("Fwd##con", &consumeForward, 0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##con", &consumeStrafe, 0.2f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("-- Bow --");
            ui::SliderFloat("Fwd##bow", &bowForward, 0.2f, 1.0f, "%.2f");
            ui::SliderFloat("Strafe##bow", &bowStrafe, 0.2f, 1.0f, "%.2f");
        });
}



void NoSlowDown::saveSettings(json::Value& node) const {
    node["packetMode"] = static_cast<int>(packetMode);
    node["blockForward"] = blockForward;
    node["blockStrafe"] = blockStrafe;
    node["consumeForward"] = consumeForward;
    node["consumeStrafe"] = consumeStrafe;
    node["bowForward"] = bowForward;
    node["bowStrafe"] = bowStrafe;
}

void NoSlowDown::loadSettings(const json::Value& node) {
    packetMode = static_cast<PacketMode>(
        (std::clamp)(node["packetMode"].as_int(static_cast<int>(packetMode)), 0, 3));
    blockForward = (std::clamp)(node["blockForward"].as_float(blockForward), 0.2f, 1.0f);
    blockStrafe = (std::clamp)(node["blockStrafe"].as_float(blockStrafe), 0.2f, 1.0f);
    consumeForward = (std::clamp)(node["consumeForward"].as_float(consumeForward), 0.2f, 1.0f);
    consumeStrafe = (std::clamp)(node["consumeStrafe"].as_float(consumeStrafe), 0.2f, 1.0f);
    bowForward = (std::clamp)(node["bowForward"].as_float(bowForward), 0.2f, 1.0f);
    bowStrafe = (std::clamp)(node["bowStrafe"].as_float(bowStrafe), 0.2f, 1.0f);
}
