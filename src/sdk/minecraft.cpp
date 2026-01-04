#include "minecraft.h"
#include "../util/logger.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

Minecraft mc;

void SDK_Init()
{
	if (!Java::Initialized) {
		log_err("SDK_Init: java not initialized");
		return;
	}

	// Get Minecraft class
	if (!Java::AssignClass("net.minecraft.client.Minecraft", mc.cls)) {
		log_err("SDK_Init: failed to find Minecraft class");
		return;
	}

	// getMinecraft() -> Minecraft instance
	mc.mid_getMinecraft = Java::Env->GetStaticMethodID(mc.cls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
	if (!mc.mid_getMinecraft) {
		log_err("SDK_Init: failed to find getMinecraft");
		return;
	}

	mc.instance = Java::Env->CallStaticObjectMethod(mc.cls, mc.mid_getMinecraft);
	if (!mc.instance) {
		log_err("SDK_Init: getMinecraft returned null");
		return;
	}

	// thePlayer field - signature depends on version
	const char* playerSig = (Java::Version == MinecraftVersion::MC_1_7_10)
		? "Lnet/minecraft/client/entity/EntityClientPlayerMP;"
		: "Lnet/minecraft/client/entity/EntityPlayerSP;";
	mc.fid_thePlayer = Java::Env->GetFieldID(mc.cls, "thePlayer", playerSig);

	// theWorld field
	mc.fid_theWorld = Java::Env->GetFieldID(mc.cls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");

	// currentScreen field (to detect if GUI is open)
	mc.fid_currentScreen = Java::Env->GetFieldID(mc.cls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");

	// clickMouse method
	mc.mid_clickMouse = Java::Env->GetMethodID(mc.cls, "clickMouse", "()V");

	// Click delay fields
	mc.fid_rightClickDelayTimer = Java::Env->GetFieldID(mc.cls, "rightClickDelayTimer", "I");
	mc.fid_leftClickCounter = Java::Env->GetFieldID(mc.cls, "leftClickCounter", "I");

	// KeyBinding cache for releaseMovementKeys
	jclass kbCls = nullptr;
	if (Java::AssignClass("net.minecraft.client.settings.KeyBinding", kbCls)) {
		// NewGlobalRef is ESSENTIAL for sharing jclass across threads
		mc.cls_keyBinding = (jclass)Java::Env->NewGlobalRef(kbCls);
		
		mc.mid_unPressAllKeys = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "unPressAllKeys", "()V");
		if (!mc.mid_unPressAllKeys) {
			Java::Env->ExceptionClear();
			mc.mid_unPressAllKeys = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "func_74505_d", "()V");
		}
		
		mc.mid_setKeyBindState = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "setKeyBindState", "(IZ)V");
		mc.mid_getKeyCode = Java::Env->GetMethodID(mc.cls_keyBinding, "getKeyCode", "()I");
		mc.fid_pressed = Java::Env->GetFieldID(mc.cls_keyBinding, "pressed", "Z");
	}

	// Initialize player (will lazy-load fields on first refresh)
	player.refresh();

	log_ok("SDK initialized");
}

jobject Minecraft::getPlayer()
{
	if (!isValid() || !fid_thePlayer) return nullptr;
	return Java::Env->GetObjectField(instance, fid_thePlayer);
}

jobject Minecraft::getWorld()
{
	if (!isValid() || !fid_theWorld) return nullptr;
	return Java::Env->GetObjectField(instance, fid_theWorld);
}

bool Minecraft::isInGui()
{
	if (!isValid() || !fid_currentScreen) return false;
	jobject screen = Java::Env->GetObjectField(instance, fid_currentScreen);
	bool result = (screen != nullptr);
	if (screen) Java::Env->DeleteLocalRef(screen);
	return result;
}

void Minecraft::clickMouse()
{
	if (!isValid() || !mid_clickMouse) return;
	Java::Env->CallVoidMethod(instance, mid_clickMouse);
}

int Minecraft::getRightClickDelay()
{
	if (!isValid() || !fid_rightClickDelayTimer) return 0;
	return Java::Env->GetIntField(instance, fid_rightClickDelayTimer);
}

void Minecraft::setRightClickDelay(int value)
{
	if (!isValid() || !fid_rightClickDelayTimer) return;
	Java::Env->SetIntField(instance, fid_rightClickDelayTimer, value);
}

void Minecraft::setLeftClickCounter(int value)
{
	if (!isValid() || !fid_leftClickCounter) return;
	Java::Env->SetIntField(instance, fid_leftClickCounter, value);
}

float Minecraft::getMouseSensitivity()
{
	if (!isValid()) return 0.5f; // Default

	// Get gameSettings field
	jfieldID fid_gameSettings = Java::Env->GetFieldID(cls, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
	if (!fid_gameSettings) return 0.5f;

	jobject gameSettings = Java::Env->GetObjectField(instance, fid_gameSettings);
	if (!gameSettings) return 0.5f;

	// Get GameSettings class and mouseSensitivity field
	jclass settingsClass = Java::Env->GetObjectClass(gameSettings);
	if (!settingsClass) {
		Java::Env->DeleteLocalRef(gameSettings);
		return 0.5f;
	}

	jfieldID fid_sensitivity = Java::Env->GetFieldID(settingsClass, "mouseSensitivity", "F");
	if (!fid_sensitivity) {
		Java::Env->DeleteLocalRef(settingsClass);
		Java::Env->DeleteLocalRef(gameSettings);
		return 0.5f;
	}

	float sensitivity = Java::Env->GetFloatField(gameSettings, fid_sensitivity);

	Java::Env->DeleteLocalRef(settingsClass);
	Java::Env->DeleteLocalRef(gameSettings);

	return sensitivity;
}

bool Minecraft::isTargetingBlock()
{
	if (!isValid()) return false;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return false;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return false;

	// Get MovingObjectPosition class and typeOfHit field
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return false;
	}

	jfieldID fid_typeOfHit = Java::Env->GetFieldID(mopClass, "typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_typeOfHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject typeOfHit = Java::Env->GetObjectField(mouseOver, fid_typeOfHit);
	Java::Env->DeleteLocalRef(mouseOver);

	if (!typeOfHit) {
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	// Get the BLOCK enum constant
	jclass typeClass = Java::Env->GetObjectClass(typeOfHit);
	if (!typeClass) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jfieldID fid_block = Java::Env->GetStaticFieldID(typeClass, "BLOCK", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_block) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(typeClass);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject blockEnum = Java::Env->GetStaticObjectField(typeClass, fid_block);
	bool result = Java::Env->IsSameObject(typeOfHit, blockEnum);

	Java::Env->DeleteLocalRef(blockEnum);
	Java::Env->DeleteLocalRef(typeOfHit);
	Java::Env->DeleteLocalRef(typeClass);
	Java::Env->DeleteLocalRef(mopClass);

	return result;
}

// ============================================================
// Cheap debug helpers (rate-limited)
// ============================================================
static bool every_ms(int ms)
{
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last >= (DWORD)ms) { last = now; return true; }
    return false;
}

static void jni_log_if_exception(const char* where)
{
    if (!Java::Env) return;

    if (Java::Env->ExceptionCheck()) {
        // WARNING: ExceptionDescribe is slow. Keep it rate-limited.
        if (every_ms(500)) {
            Java::Env->ExceptionDescribe();
        }
        Java::Env->ExceptionClear();
        if (every_ms(500)) {
            log_debug("JNI exception at: %s", where);
        }
    }
}


std::vector<PlayerInfo> Minecraft::getPlayerList()
{
    std::vector<PlayerInfo> result;
    if (!isValid()) return result;

    jobject world = getWorld();
    if (!world) return result;

    jclass worldCls = nullptr;
    if (!Java::AssignClass("net.minecraft.client.multiplayer.WorldClient", worldCls)) {
        Java::Env->DeleteLocalRef(world);
        return result;
    }

    jfieldID fid_playerEntities = Java::Env->GetFieldID(worldCls, "playerEntities", "Ljava/util/List;");
    if (!fid_playerEntities) {
        jni_log_if_exception("GetFieldID WorldClient.playerEntities");
        Java::Env->DeleteLocalRef(world);
        Java::Env->DeleteLocalRef(worldCls);
        return result;
    }

    jobject playerList = Java::Env->GetObjectField(world, fid_playerEntities);
    Java::Env->DeleteLocalRef(world);
    Java::Env->DeleteLocalRef(worldCls);

    if (!playerList) return result;

    jclass listCls = Java::Env->GetObjectClass(playerList);
    jmethodID mid_size = Java::Env->GetMethodID(listCls, "size", "()I");
    jmethodID mid_get  = Java::Env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    if (!mid_size || !mid_get) {
        jni_log_if_exception("GetMethodID List.size/get");
        Java::Env->DeleteLocalRef(playerList);
        Java::Env->DeleteLocalRef(listCls);
        return result;
    }

    int size = Java::Env->CallIntMethod(playerList, mid_size);
    jni_log_if_exception("CallIntMethod List.size");
    if (size <= 0) {
        Java::Env->DeleteLocalRef(playerList);
        Java::Env->DeleteLocalRef(listCls);
        return result;
    }

    result.reserve(size);

    // Cache classes + ids ONCE
    jclass entityCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.Entity", entityCls)) {
        Java::Env->DeleteLocalRef(playerList);
        Java::Env->DeleteLocalRef(listCls);
        return result;
    }

    jfieldID fid_posX      = Java::Env->GetFieldID(entityCls, "posX", "D");
    jfieldID fid_posY      = Java::Env->GetFieldID(entityCls, "posY", "D");
    jfieldID fid_posZ      = Java::Env->GetFieldID(entityCls, "posZ", "D");
    jfieldID fid_lastPosX  = Java::Env->GetFieldID(entityCls, "lastTickPosX", "D");
    jfieldID fid_lastPosY  = Java::Env->GetFieldID(entityCls, "lastTickPosY", "D");
    jfieldID fid_lastPosZ  = Java::Env->GetFieldID(entityCls, "lastTickPosZ", "D");
    jmethodID mid_isInvisible = Java::Env->GetMethodID(entityCls, "isInvisible", "()Z");
    jni_log_if_exception("Cache Entity fields/methods");

    // IMPORTANT: name should be on EntityPlayer, not Entity
    jclass playerCls = nullptr;
    Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls);

    jmethodID mid_getName = nullptr;
    if (playerCls) {
        if (Java::Version == MinecraftVersion::MC_1_7_10)
            mid_getName = Java::Env->GetMethodID(playerCls, "getCommandSenderName", "()Ljava/lang/String;");
        else
            mid_getName = Java::Env->GetMethodID(playerCls, "getName", "()Ljava/lang/String;");
        jni_log_if_exception("Cache EntityPlayer.getName");
    }

    // LivingBase cached once
    jclass livingCls = nullptr;
    jmethodID mid_getHealth = nullptr;
    jmethodID mid_getMaxHealth = nullptr;
    jfieldID fid_hurtTime = nullptr;

    if (Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
        mid_getHealth    = Java::Env->GetMethodID(livingCls, "getHealth", "()F");
        mid_getMaxHealth = Java::Env->GetMethodID(livingCls, "getMaxHealth", "()F");
        fid_hurtTime     = Java::Env->GetFieldID(livingCls, "hurtTime", "I");
        jni_log_if_exception("Cache LivingBase ids");
    }

    jobject localPlayer = getPlayer();

    for (int i = 0; i < size; i++) {
        jobject entity = Java::Env->CallObjectMethod(playerList, mid_get, i);
        if (!entity) {
            jni_log_if_exception("List.get(i)");
            continue;
        }

        if (localPlayer && Java::Env->IsSameObject(entity, localPlayer)) {
            Java::Env->DeleteLocalRef(entity);
            continue;
        }

        PlayerInfo info;
        info.instance = entity; // caller later deletes (como ya hacés en findTarget)

        info.posX = fid_posX ? Java::Env->GetDoubleField(entity, fid_posX) : 0.0;
        info.posY = fid_posY ? Java::Env->GetDoubleField(entity, fid_posY) : 0.0;
        info.posZ = fid_posZ ? Java::Env->GetDoubleField(entity, fid_posZ) : 0.0;

        info.lastPosX = fid_lastPosX ? Java::Env->GetDoubleField(entity, fid_lastPosX) : info.posX;
        info.lastPosY = fid_lastPosY ? Java::Env->GetDoubleField(entity, fid_lastPosY) : info.posY;
        info.lastPosZ = fid_lastPosZ ? Java::Env->GetDoubleField(entity, fid_lastPosZ) : info.posZ;

        info.isInvisible = mid_isInvisible ? Java::Env->CallBooleanMethod(entity, mid_isInvisible) : false;

        // Name (no exceptions per loop)
        if (mid_getName) {
            jstring nameStr = (jstring)Java::Env->CallObjectMethod(entity, mid_getName);
            if (nameStr) {
                info.name = jstringToString(nameStr);
                Java::Env->DeleteLocalRef(nameStr);
            } else {
                jni_log_if_exception("CallObjectMethod getName");
                info.name = "Unknown";
            }
        } else {
            info.name = "Unknown";
        }

        if (info.name.empty()) info.name = "Player";

        // DisplayName with color codes for team detection
        // Try getDisplayName() -> getUnformattedTextForChat() for the REAL name
        // This bypasses anti-cheat name obfuscation
        static jmethodID mid_getDisplayName = nullptr;
        static jmethodID mid_getUnformattedText = nullptr;
        static jclass iChatComponentCls = nullptr;
        static bool displayNameInit = false;
        
        if (!displayNameInit && playerCls) {
            // 1.8.9+: getDisplayName returns IChatComponent
            mid_getDisplayName = Java::Env->GetMethodID(playerCls, "getDisplayName", 
                "()Lnet/minecraft/util/IChatComponent;");
            if (!mid_getDisplayName) {
                Java::Env->ExceptionClear();
                // Try 1.7.10: getDisplayNameString returns String directly
                mid_getDisplayName = Java::Env->GetMethodID(playerCls, "getDisplayNameString", "()Ljava/lang/String;");
            }
            
            // Cache IChatComponent class for getUnformattedText
            if (!Java::AssignClass("net.minecraft.util.IChatComponent", iChatComponentCls)) {
                Java::Env->ExceptionClear();
                // Try alternative class name
                Java::AssignClass("net.minecraft.util.ChatComponentText", iChatComponentCls);
            }
            
            if (iChatComponentCls) {
                // getUnformattedTextForChat gives the clean name without color codes
                mid_getUnformattedText = Java::Env->GetMethodID(iChatComponentCls, 
                    "getUnformattedTextForChat", "()Ljava/lang/String;");
                if (!mid_getUnformattedText) {
                    Java::Env->ExceptionClear();
                    // Fallback to getUnformattedText
                    mid_getUnformattedText = Java::Env->GetMethodID(iChatComponentCls, 
                        "getUnformattedText", "()Ljava/lang/String;");
                }
                Java::Env->ExceptionClear();
            }
            
            displayNameInit = true;
        }
        
        std::string realName;
        if (mid_getDisplayName) {
            jobject displayResult = Java::Env->CallObjectMethod(entity, mid_getDisplayName);
            if (displayResult) {
                // Check if it's a String or IChatComponent
                jclass strCls = Java::Env->FindClass("java/lang/String");
                if (strCls && Java::Env->IsInstanceOf(displayResult, strCls)) {
                    // Direct string (1.7.10 style)
                    realName = jstringToString((jstring)displayResult);
                    info.displayName = realName;
                } else if (iChatComponentCls && Java::Env->IsInstanceOf(displayResult, iChatComponentCls)) {
                    // IChatComponent - use getUnformattedTextForChat for clean name
                    if (mid_getUnformattedText) {
                        jstring unformatted = (jstring)Java::Env->CallObjectMethod(displayResult, mid_getUnformattedText);
                        if (unformatted) {
                            realName = jstringToString(unformatted);
                            Java::Env->DeleteLocalRef(unformatted);
                        }
                    }
                    
                    // Also get formatted text for team color detection
                    jclass chatCls = Java::Env->GetObjectClass(displayResult);
                    if (chatCls) {
                        jmethodID mid_getFormattedText = Java::Env->GetMethodID(chatCls, "getFormattedText", "()Ljava/lang/String;");
                        if (mid_getFormattedText) {
                            jstring formatted = (jstring)Java::Env->CallObjectMethod(displayResult, mid_getFormattedText);
                            if (formatted) {
                                info.displayName = jstringToString(formatted);
                                Java::Env->DeleteLocalRef(formatted);
                            }
                        }
                        Java::Env->DeleteLocalRef(chatCls);
                    }
                }
                if (strCls) Java::Env->DeleteLocalRef(strCls);
                Java::Env->DeleteLocalRef(displayResult);
            }
            Java::Env->ExceptionClear();
        }
        
        // Use the real unformatted name if available, otherwise keep getName result
        if (!realName.empty()) {
            info.name = realName;
        }
        
        // Parse teamColor from displayName
        // Look for color codes: §X where X is 0-9 or a-f
        // Also handles corrupted encoding like ┬ºX
        if (!info.displayName.empty()) {
            // Debug: log first 10 bytes in hex to identify encoding pattern
            static int hexDebugCounter = 0;
            if (++hexDebugCounter % 200 == 1 && info.displayName.size() > 2) {
                char hexBuf[64] = {0};
                int pos = 0;
                for (size_t b = 0; b < std::min((size_t)8, info.displayName.size()) && pos < 60; b++) {
                    pos += snprintf(hexBuf + pos, 64 - pos, "%02X ", (unsigned char)info.displayName[b]);
                }
                log_debug("[DisplayName] '%s' bytes: %s", info.displayName.c_str(), hexBuf);
            }
            
            for (size_t j = 0; j < info.displayName.size(); j++) {
                unsigned char c = info.displayName[j];
                
                // Check for UTF-8 § (0xC2 0xA7)
                if (c == 0xC2 && j + 2 < info.displayName.size() && 
                    (unsigned char)info.displayName[j + 1] == 0xA7) {
                    char code = std::tolower(info.displayName[j + 2]);
                    // Only color codes 0-9 and a-f, NOT format codes k-o, r
                    if (code >= '0' && code <= '9') {
                        info.teamColor = code - '0';
                        break; // Found a color, stop searching
                    } else if (code >= 'a' && code <= 'f') {
                        info.teamColor = 10 + (code - 'a');
                        break; // Found a color, stop searching
                    }
                    // If it's a format code (k-o, r), skip it and continue searching
                    j += 2; // Skip the §X sequence and continue
                    continue;
                }
                
                // Check for raw § byte (0xA7) - Latin-1 encoding
                if (c == 0xA7 && j + 1 < info.displayName.size()) {
                    char code = std::tolower(info.displayName[j + 1]);
                    if (code >= '0' && code <= '9') {
                        info.teamColor = code - '0';
                        break;
                    } else if (code >= 'a' && code <= 'f') {
                        info.teamColor = 10 + (code - 'a');
                        break;
                    }
                    // Format code, skip and continue
                    j += 1;
                    continue;
                }
            }
        }

        // Health/hurtTime cached
        if (livingCls) {
            if (mid_getHealth)    info.health    = Java::Env->CallFloatMethod(entity, mid_getHealth);
            if (mid_getMaxHealth) info.maxHealth = Java::Env->CallFloatMethod(entity, mid_getMaxHealth);
            if (fid_hurtTime)     info.hurtTime  = Java::Env->GetIntField(entity, fid_hurtTime);
            // si hay excepción, logueá rate-limited
            if (every_ms(500)) jni_log_if_exception("LivingBase read");
        }

        result.push_back(std::move(info));
    }

    if (localPlayer) Java::Env->DeleteLocalRef(localPlayer);

    if (livingCls) Java::Env->DeleteLocalRef(livingCls);
    if (playerCls) Java::Env->DeleteLocalRef(playerCls);

    Java::Env->DeleteLocalRef(entityCls);
    Java::Env->DeleteLocalRef(playerList);
    Java::Env->DeleteLocalRef(listCls);

    return result;
}


bool Minecraft::canEntityBeSeen(jobject target)
{
	if (!isValid() || !target) return false;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return false;

	// Get EntityLivingBase class for canEntityBeSeen method
	jclass livingCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return false;
	}

	jmethodID mid_canSee = Java::Env->GetMethodID(livingCls, "canEntityBeSeen", "(Lnet/minecraft/entity/Entity;)Z");
	if (!mid_canSee) {
		Java::Env->DeleteLocalRef(localPlayer);
		Java::Env->DeleteLocalRef(livingCls);
		return false;
	}

	bool result = Java::Env->CallBooleanMethod(localPlayer, mid_canSee, target);

	Java::Env->DeleteLocalRef(localPlayer);
	Java::Env->DeleteLocalRef(livingCls);

	return result;
}

int Minecraft::getLocalPlayerTeamColor()
{
	if (!isValid()) return -1;
	
	jobject localPlayer = getPlayer();
	if (!localPlayer) return -1;
	
	int teamColor = -1;
	
	// Get EntityPlayer class
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return -1;
	}
	
	// Get IChatComponent class
	jclass iChatComponentCls = nullptr;
	Java::AssignClass("net.minecraft.util.IChatComponent", iChatComponentCls);
	
	// Try getDisplayName
	jmethodID mid_getDisplayName = Java::Env->GetMethodID(playerCls, "getDisplayName", 
		"()Lnet/minecraft/util/IChatComponent;");
	if (!mid_getDisplayName) {
		Java::Env->ExceptionClear();
		// Try 1.7.10 variant
		mid_getDisplayName = Java::Env->GetMethodID(playerCls, "getDisplayNameString", "()Ljava/lang/String;");
	}
	
	if (mid_getDisplayName) {
		jobject displayResult = Java::Env->CallObjectMethod(localPlayer, mid_getDisplayName);
		if (displayResult) {
			std::string displayName;
			
			// Check if it's a String or IChatComponent
			jclass strCls = Java::Env->FindClass("java/lang/String");
			if (strCls && Java::Env->IsInstanceOf(displayResult, strCls)) {
				// Direct string
				displayName = jstringToString((jstring)displayResult);
			} else if (iChatComponentCls && Java::Env->IsInstanceOf(displayResult, iChatComponentCls)) {
				// IChatComponent - get formatted text with color codes
				jclass chatCls = Java::Env->GetObjectClass(displayResult);
				if (chatCls) {
					jmethodID mid_getFormattedText = Java::Env->GetMethodID(chatCls, "getFormattedText", "()Ljava/lang/String;");
					if (mid_getFormattedText) {
						jstring formatted = (jstring)Java::Env->CallObjectMethod(displayResult, mid_getFormattedText);
						if (formatted) {
							displayName = jstringToString(formatted);
							Java::Env->DeleteLocalRef(formatted);
						}
					}
					Java::Env->DeleteLocalRef(chatCls);
				}
			}
			if (strCls) Java::Env->DeleteLocalRef(strCls);
			Java::Env->DeleteLocalRef(displayResult);
			
			// Parse color code from displayName
			// Look for § - skip format codes (k-o, r), only use color codes (0-9, a-f)
			for (size_t j = 0; j < displayName.size(); j++) {
				unsigned char c = displayName[j];
				
				// Check for UTF-8 § (0xC2 0xA7)
				if (c == 0xC2 && j + 2 < displayName.size() && (unsigned char)displayName[j + 1] == 0xA7) {
					char code = std::tolower(displayName[j + 2]);
					if (code >= '0' && code <= '9') {
						teamColor = code - '0';
						break; // Found color
					} else if (code >= 'a' && code <= 'f') {
						teamColor = 10 + (code - 'a');
						break; // Found color
					}
					// Format code (k-o, r), skip and continue searching
					j += 2;
					continue;
				}
				
				// Check for raw § byte (0xA7)
				if (c == 0xA7 && j + 1 < displayName.size()) {
					char code = std::tolower(displayName[j + 1]);
					if (code >= '0' && code <= '9') {
						teamColor = code - '0';
						break;
					} else if (code >= 'a' && code <= 'f') {
						teamColor = 10 + (code - 'a');
						break;
					}
					j += 1;
					continue;
				}
			}
		}
		Java::Env->ExceptionClear();
	}
	
	if (iChatComponentCls) Java::Env->DeleteLocalRef(iChatComponentCls);
	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(localPlayer);
	
	return teamColor;
}

RenderPos Minecraft::getRenderPos()
{
	RenderPos result;
	if (!isValid()) return result;
	
	static bool debugLogged = false;
	
	if (Java::Version == MinecraftVersion::MC_1_7_10) {
		// 1.7.10: RenderManager uses STATIC fields
		jclass rmCls = nullptr;
		if (!Java::AssignClass("net.minecraft.client.renderer.entity.RenderManager", rmCls)) {
			if (!debugLogged) log_err("getRenderPos (1.7.10): RenderManager class not found");
			debugLogged = true;
			return result;
		}

		jfieldID fid_renderPosX = Java::Env->GetStaticFieldID(rmCls, "renderPosX", "D");
		jfieldID fid_renderPosY = Java::Env->GetStaticFieldID(rmCls, "renderPosY", "D");
		jfieldID fid_renderPosZ = Java::Env->GetStaticFieldID(rmCls, "renderPosZ", "D");

		if (fid_renderPosX && fid_renderPosY && fid_renderPosZ) {
			result.x = Java::Env->GetStaticDoubleField(rmCls, fid_renderPosX);
			result.y = Java::Env->GetStaticDoubleField(rmCls, fid_renderPosY);
			result.z = Java::Env->GetStaticDoubleField(rmCls, fid_renderPosZ);
			debugLogged = true;
		} else {
			Java::Env->ExceptionClear();
			if (!debugLogged) {
				log_err("getRenderPos (1.7.10): Static fields renderPosX/Y/Z not found");
				debugLogged = true;
			}
		}
		
		Java::Env->DeleteLocalRef(rmCls);
	}
	else {
		// 1.8.9+: RenderManager uses INSTANCE fields
		jfieldID fid_renderManager = Java::Env->GetFieldID(cls, "renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;");
		if (!fid_renderManager) {
			Java::Env->ExceptionClear();
			return result;
		}
		
		jobject renderManager = Java::Env->GetObjectField(instance, fid_renderManager);
		if (!renderManager) return result;
		
		jclass rmCls = Java::Env->GetObjectClass(renderManager);
		
		jfieldID fid_renderPosX = Java::Env->GetFieldID(rmCls, "renderPosX", "D");
		jfieldID fid_renderPosY = Java::Env->GetFieldID(rmCls, "renderPosY", "D");
		jfieldID fid_renderPosZ = Java::Env->GetFieldID(rmCls, "renderPosZ", "D");
		
		// Fallback for some 1.8 versions or custom clients (viewerPosX)
		if (!fid_renderPosX) {
			Java::Env->ExceptionClear();
			fid_renderPosX = Java::Env->GetFieldID(rmCls, "viewerPosX", "D");
			fid_renderPosY = Java::Env->GetFieldID(rmCls, "viewerPosY", "D");
			fid_renderPosZ = Java::Env->GetFieldID(rmCls, "viewerPosZ", "D");
		}
		
		if (fid_renderPosX && fid_renderPosY && fid_renderPosZ) {
			result.x = Java::Env->GetDoubleField(renderManager, fid_renderPosX);
			result.y = Java::Env->GetDoubleField(renderManager, fid_renderPosY);
			result.z = Java::Env->GetDoubleField(renderManager, fid_renderPosZ);
			debugLogged = true;
		}
		
		Java::Env->DeleteLocalRef(rmCls);
		Java::Env->DeleteLocalRef(renderManager);
	}
	
	return result;
}

float Minecraft::getPartialTicks()
{
	if (!isValid()) return 0.0f;
	
	// Get Timer from Minecraft
	jfieldID fid_timer = Java::Env->GetFieldID(cls, "timer", "Lnet/minecraft/util/Timer;");
	if (!fid_timer) {
		Java::Env->ExceptionClear();
		return 0.0f;
	}
	
	jobject timer = Java::Env->GetObjectField(instance, fid_timer);
	if (!timer) return 0.0f;
	
	jclass timerCls = Java::Env->GetObjectClass(timer);
	
	// Try 1.8 field name first, then 1.7
	jfieldID fid_renderPartialTicks = Java::Env->GetFieldID(timerCls, "renderPartialTicks", "F");
	if (!fid_renderPartialTicks) {
		Java::Env->ExceptionClear();
		fid_renderPartialTicks = Java::Env->GetFieldID(timerCls, "field_74281_c", "F"); // SRG name
	}
	
	float result = 0.0f;
	if (fid_renderPartialTicks) {
		result = Java::Env->GetFloatField(timer, fid_renderPartialTicks);
	}
	
	Java::Env->DeleteLocalRef(timerCls);
	Java::Env->DeleteLocalRef(timer);
	
	return result;
}

std::vector<SlotInfo> Minecraft::getInventorySlots()
{
	std::vector<SlotInfo> result;
	if (!isValid()) return result;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return result;

	// Get EntityPlayer class for inventory field
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get InventoryPlayer class
	jclass inventoryCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get ItemStack class
	jclass itemStackCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get Item class
	jclass itemCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get ItemPotion class for splash detection
	jclass potionCls = nullptr;
	Java::AssignClass("net.minecraft.item.ItemPotion", potionCls);

	// player.inventory field
	jfieldID fid_inventory = Java::Env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
	if (!fid_inventory) {
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	jobject inventory = Java::Env->GetObjectField(localPlayer, fid_inventory);
	if (!inventory) {
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// inventory.mainInventory field (ItemStack[36])
	jfieldID fid_mainInventory = Java::Env->GetFieldID(inventoryCls, "mainInventory", "[Lnet/minecraft/item/ItemStack;");
	if (!fid_mainInventory) {
		Java::Env->DeleteLocalRef(inventory);
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	jobjectArray mainInventory = (jobjectArray)Java::Env->GetObjectField(inventory, fid_mainInventory);
	if (!mainInventory) {
		Java::Env->DeleteLocalRef(inventory);
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// ItemStack methods
	jmethodID mid_getItem = Java::Env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
	jfieldID fid_stackSize = Java::Env->GetFieldID(itemStackCls, "stackSize", "I");
	jmethodID mid_getMetadata = Java::Env->GetMethodID(itemStackCls, "getMetadata", "()I");

	// Item.getIdFromItem static method
	jmethodID mid_getIdFromItem = Java::Env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");

	// ItemPotion.isSplash (if available)
	jmethodID mid_isSplash = nullptr;
	if (potionCls) {
		mid_isSplash = Java::Env->GetMethodID(potionCls, "isSplash", "(I)Z");
	}

	int arrayLen = Java::Env->GetArrayLength(mainInventory);
	
	// Iterate all 36 slots (0-8 = hotbar, 9-35 = main inventory)
	for (int i = 0; i < arrayLen && i < 36; i++) {
		SlotInfo slot;
		slot.slotId = i;

		jobject itemStack = Java::Env->GetObjectArrayElement(mainInventory, i);
		if (!itemStack) {
			// Empty slot
			result.push_back(slot);
			continue;
		}

		// Get item
		jobject item = Java::Env->CallObjectMethod(itemStack, mid_getItem);
		if (!item) {
			Java::Env->DeleteLocalRef(itemStack);
			result.push_back(slot);
			continue;
		}

		// Get item ID
		slot.itemId = Java::Env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
		slot.stackSize = Java::Env->GetIntField(itemStack, fid_stackSize);
		slot.damage = Java::Env->CallIntMethod(itemStack, mid_getMetadata);

		// Check if it's a potion (ID 373)
		if (slot.itemId == 373) {
			// Check splash via ItemPotion.isSplash(damage)
			if (mid_isSplash && potionCls) {
				// Need to check if item is instance of ItemPotion
				if (Java::Env->IsInstanceOf(item, potionCls)) {
					slot.isSplashPotion = Java::Env->CallBooleanMethod(item, mid_isSplash, slot.damage);
				}
			}
			
			// Check if health potion by damage value
			// 1.8.9 potion damage values:
			// 8197 = Instant Health I, 8229 = Instant Health II
			// 16389 = Splash Instant Health I, 16421 = Splash Instant Health II
			int baseDamage = slot.damage & 0x3F;  // Lower 6 bits contain effect ID
			// Effect ID 5 = Instant Health, ID 6 = Instant Health II
			// Actually in 1.8.9: damage 8197 & 0x3F = 5, damage 8229 & 0x3F = 5
			// Better approach: check if it contains Instant Health effect
			// Instant Health potions: 8197, 8229, 16389, 16421
			if (slot.damage == 8197 || slot.damage == 8229 || 
				slot.damage == 16389 || slot.damage == 16421) {
				slot.isHealthPotion = true;
			}
		}

		Java::Env->DeleteLocalRef(item);
		Java::Env->DeleteLocalRef(itemStack);
		result.push_back(slot);
	}

	// Cleanup
	Java::Env->DeleteLocalRef(mainInventory);
	Java::Env->DeleteLocalRef(inventory);
	if (potionCls) Java::Env->DeleteLocalRef(potionCls);
	Java::Env->DeleteLocalRef(itemCls);
	Java::Env->DeleteLocalRef(itemStackCls);
	Java::Env->DeleteLocalRef(inventoryCls);
	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(localPlayer);

	return result;
}

void Minecraft::windowClick(int slotId, int button)
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Get player's openContainer to get the windowId
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// player.openContainer field
	jfieldID fid_openContainer = Java::Env->GetFieldID(playerCls, "openContainer", "Lnet/minecraft/inventory/Container;");
	if (!fid_openContainer) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject openContainer = Java::Env->GetObjectField(localPlayer, fid_openContainer);
	if (!openContainer) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Get Container class for windowId field
	jclass containerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.inventory.Container", containerCls)) {
		Java::Env->DeleteLocalRef(openContainer);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// container.windowId field
	jfieldID fid_windowId = Java::Env->GetFieldID(containerCls, "windowId", "I");
	int windowId = 0;
	if (fid_windowId) {
		windowId = Java::Env->GetIntField(openContainer, fid_windowId);
	}

	Java::Env->DeleteLocalRef(containerCls);
	Java::Env->DeleteLocalRef(openContainer);
	Java::Env->DeleteLocalRef(playerCls);

	// Get playerController from Minecraft
	jfieldID fid_playerController = Java::Env->GetFieldID(cls, "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;");
	if (!fid_playerController) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject playerController = Java::Env->GetObjectField(instance, fid_playerController);
	if (!playerController) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jclass controllerCls = Java::Env->GetObjectClass(playerController);

	// windowClick(int windowId, int slotId, int mouseButton, int mode, EntityPlayer player)
	// mode 1 = shift-click (quick move)
	jmethodID mid_windowClick = Java::Env->GetMethodID(controllerCls, "windowClick", 
		"(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");

	if (!mid_windowClick) {
		Java::Env->DeleteLocalRef(controllerCls);
		Java::Env->DeleteLocalRef(playerController);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Container slot mapping:
	// For player inventory GUI (windowId 0):
	// - Crafting output: slot 0
	// - Crafting grid: slots 1-4
	// - Armor: slots 5-8
	// - Main inventory: slots 9-35
	// - Hotbar: slots 36-44
	
	int containerSlot = slotId;
	if (slotId >= 0 && slotId <= 8) {
		// Hotbar: mainInventory[0-8] -> container slots 36-44
		containerSlot = slotId + 36;
	}
	// else slotId 9-35 maps directly to container slots 9-35

	// Call windowClick with exception handling
	jobject result = Java::Env->CallObjectMethod(playerController, mid_windowClick, 
		windowId, containerSlot, button, 1, localPlayer);
	
	// Check for exception
	if (Java::Env->ExceptionCheck()) {
		Java::Env->ExceptionClear();
	}

	if (result) Java::Env->DeleteLocalRef(result);
	Java::Env->DeleteLocalRef(controllerCls);
	Java::Env->DeleteLocalRef(playerController);
	Java::Env->DeleteLocalRef(localPlayer);
}

void Minecraft::openInventory()
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Create GuiInventory and display it
	// new GuiInventory(player) then minecraft.displayGuiScreen(guiInventory)
	
	jclass guiInventoryCls = nullptr;
	if (!Java::AssignClass("net.minecraft.client.gui.inventory.GuiInventory", guiInventoryCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Constructor: GuiInventory(EntityPlayer player)
	jmethodID mid_init = Java::Env->GetMethodID(guiInventoryCls, "<init>", "(Lnet/minecraft/entity/player/EntityPlayer;)V");
	if (!mid_init) {
		Java::Env->DeleteLocalRef(guiInventoryCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject guiInventory = Java::Env->NewObject(guiInventoryCls, mid_init, localPlayer);
	if (!guiInventory) {
		Java::Env->DeleteLocalRef(guiInventoryCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Minecraft.displayGuiScreen(guiScreen)
	jmethodID mid_displayGuiScreen = Java::Env->GetMethodID(cls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
	if (!mid_displayGuiScreen) {
		Java::Env->ExceptionClear();
		// SRG name? func_147108_a
		mid_displayGuiScreen = Java::Env->GetMethodID(cls, "func_147108_a", "(Lnet/minecraft/client/gui/GuiScreen;)V");
	}

	if (mid_displayGuiScreen) {
		Java::Env->CallVoidMethod(instance, mid_displayGuiScreen, guiInventory);
	}

	Java::Env->DeleteLocalRef(guiInventory);
	Java::Env->DeleteLocalRef(guiInventoryCls);
	Java::Env->DeleteLocalRef(localPlayer);
}



void Minecraft::setKeyBindState(int keyCode, bool pressed)
{
	if (!isValid() || !cls_keyBinding || !mid_setKeyBindState) return;
	
	// static void setKeyBindState(int keyCode, bool pressed)
	Java::Env->CallStaticVoidMethod(cls_keyBinding, mid_setKeyBindState, keyCode, pressed);
}

int Minecraft::getSneakKey()
{
	if (!isValid()) return 42; // LSHIFT default
	
	// Get gameSettings
	jfieldID fid_gameSettings = Java::Env->GetFieldID(cls, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
	if (!fid_gameSettings) return 42;
	
	jobject gameSettings = Java::Env->GetObjectField(instance, fid_gameSettings);
	if (!gameSettings) return 42;
	
	jclass settingsCls = Java::Env->GetObjectClass(gameSettings);
	if (!settingsCls) {
		Java::Env->DeleteLocalRef(gameSettings);
		return 42;
	}
	
	// Get keyBindSneak field
	jfieldID fid_keyBindSneak = Java::Env->GetFieldID(settingsCls, "keyBindSneak", "Lnet/minecraft/client/settings/KeyBinding;");
	if (!fid_keyBindSneak) {
		Java::Env->DeleteLocalRef(settingsCls);
		Java::Env->DeleteLocalRef(gameSettings);
		return 42;
	}
	
	jobject keyBindSneak = Java::Env->GetObjectField(gameSettings, fid_keyBindSneak);
	if (!keyBindSneak) {
		Java::Env->DeleteLocalRef(settingsCls);
		Java::Env->DeleteLocalRef(gameSettings);
		return 42;
	}
	
	// Get keyCode
	// Assuming KeyBinding.getKeyCode() is available (we cache mid_getKeyCode in init)
	int code = 42; 
	if (mid_getKeyCode) {
		code = Java::Env->CallIntMethod(keyBindSneak, mid_getKeyCode);
	}
	
	Java::Env->DeleteLocalRef(keyBindSneak);
	Java::Env->DeleteLocalRef(settingsCls);
	Java::Env->DeleteLocalRef(gameSettings);
	
	return code;
}

void Minecraft::closeInventory()
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Call player.closeScreen()
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jmethodID mid_closeScreen = Java::Env->GetMethodID(playerCls, "closeScreen", "()V");
	if (!mid_closeScreen) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	Java::Env->CallVoidMethod(localPlayer, mid_closeScreen);

	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(localPlayer);
}

bool Minecraft::isInventoryOpen()
{
	if (!isValid() || !fid_currentScreen) return false;
	
	jobject screen = Java::Env->GetObjectField(instance, fid_currentScreen);
	if (!screen) return false;

	// Check if it's a GuiInventory or GuiContainer
	jclass guiContainerCls = nullptr;
	bool result = false;
	
	if (Java::AssignClass("net.minecraft.client.gui.inventory.GuiContainer", guiContainerCls)) {
		result = Java::Env->IsInstanceOf(screen, guiContainerCls);
		Java::Env->DeleteLocalRef(guiContainerCls);
	}

	Java::Env->DeleteLocalRef(screen);
	return result;
}

bool Minecraft::isTargetingEntity()
{
	if (!isValid()) return false;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return false;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return false;

	// Get MovingObjectPosition class and typeOfHit field
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return false;
	}

	jfieldID fid_typeOfHit = Java::Env->GetFieldID(mopClass, "typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_typeOfHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject typeOfHit = Java::Env->GetObjectField(mouseOver, fid_typeOfHit);
	Java::Env->DeleteLocalRef(mouseOver);

	if (!typeOfHit) {
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	// Get the ENTITY enum constant
	jclass typeClass = Java::Env->GetObjectClass(typeOfHit);
	if (!typeClass) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jfieldID fid_entity = Java::Env->GetStaticFieldID(typeClass, "ENTITY", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_entity) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(typeClass);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject entityEnum = Java::Env->GetStaticObjectField(typeClass, fid_entity);
	bool result = Java::Env->IsSameObject(typeOfHit, entityEnum);

	Java::Env->DeleteLocalRef(entityEnum);
	Java::Env->DeleteLocalRef(typeOfHit);
	Java::Env->DeleteLocalRef(typeClass);
	Java::Env->DeleteLocalRef(mopClass);

	return result;
}

int Minecraft::getTargetedEntityHurtTime()
{
	if (!isValid()) return 0;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return 0;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return 0;

	// Get MovingObjectPosition class
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return 0;
	}

	// Get entityHit field (the entity being looked at)
	jfieldID fid_entityHit = Java::Env->GetFieldID(mopClass, "entityHit", "Lnet/minecraft/entity/Entity;");
	if (!fid_entityHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return 0;
	}

	jobject entityHit = Java::Env->GetObjectField(mouseOver, fid_entityHit);
	Java::Env->DeleteLocalRef(mouseOver);
	Java::Env->DeleteLocalRef(mopClass);

	if (!entityHit) return 0;

	// Get EntityLivingBase class for hurtTime
	jclass livingCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
		Java::Env->DeleteLocalRef(entityHit);
		return 0;
	}

	// Check if entity is an EntityLivingBase
	if (!Java::Env->IsInstanceOf(entityHit, livingCls)) {
		Java::Env->DeleteLocalRef(entityHit);
		Java::Env->DeleteLocalRef(livingCls);
		return 0;
	}

	jfieldID fid_hurtTime = Java::Env->GetFieldID(livingCls, "hurtTime", "I");
	if (!fid_hurtTime) {
		Java::Env->DeleteLocalRef(entityHit);
		Java::Env->DeleteLocalRef(livingCls);
		return 0;
	}

	int hurtTime = Java::Env->GetIntField(entityHit, fid_hurtTime);

	Java::Env->DeleteLocalRef(entityHit);
	Java::Env->DeleteLocalRef(livingCls);

	return hurtTime;
}

// Helper to get game settings (reduces duplication)
jobject getGameSettings(JNIEnv* env, jobject mcInstance, jclass mcClass) {
	jfieldID fid_gameSettings = env->GetFieldID(mcClass, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
	if (!fid_gameSettings) return nullptr;
	return env->GetObjectField(mcInstance, fid_gameSettings);
}

void Minecraft::releaseMovementKeys()
{
	if (!isValid() || !cls_keyBinding) return;
    
    // Check if already released to prevent spam and double-save
    if (keysReleased) return;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;
    
    jobject gameSettings = getGameSettings(env, instance, cls);
    if (!gameSettings) return;

    jclass settingsClass = env->GetObjectClass(gameSettings);

    const char* keyBindNames[] = {
		"keyBindForward", "keyBindBack", "keyBindLeft", "keyBindRight",
		"keyBindJump", "keyBindSneak", "keyBindAttack", "keyBindUseItem"
	};

    savedKeyStates.clear();

    for (const char* name : keyBindNames) {
        jfieldID fid_kb = env->GetFieldID(settingsClass, name, "Lnet/minecraft/client/settings/KeyBinding;");
		if (!fid_kb) continue;
		
		jobject keyBind = env->GetObjectField(gameSettings, fid_kb);
		if (!keyBind) continue;

        // Save current state
        bool isPressed = false;
        if (fid_pressed) {
            isPressed = env->GetBooleanField(keyBind, fid_pressed);
            savedKeyStates[name] = isPressed;
        }

        // Force release via setKeyBindState
        int keyCode = env->CallIntMethod(keyBind, mid_getKeyCode);
        env->CallStaticVoidMethod(cls_keyBinding, mid_setKeyBindState, keyCode, JNI_FALSE);

        // Also set internal pressed field to false if possible
        if (fid_pressed) {
            env->SetBooleanField(keyBind, fid_pressed, JNI_FALSE);
        }

        env->DeleteLocalRef(keyBind);
    }
    
    keysReleased = true;

    env->DeleteLocalRef(settingsClass);
    env->DeleteLocalRef(gameSettings);
}

void Minecraft::restoreMovementKeys()
{
	if (!isValid() || !cls_keyBinding || !mid_setKeyBindState || !mid_getKeyCode) return;
    
    // Only restore if we actually released them
    if (!keysReleased) return;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

	jobject gameSettings = getGameSettings(env, instance, cls);
	if (!gameSettings) return;

	jclass settingsClass = env->GetObjectClass(gameSettings);

	const char* keyBindNames[] = {
		"keyBindForward", "keyBindBack", "keyBindLeft", "keyBindRight",
		"keyBindJump", "keyBindSneak", "keyBindAttack", "keyBindUseItem"
	};

	for (const char* name : keyBindNames) {
		jfieldID fid_kb = env->GetFieldID(settingsClass, name, "Lnet/minecraft/client/settings/KeyBinding;");
		if (!fid_kb) continue;
		
		jobject keyBind = env->GetObjectField(gameSettings, fid_kb);
		if (!keyBind) continue;

		int keyCode = env->CallIntMethod(keyBind, mid_getKeyCode);
        
        // 1. Check Saved State
        bool wasPressed = savedKeyStates[name];
		
        // 2. Check Physical State
		bool isPhysicallyHeld = false;
        UINT vk = 0;

		if (keyCode < 0) {
			int btn = keyCode + 100;
			if (btn == 0) isPhysicallyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			else if (btn == 1) isPhysicallyHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			else if (btn == 2) isPhysicallyHeld = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
		} else {
			vk = MapVirtualKeyA(keyCode, MAPVK_VSC_TO_VK);
			if (vk != 0) {
				isPhysicallyHeld = (GetAsyncKeyState(vk) & 0x8000) != 0;
			}
		}

        // Restore if EITHER was pressed or IS pressed
		if (wasPressed || isPhysicallyHeld) {
            // Restore via JNI (sets .pressed = true and pressedTick state)
			env->CallStaticVoidMethod(cls_keyBinding, mid_setKeyBindState, keyCode, JNI_TRUE);
            
            // Should explicit set .pressed field too? setKeyBindState usually handles it.
            if (fid_pressed) {
                env->SetBooleanField(keyBind, fid_pressed, JNI_TRUE);
            }
		}

		env->DeleteLocalRef(keyBind);
	}
    
    keysReleased = false;
    savedKeyStates.clear();

	env->DeleteLocalRef(settingsClass);
	env->DeleteLocalRef(gameSettings);
}

int Minecraft::getBlockId(int x, int y, int z)
{
	if (!isValid()) return 0;

	jobject world = getWorld();
	if (!world) return 0;

	int blockId = 0;

	// 1.8.x: BlockPos(x,y,z) -> world.getBlockState(pos) -> blockState.getBlock() -> Block.getIdFromBlock(block)
	// Note: This implementation is for 1.8.9. 1.7.10 uses different method signatures.

	jclass blockPosCls = nullptr;
	if (!Java::AssignClass("net.minecraft.util.BlockPos", blockPosCls)) {
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	// BlockPos constructor: (int x, int y, int z)
	jmethodID mid_blockPosInit = Java::Env->GetMethodID(blockPosCls, "<init>", "(III)V");
	if (!mid_blockPosInit) {
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	jobject blockPos = Java::Env->NewObject(blockPosCls, mid_blockPosInit, x, y, z);
	if (!blockPos) {
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	// Get World class for getBlockState method
	jclass worldCls = nullptr;
	if (!Java::AssignClass("net.minecraft.world.World", worldCls)) {
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	// world.getBlockState(BlockPos) -> IBlockState
	jmethodID mid_getBlockState = Java::Env->GetMethodID(worldCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
	if (!mid_getBlockState) {
		Java::Env->ExceptionClear();
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	jobject blockState = Java::Env->CallObjectMethod(world, mid_getBlockState, blockPos);
	if (!blockState) {
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	// IBlockState.getBlock() -> Block
	jclass blockStateCls = nullptr;
	if (!Java::AssignClass("net.minecraft.block.state.IBlockState", blockStateCls)) {
		Java::Env->DeleteLocalRef(blockState);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	jmethodID mid_getBlock = Java::Env->GetMethodID(blockStateCls, "getBlock", "()Lnet/minecraft/block/Block;");
	if (!mid_getBlock) {
		Java::Env->ExceptionClear();
		Java::Env->DeleteLocalRef(blockStateCls);
		Java::Env->DeleteLocalRef(blockState);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	jobject block = Java::Env->CallObjectMethod(blockState, mid_getBlock);
	if (!block) {
		Java::Env->DeleteLocalRef(blockStateCls);
		Java::Env->DeleteLocalRef(blockState);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	// Block.getIdFromBlock(Block) -> int (static method)
	jclass blockCls = nullptr;
	if (!Java::AssignClass("net.minecraft.block.Block", blockCls)) {
		Java::Env->DeleteLocalRef(block);
		Java::Env->DeleteLocalRef(blockStateCls);
		Java::Env->DeleteLocalRef(blockState);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	jmethodID mid_getIdFromBlock = Java::Env->GetStaticMethodID(blockCls, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
	if (!mid_getIdFromBlock) {
		Java::Env->ExceptionClear();
		Java::Env->DeleteLocalRef(blockCls);
		Java::Env->DeleteLocalRef(block);
		Java::Env->DeleteLocalRef(blockStateCls);
		Java::Env->DeleteLocalRef(blockState);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(blockPos);
		Java::Env->DeleteLocalRef(blockPosCls);
		Java::Env->DeleteLocalRef(world);
		return 0;
	}

	blockId = Java::Env->CallStaticIntMethod(blockCls, mid_getIdFromBlock, block);

	// Cleanup
	Java::Env->DeleteLocalRef(blockCls);
	Java::Env->DeleteLocalRef(block);
	Java::Env->DeleteLocalRef(blockStateCls);
	Java::Env->DeleteLocalRef(blockState);
	Java::Env->DeleteLocalRef(worldCls);
	Java::Env->DeleteLocalRef(blockPos);
	Java::Env->DeleteLocalRef(blockPosCls);
	Java::Env->DeleteLocalRef(world);

	return blockId;
}

Minecraft::RAYTRACE_BLOCKS_RESULT Minecraft::rayTraceBlocks(Vec3 from, Vec3 direction, Vec3& result, bool stopOnLiquid)
{
	result = { -1.5f, -1.5f, -1.5f }; // Default error value
	
	if (!isValid()) return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

	jobject world = getWorld();
	if (!world) return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

	jclass worldCls = nullptr;
	if (!Java::AssignClass("net.minecraft.world.World", worldCls)) {
		Java::Env->DeleteLocalRef(world);
		return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
	}

	// We need Vec3 class
	jclass vec3Cls = nullptr;
	if (!Java::AssignClass("net.minecraft.util.Vec3", vec3Cls)) {
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(world);
		return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
	}

	// Vec3 constructor (x, y, z) - uses double
	jmethodID mid_vec3Init = Java::Env->GetMethodID(vec3Cls, "<init>", "(DDD)V");
	if (!mid_vec3Init) {
		Java::Env->DeleteLocalRef(vec3Cls);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(world);
		return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
	}

	// Create from and direction Vec3 objects 
	jobject fromObj = Java::Env->NewObject(vec3Cls, mid_vec3Init, (double)from.x, (double)from.y, (double)from.z);
	if (!fromObj) {
		Java::Env->DeleteLocalRef(vec3Cls);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(world);
		return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
	}

	jobject directionObj = Java::Env->NewObject(vec3Cls, mid_vec3Init, (double)direction.x, (double)direction.y, (double)direction.z);
	if (!directionObj) {
		Java::Env->DeleteLocalRef(fromObj);
		Java::Env->DeleteLocalRef(vec3Cls);
		Java::Env->DeleteLocalRef(worldCls);
		Java::Env->DeleteLocalRef(world);
		return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
	}

	// Signature: (Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;Z)Lnet/minecraft/util/MovingObjectPosition;
	jmethodID mid_rayTrace = Java::Env->GetMethodID(worldCls, "rayTraceBlocks", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;Z)Lnet/minecraft/util/MovingObjectPosition;");
	if (!mid_rayTrace) {
		Java::Env->ExceptionClear();
		// Try obfuscated name (func_72933_a is 2-arg, func_147447_a is 5-arg)
		// For 3-arg: maybe a different obfuscated name or it doesn't exist
		mid_rayTrace = Java::Env->GetMethodID(worldCls, "func_72933_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;Z)Lnet/minecraft/util/MovingObjectPosition;");
	}
	
	if (!mid_rayTrace) {
		Java::Env->ExceptionClear();
		// Try simple 2-arg version
		mid_rayTrace = Java::Env->GetMethodID(worldCls, "rayTraceBlocks", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;");
	}
	
	if (!mid_rayTrace) {
		Java::Env->ExceptionClear();
		mid_rayTrace = Java::Env->GetMethodID(worldCls, "func_72933_a", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;");
	}

	jobject mop = nullptr;
	if (mid_rayTrace) {
		// Check if 3-arg or 2-arg by trying both
		mop = Java::Env->CallObjectMethod(world, mid_rayTrace, fromObj, directionObj, stopOnLiquid);
		if (Java::Env->ExceptionCheck()) {
			Java::Env->ExceptionClear();
			// Try 2-arg
			jmethodID mid_rayTrace2 = Java::Env->GetMethodID(worldCls, "rayTraceBlocks", "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;");
			if (mid_rayTrace2) {
				mop = Java::Env->CallObjectMethod(world, mid_rayTrace2, fromObj, directionObj);
			}
		}
	}

	Java::Env->DeleteLocalRef(directionObj);
	Java::Env->DeleteLocalRef(fromObj);
	Java::Env->DeleteLocalRef(vec3Cls);
	Java::Env->DeleteLocalRef(worldCls);
	Java::Env->DeleteLocalRef(world);
	
	// Check if we didn't hit anything
	if (!mop) {
		return RAYTRACE_BLOCKS_RESULT::NO_HIT;
	}

	jclass mopCls = Java::Env->GetObjectClass(mop);
	
	// Try: getBlockPos() -> BlockPos
	jmethodID mid_getBlockPos = Java::Env->GetMethodID(mopCls, "getBlockPos", "()Lnet/minecraft/util/BlockPos;");
	if (!mid_getBlockPos) {
		Java::Env->ExceptionClear();
		mid_getBlockPos = Java::Env->GetMethodID(mopCls, "func_178782_a", "()Lnet/minecraft/util/BlockPos;");
	}
	
	if (mid_getBlockPos) {
		jobject blockPosObj = Java::Env->CallObjectMethod(mop, mid_getBlockPos);
		if (blockPosObj) {
			jclass blockPosCls = Java::Env->GetObjectClass(blockPosObj);
			
			// BlockPos extends Vec3i, get X/Y/Z
			jmethodID mid_getX = Java::Env->GetMethodID(blockPosCls, "getX", "()I");
			jmethodID mid_getY = Java::Env->GetMethodID(blockPosCls, "getY", "()I");
			jmethodID mid_getZ = Java::Env->GetMethodID(blockPosCls, "getZ", "()I");
			
			if (!mid_getX) { Java::Env->ExceptionClear(); mid_getX = Java::Env->GetMethodID(blockPosCls, "func_177958_n", "()I"); }
			if (!mid_getY) { Java::Env->ExceptionClear(); mid_getY = Java::Env->GetMethodID(blockPosCls, "func_177956_o", "()I"); }
			if (!mid_getZ) { Java::Env->ExceptionClear(); mid_getZ = Java::Env->GetMethodID(blockPosCls, "func_177952_p", "()I"); }
			
			if (mid_getX && mid_getY && mid_getZ) {
				result.x = (float)Java::Env->CallIntMethod(blockPosObj, mid_getX);
				result.y = (float)Java::Env->CallIntMethod(blockPosObj, mid_getY);
				result.z = (float)Java::Env->CallIntMethod(blockPosObj, mid_getZ);
				
				Java::Env->DeleteLocalRef(blockPosCls);
				Java::Env->DeleteLocalRef(blockPosObj);
				Java::Env->DeleteLocalRef(mopCls);
				Java::Env->DeleteLocalRef(mop);
				return RAYTRACE_BLOCKS_RESULT::HIT;
			}
			
			Java::Env->DeleteLocalRef(blockPosCls);
			Java::Env->DeleteLocalRef(blockPosObj);
		}
	}
	
	// Fallback: Try direct blockX/blockY/blockZ fields (1.7.10 style)
	Java::Env->ExceptionClear();
	jfieldID fid_blockX = Java::Env->GetFieldID(mopCls, "blockX", "I");
	jfieldID fid_blockY = Java::Env->GetFieldID(mopCls, "blockY", "I");
	jfieldID fid_blockZ = Java::Env->GetFieldID(mopCls, "blockZ", "I");
	
	if (fid_blockX && fid_blockY && fid_blockZ) {
		result.x = (float)Java::Env->GetIntField(mop, fid_blockX);
		result.y = (float)Java::Env->GetIntField(mop, fid_blockY);
		result.z = (float)Java::Env->GetIntField(mop, fid_blockZ);
		
		Java::Env->DeleteLocalRef(mopCls);
		Java::Env->DeleteLocalRef(mop);
		return RAYTRACE_BLOCKS_RESULT::HIT;
	}

	Java::Env->DeleteLocalRef(mopCls);
	Java::Env->DeleteLocalRef(mop);
	return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
}

void Minecraft::ungrabMouse()
{
	if (!isValid()) return;
	
	// Method 1: Call Mouse.setGrabbed(false) directly via LWJGL
	// This is the most reliable method but requires finding LWJGL classes
	
	// Method 2: Set inGameHasFocus to false and call mouseHelper.ungrabMouseCursor()
	// minecraft.inGameHasFocus = false
	// minecraft.mouseHelper.ungrabMouseCursor()
	
	JNIEnv* env = Java::GetEnv();
	if (!env) return;
	
	// Try mouseHelper approach (1.8.x)
	jfieldID fid_mouseHelper = env->GetFieldID(cls, "mouseHelper", "Lnet/minecraft/util/MouseHelper;");
	if (fid_mouseHelper) {
		jobject mouseHelper = env->GetObjectField(instance, fid_mouseHelper);
		if (mouseHelper) {
			jclass mouseHelperCls = env->GetObjectClass(mouseHelper);
			if (mouseHelperCls) {
				jmethodID mid_ungrab = env->GetMethodID(mouseHelperCls, "ungrabMouseCursor", "()V");
				if (mid_ungrab) {
					env->CallVoidMethod(mouseHelper, mid_ungrab);
				}
				env->DeleteLocalRef(mouseHelperCls);
			}
			env->DeleteLocalRef(mouseHelper);
		}
	}
	env->ExceptionClear();
	
	// Also set inGameHasFocus = false for extra safety
	jfieldID fid_inGameHasFocus = env->GetFieldID(cls, "inGameHasFocus", "Z");
	if (fid_inGameHasFocus) {
		env->SetBooleanField(instance, fid_inGameHasFocus, JNI_FALSE);
	}
	env->ExceptionClear();
	
	// Direct LWJGL approach as fallback (org.lwjgl.input.Mouse)
	jclass mouseCls = env->FindClass("org/lwjgl/input/Mouse");
	if (mouseCls) {
		jmethodID mid_setGrabbed = env->GetStaticMethodID(mouseCls, "setGrabbed", "(Z)V");
		if (mid_setGrabbed) {
			env->CallStaticVoidMethod(mouseCls, mid_setGrabbed, JNI_FALSE);
		}
		env->DeleteLocalRef(mouseCls);
	}
	env->ExceptionClear();
}

void Minecraft::grabMouse()
{
	if (!isValid()) return;
	
	JNIEnv* env = Java::GetEnv();
	if (!env) return;
	
	// Set inGameHasFocus = true first
	jfieldID fid_inGameHasFocus = env->GetFieldID(cls, "inGameHasFocus", "Z");
	if (fid_inGameHasFocus) {
		env->SetBooleanField(instance, fid_inGameHasFocus, JNI_TRUE);
	}
	env->ExceptionClear();
	
	// Call mouseHelper.grabMouseCursor()
	jfieldID fid_mouseHelper = env->GetFieldID(cls, "mouseHelper", "Lnet/minecraft/util/MouseHelper;");
	if (fid_mouseHelper) {
		jobject mouseHelper = env->GetObjectField(instance, fid_mouseHelper);
		if (mouseHelper) {
			jclass mouseHelperCls = env->GetObjectClass(mouseHelper);
			if (mouseHelperCls) {
				jmethodID mid_grab = env->GetMethodID(mouseHelperCls, "grabMouseCursor", "()V");
				if (mid_grab) {
					env->CallVoidMethod(mouseHelper, mid_grab);
				}
				env->DeleteLocalRef(mouseHelperCls);
			}
			env->DeleteLocalRef(mouseHelper);
		}
	}
	env->ExceptionClear();
	
	// Direct LWJGL fallback
	jclass mouseCls = env->FindClass("org/lwjgl/input/Mouse");
	if (mouseCls) {
		jmethodID mid_setGrabbed = env->GetStaticMethodID(mouseCls, "setGrabbed", "(Z)V");
		if (mid_setGrabbed) {
			env->CallStaticVoidMethod(mouseCls, mid_setGrabbed, JNI_TRUE);
		}
		env->DeleteLocalRef(mouseCls);
	}
	env->ExceptionClear();
}
