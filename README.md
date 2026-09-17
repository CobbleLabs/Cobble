# Cobble

Injectable ghost client for Lunar Client (Minecraft 1.7.10 and 1.8.9). This repository produces `Cobble.dll` and `CobbleLoader.exe`. The loader authenticates against the license server, decrypts the embedded DLL, and maps it into Lunar’s `javaw.exe`. The DLL attaches to the JVM, installs an ImGui overlay, and runs the modules.

Private. Daily work is on `testing`; `main` is production.

## Layout

```
src/            DLL: modules, SDK, JNI gates, overlay, config, security
infra/ext/      imgui, MinHook, JNI headers, stb
protection/     VMProtect Ultra profile used by -Pack
reprotect/      VMProtect SDK (git submodule; do not delete)
tools/          payload encrypt, manifest sign, key helpers
docs/           architecture, development, release
cobble.json.example
build.ps1
```

`loader/` is the injector binary. It is not a template for client features. `generated/` and `cobble.json` are gitignored; copy `cobble.json.example` before building.

## Runtime

`DllMain` starts a worker thread. That thread validates the loader handshake, attaches JNI, allocates modules, loads `%APPDATA%\Cobble\config.json`, installs packet gates, and runs the loop. After one second it hooks `wglSwapBuffers` for the overlay.

Insert opens the sidebar menu. JNI and module `onUpdate` run on the worker. ImGui, `onRender`, and `onRenderMenu` run on the OpenGL thread.

Config is local-first. Cloud sync is opt-in from Settings and uses **POST** `/api/config` and `/api/config/save` with the loader session. Schema version is 2.

## Modules

| Tab | Contents |
|-----|----------|
| Combat | AutoClicker, AimAssist, Velocity, Reach, HitSelect, NoHitDelay, Criticals, WTap, Piercing, AimBow, AutoBlock, AutoRod, AntiBot, SilentAura |
| Visuals | NameTags, ESP, ItemESP, Tracers, BlockESP, Trajectories, AntiDebuff, ArrayList, DroppedInfo, Notifications, Watermark, Pings, Chams |
| Movement | Timer, TimerRange, NoSlowDown, NoJumpDelay, Sprint, InventoryMove, FastStop |
| Utility | ArmorSwitch, AutoRefill, ThrowPots, AutoPot, FastPlace, NoItemRelease, BridgeAssist, Scaffold, Clutch, Breaker, AutoTool, InvManager, FastBreak, AltManager, Teams |
| Network | Backtrack, Blink, FakeLag |
| Configs / Settings | Cloud configs; menu key, Freelook, and related client options |

ArrayList, Notifications, and Watermark are HUD pages, not `Module` instances. Scaffold and Clutch are two allocations of the same class. Freelook lives in Settings. Team detection is `FriendsManager`; the Teams module edits its policy.

How to add or remove a module, including the menu index map: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Build

Windows, Visual Studio 2022 (MSVC v143), `Release|x64` only.

```powershell
copy cobble.json.example cobble.json   # fill host, version, keys, deploy
.\build.ps1 -Build                     # Dev binaries under out/bin/Dev/
.\build.ps1 -Pack -Deploy              # production pack, SCP to the VPS, tag v<version>
```

`payload_key` in `cobble.json` must match cobble-server `PAYLOAD_KEY`. Pack requires the `reprotect` submodule and `infra/keys/private.key`. Full pipeline: [docs/UPDATE_WORKFLOW.md](docs/UPDATE_WORKFLOW.md).

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — DLL lifecycle, threads, JNI gates, config, server auth
- [Development](docs/DEVELOPMENT.md) — local build, adding and removing modules, menu indices
- [Updates and releases](docs/UPDATE_WORKFLOW.md) — branches, pack/deploy, autoupdate
