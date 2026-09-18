# Development

Work on `testing`. `main` is production. Feature branches come off `testing` and merge back into it. Releases are documented in [UPDATE_WORKFLOW.md](UPDATE_WORKFLOW.md).

This is a Windows / MSVC codebase. The Mac checkout can edit sources; it cannot run `build.ps1`.

---

## Local build

Copy `cobble.json.example` to `cobble.json` and fill `server.host`, `version`, `payload_key` (64 hex chars, must match cobble-server `PAYLOAD_KEY`), and `public_key_hex` (64 hex chars). `cobble.json` is gitignored.

```powershell
.\build.ps1 -Build
```

That is a Dev binary (`COBBLE_DEV_BUILD` defined, output `out/bin/Dev/`). It does not check for updates. There is no Debug configuration; every MSBuild pass is `Release|x64` (VS 2022, toolset v143).

`generate_config.ps1` is invoked by `build.ps1`, not by a PreBuildEvent. Opening `Cobble.sln` in Visual Studio and hitting Build requires `generated/` headers already present.

Do not delete `reprotect/`. It is the VMProtect SDK root referenced by the vcxproj and by `-Pack` / `-Obfuscate`. It is empty until `git submodule update --init`.

Vendor code under `infra/ext` (imgui, MinHook, JNI headers, stb) is not a place to add features.

---

## Source layout

```
src/
  cobble.cpp / cobble.hpp   DLL worker: init, main loop, shutdown
  main.cpp                  DllMain → CreateThread
  module/                   combat, movement, utility, visual + ModuleManager
  ui/                       overlay, menu, widgets, HUD
  sdk/                      Minecraft JNI wrappers
  jni/                      JVMTI gates, Apollo, packet interceptor
  config/                   config.json + cloud sync
  security/                 loader verify, session, integrity, heartbeat
  util/                     math, HTTP/JSON, FriendsManager, HealthPredictor
```

`cobble.hpp` holds only `cobble::context`. Module pointers are `extern` in each module header, defined `= nullptr` in the `.cpp`, and heap-allocated in `cobble.cpp`.

---

## Adding a module

Reach (`src/module/combat/reach.h`, `reach.cpp`) is the pattern.

### 1. Files

`src/module/<combat|movement|utility|visual>/<name>.h` and `.cpp`.

```cpp
#pragma once
#include "../module.h"

class Reach : public Module {
public:
    Reach();
    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        float distance = 3.6f;
    } settings;
};

extern Reach* reach;
```

In the `.cpp`: `Reach* reach = nullptr;` and `Reach::Reach() : Module("Reach")`. The constructor string is the config key and the ArrayList name.

A module with no extra fields can omit `saveSettings` / `loadSettings`. Set `renders`, `worldVisual`, `usesPlayerCache`, or `locked` in the constructor when they apply.

### 2. Project

Add both files to `Cobble.vcxproj` (`ClCompile` and `ClInclude`). There is no `.filters` file. A JNI gate gets the same treatment under `src/jni/`.

### 3. Lifecycle in `cobble.cpp`

Include the header. `new` after `moduleManager`. `registerModule`. `delete` in shutdown after overlay/JNI teardown.

Keep existing alloc order:

- AntiBot before combat modules that query it
- `scaffold = new Clutch(PlaceMode::Scaffold)`
- `friendsManager` then `teamsModule`
- Do not `registerModule(friendsManager)`

Shutdown already disables registered modules by setting `enabled = false` and calling `onDisable()` (it does not go through `toggle()`, so locks do not block eject).

### 4. Menu (two lists, one index)

Append the label to the correct vector in `ui::tabs` (`src/ui/widgets/widgets.cpp`). Add the matching `case` in `getModuleForSubtab` (`src/ui/menu/menu.cpp`) and include the header there.

Those two lists are the same index space. Inserting in the middle of one without shifting the other maps the wrong module for every later card. Search, gear, and right-click all go through `getModuleForSubtab`.

Combat currently ends **AntiBot then SilentAura**. Visuals special pages (not modules): ArrayList **6**, Notifications **8**, Watermark **9**.

### 5. Config

Override `saveSettings` / `loadSettings`. Read with `as_float` / `as_bool` / `as_int` defaults so a missing key keeps the in-memory default. Do not add `if (name == "…")` in `config.cpp`.

### 6. Descriptions

Add a row to `kModuleDescriptions` in `src/ui/widgets/widgets.cpp`. Missing entries fall back to a generic sentence. Add `kOptionDescriptions` only when a shared label (“Mode”, “Range”) needs a per-module or per-combo-value tip.

### 7. `onRenderMenu`

Use `ui::` widgets (`src/ui/widgets/widgets.hpp`). Standard layout is `ui::two_column`. Do not bind `Enabled` to `&enabled` — that skips `toggle()` / `onEnable` / `onDisable`:

```cpp
bool enabledValue = enabled;
if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
    if (enabledValue != enabled)
        setEnabled(enabledValue);
}
```

Call `ui::TickTooltip()` immediately after a widget whose value is in game ticks. Use `ui::DescribeLastItem` for buttons that are not a standard checkbox/slider/combo.

---

## Menu index map

**Combat (tab 0)**

| i | Label | Pointer |
|---|--------|---------|
| 0 | AutoClicker | `autoClicker` |
| 1 | AimAssist | `aimAssist` |
| 2 | Velocity | `velocity` |
| 3 | Reach | `reach` |
| 4 | HitSelect | `hitSelect` |
| 5 | NoHitDelay | `noHitDelay` |
| 6 | Criticals | `criticals` |
| 7 | WTap | `wTap` |
| 8 | Piercing | `piercing` |
| 9 | AutoBlock | `blockHit` (`Module` name `BlockHit`; config key `BlockHit`) |
| 10 | AntiBot | `antiBot` |
| 11 | SilentAura | `silentAura` |

**Visuals (tab 1)**

| i | Label | Pointer |
|---|--------|---------|
| 0 | NameTags | `nameTags` |
| 1 | ESP | `esp` |
| 2 | ItemESP | `itemEsp` |
| 3 | Tracers | `tracers` |
| 4 | BlockESP | `blockESP` |
| 5 | AntiDebuff | `antiDebuff` |
| 6 | ArrayList | HUD (`src/ui/hud/arraylist.*`) |
| 7 | DroppedInfo | `droppedInfo` |
| 8 | Notifications | HUD |
| 9 | Watermark | HUD |
| 10 | Pings | `pingsModule` |
| 11 | Chams | `chams` |

**Movement (tab 2):** Timer, TimerRange, NoSlowDown, NoJumpDelay, Sprint, InventoryMove, FastStop.

**Utility (tab 3):** ArmorSwitch, AutoRefill, ThrowPots, AutoPot, FastPlace, NoItemRelease, BridgeAssist, Scaffold, AutoTool, InvManager, AltManager, Teams.

**Network (tab 4):** Backtrack, Blink, FakeLag.

**Configs (tab 5)** and **Settings (tab 6)** have no modules. Freelook is a Settings checkbox (`Settings::Freelook`), ticked from `ModuleManager::update`. Camera hooks go through `BacktrackGhostGate`.

Locked modules (`locked = true` in the constructor) show a lock on the card until the experimental unlock on the config page. Currently Scaffold and FakeLag.

---

## Removing a module

Delete all of it in one change:

- `.h` / `.cpp` (and any JNI gate)
- `Cobble.vcxproj` ClCompile / ClInclude
- `cobble.cpp` include, `new`, `registerModule`, `delete`
- `ui::tabs` label **and** `getModuleForSubtab` case, then reindex later cases
- `kModuleDescriptions` / `kOptionDescriptions`
- menu.cpp include

Do not leave comments, `#if 0`, or empty vcxproj rows “for later”. `Clutch::aimAt(..., bool freshOpportunity)` is an aim flag, not a leftover of the removed Opportunity module. `HealthPredictor` is used by ESP and NameTags.

---

## Invariants

- JNI on the Cobble worker; ImGui on `wglSwapBuffers`.
- Font atlas slots 0–5 stay in that order. Do not strip slot 4. Keep FontAwesome.
- `ui::tabs` order ≡ `getModuleForSubtab`.
- Do not rewrite `loader/`.
- Do not put module pointers in `cobble.hpp`.
- Do not restore ClickGUI.
- Chams draws in the game world pass (`onRender3D` from the ghost gate), not as overlay `worldVisual`.
- Overlay font slot 5 is ArrayList Regular; HUD family pickers append after slot 5.
