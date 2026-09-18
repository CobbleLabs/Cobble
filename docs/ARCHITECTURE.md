# Architecture

Cobble is an injectable client for Lunar Client (Minecraft 1.7.10 / 1.8.9). This repository builds `Cobble.dll` and `CobbleLoader.exe`. A sibling Go service (`cobble-server`) issues licenses, session tokens, and signed updates. A sibling web app (`cobble-web`) is the user dashboard.

The loader is a separate binary. It authenticates, decrypts the embedded DLL, and maps it into Lunar’s `javaw.exe`. This document describes the DLL and how it talks to the server. Do not treat `loader/` as a template for client work.

---

## Process

```
CobbleLoader.exe  →  license check (api.cobble.vip, fallback api2.cobble.vip)
                  →  inject Cobble.dll into javaw.exe
                  →  shared-memory handshake

Cobble.dll        →  cobble::context::initialize
                  →  JNI / SDK
                  →  modules + config
                  →  packet gates + overlay
                  →  main loop until eject
```

`DllMain` (`src/main.cpp`) starts a worker thread. All client init and the JNI loop run there, not in `DllMain`.

---

## DLL lifecycle

`cobble::context::initialize` (`src/cobble.cpp`), in order:

1. `LoaderVerify::verifyOrDie` — abort if the DLL was loaded with `LoadLibrary`.
2. `SessionValidator::ValidateInjection` — abort if the loader handshake is missing or stale.
3. `LoaderValidator::RunStartupValidation` — failure marks SDK state corrupted; it does not kill the process.
4. `DllIntegrity::Initialize` — FNV-1a of `.text`, rechecked on the heartbeat thread.
5. `SessionValidator::StartHeartbeatThread` — first beat after 30s, then every ~2–3 minutes.
6. `Java::Init` / `SDK_Init` — attach to the JVM, resolve Minecraft, fill `JniCache`. Failure unloads the DLL.
7. Heap-allocate `ModuleManager`, every module, and `FriendsManager`. Register modules. `FriendsManager` is not a `Module`.
8. `ConfigManager::init` — local `%APPDATA%\Cobble\config.json`, then optional cloud fetch.
9. `WinsockHooks::Init`, `PacketInterceptor::Init`, `LunarPacketListener::Init`.
10. Main loop (`is_running`):
    - Overlay after 1s (`Overlay::Init`, MinHook on `wglSwapBuffers`).
    - Every 1s: session refresh, Apollo handshake tick, world-change check, SDK recovery.
    - Every 2ms: `player.refresh()`, `moduleManager->update()`.
    - Every 4ms: `RenderData::Update()`, and `VisualCache::Update()` if an enabled module sets `usesPlayerCache`.

`shutdown()` disables every registered module (bypassing UI locks), tears down gates and the overlay, then deletes pointers. If bytecode or backtrack teardown is incomplete, `shutdown()` returns false and the DLL stays loaded so dangling natives are not freed.

---

## Threads

| Thread | Owns |
|--------|------|
| Cobble worker | JNI, SDK, module `onUpdate`, config I/O, heartbeat client |
| OpenGL (`wglSwapBuffers`) | ImGui, `onRender` / `onRenderMenu`, ArrayList / Watermark / Notifications |
| Heartbeat | `DllIntegrity::Check`, loader re-verify, HTTP heartbeat |
| Netty / JVMTI natives | Packet gates (`OutgoingPacketGate`, Backtrack, SlowDown, Ghost, Apollo) |

Do not call Minecraft JNI from the overlay thread. Do not call ImGui from `onUpdate`. `Java::Env()` will attach the current thread; that does not make Minecraft objects safe on GL.

World-space ImGui (`Module::worldVisual`) is skipped while `RenderData::guiCoversWorld` is true. Cache FPS and that flag on the worker (`RenderData::gameFps`, `RenderData::guiCoversWorld`).

---

## Modules

Base type: `src/module/module.h`. Lifecycle: `onEnable` / `onDisable` / `onWorldChange` / `onUpdate` / `onRender` / `onRenderMenu`. Persistence: `saveSettings` / `loadSettings` for module-owned fields; `ConfigManager` writes `enabled` and `keybind`.

`ModuleManager` (`src/module/module_manager.cpp`) ticks enabled modules, dispatches keybinds, and calls `Settings::Freelook::update()` every pass. AutoRefill may run while disabled (inventory-open refill). Scaffold and Clutch are two `Clutch*` instances (`PlaceMode::Scaffold` and `PlaceMode::Clutch`).

The live menu lists and index map are in [DEVELOPMENT.md](DEVELOPMENT.md#menu-index-map).

Removed from this tree (do not resurrect as leftovers): Opportunity, Spectate, TargetHUD, Animations, ClickGUI, NetworkProbe, LunarSender, the old `Math` class, VisualTeamResolver, Freelook-as-a-Visuals-module, AutoRod.

---

## Menu and overlay

One sidebar. Insert (`VK_INSERT`) toggles it (`Menu::menuKey`). Escape closes.

Tabs live in `ui::tabs` (`src/ui/widgets/widgets.cpp`). `getModuleForSubtab` (`src/ui/menu/menu.cpp`) must match that order exactly. Visuals indices 7 / 9 / 10 are HUD pages (ArrayList, Notifications, Watermark), not `Module*`. Combat label `AutoBlock` maps to `blockHit`. Freelook is a Settings control (`Settings::Freelook`), not a module.

`HookedSwapBuffers` draws the overlay HUD, then `Menu::Render`. `Overlay::Render` calls `moduleManager->onRender()` plus ArrayList, Watermark, and Notifications.

Font atlas slots 0–5 are fixed (`src/ui/core/overlay.cpp`). Slot 4 (`notificationFont`) is still allocated even if HUD drawing uses slot 0; removing it shifts slot 5 and breaks ArrayList. FontAwesome is merged into slot 0 and is required by the sidebar.

---

## JNI and packets

`PacketInterceptor::Init` installs JVMTI bytecode hooks (`src/jni/packet/bytecode_patcher.*`) and registers natives:

| Unit | Role |
|------|------|
| `OutgoingPacketGate` | Single `sendPacket` native: SilentAura, FakeLag, Blink, pots, piercing, alt spoof, TimerRange, NoItemRelease, AimAssist rotation |
| `NoItemReleaseGate` | Drop `C07PacketPlayerDigging.RELEASE_USE_ITEM` while the module holds use |
| `BlinkGate` | Hold selected outgoing packets |
| `BacktrackGate` | Hold incoming entity movement; replay on the client thread |
| `SlowDownGate` | Restore move input inside `EntityLivingBase.onLivingUpdate` (the 2ms loop cannot) |
| `BacktrackGhostGate` | Lagged entity render; also Freelook camera begin/ready |
| `AltManagerGate` | Rewrite login-start / session |
| `ApolloPacketObserver` | Incoming Apollo teams, nametags, effects |
| `LunarPacketListener` | Lunar nametag update/override |
| `WinsockHooks` | Process-wide `WSASend`/`WSARecv` stall for Blink |

SDK wrappers: `src/sdk/core/minecraft.*`, `src/sdk/player/*`, `src/sdk/mc/*`, `src/sdk/jni/jni_cache.*`. Geometry: `src/util/math/geometry.h` and `worldToScreen.cpp`.

Team policy is `FriendsManager` (`src/util/team/`). The Teams module is the editor for that policy, not a second detector. `HealthPredictor` is used by ESP and NameTags.

---

## Config

Path: `%APPDATA%\Cobble\config.json`. Schema version **2**. Cloud sync is off until Settings enables it.

Startup: parse local (SEH-guarded) → migrate → apply → optional **POST** `/api/config` with the loader session → merge → save. Menu close and `onSettingChanged` write disk immediately and debounce **POST** `/api/config/save` by 400ms.

Module-specific keys belong in `saveSettings` / `loadSettings`. Do not add per-name branches in `config.cpp`. Stale keys stripped on migrate include `modules.Freelook` and `guiMode` (unread). User configs live in `%APPDATA%\Cobble\configs\`.

---

## Security that actually runs

| Check | When | Failure |
|-------|------|---------|
| `LoaderVerify::verifyOrDie` | boot | `ExitProcess` |
| `SessionValidator::ValidateInjection` | boot | `ExitProcess` |
| `LoaderValidator::RunStartupValidation` | boot | `markCorrupted` |
| `DllIntegrity` (FNV-1a `.text`) | boot + heartbeat | session invalid after repeated misses |
| Heartbeat HTTP | ~2–3 min | session invalid |
| `sdk::internal::gateAB` / `gateD` | every module tick / render | skip work |

The main loop does not hash `.text` every tick. Honeypot exports exist for reverse-engineering bait; they are not on the init path.

Ed25519 public key and API host are baked in at compile time from `cobble.json` via `generate_config.ps1`. The AES payload key used to encrypt the DLL into the loader must match cobble-server `PAYLOAD_KEY`. At runtime the loader fetches that key from `POST /api/loader/payload-key`.

---

## License server

Repo: `Documents/Cobble/cobble-server`. Go module path is still `license-server`. PostgreSQL. Docker on the VPS, nginx in front (`api.cobble.vip` / `api2.cobble.vip`).

Two auth systems:

- **Web:** JWT. Live type is `models.LoginResponse` (not `AuthResponse`). `RequireAuth` reloads `GetUserByID` for ban and email verification.
- **Loader / DLL:** HWID + session token + one-time `injection_secret` (5 min). `POST /api/loader/login` or `POST /api/verify` issues a signed envelope. `POST /api/dll/validate` consumes the secret and issues a new session. Heartbeat rotates the session.

Config, pings, and public configs authenticate with the loader session (`hwid` + `token`), not JWT. Pings use `authLoaderSession` in `internal/handlers/pings.go`.

Updates: `GET /api/update/manifest` returns raw `manifest.json` + `manifest.sig` as base64 (not an unmarshaled struct). `GET /api/update/loader` serves `releases/latest/CobbleLoader.exe` (JWT or `?hwid=`). `GET /api/update/dll` serves `Cobble.dll`. Clicker is optional and not produced by this repo’s `build.ps1`.

Release mechanics: [UPDATE_WORKFLOW.md](UPDATE_WORKFLOW.md).
