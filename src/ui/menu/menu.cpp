#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "menu.h"
#include "../../cobble.hpp"
#include "../../config/config.hpp"
#include "../../spectate/spectate.hpp"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/aimbow.h"
#include "../../module/combat/autoclicker.h"
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
#include "../../module/movement/nojumpdelay.h"
#include "../../module/movement/no_slow_down.h"
#include "../../module/movement/blink.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timer.h"
#include "../../module/movement/timerrange.h"
#include "../../module/movement/sprint.h"
#include "../../module/utility/armorswitch.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/autotool.h"
#include "../../module/utility/breaker.h"
#include "../../module/utility/bridgeassist.h"
#include "../../module/utility/clutch.h"
#include "../../module/utility/fastplace.h"
#include "../../module/utility/fastbreak.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/inventorymanager.h"
#include "../../module/utility/altmanager.h"
#include "../../module/utility/tcpoptimizer.h"
#include "../../module/visual/blockesp.h"
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
        }
    }
    // Movement Tab (2): Timer, TimerRange, NoSlowDown, NoJumpDelay, Sprint
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
                return fastPlace;
            case 3:
                return noItemRelease;
            case 4:
                return bridgeAssist;
            case 5:
                return clutch;
            case 6:
                return breaker;
            case 7:
                return autoTool;
            case 8:
                return inventoryManager;
            case 9:
                return altManager;
            case 10:
                return fastBreak;
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
            case 3:
                return tcpOptimizer;
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

                ui::Checkbox("Teams", &friendsManager->teams.enabled);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::PopStyleColor();

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

// Module_List_View: render the active category's modules as a single-column
// list of rounded Module_Cards in the main content area. Each Module_Card
// carries two disjoint hit areas drawn with the drawlist + ButtonBehavior
// pattern (no raw ImGui interactive widgets, so the grid fades with the menu):
//   * Name_Region (left, id #name)        -> Module_Toggle via m->toggle()
//   * Gear_Control (right, id #name##gear) -> open the Module_Config_View
// A right-click anywhere on the card also opens its config. Routing stays
// centralized in getModuleForSubtab. Active + hover states use the same soft
// accent pill the sidebar category rows use.
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
        bool isEnabled = m ? m->enabled
                           : (isArrayList ? ArrayListRenderer::enabled
                                          : (isNotifications ? NotificationManager::enabled
                                                             : (isWatermark ? Watermark::enabled : false)));

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
        };
        auto& obj = ui::anim_obj(name, 2, card_state{0.f, 0.f, 0.f});
        obj.hover = ui::animate(obj.hover, 0.f, 1.f, nHovered || gHovered);
        obj.gearHover = ui::animate(obj.gearHover, 0.f, 1.f, gHovered);
        obj.enabled = ui::animate(obj.enabled, 0.f, 1.f, isEnabled);

        ImDrawList* draw = window->DrawList;

        // Soft accent "pill" behind the card for both the active and hover states,
        // matching the sidebar category selection (no bright border, no comet).
        // Active state is fully lit; hover is a lighter wash.
        float pillT = ImMax(obj.enabled, obj.hover * 0.55f);
        if (pillT > 0.01f) {
            draw->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(0.14f * pillT), cardRound);
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
        ui::SetDescriptionContext(mod->name);
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
        ui::SetDescriptionContext("ArrayList");
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
        ui::SetDescriptionContext("Notifications");
        ui::section("Settings", []() { ui::Checkbox("Enabled", &NotificationManager::enabled); });
    } else if (ui::cur_tab == 1 && t.cur_subtab == 10) {
        // Watermark settings (its own page now, independent of the ArrayList)
        ui::SetDescriptionContext("Watermark");
        ui::two_column(
            "Settings",
            []() {
                ui::Checkbox("Enabled", &Watermark::enabled);
                ui::Checkbox("Rainbow", &Watermark::rainbow);
            },
            "Options",
            []() {
                ui::SliderFloat("Size", &Watermark::scale, 0.5f, 2.0f, "%.1fx");

                // Dragged directly on screen, same manner as the ArrayList.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Drag the Watermark directly on screen to move it.");
                ImGui::PopStyleColor();
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
    const int spectateIdx = settingsIdx - 2;

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
    if (ui::cur_tab == spectateIdx) {
        ui::SetDescriptionContext("Spectate");
        renderSpectatePage();
        return;
    }
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

void Menu::Render() {
    // Reset binder state at start of each frame
    ui::ResetBinderState();

    // If hidden (e.g. during HUD drag), skip rendering but keep state
    if (hidden && isOpen) {
        COBBLE_TRACE_ONCE("Menu::Render skip: hidden");
        return;
    }

    // Animate open/close
    float targetAnim = isOpen ? 1.f : 0.f;
    float dt = ImGui::GetIO().DeltaTime;
    if (dt > (1.0f / 60.0f)) {
        dt = 1.0f / 60.0f;
    }
    openAnim = ImLerp(openAnim, targetAnim, 15.f * dt);

    if (openAnim < 0.01f && !isOpen) {
        return;
    }

    // Apply style properly (set alpha etc)
    ApplyStyle();

    ImGuiIO& io = ImGui::GetIO();
    ui::updateScale();

    // Dark overlay behind menu
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                  IM_COL32(0, 0, 0, (int)(115 * openAnim)));

    // Keep layout geometry stable while opening; only opacity is animated.
    ui::size = ui::s(ui::baseSize);

    // Center window
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui::size);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAnim);

    ImGui::Begin("Cobble", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* windowDraw = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowMax = windowPos + ImGui::GetWindowSize();
        // Draw separate background rectangles for left and right panels to keep the separator transparent
        ImVec2 leftPanelMin = windowPos;
        ImVec2 leftPanelMax = windowPos + ImVec2(ui::s(176), ImGui::GetWindowHeight());
        windowDraw->AddRectFilled(leftPanelMin, leftPanelMax, ui::GetColor(ImGuiCol_WindowBg),
                                  GImGui->Style.WindowRounding);
        windowDraw->AddRect(leftPanelMin, leftPanelMax, ui::GetColor(ImGuiCol_Border, 1.0f),
                            GImGui->Style.WindowRounding, 0, ui::s(1.0f));

        ImVec2 rightPanelMin = windowPos + ImVec2(ui::s(185), 0);
        ImVec2 rightPanelMax = windowPos + ImGui::GetWindowSize();
        windowDraw->AddRectFilled(rightPanelMin, rightPanelMax, ui::GetColor(ImGuiCol_WindowBg),
                                  GImGui->Style.WindowRounding);
        windowDraw->AddRect(rightPanelMin, rightPanelMax, ui::GetColor(ImGuiCol_Border, 1.0f),
                            GImGui->Style.WindowRounding, 0, ui::s(1.0f));

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
                ImGui::GetWindowDrawList()->AddRectFilled(
                    {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2},
                    {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                     ImGui::GetWindowPos().y + ImGui::GetWindowHeight()},
                    GetSchemeColor());

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
                navDraw->AddRectFilled(panelMin, panelMax,
                                       ImGui::GetColorU32(ImVec4(0.039f, 0.039f, 0.039f, 1.0f)),
                                       GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom);
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
                ImGui::GetWindowDrawList()->AddRectFilled(
                    {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2},
                    {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                     ImGui::GetWindowPos().y + ImGui::GetWindowHeight()},
                    GetSchemeColor());

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
                                                      ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                                                      ui::GetColor(ImGuiCol_WindowBg), GImGui->Style.WindowRounding,
                                                      ImDrawFlags_RoundCornersBottom);

            // Content
            // Slightly wider vertical item spacing so module config rows
            // (checkboxes, sliders) breathe instead of stacking tightly.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::s(10), ui::s(13)});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::s(15), ui::s(15)});

            // Sync alpha_mod for custom widgets BEFORE pushing style
            // This ensures both ImGui native widgets AND custom ui:: widgets fade together
            float contentAlpha = openAnim * ui::anim * ui::content_anim;
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
