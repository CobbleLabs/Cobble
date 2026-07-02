#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "menu.h"
#include "../../cobble.hpp"
#include "../../config/config.hpp"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/aimbow.h"
#include "../../module/combat/autoclicker.h"
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
#include "../../util/logger.hpp"
#include "../../util/network/http.hpp"
#include "../../util/team/friends_manager.h"
#include "../hud/arraylist.h"
#include "../hud/watermark.h"
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
                        Minecraft mc;
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

    if (configView == 0)
    ui::two_column(
        "Saved",
        [&]() {
            if (ImGui::BeginListBox("##configlist", ImVec2(ImGui::GetContentRegionAvail().x, ui::s(280)))) {
                for (int i = 0; i < (int)configs.size(); i++) {
                    const bool isSel = (i == selected);
                    if (ImGui::Selectable(configs[i].c_str(), isSel)) {
                        selected = i;
                        strncpy_s(nameBuf, sizeof(nameBuf), configs[i].c_str(), _TRUNCATE);
                    }
                }
                ImGui::EndListBox();
            }
        },
        "Actions",
        [&]() {
            ImGui::Text("Name");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##configname", nameBuf, sizeof(nameBuf));

            ImGui::Spacing();

            std::string err;
            std::string n = nameBuf[0] != '\0'
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
                    NotificationManager::addSystem("Error: " + (err.empty() ? std::string("delete failed") : err));
                }
            }
        });

    // ── Public configs (browse / publish / apply) ─────────────────────────
    static char pubNameBuf[64] = "";
    static char pubDescBuf[128] = "";
    static int pubSelected = -1;
    static double pubLastPoll = 0.0;
    constexpr double kPubPollIntervalSec = 15.0;

    // Fetch the list when entering the Public view, then poll periodically so
    // configs published by others show up without manual refresh.
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

    ui::two_column(
        "Browse Public",
        [&]() {
            // List fills the panel down to the bottom; reserve a few lines for
            // the description only when an entry is selected (keeps layout
            // stable when nothing is selected).
            bool hasSel = (pubSelected >= 0 && pubSelected < (int)browse.entries.size());
            float reserve = hasSel ? ui::s(72) : 0.0f;
            float listH = ImGui::GetContentRegionAvail().y - reserve;
            if (listH < ui::s(60))
                listH = ui::s(60);
            if (ImGui::BeginListBox("##pubconfiglist", ImVec2(ImGui::GetContentRegionAvail().x, listH))) {
                for (int i = 0; i < (int)browse.entries.size(); i++) {
                    const auto& e = browse.entries[i];
                    std::string label = e.name + "  by " + e.owner + "  (" + std::to_string(e.downloads) + ")";
                    if (ImGui::Selectable(label.c_str(), i == pubSelected))
                        pubSelected = i;
                }
                ImGui::EndListBox();
            }
            // Description of the highlighted entry.
            if (hasSel) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", browse.entries[pubSelected].description.c_str());
                ImGui::PopStyleColor();
            }
        },
        "Share",
        [&]() {
            if (ImGui::Button("Refresh", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                ConfigManager::get().refreshPublicConfigsAsync();

            bool busy = (browse.status == ConfigManager::BrowseState::Loading);
            if (ImGui::Button("Apply Selected", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy) {
                if (pubSelected >= 0 && pubSelected < (int)browse.entries.size())
                    ConfigManager::get().downloadAndApplyPublicConfigAsync(browse.entries[pubSelected].id);
                else
                    NotificationManager::addSystem("Error: select a config");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Publish current settings");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##pubname", "Name", pubNameBuf, sizeof(pubNameBuf));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##pubdesc", "Description", pubDescBuf, sizeof(pubDescBuf));
            if (ImGui::Button("Publish", ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !busy) {
                if (pubNameBuf[0] == '\0') {
                    NotificationManager::addSystem("Error: enter a name");
                } else {
                    ConfigManager::get().publishPublicConfigAsync(pubNameBuf, pubDescBuf);
                }
            }

            // Status line from the async worker.
            if (!browse.message.empty()) {
                ImGui::Spacing();
                bool isErr = (browse.status == ConfigManager::BrowseState::Error);
                ImGui::PushStyleColor(ImGuiCol_Text, isErr ? IM_COL32(255, 90, 90, 255)
                                                           : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", browse.message.c_str());
                ImGui::PopStyleColor();
            }
        });
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
        ImGui::Spacing();
        ImGui::Spacing();
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
        // ArrayList (7) and Notifications (9) special pages.
        Module* m = getModuleForSubtab(originalTab, originalSubtab);
        bool isLocked = m && m->showsDeveloperOnly();
        // Special pages (no Module): reflect their own enabled flag so the card
        // glows when active and can be toggled from the list like a module.
        bool isArrayList = !m && strcmp(name, "ArrayList") == 0;
        bool isNotifications = !m && strcmp(name, "Notifications") == 0;
        bool isEnabled = m ? m->enabled
                           : (isArrayList ? ArrayListRenderer::enabled
                                          : (isNotifications ? NotificationManager::enabled : false));

        ImGui::PushID(idx);

        // Reserve the card rect; ItemSize advances the cursor (cardGapY between).
        ImVec2 pos = window->DC.CursorPos;
        ImRect bb{pos, pos + ImVec2(cardW, cardH)};
        ImGui::ItemSize(bb);

        // Gear control lives at the far right; the name + subtitle fill the rest.
        const float padR = ui::s(14);
        const float gearZoneW = ui::s(30);

        ImRect gearRect{ImVec2(bb.Max.x - padR - gearZoneW, bb.Min.y), ImVec2(bb.Max.x - padR, bb.Max.y)};

        // Open the Module_Config_View for this card. Shared by the gear control
        // and by a right-click anywhere on the card.
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

        // Toggle this card's module / special page on or off.
        auto toggleModule = [&]() {
            if (m) {
                // toggle() guards locked / canToggle internally.
                m->toggle();
            } else if (isArrayList) {
                // No config save here (render thread); persists on menu close.
                ArrayListRenderer::enabled = !ArrayListRenderer::enabled;
            } else if (isNotifications) {
                NotificationManager::enabled = !NotificationManager::enabled;
            }
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

    // ----- Back_Control -----
    // Minimalist back row drawn with the same drawlist + ButtonBehavior pattern
    // as the module list rows so it fades with the menu (no raw ImGui widgets).
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const char* backLabel = "Back";
        ImGuiID id = window->GetID("##module_config_back");

        float rowHeight = ui::s(28);
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImRect bb{window->DC.CursorPos, window->DC.CursorPos + ImVec2(availWidth, rowHeight)};
        ImGui::ItemAdd(bb, id);
        ImGui::ItemSize(bb);

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

        // Subtle hover background for the row.
        if (obj.hover > 0.01f) {
            window->DrawList->AddRectFilled(bb.Min, bb.Max,
                                            ui::GetColor(ImGuiCol_FrameBgHovered, obj.hover * 0.35f),
                                            ui::s(4));
        }

        ImU32 col = ui::col_anim(ui::GetColor(ImGuiCol_Text, 0.85f), GetSchemeColor(), obj.hover);

        const char* chevron = ICON_FA_CHEVRON_LEFT;
        ImVec2 chevSize = ImGui::CalcTextSize(chevron, 0, 1);
        float chevX = bb.Min.x + ui::s(6);
        window->DrawList->AddText(ImVec2(chevX, bb.GetCenter().y - chevSize.y / 2), col, chevron,
                                  ImGui::FindRenderedTextEnd(chevron));

        ImVec2 labelSize = ImGui::CalcTextSize(backLabel, 0, 1);
        window->DrawList->AddText(ImVec2(chevX + chevSize.x + ui::s(6), bb.GetCenter().y - labelSize.y / 2), col,
                                  backLabel, ImGui::FindRenderedTextEnd(backLabel));
    }

    ImGui::Spacing();

    // ----- Module body (moved as-is from renderPage) -----
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
                ui::Checkbox("Watermark", &Watermark::enabled);
                ui::Checkbox("Watermark Rainbow", &Watermark::rainbow);
            },
            "Options",
            []() {
                ui::Checkbox("Sort by Length", &ArrayListRenderer::sortByLength);
                ui::Checkbox("Show Info", &ArrayListRenderer::showInfo);
                ui::SliderFloat("Size", &ArrayListRenderer::scale, 0.5f, 2.0f, "%.1fx");

                // Move button: the block sticks to the cursor (menu hides), click
                // once to drop it. You can also just drag the block directly.
                if (ImGui::Button("Move ArrayList")) {
                    ArrayListRenderer::enabled = true;  // must be on to be visible/placeable
                    ArrayListRenderer::placing = true;
                    Menu::hidden = true;  // hide menu visuals; it stays "open" so the cursor works
                }
                ui::DescribeLastItem("Move ArrayList",
                                     "Pega la ArrayList al cursor; haz click para soltarla donde quieras. "
                                     "Tambien puedes arrastrarla directamente con el mouse.");

                // Move the watermark the same way as the ArrayList.
                if (ImGui::Button("Move Watermark")) {
                    Watermark::enabled = true;  // must be on to be visible/placeable
                    Watermark::placing = true;
                    Menu::hidden = true;
                }
                ui::DescribeLastItem("Move Watermark",
                                     "Pega el watermark al cursor; haz click para soltarlo donde quieras. "
                                     "Tambien puedes arrastrarlo directamente con el mouse.");
                ui::SliderFloat("Watermark Size", &Watermark::scale, 0.5f, 2.0f, "%.1fx");

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
}

// Render current page - calls module's onRenderMenu() dynamically
static void renderPage() {
    if (ui::cur_tab >= ui::tabs.size())
        return;
    auto& tab = ui::tabs[ui::cur_tab];

    // Special tabs (no subtabs)
    const int settingsIdx = (int)ui::tabs.size() - 1;
    const int configsIdx = settingsIdx - 1;

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
    if (ui::cur_tab == configsIdx) {
        ui::SetDescriptionContext("Configs");
        renderConfigsPage();
        return;
    }

    // Module_Category: dispatch on the per-category view state. The full
    // per-module body (locked/unlock prompt, onRenderMenu, inline ArrayList /
    // Notifications, placeholder) lives in renderModuleConfig; the selectable
    // module list lives in renderModuleList. Routing stays centralized via
    // getModuleForSubtab inside those functions.
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
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::s(10), ui::s(10)});
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
