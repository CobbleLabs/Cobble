#pragma once

#include <jni.h>
#include <string>
#include "../module.h"

// InventoryMove — keep walking, jumping and sprinting while a screen is open.
//
// Vanilla parks every keybind the moment a screen opens: displayGuiScreen() calls
// setIngameNotInFocus() → KeyBinding.unPressAllKeys(), and the keyboard event loop stops
// feeding setKeyBindState() while currentScreen is non-null. So the whole job here is to
// keep writing KeyBinding.pressed from the real key state for as long as the screen is up,
// and to hand the keybinds back — synced to what is physically held — the moment it closes.
//
// Writing `pressed` is the same channel a real key event uses, so MovementInput picks it up
// in the next updatePlayerMoveState() and the movement that follows is ordinary vanilla
// movement: normal packets, normal simulation, nothing to detect beyond the fact that a
// player is moving while a container is open.
class InventoryMove : public Module {
public:
    InventoryMove();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        // What is allowed to pass through to the game while a screen is open.
        bool movement = true;  // WASD
        bool jump = true;
        bool sneak = true;
        bool sprint = true;

        // Which screens count. Text screens are off by default for the obvious reason: the
        // letters in "wasd" are also movement keys, and nobody wants to slide across the map
        // while typing in chat or renaming an item. "Other" covers the pause menu, options,
        // and anything else that is neither a container nor a text screen.
        bool inContainers = true;
        bool inTextEntry = false;
        bool inOther = false;
    } settings;

private:
    // One managed keybind: the GameSettings field it lives in, and the last state we wrote.
    struct Bind {
        jfieldID* field;  // points at the JniCache slot (resolved after we are constructed)
        int keyCode;      // LWJGL code, cached per activation
        bool wanted;      // set per pass from the physical key
        bool written;     // last value we pushed, so we only write on a change
        bool owned;       // true while we are the reason it is pressed
    };

    enum BindIndex : int { Forward = 0, Back, Left, Right, Jump, Sneak, Sprint, Count };

    // Classification of Minecraft.currentScreen. Cheap: a couple of IsInstanceOf calls,
    // and only while a screen is actually open.
    enum class ScreenKind : int { None = 0, Container, TextEntry, Other };
    ScreenKind classifyScreen(JNIEnv* env);
    bool screenAllowed(ScreenKind kind) const;
    bool bindAllowed(int index) const;
    void resolveClasses(JNIEnv* env);
    // Re-read the LWJGL key codes out of GameSettings (called when a screen opens, so a
    // rebind made in the controls menu is picked up).
    void cacheKeyCodes(JNIEnv* env, jobject gameSettings);

    // Push `wanted` into the game for every managed bind that changed.
    void flush(JNIEnv* env, jobject gameSettings);
    // Screen closed (or module turned off). With syncPhysical, every bind we touched is
    // re-synced to the physical key, because vanilla never sends another key-down for a key
    // that is already held; without it we simply drop ownership (Cobble's own menu has
    // already parked the keys and must be left alone).
    void handBack(JNIEnv* env, bool syncPhysical);

    Bind binds_[Count]{};
    bool active_ = false;  // a screen we act on is currently open
    ScreenKind lastKind_ = ScreenKind::None;
    bool classesResolved_ = false;
    jclass clsGuiContainer_ = nullptr;  // GlobalRef
    jclass clsGuiChat_ = nullptr;       // GlobalRef
    jclass clsGuiRepair_ = nullptr;     // GlobalRef — anvil: a container with a text field
    jclass clsGuiEditSign_ = nullptr;   // GlobalRef
    jclass clsGuiBook_ = nullptr;       // GlobalRef
};

extern InventoryMove* inventoryMove;
