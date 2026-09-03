#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "menu.h"
#include "../../cobble.hpp"
#include "../../config/config.hpp"
#include "../../spectate/spectate.hpp"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/aimbow.h"
#include "../../module/combat/antibot.h"
#include "../../module/combat/killaura.h"
#include "../../module/combat/autoclicker.h"
#include "../../module/combat/autorod.h"
#include "../../module/combat/blockhit.h"
#include "../../module/combat/backtrack.h"
#include "../../module/combat/hitselect.h"
#include "../../module/combat/nohitdelay.h"
#include "../../module/combat/criticals.h"
#include "../../module/combat/piercing.h"
#include "../../module/combat/wtap.h"

#include "../../module/combat/reach.h"
#include "../../module/combat/velocity.h"
#include "../../module/module_manager.h"
#include "../../module/movement/inventorymove.h"
#include "../../module/movement/nojumpdelay.h"
#include "../../module/movement/no_slow_down.h"
#include "../../module/movement/blink.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timer.h"
#include "../../module/movement/timerrange.h"
#include "../../module/movement/sprint.h"
#include "../../module/utility/armorswitch.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/altmanager.h"
#include "../../module/utility/autotool.h"
#include "../../module/utility/breaker.h"
#include "../../module/utility/bridgeassist.h"
#include "../../module/utility/clutch.h"
#include "../../module/utility/fastplace.h"
#include "../../module/utility/fastbreak.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/inventorymanager.h"
#include "../../module/utility/teams.h"
#include "../../module/utility/throwpots.h"
#include "../../module/utility/autopot.h"
#include "../../module/visual/blockesp.h"
#include "../../module/visual/chams.h"
#include "../../module/visual/esp.h"
#include "../../module/visual/item_esp.h"
#include "../../module/visual/nametags.h"
#include "../../module/visual/anti_debuff.h"
#include "../../module/visual/tracers.h"
#include "../../module/visual/trajectories.h"
#include "../../module/visual/dropped_info.h"
#include "../../sdk/core/minecraft.h"
#include "../../security/session_validator.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/http.hpp"
#include "../../util/team/friends_manager.h"
#include "../hud/arraylist.h"
#include "../hud/watermark.h"
#include "../../module/visual/target_hud.h"
#include "../../module/visual/pings.h"
#include "../hud/notifications.h"
#include "../widgets/icons.hpp"
#include "../widgets/theme.hpp"
#include "../widgets/widgets.hpp"
#include "menu_nav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <gl/GL.h>

#include "../../sdk/player/render_data.h"

// Legacy <gl/GL.h> (GL 1.1) predates GL_CLAMP_TO_EDGE (GL 1.2); the runtime driver
// supports it, so define the token if the header didn't.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// One-shot refresh trigger for Configs page (set when user enters the tab).
static bool g_configsNeedsRefresh = false;

// Module lookup by tab and subtab index
// Returns the module that corresponds to the current subtab
Module* getModuleForSubtab(int tabIdx, int subtabIdx) {
    // Combat Tab (0)
    if (tabIdx == 0) {
        switch (subtabIdx) {
            case 0:
                return autoClicker;
            case 1:
                return aimAssist;
            case 2:
                return velocity;
            case 3:
                return reach;
            case 4:
                return hitSelect;
            case 5:
                return noHitDelay;
            case 6:
                return criticals;
            case 7:
                return wTap;
            case 8:
                return piercing;
            case 9:
                return aimBow;
            case 10:
                return blockHit;
            case 11:
                return killAura;
            case 12:
                return autoRod;
            case 13:
                return antiBot;
        }
    }
    // Visuals Tab (1)
    else if (tabIdx == 1) {
        switch (subtabIdx) {
            case 0:
                return nameTags;
            case 1:
                return esp;
            case 2:
                return itemEsp;
            case 3:
                return tracers;
            case 4:
                return blockESP;
            case 5:
                return trajectories;
            case 6:
                return antiDebuff;
            case 7:
                return nullptr;  // ArrayList - special handling
            case 8:
                return droppedInfo;  // DroppedInfo
            case 9:
                return nullptr;  // Notifications - special handling
            case 10:
                return nullptr;  // Watermark - special handling
            case 11:
                return targetHud;
            case 12:
                return nullptr;  // ClickGUI - special handling (mode toggle)
            case 13:
                return pingsModule;
            case 14:
                return chams;
        }
    }
    // Movement Tab (2): Timer, TimerRange, NoSlowDown, NoJumpDelay, Sprint, InventoryMove
    else if (tabIdx == 2) {
        switch (subtabIdx) {
            case 0:
                return timerModule;
            case 1:
                return timerRange;
            case 2:
                return noSlowDown;
            case 3:
                return noJumpDelay;
            case 4:
                return sprint;
            case 5:
                return inventoryMove;
        }
    }
    // Utility Tab (3)
    else if (tabIdx == 3) {
        switch (subtabIdx) {
            case 0:
                return armorSwitch;
            case 1:
                return autoRefill;
            case 2:
                return throwPots;
            case 3:
                return autoPot;
            case 4:
                return fastPlace;
            case 5:
                return noItemRelease;
            case 6:
                return bridgeAssist;
            case 7:
                return scaffold;
            case 8:
                return clutch;
            case 9:
                return breaker;
            case 10:
                return autoTool;
            case 11:
                return inventoryManager;
            case 12:
                return fastBreak;
            case 13:
                return altManager;
            case 14:
                return teamsModule;
        }
    }
    // Network Tab (4)
    else if (tabIdx == 4) {
        switch (subtabIdx) {
            case 0:
                return backtrack;
            case 1:
                return blink;
            case 2:
                return fakeLag;
        }
    }
    // Configs/Settings tabs - no module, handled specially

    return nullptr;
}

// Settings tab content - special case (not a module)
static void renderSettingsPage() {
    float halfWidth =
        (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
    float bottomRowHeight = ui::s(100);
    float topRowHeight = ImGui::GetContentRegionAvail().y - bottomRowHeight - ui::s(10);

    // General column (accent color + cloud sync)
    ImGui::BeginGroup();
    {
        ui::begin_child("General", {halfWidth, topRowHeight});
        {
            ui::Binder("Menu Key", &Menu::menuKey);

            ImGui::Spacing();

            ui::color("Accent Color", g_SchemeColor);
            ui::Checkbox("Accent Rainbow", &ui::theme::accentRainbow);
            if (ui::theme::accentRainbow) {
                const char* rbTypes[] = {"Flow", "Solid", "Fast", "Wide"};
                ui::Combo("Rainbow", &ui::theme::rainbowType, rbTypes, 4);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Cloud Sync toggle
            bool syncEnabled = ConfigManager::get().isSyncEnabled();
            if (ui::Checkbox("Cloud Sync", &syncEnabled)) {
                ConfigManager::get().setSyncEnabled(syncEnabled);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Sync config across devices");
            ImGui::PopStyleColor();
        }
        ui::end_child();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // Friends column
    ImGui::BeginGroup();
    {
        ui::begin_child("Friends", {halfWidth, topRowHeight});
        {
            if (friendsManager) {
                // Add friend input
                static char friendInput[64] = "";
                static std::string lastAddStatus = "";
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ui::s(60));
                bool enterPressed = ImGui::InputText("##addfriend", friendInput, sizeof(friendInput),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Add", ImVec2(ui::s(50), 0)) || enterPressed) && friendInput[0] != '\0') {
                    std::string inputName = friendInput;
                    bool added = false;

                    // Step 1: Try Mojang API to get real UUID from username
                    // This is the CORRECT way - user types real Mojang name
                    auto mojangProfile = http::lookupMojangUUID(inputName);
                    if (mojangProfile.found) {
                        friendsManager->addFriendByUUID(mojangProfile.uuid, mojangProfile.name);
                        lastAddStatus = "Added '" + mojangProfile.name + "' (Mojang)";
                        log_debug("[Friends] Mojang API: %s -> %s", inputName.c_str(), mojangProfile.uuid.c_str());
                        added = true;
                    }

                    // Step 2: If Mojang fails, try to find in current world
                    if (!added) {
                        PlayerInfo found = mc.findPlayerByName(inputName);
                        if (!found.uuid.empty()) {
                            friendsManager->addFriendByUUID(found.uuid, inputName);
                            lastAddStatus = "Added '" + inputName + "' (World)";
                            log_debug("[Friends] World scan: %s -> %s", inputName.c_str(), found.uuid.c_str());
                            added = true;
                        }
                    }

                    // Step 3: Fallback to name-only (not recommended)
                    if (!added) {
                        friendsManager->addFriend(inputName);
                        lastAddStatus = "'" + inputName + "' not found (name only)";
                        log_debug("[Friends] Fallback to name: %s", inputName.c_str());
                    }

                    friendInput[0] = '\0';
                }

                // Friends list - show displayName as user typed it
                auto friends = friendsManager->getFriends();
                if (!friends.empty()) {
                    ImGui::Separator();
                    std::string uuidToRemove;
                    std::string nameToRemove;
                    for (const auto& f : friends) {
                        ImGui::Text("%s", f.displayName.c_str());
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ui::s(20));
                        ImGui::PushID((f.uuid.empty() ? f.displayName : f.uuid).c_str());
                        if (ImGui::SmallButton("X")) {
                            if (f.uuid.empty())
                                nameToRemove = f.displayName;
                            else
                                uuidToRemove = f.uuid;
                        }
                        ImGui::PopID();
                    }
                    if (!uuidToRemove.empty())
                        friendsManager->removeFriendByUUID(uuidToRemove);
                    if (!nameToRemove.empty())
                        friendsManager->removeFriend(nameToRemove);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("No friends added");
                    ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Team detection used to be this single checkbox. It is a module now
                // (Utility > Teams) with a switch per source, so pointing there beats
                // keeping a second control that writes the same flag.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Teams moved to Utility > Teams");
                ImGui::PopStyleColor();

                ImGui::Spacing();

                // Quick-add friend feature
                ui::CheckboxBind("Quick Add", &friendsManager->quickAddEnabled, &friendsManager->quickAddKey);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Press key while looking at player");
                ImGui::PopStyleColor();

                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::PopStyleColor();
            }
        }
        ui::end_child();
    }
    ImGui::EndGroup();

    // Pin to bottom
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - bottomRowHeight);

    ImGui::BeginGroup();
    {
        ui::begin_child("Actions", {ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2, bottomRowHeight});
        {
            float buttonWidth = (ImGui::GetContentRegionAvail().x - ui::s(10)) / 2;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.08f, 0.1f, 1.0f));

            if (ImGui::Button("PANIC", ImVec2(buttonWidth, ui::s(32)))) {
                if (moduleManager) {
                    for (auto mod : moduleManager->getModules()) {
                        if (mod) {
                            mod->setEnabled(false);
                        }
                    }
                }
                // Persist the all-off state immediately so PANIC survives a
                // hard close, not just a clean menu-close.
                ConfigManager::get().onSettingChanged();
            }

            ImGui::PopStyleColor(3);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.08f, 0.1f, 1.0f));

            if (ImGui::Button("EJECT CLIENT", ImVec2(buttonWidth, ui::s(32)))) {
                cobble::context::is_running = false;
                Menu::isOpen = false;
            }

            ImGui::PopStyleColor(3);
        }
        ui::end_child();
    }
    ImGui::EndGroup();
}

// Spectate tab content - Cobble Spectate: privacy, player browser, live view.
// Shelved for a future update; gated behind COBBLE_SPECTATE_ENABLED so the
// full implementation stays in the tree without surfacing in the UI.
#if COBBLE_SPECTATE_ENABLED
static void renderSpectatePage() {
    auto& sm = SpectateManager::get();

    static double specLastPoll = 0.0;
    constexpr double kSpecPoll = 5.0;
    static std::string specSelected;         // selected target username
    static bool showOtherSpectators = true;  // include players who are themselves spectating

    double nowT = ImGui::GetTime();
    if (nowT - specLastPoll >= kSpecPoll) {
        sm.refreshListAsync();
        specLastPoll = nowT;
    }

    // Privacy toggles + "who is watching you" counter. Set every frame (atomic).
    {
        bool allow = sm.allowSpectate();
        ui::Checkbox("Allow others to spectate me", &allow);
        sm.setAllowSpectate(allow);

        bool cfg = sm.allowConfigView();
        ui::Checkbox("Allow spectators to view my config", &cfg);
        sm.setAllowConfigView(cfg);

        if (sm.allowSpectate()) {
            ImGui::SameLine();
            ImGui::Text("     Spectators watching you: %d", sm.spectatorCount());
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float colW =
        (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
    const float colH = ImGui::GetContentRegionAvail().y;

    // ── Active spectate session: live telemetry view + entity radar ──
    if (sm.isSpectating()) {
        spectate::SpectateSnapshot snap;
        bool have = sm.getLatestSnapshot(snap);

        ImGui::BeginGroup();
        ui::begin_child("SpectateView", {colW, colH});
        {
            ImGui::Text("Spectating: %s", sm.currentTarget().c_str());
            ImGui::SameLine();
            {
                // Live indicator: a growing frame count means frames are flowing.
                bool live = (sm.framesReceived() % 2) == 0;
                ImGui::TextColored(sm.framesReceived() > 0 ? (live ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(0.3f, 0.8f, 0.3f, 1))
                                                           : ImVec4(0.9f, 0.5f, 0.3f, 1),
                                   "  [frames: %u]", sm.framesReceived());
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (have) {
                ImGui::Text("Health: %.1f", snap.health);
                ImGui::Text("Pos: %.0f, %.0f, %.0f", snap.x, snap.y, snap.z);
                if (!snap.activity.empty())
                    ImGui::Text("Activity: %s", snap.activity.c_str());
                ImGui::Text("Entities: %d   Blocks: %d", (int)snap.entities.size(), (int)snap.blocks.size());
            } else {
                ImGui::TextWrapped("Waiting for frames...");
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (ImGui::Button("Stop Spectating", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                sm.stopSpectating();
        }
        ui::end_child();
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ui::begin_child("SpectateRadar", {colW, colH});
        {
            static int specCamMode = 0;  // 0 = third person, 1 = first person
            static float freeYaw = 0.0f, freePitch = 0.0f;
            if (ImGui::SmallButton(specCamMode == 1 ? "View: 1st" : "View: 3rd"))
                specCamMode ^= 1;
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                freeYaw = 0.0f;
                freePitch = 0.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("drag = freelook");
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float side = avail.x < avail.y ? avail.x : avail.y;
            if (side < ui::s(80))
                side = ui::s(80);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 vc = ImVec2(p0.x + side * 0.5f, p0.y + side * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(24, 26, 32, 255), 4.0f);
            dl->AddRect(p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
            dl->PushClipRect(p0, ImVec2(p0.x + side, p0.y + side), true);

            if (have) {
                // Third-person perspective camera: sits behind the host along its
                // look vector, looking the same way. Real 3D projection.
                // Freelook: drag inside the panel to orbit the camera around the
                // host's shared scene.
                if (ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + side, p0.y + side)) && ImGui::IsMouseDragging(0)) {
                    ImVec2 dd = ImGui::GetMouseDragDelta(0);
                    freeYaw += dd.x * 0.4f;
                    freePitch = std::clamp(freePitch + dd.y * 0.4f, -80.0f, 80.0f);
                    ImGui::ResetMouseDragDelta(0);
                }
                const float d2r = 3.14159265f / 180.0f;
                const float cyaw = (snap.yaw + freeYaw) * d2r;
                const float cpitch = std::clamp(snap.pitch + freePitch, -89.0f, 89.0f) * d2r;
                const float fx = -std::sin(cyaw) * std::cos(cpitch);
                const float fy = -std::sin(cpitch);
                const float fz = std::cos(cyaw) * std::cos(cpitch);
                float rx = -fz, rz = fx;  // right = normalize(cross(fwd, up))
                float rl = std::sqrt(rx * rx + rz * rz);
                if (rl < 1e-4f)
                    rl = 1.0f;
                rx /= rl;
                rz /= rl;
                const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;  // up = cross(right, fwd)
                const float camDist = specCamMode == 1 ? 0.2f : 4.5f, focal = side * 0.85f;
                const double ehx = snap.x, ehy = snap.y + 1.62, ehz = snap.z;  // host eye
                const double cpx = ehx - fx * camDist, cpy = ehy - fy * camDist, cpz = ehz - fz * camDist;

                auto project = [&](double wx, double wy, double wz, float& ox, float& oy, float& depth) -> bool {
                    double relx = wx - cpx, rely = wy - cpy, relz = wz - cpz;
                    double cz = relx * fx + rely * fy + relz * fz;
                    if (cz < 0.1)
                        return false;
                    double cx = relx * rx + relz * rz;              // right has no y
                    double cy = relx * ux + rely * uy + relz * uz;  // up
                    ox = vc.x + (float)(cx / cz) * focal;
                    oy = vc.y - (float)(cy / cz) * focal;
                    depth = (float)cz;
                    return true;
                };

                struct BV { float x, y, depth; ImU32 col; };
                std::vector<BV> vis;
                vis.reserve(4096);
                for (const auto& b : snap.blocks) {
                    double wx = (double)snap.originX + b.dx, wy = (double)snap.originY + b.dy,
                           wz = (double)snap.originZ + b.dz;
                    float ox, oy, od;
                    if (!project(wx, wy, wz, ox, oy, od))
                        continue;
                    if (ox < p0.x - 30 || ox > p0.x + side + 30 || oy < p0.y - 30 || oy > p0.y + side + 30)
                        continue;
                    ImU32 col = IM_COL32(60 + (b.id * 37) % 170, 60 + (b.id * 61) % 170, 60 + (b.id * 97) % 170, 255);
                    vis.push_back({ox, oy, od, col});
                    if (vis.size() >= 5000)
                        break;
                }
                std::sort(vis.begin(), vis.end(), [](const BV& a, const BV& b) { return a.depth > b.depth; });
                for (const auto& v : vis) {
                    float s = focal / v.depth * 0.55f;
                    if (s < 1.0f) s = 1.0f;
                    if (s > 42.0f) s = 42.0f;
                    dl->AddRectFilled(ImVec2(v.x - s * 0.5f, v.y - s * 0.5f), ImVec2(v.x + s * 0.5f, v.y + s * 0.5f),
                                      v.col);
                }

                // Host avatar (feet -> head).
                float fxs, fys, fdd, hxs, hys, hdd;
                if (project(snap.x, snap.y, snap.z, fxs, fys, fdd) &&
                    project(snap.x, snap.y + 1.8, snap.z, hxs, hys, hdd)) {
                    float lw = focal / fdd * 0.30f;
                    dl->AddLine(ImVec2(fxs, fys), ImVec2(hxs, hys), IM_COL32(90, 200, 255, 255), lw < 2 ? 2 : lw);
                    dl->AddCircleFilled(ImVec2(hxs, hys), lw < 3 ? 3 : lw * 0.6f, IM_COL32(120, 210, 255, 255));
                    std::string tn = sm.currentTarget();
                    if (!tn.empty()) {
                        ImVec2 tsz = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0, tn.c_str());
                        dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(hxs - tsz.x * 0.5f, hys - 18),
                                    IM_COL32(120, 210, 255, 255), tn.c_str());
                    }
                }

                // Entities.
                for (const auto& e : snap.entities) {
                    float ox, oy, od;
                    if (!project(e.x, e.y, e.z, ox, oy, od))
                        continue;
                    float s = focal / od * 0.4f;
                    if (s < 2.0f) s = 2.0f;
                    if (s > 22.0f) s = 22.0f;
                    ImU32 col = e.type == 0 ? IM_COL32(255, 90, 90, 255) : IM_COL32(255, 210, 90, 255);
                    dl->AddCircleFilled(ImVec2(ox, oy), s, col);
                    if (e.type == 0 && !e.name.empty()) {
                        ImVec2 tsz = ImGui::GetFont()->CalcTextSizeA(12.0f, FLT_MAX, 0, e.name.c_str());
                        dl->AddText(ImGui::GetFont(), 12.0f, ImVec2(ox - tsz.x * 0.5f, oy - s - 14),
                                    IM_COL32(255, 255, 255, 235), e.name.c_str());
                    }
                }
            }
            dl->PopClipRect();
            ImGui::Dummy(ImVec2(side, side));
        }
        ui::end_child();
        ImGui::EndGroup();
        return;
    }

    // ── Not spectating: player browser ──
    auto browse = sm.getBrowseState();
    bool busy = (browse.status == SpectateManager::BrowseState::Loading);

    std::vector<const SpectateManager::BrowseEntry*> visible;
    for (const auto& e : browse.entries) {
        if (!showOtherSpectators && e.isSpectator)
            continue;
        visible.push_back(&e);
    }
    const SpectateManager::BrowseEntry* sel = nullptr;
    for (auto* e : visible) {
        if (e->username == specSelected) {
            sel = e;
            break;
        }
    }
    if (!sel)
        specSelected.clear();

    ImGui::BeginGroup();
    ui::begin_child("SpectateList", {colW, colH});
    {
        ui::Checkbox("Show other spectators", &showOtherSpectators);
        ImGui::Spacing();
        if (visible.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", browse.entries.empty() ? "No players available to spectate." : "");
            ImGui::PopStyleColor();
        }
        const float cardH = ui::s(46);
        const float pad = ui::s(6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (auto* e : visible) {
            ImGui::PushID(e->username.c_str());
            ImVec2 cp = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##srow", e->username == specSelected, 0, ImVec2(0, cardH)))
                specSelected = e->username;
            dl->AddText(ImVec2(cp.x + pad, cp.y + pad), ImGui::GetColorU32(ImGuiCol_Text), e->username.c_str());
            std::string sub = (e->activity.empty() ? std::string("In game") : e->activity) + "   \xC2\xB7   " +
                              std::to_string(e->spectatorCount) + " watching";
            dl->AddText(ImVec2(cp.x + pad, cp.y + pad + ui::s(20)), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        sub.c_str());
            ImGui::PopID();
        }
    }
    ui::end_child();
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ui::begin_child("SpectateDetail", {colW, colH});
    {
        if (sel) {
            ImGui::Text("%s", sel->username.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", sel->activity.empty() ? "In game" : sel->activity.c_str());
            ImGui::Text("%d watching", sel->spectatorCount);
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (ImGui::Button("Spectate", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy)
                sm.startSpectatingAsync(sel->username);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Select a player to spectate.");
            ImGui::PopStyleColor();
        }
        if (!browse.message.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", browse.message.c_str());
            ImGui::PopStyleColor();
        }
    }
    ui::end_child();
    ImGui::EndGroup();
}
#endif // COBBLE_SPECTATE_ENABLED

// Configs tab content - special case (not a module)
static void renderConfigsPage() {
    static bool didInit = false;
    static std::vector<std::string> configs;
    static int selected = -1;
    static char nameBuf[64] = "";
    static double lastPollTime = 0.0;
    constexpr double kPollIntervalSec = 6.0;

    auto refresh = [&]() {
        std::string selectedName;
        if (selected >= 0 && selected < (int)configs.size())
            selectedName = configs[selected];

        configs = ConfigManager::get().listUserConfigs();

        selected = -1;
        if (!selectedName.empty()) {
            for (int i = 0; i < (int)configs.size(); i++) {
                if (configs[i] == selectedName) {
                    selected = i;
                    break;
                }
            }
        }
    };

    if (!didInit) {
        refresh();
        didInit = true;
    }

    // Refresh on tab entry or periodic polling.
    if (g_configsNeedsRefresh) {
        refresh();
        g_configsNeedsRefresh = false;
        lastPollTime = ImGui::GetTime();
    } else {
        double now = ImGui::GetTime();
        if (now - lastPollTime >= kPollIntervalSec) {
            refresh();
            lastPollTime = now;
        }
    }

    // View toggle: Local vs Public. Only one renders at a time so the single
    // two_column fills the page height normally (stacking two would push the
    // second off-screen, since two_column expands to all available height).
    static int configView = 0;  // 0 = local, 1 = public
    static int prevConfigView = -1;
    {
        float halfW = (ImGui::GetContentRegionAvail().x - GImGui->Style.ItemSpacing.x) / 2;
        auto viewBtn = [&](const char* label, int idx) {
            bool active = (configView == idx);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, GetSchemeColor());
            if (ImGui::Button(label, ImVec2(halfW, 0)))
                configView = idx;
            if (active)
                ImGui::PopStyleColor();
        };
        viewBtn("Local", 0);
        ImGui::SameLine();
        viewBtn("Public", 1);
    }
    ImGui::Spacing();

    // Two fixed-width columns via begin_child (matches the Settings page). Unlike
    // ui::two_column, this makes GetContentRegionAvail() report each column's own
    // width, so the listbox/buttons/inputs below size to their column instead of
    // spilling across the whole page.
    const float colHalfWidth =
        (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
    const float colHeight = ImGui::GetContentRegionAvail().y;

    if (configView == 0) {
        ImGui::BeginGroup();
        {
            ui::begin_child("Saved", {colHalfWidth, colHeight});
            {
                if (ImGui::BeginListBox("##configlist", ImGui::GetContentRegionAvail())) {
                    for (int i = 0; i < (int)configs.size(); i++) {
                        const bool isSel = (i == selected);
                        if (ImGui::Selectable(configs[i].c_str(), isSel)) {
                            selected = i;
                            strncpy_s(nameBuf, sizeof(nameBuf), configs[i].c_str(), _TRUNCATE);
                        }
                    }
                    ImGui::EndListBox();
                }
            }
            ui::end_child();
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        {
            ui::begin_child("Actions", {colHalfWidth, colHeight});
            {
                ImGui::Text("Name");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputText("##configname", nameBuf, sizeof(nameBuf));

                ImGui::Spacing();

                std::string err;
                std::string n =
                    nameBuf[0] != '\0'
                        ? std::string(nameBuf)
                        : (selected >= 0 && selected < (int)configs.size() ? configs[selected] : std::string());

                if (ImGui::Button("Save", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    if (n.empty()) {
                        NotificationManager::addSystem("Error: enter a name");
                    } else if (ConfigManager::get().saveUserConfig(n, &err)) {
                        NotificationManager::addSystem("Saved config: " + n);
                        refresh();
                    } else {
                        NotificationManager::addSystem("Error: " + (err.empty() ? std::string("save failed") : err));
                    }
                }

                if (ImGui::Button("Load", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    if (n.empty()) {
                        NotificationManager::addSystem("Error: select a config");
                    } else if (ConfigManager::get().loadUserConfig(n, &err)) {
                        NotificationManager::addSystem("Loaded config: " + n);
                    } else {
                        NotificationManager::addSystem("Error: " + (err.empty() ? std::string("load failed") : err));
                    }
                }

                if (ImGui::Button("Delete", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    if (n.empty()) {
                        NotificationManager::addSystem("Error: select a config");
                    } else if (ConfigManager::get().deleteUserConfig(n, &err)) {
                        NotificationManager::addSystem("Deleted config: " + n);
                        if (selected >= 0 && selected < (int)configs.size() && configs[selected] == n)
                            selected = -1;
                        if (std::string(nameBuf) == n)
                            nameBuf[0] = '\0';
                        refresh();
                    } else {
                        NotificationManager::addSystem("Error: " +
                                                       (err.empty() ? std::string("delete failed") : err));
                    }
                }
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    }

    // ── Community configs (browse / detail / publish) ─────────────────────
    static char pubNameBuf[64] = "";
    static char pubDescBuf[128] = "";
    static std::string pubSelectedId;  // selection by id: survives re-sort/refresh/filter
    static bool pubAnonymous = false;  // publish author choice
    static int pubFilter = 0;          // 0 = Browse (all), 1 = Mine (owned)
    static double pubLastPoll = 0.0;
    constexpr double kPubPollIntervalSec = 15.0;

    // Fetch when entering the Public view, then poll so others' uploads appear.
    if (configView == 1 && prevConfigView != 1) {
        ConfigManager::get().refreshPublicConfigsAsync();
        pubLastPoll = ImGui::GetTime();
    }
    prevConfigView = configView;

    if (configView != 1)
        return;

    if (ImGui::GetTime() - pubLastPoll >= kPubPollIntervalSec) {
        ConfigManager::get().refreshPublicConfigsAsync();
        pubLastPoll = ImGui::GetTime();
    }

    auto browse = ConfigManager::get().getBrowseState();
    bool busy = (browse.status == ConfigManager::BrowseState::Loading);

    // Browse / Mine filter (same styled-button pattern as the Local/Public toggle).
    {
        float halfW = (ImGui::GetContentRegionAvail().x - GImGui->Style.ItemSpacing.x) / 2;
        auto filterBtn = [&](const char* label, int idx) {
            bool active = (pubFilter == idx);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, GetSchemeColor());
            if (ImGui::Button(label, ImVec2(halfW, 0)))
                pubFilter = idx;
            if (active)
                ImGui::PopStyleColor();
        };
        filterBtn("Browse", 0);
        ImGui::SameLine();
        filterBtn("Mine", 1);
    }
    ImGui::Spacing();

    // Visible subset (apply the Mine filter). Pointers into `browse` (frame-stable).
    std::vector<const ConfigManager::PublicConfigEntry*> visible;
    for (const auto& e : browse.entries) {
        if (pubFilter == 1 && !e.owned)
            continue;
        visible.push_back(&e);
    }

    // Resolve current selection; clear it if the id vanished (e.g. just deleted).
    const ConfigManager::PublicConfigEntry* sel = nullptr;
    for (auto* e : visible) {
        if (e->id == pubSelectedId) {
            sel = e;
            break;
        }
    }
    if (!sel)
        pubSelectedId.clear();

    const float pubColWidth =
        (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
    const float pubColHeight = ImGui::GetContentRegionAvail().y;

    // Left: scrollable list of config cards.
    ImGui::BeginGroup();
    {
        ui::begin_child("Browse Public", {pubColWidth, pubColHeight});
        {
            if (visible.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", pubFilter == 1
                                             ? "You haven't published any configs."
                                             : (browse.entries.empty() ? "No configs found." : ""));
                ImGui::PopStyleColor();
            }

            const float cardH = ui::s(62);
            const float pad = ui::s(6);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (auto* e : visible) {
                ImGui::PushID(e->id.c_str());
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                bool selected = (e->id == pubSelectedId);
                if (ImGui::Selectable("##card", selected, 0, ImVec2(0, cardH)))
                    pubSelectedId = e->id;

                ImU32 nameCol = ImGui::GetColorU32(ImGuiCol_Text);
                ImU32 subCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                dl->AddText(ImVec2(p0.x + pad, p0.y + pad), nameCol, e->name.c_str());
                std::string meta = "by " + e->owner + "  \xC2\xB7  " + std::to_string(e->downloads) + " downloads";
                dl->AddText(ImVec2(p0.x + pad, p0.y + pad + ui::s(20)), subCol, meta.c_str());
                if (!e->description.empty()) {
                    std::string snip = e->description.substr(0, 48);
                    if (e->description.size() > 48)
                        snip += "...";
                    dl->AddText(ImVec2(p0.x + pad, p0.y + pad + ui::s(38)), subCol, snip.c_str());
                }
                ImGui::PopID();
            }
        }
        ui::end_child();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // Right: detail of the selected card + publish panel.
    ImGui::BeginGroup();
    {
        ui::begin_child("Detail", {pubColWidth, pubColHeight});
        {
            if (sel) {
                ImGui::TextWrapped("%s", sel->name.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                std::string meta = "by " + sel->owner + "  \xC2\xB7  " + std::to_string(sel->downloads) + " downloads";
                ImGui::TextWrapped("%s", meta.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
                if (!sel->description.empty())
                    ImGui::TextWrapped("%s", sel->description.c_str());
                ImGui::Spacing();

                if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy)
                    ConfigManager::get().downloadAndApplyPublicConfigAsync(sel->id);
                if (sel->owned) {
                    if (ImGui::Button("Delete", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy) {
                        ConfigManager::get().deletePublicConfigAsync(sel->id);
                        pubSelectedId.clear();
                    }
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Select a config to view details.");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Publish current settings");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##pubname", "Name", pubNameBuf, sizeof(pubNameBuf));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##pubdesc", "Description", pubDescBuf, sizeof(pubDescBuf));

            // Author selector: Anonymous | <cobble user>.
            std::string uname = SessionValidator::GetUsername();
            if (uname.empty())
                uname = "My account";
            {
                float halfW = (ImGui::GetContentRegionAvail().x - GImGui->Style.ItemSpacing.x) / 2;
                auto authorBtn = [&](const char* label, bool anon) {
                    bool active = (pubAnonymous == anon);
                    if (active)
                        ImGui::PushStyleColor(ImGuiCol_Button, GetSchemeColor());
                    if (ImGui::Button(label, ImVec2(halfW, 0)))
                        pubAnonymous = anon;
                    if (active)
                        ImGui::PopStyleColor();
                };
                authorBtn("Anonymous", true);
                ImGui::SameLine();
                authorBtn(uname.c_str(), false);
            }

            if (ImGui::Button("Publish", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy) {
                if (pubNameBuf[0] == '\0')
                    NotificationManager::addSystem("Error: enter a name");
                else
                    ConfigManager::get().publishPublicConfigAsync(pubNameBuf, pubDescBuf, pubAnonymous);
            }

            if (!browse.message.empty()) {
                ImGui::Spacing();
                bool isErr = (browse.status == ConfigManager::BrowseState::Error);
                ImGui::PushStyleColor(ImGuiCol_Text, isErr ? IM_COL32(255, 90, 90, 255)
                                                           : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", browse.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        ui::end_child();
    }
    ImGui::EndGroup();
}

// ---- Contour sweep helpers ---------------------------------------------------
// Build the animated accent light that revolves around each active Module_Card.

// Lift a rounded rect's outline out as an ordered, closed polyline. Reuses
// ImDrawList's own corner tessellation via PathRect, then copies the points and
// clears the scratch path so nothing strokes it implicitly. Corner arcs carry
// many points while straight edges are single segments, so callers must walk it
// by true arc length (below), not by point index, or the motion looks uneven.
static void GetRoundedRectOutline(ImDrawList* draw, const ImVec2& a, const ImVec2& b, float rounding,
                                  ImVector<ImVec2>& out) {
    draw->PathClear();
    draw->PathRect(a, b, rounding);
    out.resize(draw->_Path.Size);
    for (int i = 0; i < draw->_Path.Size; ++i)
        out[i] = draw->_Path[i];
    draw->PathClear();
}

// Sample a point at absolute arc-length `dist` along a closed polyline, given
// its cumulative-length table (cum[0..n], cum[n] == perimeter). `dist` wraps.
static ImVec2 SampleClosedPolyline(const ImVector<ImVec2>& pts, const ImVector<float>& cum, float dist) {
    const int n = pts.Size;
    const float total = cum[n];
    dist -= total * floorf(dist / total);  // wrap into [0, total)
    int i = 0;
    while (i < n - 1 && cum[i + 1] < dist)
        ++i;
    const float segLen = cum[i + 1] - cum[i];
    const float t = segLen > 0.f ? (dist - cum[i]) / segLen : 0.f;
    const ImVec2 p0 = pts[i];
    const ImVec2 p1 = pts[(i + 1) % n];
    return ImVec2(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t);
}

// Continuous contour sweep: an accent light-gradient that revolves around the
// card outline forever while the module is on. Instead of a single comet, the
// WHOLE perimeter is drawn as a rotating brightness gradient — brightest at the
// head (`headU`, 0..1 of the perimeter) and falling off around the loop back to
// a faint always-lit base, so the border reads as a living ring of light rather
// than a dot chasing itself. Two stroke passes (a wide soft bloom under a crisp
// core) plus a white-hot head bloom give it depth. `intensity` (0..1) fades the
// whole thing in on enable and out on disable.
static void AddOutlineSweep(ImDrawList* draw, const ImVec2& a, const ImVec2& b, float rounding,
                            float headU, float intensity) {
    if (!draw || intensity <= 0.01f)
        return;

    ImVector<ImVec2> pts;
    GetRoundedRectOutline(draw, a, b, rounding, pts);
    const int n = pts.Size;
    if (n < 3)
        return;

    ImVector<float> cum;
    cum.resize(n + 1);
    cum[0] = 0.f;
    for (int i = 0; i < n; ++i) {
        const ImVec2 p0 = pts[i];
        const ImVec2 p1 = pts[(i + 1) % n];
        const float dx = p1.x - p0.x, dy = p1.y - p0.y;
        cum[i + 1] = cum[i] + sqrtf(dx * dx + dy * dy);
    }
    const float total = cum[n];
    if (total <= 0.f)
        return;

    // Resample the perimeter into many equal-arc-length points so the gradient is
    // smooth even along long straight edges (which the raw outline stores as a
    // single segment). Brightness is a COMET profile centered on the head: ahead of
    // the head a short GAUSSIAN eases in (soft leading edge, no hard wall); behind
    // the head an EXPONENTIAL decay gives a long, continuously-DIMMING tail. The
    // exponential is the key to a *directional* look — a long gaussian tail would
    // stay near-1 for a while and read as a symmetric bright plateau (the "wave"),
    // whereas exp() drops immediately from the peak so the head is clearly the
    // bright point and the light visibly trails off around the contour. Width tracks
    // brightness too, so the tail also tapers to a thin line. The far side falls to
    // ~0, so it reads as a rim light gliding the contour, not a lit rectangle.
    const int M = 192;                // gradient resolution around the loop (kept modest to bound vertex count)
    const float headSigma = 0.022f;   // leading falloff (ahead of the head), in perimeter fraction — sharp bright head
    const float tailTau = 0.090f;     // trailing exponential scale (behind the head) — long, dimming, directional tail

    ImVector<ImVec2> sp;
    ImVector<float> sb;
    sp.resize(M + 1);
    sb.resize(M + 1);
    for (int i = 0; i <= M; ++i) {
        const float u = (float)i / M;
        sp[i] = SampleClosedPolyline(pts, cum, u * total);
        // Signed shortest arc-distance from the head, wrapped into (-0.5, 0.5].
        // d > 0 => this point is behind the head (tail side); d < 0 => ahead.
        float d = headU - u;
        d -= floorf(d + 0.5f);
        if (d >= 0.f)
            sb[i] = expf(-d / tailTau);  // long dimming comet tail (directional)
        else
            sb[i] = expf(-(d * d) / (2.f * headSigma * headSigma));  // sharp soft leading edge
    }

    // Per-vertex OUTWARD normal (neighbour tangent rotated 90°, then flipped to
    // point away from the card centre). We give each ribbon segment a width to
    // tile the glow as filled quads; adjacent quads share the SAME edge vertices,
    // so there are no butt-cap gaps. Crucially the width is applied almost entirely
    // OUTWARD: offsetting a rounded rect INWARD by more than its corner radius makes
    // the inner edge self-intersect at the corners (the "lines colliding inside"
    // artifact). Blooming outward, convex corners only ever diverge — never cross.
    const ImVec2 center((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    ImVector<ImVec2> nrm;
    nrm.resize(M);
    for (int i = 0; i < M; ++i) {
        const ImVec2 pa = sp[(i - 1 + M) % M];
        const ImVec2 pb = sp[(i + 1) % M];
        float tx = pb.x - pa.x, ty = pb.y - pa.y;
        const float len = sqrtf(tx * tx + ty * ty);
        if (len > 1e-6f) {
            tx /= len;
            ty /= len;
        }
        ImVec2 nn(-ty, tx);
        // Flip so the normal points OUTWARD (away from the centre) at every point.
        if (nn.x * (sp[i].x - center.x) + nn.y * (sp[i].y - center.y) < 0.f)
            nn = ImVec2(-nn.x, -nn.y);
        nrm[i] = nn;
    }

    // Filled-fringe anti-aliasing would feather every quad's edges to zero, leaving
    // a faint seam at each shared boundary; disable it so the quads tile perfectly,
    // then restore it. The stacked, low-alpha layers hide the hard outer edge.
    const bool aaFill = (draw->Flags & ImDrawListFlags_AntiAliasedFill) != 0;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;

    // Additive glow from MANY filled ribbons straddling the border: each ribbon
    // reaches `+outer` OUTWARD and `-inner` INWARD from the contour, so the border
    // itself glows on both sides (not just outside). Width grows and alpha falls
    // from a thin bright core (k=0) to a wider soft halo (k=PASSES-1). The INWARD
    // reach is hard-clamped to just under the corner radius: offsetting a rounded
    // rect inward past its corner radius flips the inner edge and makes it self-
    // intersect at the corners (the old "lines colliding inside" artifact) — the
    // clamp makes that impossible while still lighting the interior lip. Outward has
    // no such limit (convex corners only diverge). Stacked low-alpha layers blend
    // the steps into one smooth, thick, metallic falloff; sb sweeps the lit arc.
    const float innerCap = rounding * 0.85f;  // never cross the corner arc centre
    // Squash the bloom vertically so it hugs the long top/bottom edges and reads as
    // a horizontal sweep rather than billowing up/down off the card.
    const float vGlow = 0.60f;
    const int PASSES = 8;
    for (int k = 0; k < PASSES; ++k) {
        const float t = (float)k / (float)(PASSES - 1);     // 0 = bright core, 1 = wide halo
        // Balanced width in/out (inner is capped at the corner radius anyway), so the
        // long top/bottom edges don't billow vertically. A bright, fairly narrow core
        // reads as a defined LINE — thicker than the card's 1px outline — with a
        // tighter soft halo hugging it rather than a big diffuse cloud.
        const float outerBase = ImLerp(0.55f, 0.85f, t);
        const float outerGain = ImLerp(1.05f, 2.30f, t);    // crisp thin line -> very tight halo
        const float innerBase = ImLerp(0.40f, 0.60f, t);
        const float innerGain = ImLerp(1.05f, 2.10f, t);    // inner side, clamped below
        const float pAlpha = ImLerp(0.20f, 0.010f, t * t);  // defined core, faint metallic halo (dimmer)
        for (int i = 0; i < M; ++i) {
            const int j = (i + 1) % M;
            const float bmid = (sb[i] + sb[j]) * 0.5f;
            const float alpha = intensity * bmid * pAlpha;
            if (alpha <= 0.0020f)
                continue;  // skip the dark part of the ring
            const float outI = ui::s(outerBase + outerGain * sb[i]);
            const float outJ = ui::s(outerBase + outerGain * sb[j]);
            const float inI = ImMin(ui::s(innerBase + innerGain * sb[i]), innerCap);
            const float inJ = ImMin(ui::s(innerBase + innerGain * sb[j]), innerCap);
            const ImVec2 quad[4] = {
                ImVec2(sp[i].x - nrm[i].x * inI, sp[i].y - nrm[i].y * inI * vGlow),    // inner @ i
                ImVec2(sp[j].x - nrm[j].x * inJ, sp[j].y - nrm[j].y * inJ * vGlow),    // inner @ j
                ImVec2(sp[j].x + nrm[j].x * outJ, sp[j].y + nrm[j].y * outJ * vGlow),  // outer @ j
                ImVec2(sp[i].x + nrm[i].x * outI, sp[i].y + nrm[i].y * outI * vGlow),  // outer @ i
            };
            const int a8 = (int)(ImClamp(alpha, 0.f, 1.f) * 255.f);
            const ImU32 base = GetAccentRGB();
            const ImU32 glowCol = (base & ~IM_COL32_A_MASK) | ((ImU32)a8 << IM_COL32_A_SHIFT);
            draw->AddConvexPolyFilled(quad, 4, glowCol);
        }
    }

    if (aaFill)
        draw->Flags |= ImDrawListFlags_AntiAliasedFill;
    // No head dot — the stacked filled ribbons blend into one soft, diffuse glow.
}

// Module_List_View: render the active category's modules as a single-column
// list of rounded Module_Cards in the main content area. Each Module_Card
// carries two disjoint hit areas drawn with the drawlist + ButtonBehavior
// pattern (no raw ImGui interactive widgets, so the grid fades with the menu):
//   * Name_Region (left, id #name)        -> Module_Toggle via m->toggle()
//   * Gear_Control (right, id #name##gear) -> open the Module_Config_View
// A right-click anywhere on the card also opens its config. Routing stays
// centralized in getModuleForSubtab. Active + hover states use an accent glow
// + vertical gradient pill (matching the module glow style).
static void renderModuleList(int tab) {
    if (tab < 0 || tab >= (int)ui::tabs.size())
        return;

    // Repeated labels in this list aren't module options, so keep the generic
    // description context (matches ui::subtab behavior for the sidebar rows).
    ui::SetDescriptionContext("__modules__");

    auto& t = ui::tabs[tab];

    // ── Module search bar ──
    static char searchBuf[64] = "";
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::s(ui::theme::rounding::Frame));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::s(8), ui::s(6)));

        // Simple, native input text with hint
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##modulesearch", "Search settings...", searchBuf, sizeof(searchBuf));

        ImGui::PopStyleVar(2);
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    // Unified SearchResult structure for global searching
    struct SearchResult {
        int tabIdx;
        int subtabIdx;
        const char* name;
    };

    std::vector<SearchResult> filteredResults;
    if (searchBuf[0] == '\0') {
        // Empty search query: show only the subtabs from the current tab
        const int n = (int)t.m_subtabs.size();
        for (int i = 0; i < n; ++i) {
            filteredResults.push_back({tab, i, t.m_subtabs[i]});
        }
    } else {
        // Active search query: search globally across every module category.
        // Configs/Settings have no subtabs, so they contribute nothing.
        std::string searchStr = searchBuf;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        for (int tIdx = 0; tIdx < (int)ui::tabs.size(); ++tIdx) {
            auto& otherTab = ui::tabs[tIdx];
            const int n = (int)otherTab.m_subtabs.size();
            for (int i = 0; i < n; ++i) {
                const char* name = otherTab.m_subtabs[i];
                std::string nameStr = name;
                std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
                if (nameStr.find(searchStr) != std::string::npos) {
                    filteredResults.push_back({tIdx, i, name});
                }
            }
        }
    }

    const int filteredCount = (int)filteredResults.size();

    const float cardGapY = ui::s(6);   // between cards
    const float cardH = ui::s(56);     // room for name + subtitle
    const float cardRound = ui::s(ui::theme::rounding::ModuleCard);

    const float avail = ImGui::GetContentRegionAvail().x;
    const float cardW = avail;  // single full-width column

    // Tight, explicit spacing: the only vertical gap between cards is cardGapY.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, cardGapY));

    // Render one Module_Card at the current group cursor.
    auto drawCard = [&](int idx) {
        const auto& result = filteredResults[idx];
        const char* name = result.name;
        int originalTab = result.tabIdx;
        int originalSubtab = result.subtabIdx;

        // Centralized routing (Req 3.3 / 4.4). m == nullptr for the Visuals
        // ArrayList (7), Notifications (9) and Watermark (10) special pages.
        Module* m = getModuleForSubtab(originalTab, originalSubtab);
        bool isLocked = m && m->showsDeveloperOnly();
        // Special pages (no Module): reflect their own enabled flag so the card
        // glows when active and can be toggled from the list like a module.
        bool isArrayList = !m && strcmp(name, "ArrayList") == 0;
        bool isNotifications = !m && strcmp(name, "Notifications") == 0;
        bool isWatermark = !m && strcmp(name, "Watermark") == 0;
        bool isClickGui = !m && strcmp(name, "ClickGUI") == 0;  // card toggles Old<->New
        bool isEnabled = m ? m->enabled
                           : (isArrayList ? ArrayListRenderer::enabled
                                          : (isNotifications ? NotificationManager::enabled
                                                             : (isWatermark ? Watermark::enabled
                                                                            : (isClickGui ? (ui::gui_mode == 1)
                                                                                          : false))));

        ImGui::PushID(idx);

        // Reserve the card rect; ItemSize advances the cursor (cardGapY between).
        ImVec2 pos = window->DC.CursorPos;
        ImRect bb{pos, pos + ImVec2(cardW, cardH)};
        ImGui::ItemSize(bb);

        // Gear control lives at the far right; the name + subtitle fill the rest.
        const float padR = ui::s(14);
        const float gearZoneW = ui::s(30);

        ImRect gearRect{ImVec2(bb.Max.x - padR - gearZoneW, bb.Min.y), ImVec2(bb.Max.x - padR, bb.Max.y)};

        // Open the Module_Config_View for this card (transforms the content area
        // in place). Shared by the gear control and by a right-click on the card.
        auto openConfig = [&]() {
            // If the module belongs to another tab, sync the current active tab.
            if (ui::cur_tab != originalTab) {
                ui::cur_tab = originalTab;
            }
            auto& activeTab = ui::tabs[originalTab];
            activeTab.next_subtab = originalSubtab;
            activeTab.next_view = ui::CategoryView::Config;
            ui::content_anim_dst = 0.f;
        };

        // Toggle this card's module / special page on or off, then persist
        // immediately. Saving from the render thread is safe here (local write
        // only, no JNI) and matches the alt-tab auto-close path in overlay.cpp.
        // Persisting on the toggle itself means state survives even if the game
        // is closed without a clean menu-close, or the main thread later freezes.
        auto toggleModule = [&]() {
            if (m) {
                // toggle() guards locked / canToggle internally.
                m->toggle();
            } else if (isArrayList) {
                ArrayListRenderer::enabled = !ArrayListRenderer::enabled;
            } else if (isNotifications) {
                NotificationManager::enabled = !NotificationManager::enabled;
            } else if (isWatermark) {
                Watermark::enabled = !Watermark::enabled;
            } else if (isClickGui) {
                ui::gui_mode = (ui::gui_mode == 1) ? 0 : 1;  // toggle Old <-> New
            }
            ConfigManager::get().onSettingChanged();
        };

        // --- Gear_Control: register first so it owns its right-edge zone ---
        ImGuiID gearId = window->GetID((std::string(name) + "##gear").c_str());
        ImGui::ItemAdd(gearRect, gearId);
        bool gHovered, gHeld;
        bool gPressed = ImGui::ButtonBehavior(gearRect, gearId, &gHovered, &gHeld);
        if (gPressed) {
            openConfig();
        }

        // --- Name_Region: the remaining left area toggles the module ---
        ImRect nameRect{bb.Min, ImVec2(gearRect.Min.x, bb.Max.y)};
        ImGuiID nameId = window->GetID(name);
        ImGui::ItemAdd(nameRect, nameId);
        bool nHovered, nHeld;
        bool nPressed = ImGui::ButtonBehavior(nameRect, nameId, &nHovered, &nHeld);
        if (nPressed) {
            toggleModule();
        }

        // Right-click anywhere on the card opens its config, same as the gear.
        if ((nHovered || gHovered) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            openConfig();
        }

        struct card_state {
            float hover;
            float gearHover;
            float enabled;
            float spin;  // accumulated sweep phase (laps); wraps when sampled
        };
        auto& obj = ui::anim_obj(name, 2, card_state{0.f, 0.f, 0.f, 0.f});
        obj.hover = ui::animate(obj.hover, 0.f, 1.f, nHovered || gHovered);
        obj.gearHover = ui::animate(obj.gearHover, 0.f, 1.f, gHovered);
        obj.enabled = ui::animate(obj.enabled, 0.f, 1.f, isEnabled);

        // The contour sweep runs continuously while enabled, at a single steady
        // speed — `spin` accumulates laps in real time (frame-rate independent),
        // never accelerates or decelerates.
        const float dt = ImGui::GetIO().DeltaTime;
        const float baseSpeed = 0.4f;  // laps per second, constant
        obj.spin += dt * baseSpeed;
        if (obj.spin > 1.0e6f)  // guard against unbounded float growth over time
            obj.spin -= 1.0e6f;

        ImDrawList* draw = window->DrawList;

        // Base panel: every card gets a dark rounded plate so the list reads as
        // discrete rows. It lifts a touch on hover and sits a hair stronger when
        // enabled — a neutral body, no colored/whitish tint of any kind.
        const float plateA = 0.30f + obj.hover * 0.16f + obj.enabled * 0.10f;
        draw->AddRectFilled(bb.Min, bb.Max, ui::GetColor(ImGuiCol_FrameBg, plateA), cardRound);

        // Thin edge is hover-only feedback; the enabled state is carried entirely
        // by the animated contour sweep + the accent title text (no fill wash).
        if (obj.hover > 0.01f)
            draw->AddRect(bb.Min, bb.Max, GetSchemeColor(0.30f * obj.hover), cardRound, 0, ui::s(1.0f));

        // Continuous contour sweep: a revolving accent light-gradient traced along
        // the card's own rounded-rect outline (not a pill) while the module is
        // enabled. The triangle-strip ribbon shares vertices between segments, so
        // the corners have no butt-cap gaps (the old "missing pixels"); a high
        // sample count keeps the rounded corners smooth. Fades in/out via enabled.
        const float sweepT = obj.enabled;
        if (sweepT > 0.01f) {
            const float headU = obj.spin - floorf(obj.spin);  // wrap laps into [0,1)
            AddOutlineSweep(draw, bb.Min, bb.Max, cardRound, headU, sweepT);
        }

        // Name + subtitle stacked on the left.
        const char* nameEnd = ImGui::FindRenderedTextEnd(name);
        const char* subtitle = ui::GetModuleDescription(name);
        ImFont* nameFont = font(2);  // bold
        ImFont* subFont = font(0);   // regular
        const float nameFS = nameFont->FontSize * 0.96f;
        const float subFS = subFont->FontSize * 0.9f;
        ImVec2 nameSize = nameFont->CalcTextSizeA(nameFS, FLT_MAX, 0.f, name, nameEnd);
        const float textX = nameRect.Min.x + ui::s(14);
        const float lineGap = ui::s(2);
        const float blockH = nameSize.y + lineGap + subFS;
        const float nameY = bb.GetCenter().y - blockH * 0.5f;

        ImU32 nameCol;
        if (isLocked) {
            nameCol = IM_COL32(100, 100, 100, 150);
        } else {
            float accentT = ImMax(obj.enabled, obj.hover);
            nameCol = ui::col_anim(ui::GetColor(ImGuiCol_Text, 0.9f), GetSchemeColor(), accentT);
        }
        draw->AddText(nameFont, nameFS, ImVec2(textX, nameY), nameCol, name, nameEnd);

        ImU32 subCol = isLocked ? IM_COL32(90, 90, 90, 130) : ui::GetColor(ImGuiCol_TextDisabled, 0.75f);
        draw->AddText(subFont, subFS, ImVec2(textX, nameY + nameSize.y + lineGap), subCol, subtitle);

        // Gear glyph centered in its zone.
        const char* gear = ICON_FA_COG;
        const char* gearEnd = ImGui::FindRenderedTextEnd(gear);
        ImVec2 gearSize = ImGui::CalcTextSize(gear, gearEnd);
        ImVec2 gearPos(gearRect.GetCenter().x - gearSize.x / 2.f, gearRect.GetCenter().y - gearSize.y / 2.f);
        ImU32 gearCol = ui::GetColor(ImGuiCol_TextDisabled, 0.45f + obj.gearHover * 0.55f);
        draw->AddText(gearPos, gearCol, gear, gearEnd);

        ui::HoverDescription(name, nHovered || gHovered,
                             isLocked ? "Developer only (for now.)" : subtitle, isLocked);

        ImGui::PopID();
    };

    // Single full-width column.
    ImGui::BeginGroup();
    for (int i = 0; i < filteredCount; ++i)
        drawCard(i);
    ImGui::EndGroup();

    ImGui::PopStyleVar();
}

// Module_Config_View: render a Back_Control at the top followed by the selected
// module's configuration body. The body is the existing per-module logic moved
// in verbatim (locked/unlock prompt, mod->onRenderMenu(), the inline ArrayList
// page at Visuals subtab 7, and the inline Notifications page at Visuals subtab
// 9), with a neutral placeholder for any other unresolved entry.
//
// NOTE (task 9.1): this temporarily duplicates the body still present in
// renderPage(); task 10.1 removes the copy in renderPage and wires dispatch.
// The duplicated blocks are local code (function-local statics included), so
// there is no ODR/global-symbol conflict. All rendering uses ui:: wrappers,
// runs on the GL render thread, and routes through getModuleForSubtab.
static void renderModuleConfig(int tab) {
    if (tab < 0 || tab >= (int)ui::tabs.size())
        return;

    auto& t = ui::tabs[tab];

    // Top of the content region: Back is pinned top-right, then the cursor snaps
    // back up so the first section header sits on the same line as Back.
    const float bodyTopY = ImGui::GetCursorPosY();
    float bodyMargin = 0.f;  // symmetric horizontal margin used to center the body

    // ----- Back_Control -----
    // Compact "< Back" pinned to the top-right, drawn with the same drawlist +
    // ButtonBehavior pattern as the module list rows so it fades with the menu.
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const char* backLabel = "Back";
        ImGuiID id = window->GetID("##module_config_back");

        const char* chevron = ICON_FA_CHEVRON_LEFT;
        ImVec2 chevSize = ImGui::CalcTextSize(chevron, 0, 1);
        ImVec2 labelSize = ImGui::CalcTextSize(backLabel, 0, 1);
        float iconGap = ui::s(6);
        float padX = ui::s(8);
        float rowHeight = ui::s(26);
        float controlW = padX + chevSize.x + iconGap + labelSize.x + padX;

        // Right-aligned within the content region.
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 origin = window->DC.CursorPos;
        ImVec2 bbMin{origin.x + availWidth - controlW, origin.y};
        ImRect bb{bbMin, bbMin + ImVec2(controlW, rowHeight)};
        ImGui::ItemAdd(bb, id);
        ImGui::ItemSize(ImVec2(availWidth, rowHeight));

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        // Back: return to the Module_List_View for the active category.
        if (pressed) {
            t.next_view = ui::CategoryView::List;
            ui::content_anim_dst = 0.f;
        }

        struct back_state {
            float hover;
        };
        auto& obj = ui::anim_obj("##module_config_back", 1, back_state{0.f});
        obj.hover = ui::animate(obj.hover, 0.f, 1.f, hovered);

        if (obj.hover > 0.01f) {
            window->DrawList->AddRectFilled(bb.Min, bb.Max,
                                            ui::GetColor(ImGuiCol_FrameBgHovered, obj.hover * 0.35f),
                                            ui::s(4));
        }

        ImU32 col = ui::col_anim(ui::GetColor(ImGuiCol_Text, 0.85f), GetSchemeColor(), obj.hover);

        float chevX = bb.Min.x + padX;
        window->DrawList->AddText(ImVec2(chevX, bb.GetCenter().y - chevSize.y / 2), col, chevron,
                                  ImGui::FindRenderedTextEnd(chevron));
        window->DrawList->AddText(ImVec2(chevX + chevSize.x + iconGap, bb.GetCenter().y - labelSize.y / 2), col,
                                  backLabel, ImGui::FindRenderedTextEnd(backLabel));
    }

    // Snap the cursor back to the top so the body starts level with Back. Keep a
    // small symmetric inset so the columns nearly span the full content width
    // instead of sitting narrow and centered.
    ImGui::SetCursorPosY(bodyTopY);
    {
        float availWidth = ImGui::GetContentRegionAvail().x;
        bodyMargin = ui::s(6.f);
        ImGui::Indent(bodyMargin);
        ImGui::PushItemWidth(availWidth - bodyMargin * 2.f);
    }

    // ----- Module body -----
    Module* mod = getModuleForSubtab(ui::cur_tab, t.cur_subtab);
    if (mod) {
        ui::SetDescriptionContext(mod->name, /*allowTips=*/true);
        // Locked Module UI
        if (!mod->isAccessible()) {
            ImGui::Spacing();
            ImGui::Spacing();

            // Center content
            float width = ImGui::GetContentRegionAvail().x;

            // Lock Icon
            ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCK);
            ImGui::SetCursorPosX((width - iconSize.x) / 2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text(ICON_FA_LOCK);
            ImGui::PopStyleColor();

            // Text
            const char* text = "This feature is locked.";
            ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::SetCursorPosX((width - textSize.x) / 2);
            ImGui::Text(text);

            ImGui::Spacing();

            const char* subText = "Enter unlock code:";
            ImVec2 subSize = ImGui::CalcTextSize(subText);
            ImGui::SetCursorPosX((width - subSize.x) / 2);
            ImGui::Text(subText);

            // Input
            static char buf[64] = "";
            static bool incorrect = false;

            ImGui::SetCursorPosX((width - ui::s(200)) / 2);
            ImGui::SetNextItemWidth(ui::s(200));

            // Auto focus if just selected? Maybe too intrusive.

            bool enterPressed = ImGui::InputText("##pass", buf, sizeof(buf),
                                                 ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

            if (incorrect) {
                const char* err = "Incorrect code!";
                ImVec2 errSize = ImGui::CalcTextSize(err);
                ImGui::SetCursorPosX((width - errSize.x) / 2);
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, err);
            }

            ImGui::Spacing();

            ImGui::SetCursorPosX((width - ui::s(100)) / 2);
            if (ImGui::Button("Unlock", {ui::s(100), ui::s(30)}) || enterPressed) {
                if (Module::checkUnlockCode(buf)) {
                    Module::experimentalUnlocked = true;
                    incorrect = false;
                    memset(buf, 0, sizeof(buf));
                } else {
                    incorrect = true;
                    memset(buf, 0, sizeof(buf));
                }
            }

        } else {
            // Normal Module UI
            mod->onRenderMenu();
        }
    }
    // Special pages (ArrayList, Notifications) - no Module, render inline
    else if (ui::cur_tab == 1 && t.cur_subtab == 7) {
        // ArrayList settings
        ui::SetDescriptionContext("ArrayList", /*allowTips=*/true);
        ui::two_column(
            "Settings",
            []() {
                ui::Checkbox("Enabled", &ArrayListRenderer::enabled);
                ui::Checkbox("Rainbow", &ArrayListRenderer::rainbow);
                ui::Checkbox("Color Bar", &ArrayListRenderer::colorBar);
                ui::Checkbox("Background", &ArrayListRenderer::showBackground);
            },
            "Options",
            []() {
                ui::Checkbox("Sort by Length", &ArrayListRenderer::sortByLength);
                ui::Checkbox("Show Info", &ArrayListRenderer::showInfo);
                const char* fontItems[] = {"Inter Regular", "Inter Bold"};
                ui::Combo("Font", &ArrayListRenderer::fontStyle, fontItems, 2);
                ui::SliderFloat("Size", &ArrayListRenderer::scale, 0.5f, 2.0f, "%.1fx");

                // The ArrayList is dragged directly on screen (same manner as
                // Dropped Pots): with the menu open, grab the block and drop it.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Drag the ArrayList directly on screen to move it.");
                ImGui::PopStyleColor();

                // Changing this resets the block back to the chosen corner.
                const char* anchorItems[] = {"Top Right", "Top Left", "Bottom Right", "Bottom Left"};
                if (ui::Combo("Position (reset corner)", &ArrayListRenderer::anchor, anchorItems, 4)) {
                    ArrayListRenderer::posX = -1.0f;  // -1 -> re-derive from the new corner
                    ArrayListRenderer::posY = -1.0f;
                }

                if (ArrayListRenderer::rainbow) {
                    const char* rbTypes[] = {"Flow", "Solid", "Fast", "Wide"};
                    ui::Combo("Rainbow Type", &ArrayListRenderer::rainbowType, rbTypes, 4);
                    bool highSat = ArrayListRenderer::rainbowSaturation > 0.6f;
                    if (ui::Checkbox("High Saturation", &highSat)) {
                        ArrayListRenderer::rainbowSaturation = highSat ? 0.8f : 0.4f;
                    }
                }
                if (!ArrayListRenderer::rainbow) {
                    ui::color("Color", ArrayListRenderer::color);
                }
            });
    } else if (ui::cur_tab == 1 && t.cur_subtab == 9) {
        // Notifications settings
        ui::SetDescriptionContext("Notifications", /*allowTips=*/true);
        ui::section("Settings", []() { ui::Checkbox("Enabled", &NotificationManager::enabled); });
    } else if (ui::cur_tab == 1 && t.cur_subtab == 10) {
        // Watermark settings (its own page now, independent of the ArrayList)
        ui::SetDescriptionContext("Watermark", /*allowTips=*/true);
        ui::two_column(
            "Settings",
            []() {
                ui::Checkbox("Enabled", &Watermark::enabled);
                ui::Checkbox("Rainbow", &Watermark::rainbow);
                if (Watermark::rainbow) {
                    const char* rbTypes[] = {"Flow", "Solid", "Fast", "Wide"};
                    ui::Combo("Rainbow Type", &Watermark::rainbowType, rbTypes, 4);
                }
            },
            "Options",
            []() {
                ui::SliderFloat("Size", &Watermark::scale, 0.5f, 2.0f, "%.1fx");

                // Dragged directly on screen, same manner as the ArrayList.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Drag the Watermark directly on screen to move it.");
                ImGui::PopStyleColor();
            });
    } else if (ui::cur_tab == 1 && t.cur_subtab == 12) {
        // ClickGUI settings: switch between the Old (original) and New (redesigned)
        // menu look. The card in the list also toggles this directly.
        ui::SetDescriptionContext("ClickGUI", /*allowTips=*/true);
        ui::section("Appearance", []() {
            const char* modes[] = {"Old Renewed", "New"};
            ui::Combo("Mode", &ui::gui_mode, modes, 2);
        });
    }
    // Any other unresolved (category, entry): neutral placeholder so the menu
    // never crashes or shows a blank page. The list entry stays selectable.
    else {
        const char* msg = "Couldn't load this page.";
        float width = ImGui::GetContentRegionAvail().x;
        ImVec2 msgSize = ImGui::CalcTextSize(msg);
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SetCursorPosX((width - msgSize.x) / 2);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::Text(msg);
        ImGui::PopStyleColor();
    }

    // Close the centering inset opened before the module body.
    ImGui::PopItemWidth();
    ImGui::Unindent(bodyMargin);
}

// Render current page - calls module's onRenderMenu() dynamically
static void renderPage() {
    if (ui::cur_tab >= ui::tabs.size())
        return;
    auto& tab = ui::tabs[ui::cur_tab];

    // Special tabs (no subtabs)
    const int settingsIdx = (int)ui::tabs.size() - 1;
    const int configsIdx = settingsIdx - 1;
#if COBBLE_SPECTATE_ENABLED
    const int spectateIdx = settingsIdx - 2;
#endif

    // Used to trigger a one-shot refresh when entering the Configs tab.
    static int s_prevTab = -1;
    if (s_prevTab != ui::cur_tab && ui::cur_tab == configsIdx) {
        g_configsNeedsRefresh = true;
    }
    s_prevTab = ui::cur_tab;

    if (ui::cur_tab == settingsIdx) {
        ui::SetDescriptionContext("Settings");
        renderSettingsPage();
        return;
    }
#if COBBLE_SPECTATE_ENABLED
    if (ui::cur_tab == spectateIdx) {
        ui::SetDescriptionContext("Spectate");
        renderSpectatePage();
        return;
    }
#endif
    if (ui::cur_tab == configsIdx) {
        ui::SetDescriptionContext("Configs");
        renderConfigsPage();
        return;
    }

    // Module_Category: dispatch on the per-category view state. The content area
    // transforms in place -- the selectable module list (renderModuleList) or the
    // selected module's config body (renderModuleConfig), which carries its own
    // Back row. Routing stays centralized via getModuleForSubtab.
    //
    // Safe bounds guard: renderModuleList / renderModuleConfig also guard the
    // subtab index, but keep renderPage safe against an out-of-range subtab.
    if (tab.cur_subtab >= tab.m_subtabs.size())
        return;

    if (ui::tabs[ui::cur_tab].cur_view == ui::CategoryView::List)
        renderModuleList(ui::cur_tab);
    else
        renderModuleConfig(ui::cur_tab);
}

void Menu::ApplyStyle() {
    ui::apply_style();
}

// ==================== OPEN / CLOSE MOTION ====================
// The menu used to open with `openAnim = ImLerp(openAnim, 1, 15 * dt)`. That is an
// exponential approach: it never actually arrives, so the panel spends its whole
// life "still settling" and every derived value inherits the same mush. Driving one
// real 0..1 timer through three different curves instead gives an alpha that lands
// early, a geometry curve that overshoots once and settles, and content that trails
// the frame — three distinct motions from one clock, which is what makes an opening
// panel read as assembled rather than cross-faded.
static constexpr float kOpenDuration = 0.27f;
static float g_openT = 0.0f;

static float easeOutExpo(float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -9.5f * t);
}

// Deliberately allowed to exceed 1: callers use the overshoot.
static float easeOutBack(float t) {
    const float c = 1.52f;
    const float u = ImClamp(t, 0.0f, 1.0f) - 1.0f;
    return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
}

// Remap so the curve does not even start until `delay` of the open has elapsed.
static float staggered(float t, float delay) {
    return easeOutExpo(ImClamp((t - delay) / ImMax(0.0001f, 1.0f - delay), 0.0f, 1.0f));
}

// A critically-ish damped spring. Used for both the New menu's tab indicator and
// the Old Renewed sidebar rail: a lerp cannot lean into a move, and the leaning is
// the whole reason the motion reads as one object travelling rather than two
// positions being cross-faded. Slightly under-damped, so it overshoots once.
static void springTo(float& value, float& velocity, float target, float dt, float stiffness = 300.0f,
                     float damping = 27.0f) {
    const float step = ImMin(dt, 1.0f / 60.0f);
    velocity += ((target - value) * stiffness - velocity * damping) * step;
    value += velocity * step;
}

// The "Old Renewed" ClickGUI body: the classic vertical-sidebar layout, refreshed
// with dark-glass panels and slim accent hairlines. The New look now has its own
// renderer (renderClickGuiNew), so this path is only ever called with nu == false;
// the `nu` parameter is kept for source compatibility. Called by Menu::Render
// after the open/close animation. `pop` overshoots 1 briefly and `contentT` trails
// behind it — see the open/close motion block above.
static void renderClickGuiBody(float openAnim, bool nu, float pop, float contentT) {
    ImGuiIO& io = ImGui::GetIO();

    // Old Renewed: refined dark-glass panels (semi-transparent) so the classic
    // sidebar layout reads as a modern, "renewed" surface rather than flat opaque
    // plates — while keeping the vertical-nav identity that distinguishes it from
    // the New look. (`nu` is retained for compatibility; the New menu now has its
    // own renderer, so this path always renders Old Renewed.)
    const ImU32 panelBg = nu ? IM_COL32(12, 13, 15, 214) : IM_COL32(13, 14, 17, 234);
    const ImU32 recessBg = nu ? IM_COL32(7, 8, 9, 222) : IM_COL32(9, 10, 12, 240);

    // Keep layout geometry stable while opening; only opacity is animated.
    ui::size = ui::s(ui::baseSize);

    // Rise from below and settle. The whole window is offset rather than the drawn
    // plates, so children, hit-boxes and backgrounds all travel together — scaling
    // the plates alone would let the inner recessed panels overhang the outer plate
    // for the first few frames, because child backgrounds are drawn in child space.
    const float slide = (1.0f - pop) * ui::s(26);

    // Center window
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f + slide), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui::size);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAnim);

    ImGui::Begin("Cobble", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* windowDraw = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowMax = windowPos + ImGui::GetWindowSize();
        const float rounding = GImGui->Style.WindowRounding;

        // Draw separate background rectangles for left and right panels to keep the separator transparent
        ImVec2 leftPanelMin{windowPos.x, windowPos.y};
        ImVec2 leftPanelMax{windowPos.x + ui::s(176), windowMax.y};
        windowDraw->AddRectFilled(leftPanelMin, leftPanelMax, panelBg, rounding);
        windowDraw->AddRect(leftPanelMin, leftPanelMax, ui::GetColor(ImGuiCol_Border, 1.0f), rounding, 0, ui::s(1.0f));
        // Slim accent hairline along the top edge (on-theme, not a metallic white),
        // traced out from the centre as the panel arrives rather than switched on.
        auto traceLine = [&](float x0, float x1, float y, float alpha) {
            const float mid = (x0 + x1) * 0.5f;
            const float half = (x1 - x0) * 0.5f * ImClamp(pop, 0.0f, 1.0f);
            if (half <= 0.5f)
                return;
            windowDraw->AddLine(ImVec2(mid - half, y), ImVec2(mid + half, y), GetSchemeColor(alpha), ui::s(1.0f));
        };
        traceLine(leftPanelMin.x + rounding, leftPanelMax.x - rounding, leftPanelMin.y + ui::s(1), 0.28f);

        ImVec2 rightPanelMin{windowPos.x + ui::s(185), windowPos.y};
        ImVec2 rightPanelMax{windowMax.x, windowMax.y};
        windowDraw->AddRectFilled(rightPanelMin, rightPanelMax, panelBg, rounding);
        windowDraw->AddRect(rightPanelMin, rightPanelMax, ui::GetColor(ImGuiCol_Border, 1.0f), rounding, 0,
                            ui::s(1.0f));
        traceLine(rightPanelMin.x + rounding, rightPanelMax.x - rounding, rightPanelMin.y + ui::s(1), 0.28f);

        // Inner top highlight, one pixel in from the border on both plates. This is
        // the cheapest thing that makes a dark surface read as glass rather than as
        // paint: a real pane catches a sliver of light along its top lip.
        const ImU32 lip = IM_COL32(255, 255, 255, (int)(16.0f * openAnim));
        windowDraw->AddLine(ImVec2(leftPanelMin.x + rounding, leftPanelMin.y + ui::s(2.5f)),
                            ImVec2(leftPanelMax.x - rounding, leftPanelMin.y + ui::s(2.5f)), lip, ui::s(1.0f));
        windowDraw->AddLine(ImVec2(rightPanelMin.x + rounding, rightPanelMin.y + ui::s(2.5f)),
                            ImVec2(rightPanelMax.x - rounding, rightPanelMin.y + ui::s(2.5f)), lip, ui::s(1.0f));

        ui::nav_sel_valid = false;

        // ===== LEFT NAV =====
        ImGui::BeginChild("nav", {ui::s(176), ImGui::GetWindowHeight()}, 0,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        {
            // Brand area
            ImGui::BeginChild("brand", {ImGui::GetWindowWidth(), ui::s(42)}, 0, ImGuiWindowFlags_NoBackground);
            {
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(),
                                                          {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                                                           ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2},
                                                          ui::GetColor(ImGuiCol_ChildBg), GImGui->Style.WindowRounding,
                                                          ImDrawFlags_RoundCornersTop);
                // Accent underline, drawn out from the centre as the panel arrives so
                // the brand looks underscored rather than pre-stamped.
                {
                    const float ulY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
                    const float ulMid = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f;
                    const float ulHalf = ImGui::GetWindowWidth() * 0.5f * ImClamp(pop, 0.0f, 1.0f);
                    ImGui::GetWindowDrawList()->AddRectFilled({ulMid - ulHalf, ulY - 2}, {ulMid + ulHalf, ulY},
                                                              GetSchemeColor());
                }

                const char* brand = "Cobble";
                ImVec2 brandSize = font(3)->CalcTextSizeA(font(3)->FontSize, FLT_MAX, 0.f, brand);
                ImGui::GetWindowDrawList()->AddText(
                    font(3), font(3)->FontSize,
                    {ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2 - brandSize.x / 2,
                     ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - brandSize.y / 2 - 2},
                    GetSchemeColor(), brand);
            }
            ImGui::EndChild();

            // Tabs area
            ImGui::SetCursorPosY(ui::s(50));
            ImGui::BeginChild("tabs", {ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - ui::s(50)}, 0,
                              ImGuiWindowFlags_NoBackground);
            {
                // Recessed, darker panel for the categories so they read as
                // sitting "further back" than the brand and the main content.
                ImDrawList* navDraw = ImGui::GetWindowDrawList();
                ImVec2 panelMin = ImGui::GetWindowPos();
                ImVec2 panelMax = panelMin + ImGui::GetWindowSize();
                navDraw->AddRectFilled(panelMin, panelMax, recessBg, GImGui->Style.WindowRounding,
                                       ImDrawFlags_RoundCornersBottom);
                // Inner top shadow: a soft gradient so the brand area above
                // appears to cast onto the recessed categories panel.
                float shadowH = ui::s(20);
                navDraw->AddRectFilledMultiColor(panelMin, ImVec2(panelMax.x, panelMin.y + shadowH),
                                                 ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)),
                                                 ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)),
                                                 ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)),
                                                 ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)));

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1, 1});

                // Modules pinned to the top, Settings pinned to the bottom edge
                // (Configs stays with the modules just above Settings).
                const int tabCount = (int)ui::tabs.size();
                const int settingsIdx = tabCount - 1;
                const float rowH = ui::s(38);   // ui::tab fixed row height
                const float topPad = ui::s(8);  // clear the seam shadow cast by the brand
                const float botPad = ui::s(8);  // breathing room above the rounded corner
                const float startY = ImGui::GetCursorPosY();
                const float availH = ImGui::GetContentRegionAvail().y;

                ImGui::Dummy({0, topPad});
                for (int i = 0; i < settingsIdx; ++i) {
                    ui::tab(i);
                }

                // Push Settings down to the bottom. If the panel is too short to
                // fit everything, it just follows the modules in natural flow.
                const float settingsY = startY + availH - rowH - botPad;
                if (settingsY > ImGui::GetCursorPosY()) {
                    ImGui::SetCursorPosY(settingsY);
                }
                ui::tab(settingsIdx);

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // ===== NAV SELECTION RAIL =====
        // One accent rail that travels between rows, rather than every row owning a
        // highlight that pops on and off wherever the selection lands. Drawn on the
        // parent window's draw list *after* EndChild, which in ImGui means it renders
        // BEHIND the child's icon and label (child draw lists are appended after the
        // parent's), so the rail's glow sits under the text instead of over it.
        if (ui::nav_sel_valid) {
            static float railY = 0.0f, railH = 0.0f, railYVel = 0.0f, railHVel = 0.0f;
            static bool railInit = false;

            const float inset = ui::s(9);
            const float targetY = ui::nav_sel_min.y + inset;
            const float targetH = ImMax(ui::s(6), (ui::nav_sel_max.y - ui::nav_sel_min.y) - inset * 2.0f);

            if (!railInit || openAnim < 0.999f) {
                // Snapped for the whole reveal: the window itself is still sliding up,
                // so a spring here would trail the rows it is supposed to be marking.
                // easeOutExpo returns exactly 1 once the open clock is done, so this
                // hands over to the spring on the first settled frame and stays there.
                railY = targetY;
                railH = targetH;
                railYVel = railHVel = 0.0f;
                railInit = true;
            } else {
                springTo(railY, railYVel, targetY, io.DeltaTime);
                springTo(railH, railHVel, targetH, io.DeltaTime, 340.0f, 30.0f);
            }

            // Squash and stretch: the rail leans into the move and settles. This is
            // the difference between "an object travelled" and "two rectangles were
            // cross-faded" — the eye reads the smear as momentum.
            const float smear = ImClamp(fabsf(railYVel) * 0.030f, 0.0f, ui::s(14));
            float y0 = railY - (railYVel < 0.0f ? smear : 0.0f);
            float y1 = railY + railH + (railYVel > 0.0f ? smear : 0.0f);

            const float railX = leftPanelMin.x + ui::s(4);
            const float railW = ui::s(3);
            windowDraw->AddRectFilled({railX, y0}, {railX + railW, y1}, GetSchemeColor(0.95f), railW * 0.5f);

            // Accent bleed to the right of the rail, so the selected row is lit by it
            // instead of merely being adjacent to a marker.
            const float glowW = ui::s(46);
            const ImU32 glowIn = GetSchemeColor(0.17f);
            const ImU32 glowOut = GetSchemeColor(0.0f);
            windowDraw->AddRectFilledMultiColor({railX + railW, y0}, {railX + railW + glowW, y1}, glowIn, glowOut,
                                                glowOut, glowIn);
        }

        // Vertical seam shadow between the recessed sidebar and the main content
        // so the categories panel reads as sitting behind the modules area.
        {
            float seamX = windowPos.x + ui::s(185);
            float seamTop = windowPos.y + ui::s(8);
            float seamBot = windowMax.y - ui::s(8);
            windowDraw->AddRectFilledMultiColor(
                {seamX, seamTop}, {seamX + ui::s(12), seamBot},
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.32f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.0f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.32f)));
        }

        // ===== MAIN CONTENT =====
        ImGui::SetCursorPos({ui::s(185), 0});
        ImGui::BeginChild("main", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoBackground);
        {
            // Header
            ImGui::BeginChild("main header", {ImGui::GetWindowWidth(), ui::s(42)}, 0, ImGuiWindowFlags_NoBackground);
            {
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(),
                                                          {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                                                           ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2},
                                                          ui::GetColor(ImGuiCol_ChildBg), GImGui->Style.WindowRounding,
                                                          ImDrawFlags_RoundCornersTop);
                // Accent underline, traced out from the left edge with the reveal so it
                // reads as being drawn under the title rather than already printed.
                {
                    const float ulY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
                    const float ulX = ImGui::GetWindowPos().x;
                    const float ulW = ImGui::GetWindowWidth() * ImClamp(pop, 0.0f, 1.0f);
                    ImGui::GetWindowDrawList()->AddRectFilled({ulX, ulY - 2}, {ulX + ulW, ulY}, GetSchemeColor());
                }

                // Header Title (Tab Name)
                const char* name = ui::tabs[ui::cur_tab].m_name;
                ImFont* titleFont = font(3);
                ImFont* subFont = font(2);

                float centerY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f;
                float titleHeight = titleFont->FontSize;
                float baselineY = centerY - titleHeight * 0.5f;

                ImGui::GetWindowDrawList()->AddText(titleFont, titleFont->FontSize,
                                                    {ImGui::GetWindowPos().x + ui::s(20), baselineY},
                                                    ImGui::GetColorU32(ImGuiCol_TextDisabled, ui::anim), name);

                // Breadcrumb / Subtab Name
                // Special categories (Configs/Settings) and Module_List_View show the
                // category name only. The breadcrumb (arrow + selected module name) is
                // gated on a Module_Category being in Config view.
                const int settingsIdx = (int)ui::tabs.size() - 1;
                const int configsIdx = settingsIdx - 1;
                const bool isSpecial = (ui::cur_tab == settingsIdx || ui::cur_tab == configsIdx);
                const ui::tab_t& curTab = ui::tabs[ui::cur_tab];
                if (!isSpecial && curTab.cur_view == ui::CategoryView::Config &&
                    curTab.cur_subtab < (int)curTab.m_subtabs.size()) {
                    float nameWidth = titleFont->CalcTextSizeA(titleFont->FontSize, FLT_MAX, 0.f, name).x;

                    ImGui::RenderArrow(ImGui::GetWindowDrawList(),
                                       {ImGui::GetWindowPos().x + ui::s(27) + nameWidth, centerY - ui::s(5)},
                                       ImGui::GetColorU32(ImGuiCol_Text, ui::anim * ui::content_anim), ImGuiDir_Right,
                                       ui::s(12));

                    const char* subtabName = curTab.m_subtabs[curTab.cur_subtab];

                    ImGui::GetWindowDrawList()->AddText(subFont, subFont->FontSize,
                                                        {ImGui::GetWindowPos().x + ui::s(40) + nameWidth, baselineY},
                                                        GetSchemeColor(ui::anim * ui::content_anim), subtabName);
                }
            }
            ImGui::EndChild();

            // Content background
            ImGui::GetWindowDrawList()->AddRectFilled({ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ui::s(42)},
                                                      ImGui::GetWindowPos() + ImGui::GetWindowSize(), panelBg,
                                                      GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom);

            // Content
            // Slightly wider vertical item spacing so module config rows
            // (checkboxes, sliders) breathe instead of stacking tightly.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::s(10), ui::s(13)});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::s(15), ui::s(15)});

            // Sync alpha_mod for custom widgets BEFORE pushing style
            // This ensures both ImGui native widgets AND custom ui:: widgets fade together.
            // contentT trails the frame's own fade, so the panel lands first and the
            // widgets arrive into it — a stagger, not one flat cross-fade.
            float contentAlpha = contentT * ui::anim * ui::content_anim;
            ui::alpha_mod = contentAlpha;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);

            ImGui::SetCursorPosY(ui::s(42));
            ImGui::BeginChild("content", ImGui::GetContentRegionAvail(), 0,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding);
            { renderPage(); }
            ImGui::EndChild();
            ImGui::PopStyleVar(3);

            // Restore alpha_mod to menu-level value
            ui::alpha_mod = openAnim;
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Gaussian-ish background blur for the New ClickGUI. The overlay renders from a
// wglSwapBuffers hook, so at draw time the GL back-buffer already holds the fully
// rendered game world (ImGui draw lists are flushed later). We copy the region
// behind the panel into a texture and composite it back through a small multi-tap
// kernel (uniform average over gaussian-spaced offsets = a soft blur) — no shaders
// or FBOs, which the ImGui OpenGL2 backend here doesn't have.
static void drawBlurBehind(ImDrawList* dl, const ImVec2& wpos, const ImVec2& wsize, float round) {
    if (wsize.x < 4.0f || wsize.y < 4.0f)
        return;

    const int sw = RenderData::screenWidth;
    const int sh = RenderData::screenHeight;
    if (sw < 4 || sh < 4)
        return;

    // Panel region in top-left screen space, clamped to the framebuffer.
    int rx = static_cast<int>(wpos.x);
    int ry = static_cast<int>(wpos.y);
    int rw = static_cast<int>(wsize.x);
    int rh = static_cast<int>(wsize.y);
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > sw) rw = sw - rx;
    if (ry + rh > sh) rh = sh - ry;
    if (rw < 4 || rh < 4)
        return;

    static GLuint blurTex = 0;
    if (blurTex == 0) {
        glGenTextures(1, &blurTex);
        if (blurTex == 0)
            return;
    }

    // Copy the back-buffer region behind the panel into blurTex. Framebuffer origin
    // is bottom-left, so flip Y for the copy (and flip V when drawing below).
    const int fbY = sh - (ry + rh);
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glBindTexture(GL_TEXTURE_2D, blurTex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rx, fbY, rw, rh, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    while (glGetError() != GL_NO_ERROR) {
    }  // swallow any GL error so the overlay's pre-render check stays clean

    const ImTextureID tid = (ImTextureID)(intptr_t)blurTex;
    const ImVec2 pmin(static_cast<float>(rx), static_cast<float>(ry));
    const ImVec2 pmax(static_cast<float>(rx + rw), static_cast<float>(ry + rh));

    // Uniform average (alpha = 1/(i+1)) over a gaussian-spaced set of taps ≈ blur.
    const float bpx = ui::s(2.0f);
    const float du = bpx / static_cast<float>(rw);
    const float dv = bpx / static_cast<float>(rh);
    static const float offs[13][2] = {{0, 0},  {1, 0},   {-1, 0}, {0, 1},  {0, -1}, {1, 1},  {1, -1},
                                      {-1, 1}, {-1, -1}, {2, 0},  {-2, 0}, {0, 2},  {0, -2}};
    for (int i = 0; i < 13; ++i) {
        const float ox = offs[i][0] * du;
        const float oy = offs[i][1] * dv;
        // V is flipped (texture is bottom-up): top-left uv = (0,1), bottom-right = (1,0).
        const ImVec2 uv0(0.0f + ox, 1.0f - oy);
        const ImVec2 uv1(1.0f + ox, 0.0f - oy);
        const int a = static_cast<int>(255.0f / static_cast<float>(i + 1));
        dl->AddImageRounded(tid, pmin, pmax, uv0, uv1, IM_COL32(255, 255, 255, a), round);
    }
}

static void renderClickGuiNew(float openAnim, float pop, float contentT) {
    ImGuiIO& io = ImGui::GetIO();
    ui::size = ui::s(ui::baseSize);

    // Rise from below and settle: `pop` overshoots 1 once, so the panel comes to
    // rest by passing its mark and easing back rather than creeping up to it.
    const float slide = (1.0f - pop) * ui::s(22);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f + slide), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui::size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAnim);
    ImGui::Begin("Cobble", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        const ImVec2 wpos = ImGui::GetWindowPos();
        const ImVec2 wsize = ImGui::GetWindowSize();
        const ImVec2 wmax = wpos + wsize;
        const float round = ui::s(14);
        const float topH = ui::s(52);
        const float pad = ui::s(22);
        const float cy = wpos.y + topH * 0.5f;

        // Frosted glass: blur what's behind the panel, then a light dark tint over
        // it (lower alpha than the old opaque plate so the blur reads through), plus
        // the single soft 1px edge.
        drawBlurBehind(dl, wpos, wsize, round);
        dl->AddRectFilled(wpos, wmax, IM_COL32(0, 0, 0, 175), round);
        dl->AddRect(wpos, wmax, IM_COL32(255, 255, 255, 13), round, 0, ui::s(1.0f));

        // Brand, preceded by a slim accent tick — ties the header to the accent
        // colour without a shiny/metallic edge.
        ImFont* brandFont = font(3);
        const char* brand = "Cobble";
        ImVec2 bsz = brandFont->CalcTextSizeA(brandFont->FontSize, FLT_MAX, 0.f, brand);
        dl->AddRectFilled(ImVec2(wpos.x + pad, cy - bsz.y * 0.40f),
                          ImVec2(wpos.x + pad + ui::s(3), cy + bsz.y * 0.40f), GetSchemeColor(), ui::s(1.5f));
        const float brandX = wpos.x + pad + ui::s(13);
        const ImVec2 brandPos(brandX, cy - bsz.y * 0.5f);
        dl->AddText(brandFont, brandFont->FontSize, brandPos, IM_COL32(244, 245, 249, 255), brand);

        struct new_cat_state {
            float hover;
            float sel;
        };

        ImFont* catFont = font(2);
        const float catFS = catFont->FontSize * 0.9f;
        // Tabs sit just to the right of the brand — keep the gap tight so the row
        // reads as one header instead of drifting away from "Cobble".
        float x = brandX + bsz.x + ui::s(14);
        const int tabCount = (int)ui::tabs.size();
        static float indX = 0.0f, indW = 0.0f, indInit = 0.0f;
        float selX = indX, selW = indW;

        for (int i = 0; i < tabCount; ++i) {
            const char* nm = ui::tabs[i].m_name;
            const char* ic = ui::tabs[i].m_icon;
            ImVec2 nsz = catFont->CalcTextSizeA(catFS, FLT_MAX, 0.f, nm);
            ImVec2 isz = font(0)->CalcTextSizeA(catFS, FLT_MAX, 0.f, ic);
            const float inner = isz.x + ui::s(7) + nsz.x;
            const float bw = inner + ui::s(20);

            ImRect bb(ImVec2(x, wpos.y + ui::s(7)), ImVec2(x + bw, wpos.y + topH - ui::s(7)));
            std::string key = std::string("ncat_") + nm;
            ImGuiID id = win->GetID(key.c_str());
            ImGui::ItemAdd(bb, id);
            bool hov = false, held = false;
            bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
            bool selected = ui::next_tab == i;
            if (pressed) {
                if (!selected) {
                    ui::next_tab = i;
                    ui::anim_dst = 0.f;
                } else if (i == ui::cur_tab && ui::tabs[i].cur_view == ui::CategoryView::Config) {
                    ui::tabs[i].next_view = ui::CategoryView::List;
                    ui::content_anim_dst = 0.f;
                }
            }

            auto& st = ui::anim_obj(key.c_str(), 0, new_cat_state{0.f, 0.f});
            st.hover = ui::animate(st.hover, 0.f, 1.f, hov);
            st.sel = ui::animate(st.sel, 0.f, 1.f, selected);
            if (ImMax(st.hover, st.sel) > 0.01f) {
                dl->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(0.12f * ImMax(st.sel, st.hover * 0.55f)),
                                  ui::s(8));
            }

            // Cursor spotlight: the pill brightens where the cursor actually is. The
            // gradient's own ends are clamped into the pill's flat middle band and its
            // end alphas recomputed to match, so it reaches zero by itself rather than
            // being cut off by a clip rect — no hard edge, no spill past the rounding.
            if (st.hover > 0.01f) {
                const float r = ui::s(8);
                const float lx = bb.Min.x + r, rx = bb.Max.x - r;
                if (rx > lx) {
                    const float reach = ui::s(34);
                    const float mx = ImClamp(io.MousePos.x, lx, rx);
                    const float x0 = ImMax(lx, mx - reach);
                    const float x1 = ImMin(rx, mx + reach);
                    const float peak = 0.11f * st.hover;
                    const ImU32 cm = GetSchemeColor(peak);
                    const ImU32 c0 = GetSchemeColor(peak * (1.0f - (mx - x0) / reach));
                    const ImU32 c1 = GetSchemeColor(peak * (1.0f - (x1 - mx) / reach));
                    dl->AddRectFilledMultiColor({x0, bb.Min.y}, {mx, bb.Max.y}, c0, cm, cm, c0);
                    dl->AddRectFilledMultiColor({mx, bb.Min.y}, {x1, bb.Max.y}, cm, c1, c1, cm);
                }
            }

            ImU32 col = ui::col_anim(
                ui::col_anim(ui::GetColor(ImGuiCol_TextDisabled), ui::GetColor(ImGuiCol_Text, 0.85f), st.hover),
                IM_COL32(244, 245, 249, 255), st.sel);

            const float bx = x + (bw - inner) * 0.5f;
            dl->AddText(font(0), catFS, ImVec2(bx, cy - isz.y * 0.5f), col, ic);
            dl->AddText(catFont, catFS, ImVec2(bx + isz.x + ui::s(7), cy - nsz.y * 0.5f), col, nm);

            if (selected) {
                selX = bx;
                selW = inner;
            }
            x += bw;
        }

        static float indXVel = 0.0f, indWVel = 0.0f;
        if (indInit < 0.5f) {
            indX = selX;
            indW = selW;
            indXVel = indWVel = 0.0f;
            indInit = 1.0f;
        } else {
            springTo(indX, indXVel, selX, io.DeltaTime);
            springTo(indW, indWVel, selW, io.DeltaTime, 340.0f, 30.0f);
        }

        // Stretch the leading end in proportion to travel speed. A lerp cannot lean
        // into a move, and the lean is the whole reason this reads as one bar
        // travelling between tabs rather than two bars trading places.
        const float lead = ImClamp(fabsf(indXVel) * 0.045f, 0.0f, ui::s(26));
        const float ux0 = indX - (indXVel < 0.0f ? lead : 0.0f);
        const float ux1 = indX + indW + (indXVel > 0.0f ? lead : 0.0f);
        const float uy = wpos.y + topH;

        // Rounded accent underline with a soft halo behind it.
        dl->AddRectFilled(ImVec2(ux0 - ui::s(3), uy - ui::s(6)), ImVec2(ux1 + ui::s(3), uy - ui::s(1)),
                          GetSchemeColor(0.16f), ui::s(3));
        dl->AddRectFilled(ImVec2(ux0, uy - ui::s(5)), ImVec2(ux1, uy - ui::s(2)), GetSchemeColor(), ui::s(1.5f));

        // Accent wash bleeding down from the indicator into the content area, so the
        // light travels with the selected tab instead of the tab merely being marked.
        // Three bands (fade in / solid / fade out) because AddRectFilledMultiColor
        // only interpolates between corners.
        {
            const float washH = ui::s(60);
            const float fade = ui::s(32);
            const ImU32 lit = GetSchemeColor(0.085f);
            const ImU32 nil = GetSchemeColor(0.0f);
            dl->AddRectFilledMultiColor(ImVec2(ux0 - fade, uy), ImVec2(ux0, uy + washH), nil, lit, nil, nil);
            dl->AddRectFilledMultiColor(ImVec2(ux0, uy), ImVec2(ux1, uy + washH), lit, lit, nil, nil);
            dl->AddRectFilledMultiColor(ImVec2(ux1, uy), ImVec2(ux1 + fade, uy + washH), lit, nil, nil, nil);
        }

        // Header separator: a hairline brightest at the centre, fading toward
        // both rounded corners — softer than a full-width white rule.
        {
            const float sepY = wpos.y + topH;
            const float midX = wpos.x + wsize.x * 0.5f;
            const ImU32 sepC = IM_COL32(255, 255, 255, 24);
            const ImU32 sep0 = IM_COL32(255, 255, 255, 0);
            dl->AddRectFilledMultiColor(ImVec2(wpos.x + round, sepY - ui::s(0.5f)), ImVec2(midX, sepY + ui::s(0.5f)),
                                        sep0, sepC, sepC, sep0);
            dl->AddRectFilledMultiColor(ImVec2(midX, sepY - ui::s(0.5f)), ImVec2(wmax.x - round, sepY + ui::s(0.5f)),
                                        sepC, sep0, sep0, sepC);
        }

        ImGui::SetCursorPos(ImVec2(0, topH));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::s(10), ui::s(13)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::s(20), ui::s(16)});
        // contentT trails the frame's fade, so the panel lands first and the widgets
        // arrive into it — a stagger rather than one flat cross-fade.
        float contentAlpha = contentT * ui::anim * ui::content_anim;
        ui::alpha_mod = contentAlpha;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
        ImGui::BeginChild("newcontent", ImVec2(wsize.x, wsize.y - topH), 0,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding);
        {
            renderPage();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ui::alpha_mod = openAnim;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Menu::Render() {
    // Reset binder state at start of each frame
    ui::ResetBinderState();

    // If hidden (e.g. during HUD drag), skip rendering but keep state
    if (hidden && isOpen) {
        COBBLE_TRACE_ONCE("Menu::Render skip: hidden");
        return;
    }

    // Animate open/close
    // On OPEN: one 0..1 clock read through three curves (see the open/close motion
    // block above), so alpha, geometry and content each arrive on their own timing.
    // On CLOSE: instant snap to 0 — zero ghosting/lingering.
    if (!isOpen) {
        openAnim = 0.f;
        g_openT = 0.f;
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > (1.0f / 60.0f)) dt = 1.0f / 60.0f;
    g_openT = ImMin(1.0f, g_openT + dt / kOpenDuration);
    openAnim = easeOutExpo(g_openT);   // opacity: lands early, then holds
    const float pop = easeOutBack(g_openT);      // geometry: overshoots once, settles
    const float contentT = staggered(g_openT, 0.30f);  // widgets: trail the frame

    // Apply style properly (set alpha etc)
    ApplyStyle();
    ui::updateScale();

    // Dark overlay behind the menu: a flat dim plus a vignette pulling the edges
    // down, so attention falls to the centre where the panel is instead of the whole
    // frame going uniformly grey. Four gradient strips, one per edge — they overlap
    // in the corners, which is exactly what makes the corners the darkest part.
    {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(104 * openAnim)));

        const int edgeA = (int)(58.0f * openAnim);
        if (edgeA > 0) {
            const ImU32 e = IM_COL32(0, 0, 0, edgeA);
            const ImU32 z = IM_COL32(0, 0, 0, 0);
            const float vx = ds.x * 0.34f;
            const float vy = ds.y * 0.34f;
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(vx, ds.y), e, z, z, e);
            bg->AddRectFilledMultiColor(ImVec2(ds.x - vx, 0), ImVec2(ds.x, ds.y), z, e, e, z);
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, vy), e, e, z, z);
            bg->AddRectFilledMultiColor(ImVec2(0, ds.y - vy), ImVec2(ds.x, ds.y), z, z, e, e);
        }
    }

    if (ui::gui_mode == 1)
        renderClickGuiNew(openAnim, pop, contentT);
    else
        renderClickGuiBody(openAnim, false, pop, contentT);

    ui::handle_anims();
}

void Menu::Hotkey(const char* label, Module* module) {
    ImGui::SameLine();
    ImGui::PushID(label);

    std::string text = (moduleToBind == module) ? "..." : "[?]";

    if (ImGui::SmallButton(text.c_str())) {
        moduleToBind = module;
    }

    ImGui::PopID();
}
