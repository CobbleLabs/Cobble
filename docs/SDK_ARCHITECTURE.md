# SDK Architecture

This document describes the architecture of the Minecraft SDK after the modularization refactor.

## Overview

The SDK provides a C++ interface to interact with Minecraft's Java runtime via JNI. It abstracts game state (player, world, inventory) and provides methods for modules to query and manipulate the game.

## Rotation Fields

The LocalPlayer wrapper exposes rotation fields that mirror the Java Entity state. Understanding when to use each field is critical for smooth aim assist and avoiding detection.

### Available Fields

| Field | Purpose | When Updated by Game |
|-------|---------|---------------------|
| `rotationYaw/Pitch` | Current rotation | Every tick + mouse input |
| `prevRotationYaw/Pitch` | Previous tick rotation | Start of each tick (copied from current) |
| `lastReportedYaw/Pitch` | Last sent to server | After movement packet sent |

### SDK Methods

```cpp
// Getters
float getYaw();              // rotationYaw
float getPitch();            // rotationPitch
float getPrevYaw();          // prevRotationYaw
float getPrevPitch();        // prevRotationPitch
float getLastReportedYaw();  // lastReportedYaw
float getLastReportedPitch();// lastReportedPitch

// Setters
void setYaw(float value);
void setPitch(float value);
void setPrevYaw(float value);
void setPrevPitch(float value);
void setLastReportedYaw(float value);
void setLastReportedPitch(float value);
```

### Why prevRotation Matters

Minecraft interpolates camera between `prevRotation` and `rotationYaw` using `partialTicks`. If you only update `rotationYaw`, the camera will visually "fight" back toward `prevRotationYaw` until the next tick.

## Hitbox / Bounding Box (AxisAlignedBB)

PlayerInfo now includes full **hitbox data** from `Entity.boundingBox` (AxisAlignedBB):

### Fields
```cpp
// Raw AABB coordinates (world space)
float hitboxMinX, hitboxMinY, hitboxMinZ;
float hitboxMaxX, hitboxMaxY, hitboxMaxZ;
bool isSneaking;
```

### Helper Methods
```cpp
// Hitbox center (for aim-at-center)
float getHitboxCenterX() const;
float getHitboxCenterY() const;
float getHitboxCenterZ() const;

// Dimensions
float getHitboxWidth() const;   // Standard: 0.6
float getHitboxHeight() const;  // Standard: 1.8 (1.62 sneaking)
float getEyeHeight() const;     // 1.62 (1.54 sneaking)
```

### Usage in AimAssist
```cpp
// Aim at center of hitbox instead of posY + offset
Vec3 targetPos = {
    target.getHitboxCenterX(),
    target.getHitboxCenterY(),  // More accurate than posY + 1.62
    target.getHitboxCenterZ()
};

// Or aim at closest point in hitbox (for aim-at-closest-point feature)
float clampedX = std::clamp(localX, target.hitboxMinX, target.hitboxMaxX);
float clampedY = std::clamp(localY, target.hitboxMinY, target.hitboxMaxY);
float clampedZ = std::clamp(localZ, target.hitboxMinZ, target.hitboxMaxZ);
```

### JNI Cache
The following IDs are cached for hitbox access:
- `fid_boundingBox` - Entity.boundingBox (1.7) or entityBoundingBox (1.8)
- `cls_AxisAlignedBB` - AxisAlignedBB class
- `fid_aabb_minX/Y/Z`, `fid_aabb_maxX/Y/Z` - AABB coordinate fields
- `mid_isSneaking` - EntityLivingBase.isSneaking()

## Directory Structure

```
src/sdk/
├── minecraft.h              # Main header (public API)
├── minecraft.cpp            # Core SDK initialization and basic methods
├── player.h / player.cpp    # LocalPlayer class
├── jstring_util.h           # JNI string utilities
├── version.h                # Minecraft version detection
│
├── jni/                     # JNI infrastructure
│   ├── jni_cache.h          # Cached JNI IDs (declarations)
│   └── jni_cache.cpp        # JNI cache initialization
│
└── mc/                      # Minecraft method modules
    ├── input.cpp            # Keyboard/mouse handling
    ├── world.cpp            # Block queries, raytrace
    ├── render.cpp           # Render position, targeting
    ├── inventory.cpp        # Inventory operations
    └── player_list.cpp      # Player enumeration
```

## JNI Cache

All JNI identifiers (class references, method IDs, field IDs) are cached at startup in the `JniCache` namespace. This provides:

1. **Performance**: No repeated lookups during gameplay
2. **Thread Safety**: Classes stored as GlobalRefs
3. **Maintainability**: Single source of truth for all JNI IDs

### Usage

```cpp
// In SDK modules - use cached IDs directly
if (JniCache::mid_getHealth) {
    float health = env->CallFloatMethod(entity, JniCache::mid_getHealth);
}

// In external code - use Minecraft class methods
float health = mc.getPlayerHealth();  // Uses cache internally
```

### Initialization

The cache is initialized in `SDK_Init()`:

```cpp
void SDK_Init() {
    // ... basic init ...
    
    if (!JniCache::Init()) {
        log_warn("JniCache::Init failed");
    }
    
    // ... rest of init ...
}
```

## Module Responsibilities

| Module | Description |
|--------|-------------|
| `minecraft.cpp` | SDK_Init, getPlayer, getWorld, isInGui, click delays |
| `mc/input.cpp` | Keybinds, movement keys, mouse grab/ungrab |
| `mc/world.cpp` | Block ID queries, raytracing |
| `mc/render.cpp` | Render position, partial ticks, targeting detection |
| `mc/inventory.cpp` | Slot enumeration, window clicks, GUI control |
| `mc/player_list.cpp` | Player list, visibility checks, team detection |

## Version Compatibility

The SDK supports both Minecraft 1.7.10 and 1.8.x:

- **1.7.10**: Uses `EntityClientPlayerMP`, static RenderManager fields, `getCommandSenderName()`
- **1.8.x**: Uses `EntityPlayerSP`, instance RenderManager fields, `getName()`, `BlockPos`

Version-specific code is handled in `JniCache::Init()` with fallbacks for obfuscated names.

## Adding New JNI IDs

1. Declare in `jni_cache.h`:
   ```cpp
   extern jmethodID mid_newMethod;
   ```

2. Initialize in `jni_cache.cpp`:
   ```cpp
   TRY_METHOD(mid_newMethod, cls_SomeClass, "methodName", "(args)Return;");
   ```

3. Use in modules:
   ```cpp
   if (JniCache::mid_newMethod) {
       env->CallVoidMethod(obj, JniCache::mid_newMethod);
   }
   ```

## Thread Safety

- All `jclass` references are GlobalRefs (valid on any thread)
- Method/Field IDs are inherently thread-safe after lookup
- `JNIEnv*` must be obtained per-thread via `Java::GetEnv()`

SDK Refactoring Technical Documentation
Date: 2026-01-04
Author: Antigravity AI Assistant
Scope: Minecraft SDK modularization and JNI ID caching

Executive Summary
This document describes the complete refactoring of the Minecraft SDK (
src/sdk/minecraft.cpp
) from a monolithic 1854-line file into a modular architecture with centralized JNI caching for improved performance and maintainability.

Key Changes
Before: Single 
minecraft.cpp
 (1854 lines) with scattered JNI lookups
After:
Core 
minecraft.cpp
 (130 lines) + 5 specialized modules
Centralized JNI cache (jni_cache.h/cpp) with ~150 cached IDs
Organized directory structure (sdk/jni/, sdk/mc/)
1. Architecture Overview
1.1 New Directory Structure
src/sdk/
├── minecraft.h              # Main header (unchanged API)
├── minecraft.cpp            # Core methods only (~130 lines)
├── player.h / player.cpp    # LocalPlayer class (pre-existing)
├── jstring_util.h           # JNI string utilities
├── version.h                # Version detection
│
├── jni/                     # NEW: JNI infrastructure
│   ├── jni_cache.h          # Cache declarations (extern globals)
│   └── jni_cache.cpp        # Cache initialization logic
│
└── mc/                      # NEW: Minecraft method modules
    ├── input.cpp            # Keyboard/mouse handling
    ├── world.cpp            # Block queries, raytrace
    ├── render.cpp           # Render position, targeting
    ├── inventory.cpp        # Inventory operations
    └── player_list.cpp      # Player enumeration
1.2 Module Responsibilities
Module	Functions	Lines
minecraft.cpp
SDK_Init, getPlayer, getWorld, isInGui, clickMouse, delays	~130
mc/input.cpp
setKeyBindState, getSneakKey, releaseMovementKeys, restoreMovementKeys, ungrabMouse, grabMouse	~280
mc/world.cpp
getBlockId, rayTraceBlocks	~175
mc/render.cpp
getMouseSensitivity, isTargetingBlock, isTargetingEntity, getTargetedEntityHurtTime, getRenderPos, getPartialTicks	~225
mc/inventory.cpp
getInventorySlots, windowClick, openInventory, closeInventory, isInventoryOpen	~330
mc/player_list.cpp
getPlayerList, canEntityBeSeen, getLocalPlayerTeamColor	~295
2. JNI Cache Implementation
2.1 Problem Statement
The original 
minecraft.cpp
 performed JNI lookups (GetFieldID, GetMethodID, FindClass) inside each function call. This caused:

Performance overhead: Each GetMethodID call searches the class vtable
Repeated code: Same lookup logic duplicated across many functions
Error-prone: Easy to forget ExceptionClear() after failed lookups
Thread safety: Local jclass references can't be shared across threads
2.2 Solution: Centralized JNI Cache
The cache stores all JNI identifiers as global variables initialized once at startup:

// jni_cache.h
namespace JniCache {
    // Classes (GlobalRefs for thread safety)
    extern jclass cls_Minecraft;
    extern jclass cls_Entity;
    extern jclass cls_EntityPlayer;
    // ... 25+ classes
    
    // Method IDs
    extern jmethodID mid_getMinecraft;
    extern jmethodID mid_getHealth;
    // ... 40+ methods
    
    // Field IDs
    extern jfieldID fid_thePlayer;
    extern jfieldID fid_posX;
    // ... 80+ fields
    
    bool Init();      // Initialize all IDs
    void Cleanup();   // Release GlobalRefs
}
2.3 Cache Initialization
jni_cache.cpp

bool JniCache::Init() {
    JNIEnv* env = Java::GetEnv();
    bool is_1_7_10 = (Java::Version == MinecraftVersion::MC_1_7_10);
    
    // 1. Cache critical classes (fail if not found)
    CACHE_CLASS_CRITICAL(cls_Minecraft, "net.minecraft.client.Minecraft");
    CACHE_CLASS_CRITICAL(cls_Entity, "net.minecraft.entity.Entity");
    
    // 2. Cache optional classes (warn if not found)
    CACHE_CLASS(cls_BlockPos, "net.minecraft.util.BlockPos");  // 1.8+ only
    
    // 3. Cache method/field IDs with version-specific signatures
    if (is_1_7_10) {
        TRY_METHOD(mid_getName, cls_EntityPlayer, "getCommandSenderName", "()Ljava/lang/String;");
    } else {
        TRY_METHOD(mid_getName, cls_EntityPlayer, "getName", "()Ljava/lang/String;");
    }
    
    // 4. Handle obfuscated names (Lunar, Forge, etc.)
    TRY_METHOD(mid_getBlockPos, cls_MovingObjectPosition, "getBlockPos", "()Lnet/minecraft/util/BlockPos;");
    if (!mid_getBlockPos) {
        TRY_METHOD(mid_getBlockPos, cls_MovingObjectPosition, "func_178782_a", "()Lnet/minecraft/util/BlockPos;");
    }
    
    return true;
}
2.4 Helper Macros
#define CACHE_CLASS(var, name) \
    do { \
        jclass local = nullptr; \
        if (Java::AssignClass(name, local)) { \
            var = (jclass)env->NewGlobalRef(local); \
            env->DeleteLocalRef(local); \
        } \
    } while(0)
#define TRY_METHOD(var, cls, name, sig) \
    do { \
        var = env->GetMethodID(cls, name, sig); \
        if (!var) env->ExceptionClear(); \
    } while(0)
2.5 Thread Safety
All jclass references are stored as GlobalRefs, which:

Are valid across all threads attached to the JVM
Don't get garbage collected
Must be explicitly deleted on cleanup
void JniCache::Cleanup() {
    JNIEnv* env = Java::GetEnv();
    
    #define DELETE_GLOBAL(var) if (var) { env->DeleteGlobalRef(var); var = nullptr; }
    
    DELETE_GLOBAL(cls_Minecraft);
    DELETE_GLOBAL(cls_Entity);
    // ... all 25+ classes
}
3. Module Extraction Details
3.1 Input Module (
mc/input.cpp
)
Handles keyboard and mouse capture:

Function	Description	Cache Usage
setKeyBindState(keyCode, pressed)
Set keybind state via static method	JniCache::cls_KeyBinding, mid_setKeyBindState
getSneakKey()
Get sneak keybind keycode	fid_gameSettings, fid_keyBindSneak, mid_getKeyCode
releaseMovementKeys()
Release all movement keys on menu open	Iterates fid_keyBindForward, fid_keyBindBack, etc.
restoreMovementKeys()
Restore keys on menu close	Same fields + GetAsyncKeyState for physical state
ungrabMouse()
Release LWJGL mouse capture	fid_mouseHelper, mid_ungrabMouseCursor, cls_Mouse
grabMouse()
Re-capture mouse	Same + fid_inGameHasFocus
Design Decision: Movement key state is stored in Minecraft::savedKeyStates map and keysReleased flag to track state across menu open/close.

3.2 World Module (
mc/world.cpp
)
Block queries and raycasting:

Function	Description	Cache Usage
getBlockId(x, y, z)
Get block ID at coordinates (1.8+ only)	cls_BlockPos, mid_blockPosInit, mid_getBlockState, mid_getBlock, mid_getIdFromBlock
rayTraceBlocks(from, to, result)
Cast ray and find hit block	cls_Vec3, mid_vec3Init, mid_rayTraceBlocks, mid_getBlockPos or fid_blockX/Y/Z (1.7)
Version Handling:

1.8.x uses BlockPos objects and getBlockState()
1.7.10 uses direct blockX/blockY/blockZ fields in MovingObjectPosition
3.3 Render Module (
mc/render.cpp
)
Rendering utilities:

Function	Description	Cache Usage
getMouseSensitivity()
Get sensitivity setting	fid_gameSettings, fid_mouseSensitivity
isTargetingBlock()
Check if crosshair on block	fid_objectMouseOver, fid_typeOfHit, fid_BLOCK
isTargetingEntity()
Check if crosshair on entity	Same + fid_ENTITY
getTargetedEntityHurtTime()
Get hurt time of aimed entity	fid_entityHit, fid_hurtTime
getRenderPos()
Get camera position for W2S	fid_renderManager, fid_renderPosX/Y/Z (1.8) or static fields (1.7)
getPartialTicks()
Get interpolation value	fid_timer, fid_renderPartialTicks
Version Handling: RenderManager fields are instance fields in 1.8.x but static in 1.7.10.

3.4 Inventory Module (
mc/inventory.cpp
)
Inventory manipulation:

Function	Description	Cache Usage
getInventorySlots()
Get all 36 slots with item info	fid_inventory, mid_getItem, fid_stackSize, mid_getItemDamage
windowClick(slot, button)
Shift-click an inventory slot	fid_openContainer, fid_windowId, fid_playerController
openInventory()
Open player inventory GUI	cls_GuiInventory, constructor
closeInventory()
Close current GUI	cls_EntityPlayer, closeScreen method
isInventoryOpen()
Check if inventory GUI is open	fid_currentScreen, instanceof check
3.5 Player List Module (
mc/player_list.cpp
)
Player enumeration and visibility:

Function	Description	Cache Usage
getPlayerList()
Get all players in world	fid_playerEntities, cls_List, mid_listSize, mid_listGet, all Entity fields
canEntityBeSeen(target)
Line-of-sight check	mid_canEntityBeSeen
getLocalPlayerTeamColor()
Get team color from display name	mid_getDisplayName, mid_getFormattedText
Team Color Parsing: Extracts color code from formatted name (handles UTF-8 § encoding).

4. Integration Points
4.1 SDK_Init Changes
void SDK_Init() {
    // ... existing initialization ...
    
    // NEW: Initialize JNI cache
    if (!JniCache::Init()) {
        log_warn("JniCache::Init failed - some features may not work");
    }
    
    player.refresh();
    log_ok("SDK initialized");
}
4.2 Module Usage
Modules don't include 
jni_cache.h
 directly. They use the 
Minecraft
 class methods which internally use the cache:

// Module code (unchanged)
mc.releaseMovementKeys();  // Uses JniCache internally
mc.getPlayerList();        // Uses JniCache internally
4.3 Build System Changes
Cobble.vcxproj
 updated:

<ClCompile Include="src\sdk\jni\jni_cache.cpp" />
<ClCompile Include="src\sdk\mc\input.cpp" />
<ClCompile Include="src\sdk\mc\world.cpp" />
<ClCompile Include="src\sdk\mc\render.cpp" />
<ClCompile Include="src\sdk\mc\inventory.cpp" />
<ClCompile Include="src\sdk\mc\player_list.cpp" />
<ClInclude Include="src\sdk\jni\jni_cache.h" />
5. Performance Benefits
5.1 Before: Repeated Lookups
// OLD: Every call to getBlockId did this
jclass blockPosCls = nullptr;
Java::AssignClass("net.minecraft.util.BlockPos", blockPosCls);  // Searches classloader
jmethodID mid = env->GetMethodID(blockPosCls, "<init>", "(III)V");  // Searches vtable
// ... create object, call method ...
env->DeleteLocalRef(blockPosCls);  // Reference management
5.2 After: Cached Lookups
// NEW: IDs already cached at startup
if (!JniCache::cls_BlockPos || !JniCache::mid_blockPosInit) return 0;
jobject blockPos = env->NewObject(JniCache::cls_BlockPos, JniCache::mid_blockPosInit, x, y, z);
5.3 Estimated Savings
Operation	Before (per call)	After (per call)
Class lookup	~1-5µs	0 (cached)
Method ID lookup	~0.5-2µs	0 (cached)
Field ID lookup	~0.5-2µs	0 (cached)
Total overhead	~2-10µs	~0µs
For functions called every tick (e.g., 
getPlayerList
 at 20+ times/sec), this saves ~40-200µs per second.

6. Audit Checklist
6.1 Code Quality
 All extracted functions maintain original behavior
 No functionality removed or altered
 All JNI references properly cleaned up (LocalRef deletions)
 Exception handling preserved (ExceptionClear() after failed lookups)
 Thread safety maintained (GlobalRefs for classes)
6.2 Build Verification
 Clean build with no errors
 No linker duplicate symbol errors
 All new files added to vcxproj
 Include paths correct for new directory structure
6.3 Runtime Verification
 SDK_Init completes without errors
 JniCache::Init logs success
 All modules function correctly (manual testing needed)
 No crashes on attach/detach
6.4 Version Compatibility
 1.7.10 code paths preserved (static RenderManager fields, different method names)
 1.8.x code paths preserved (BlockPos, instance fields)
 Lunar Client testing (obfuscated names)
 Vanilla/Forge testing
7. Future Improvements
7.1 Pending Work
JniCache::Cleanup(): Call on DLL detach to release GlobalRefs
Core minecraft.cpp functions: Could be updated to use cache instead of legacy mc.fid_* fields
Dynamic cache refresh: For hot-reload scenarios
7.2 Not In Scope
Modules (aimassist, autoclicker, etc.) don't do JNI directly - they call SDK methods
No changes needed to module code
8. File Checksums (for audit verification)
File	Lines	Purpose
sdk/jni/jni_cache.h
273	Cache declarations
sdk/jni/jni_cache.cpp
609	Cache initialization
sdk/minecraft.cpp
130	Core SDK (reduced)
sdk/mc/input.cpp
283	Input handling
sdk/mc/world.cpp
174	World queries
sdk/mc/render.cpp
225	Render utilities
sdk/mc/inventory.cpp
332	Inventory operations
sdk/mc/player_list.cpp
294	Player enumeration
Total new code: ~2,320 lines (organized from original 1,854 + new cache infrastructure)

Appendix A: Cached JNI IDs Reference
Classes (25)
cls_Minecraft, cls_WorldClient, cls_EntityPlayerSP, cls_EntityClientPlayerMP
cls_Entity, cls_EntityLivingBase, cls_EntityPlayer
cls_GameSettings, cls_KeyBinding, cls_MouseHelper
cls_RenderManager, cls_MovingObjectPosition, cls_MovingObjectType
cls_BlockPos, cls_Vec3, cls_Block, cls_IBlockState
cls_GuiScreen, cls_GuiInventory, cls_Container, cls_Slot, cls_ItemStack
cls_IChatComponent, cls_List, cls_Mouse
Method IDs (40+)
Minecraft: mid_getMinecraft, mid_clickMouse
Entity: mid_isInvisible
EntityLivingBase: mid_getHealth, mid_getMaxHealth, mid_canEntityBeSeen
EntityPlayer: mid_getName, mid_getDisplayName
KeyBinding: mid_setKeyBindState, mid_unPressAllKeys, mid_getKeyCode
MouseHelper: mid_ungrabMouseCursor, mid_grabMouseCursor
World: mid_getBlockState, mid_rayTraceBlocks
BlockPos: mid_blockPosInit, mid_getX, mid_getY, mid_getZ
ItemStack: mid_getItem, mid_getItemDamage
List: mid_listSize, mid_listGet
IChatComponent: mid_getFormattedText, mid_getUnformattedText
LWJGL: mid_setGrabbed
Field IDs (80+)
Minecraft: fid_thePlayer, fid_theWorld, fid_currentScreen, fid_rightClickDelayTimer, etc.
Entity: fid_posX/Y/Z, fid_lastTickPosX/Y/Z
EntityLivingBase: fid_hurtTime
EntityPlayer: fid_inventory, fid_openContainer
GameSettings: fid_mouseSensitivity, fid_keyBindForward, etc.
RenderManager: fid_renderPosX/Y/Z (instance and static variants)
Container: fid_windowId, fid_inventorySlots
ItemStack: fid_stackSize
