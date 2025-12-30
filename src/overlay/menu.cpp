/*
 * Cobble Client Menu - Weave Style
 * Direct port of weave free layout
 */

#include "menu.h"
#include "weave_ui.hpp"
#include "../module/module_manager.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/combat/autorefill.h"
#include "../module/combat/velocity.h"
#include "../module/combat/reach.h"
#include "../module/utility/nojumpdelay.h"
#include "../module/visual/nametags.h"
#include "arraylist.h"
#include "../cobble.hpp"
#include "../sdk/minecraft.h"

#include <string>

// Initialize menu pages (called once)
static bool pages_initialized = false;

static void InitializePages() {
    if (pages_initialized) return;
    pages_initialized = true;
    
    // Combat Tab - AutoClicker page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &autoClicker.enabled, &autoClicker.keybind);
                ImGui::Spacing();
                ui::SliderFloat("Min CPS", &autoClicker.settings.minCPS, 1.0f, 20.0f, "%.1f");
                ui::SliderFloat("Max CPS", &autoClicker.settings.maxCPS, 1.0f, 20.0f, "%.1f");
                if (autoClicker.settings.minCPS > autoClicker.settings.maxCPS)
                    autoClicker.settings.minCPS = autoClicker.settings.maxCPS;
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Conditions", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Stable CPS", &autoClicker.settings.stableCPS);
                ui::Checkbox("Weapon Only", &autoClicker.settings.weaponOnly);
                ui::Checkbox("Break Blocks", &autoClicker.settings.breakBlocks);
                ui::Checkbox("Inventory", &autoClicker.settings.workInInventory);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Combat Tab - AimAssist page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &aimAssist.enabled, &aimAssist.keybind);
                ImGui::Spacing();
                ui::SliderFloat("Horizontal Speed", &aimAssist.settings.horizontalSpeed, 1.0f, 100.0f, "%.0f");
                ui::SliderFloat("Vertical Speed", &aimAssist.settings.verticalSpeed, 1.0f, 100.0f, "%.0f");
                ui::SliderFloat("FOV", &aimAssist.settings.maxFov, 10.0f, 360.0f, "%.0f");
                ui::SliderFloat("Distance", &aimAssist.settings.maxDistance, 3.0f, 8.0f, "%.1f");
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Filters", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Clicking Only", &aimAssist.settings.onlyClicking);
                ui::Checkbox("Vertical Aim", &aimAssist.settings.verticalAim);
                ui::Checkbox("Target Lock", &aimAssist.settings.targetLock);
                ui::Checkbox("Visibility", &aimAssist.settings.visibilityCheck);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Combat Tab - AutoRefill page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Binder("Keybind", &autoRefill.keybind);
                ImGui::Spacing();
                int speed = autoRefill.settings.speed;
                if (ui::SliderInt("Speed (ms)", &speed, 20, 200, "%d"))
                    autoRefill.settings.speed = speed;
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Items", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Refill Soup", &autoRefill.settings.refillSoup);
                ui::Checkbox("Refill Potions", &autoRefill.settings.refillPotions);
                ui::Checkbox("Smart Speed", &autoRefill.settings.smartSpeed);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Combat Tab - Velocity page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &velocity.enabled, &velocity.keybind);
                ImGui::Spacing();
                
                // Mode selector
                const char* modes[] = { "Regular", "Jump", "Reduce", "Ignore" };
                int modeIdx = static_cast<int>(velocity.settings.mode);
                ImGui::Text("Mode");
                ImGui::SameLine();
                if (ImGui::BeginCombo("##velmode", modes[modeIdx], ImGuiComboFlags_NoArrowButton)) {
                    for (int i = 0; i < 4; i++) {
                        bool selected = (modeIdx == i);
                        if (ImGui::Selectable(modes[i], selected))
                            velocity.settings.mode = static_cast<Velocity::Mode>(i);
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                
                ImGui::Spacing();
                
                // Mode-specific settings
                if (velocity.settings.mode == Velocity::Mode::Regular || 
                    velocity.settings.mode == Velocity::Mode::Reduce) {
                    ui::SliderFloat("Horizontal", &velocity.settings.horizontal, 0.0f, 100.0f, "%.0f%%");
                    ui::SliderFloat("Vertical", &velocity.settings.vertical, 0.0f, 100.0f, "%.0f%%");
                    ui::SliderFloat("Chance", &velocity.settings.chance, 0.0f, 100.0f, "%.0f%%");
                    
                    if (velocity.settings.mode == Velocity::Mode::Reduce) {
                        ui::Checkbox("Double Clicks", &velocity.settings.allowDoubleClicks);
                    }
                }
                else if (velocity.settings.mode == Velocity::Mode::Jump) {
                    ui::SliderFloat("Jump Chance", &velocity.settings.jumpChance, 0.0f, 100.0f, "%.0f%%");
                }
                else if (velocity.settings.mode == Velocity::Mode::Ignore) {
                    ui::SliderFloat("Chance", &velocity.settings.chance, 0.0f, 100.0f, "%.0f%%");
                    
                    // Vertical mode
                    const char* vertModes[] = { "Never", "Only Air", "Always" };
                    int vertIdx = static_cast<int>(velocity.settings.verticalMode);
                    ImGui::Text("Vertical");
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##vertmode", vertModes[vertIdx], ImGuiComboFlags_NoArrowButton)) {
                        for (int i = 0; i < 3; i++) {
                            bool selected = (vertIdx == i);
                            if (ImGui::Selectable(vertModes[i], selected))
                                velocity.settings.verticalMode = static_cast<Velocity::VerticalMode>(i);
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                
                ImGui::Spacing();
                ui::SliderFloat("FOV", &velocity.settings.fov, 0.0f, 360.0f, "%.0f");
                
                int delay = velocity.settings.reduceDelay;
                if (ui::SliderInt("Delay", &delay, 0, 5, "%d ticks"))
                    velocity.settings.reduceDelay = delay;
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Conditions", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Looking at Player", &velocity.settings.onlyLookingAtPlayer);
                ui::Checkbox("Mouse Pressed", &velocity.settings.onlyMousePressed);
                ui::Checkbox("Moving Forward", &velocity.settings.onlyMovingForward);
                ui::Checkbox("Holding Weapon", &velocity.settings.onlyHoldingWeapon);
                
                if (velocity.settings.mode == Velocity::Mode::Jump) {
                    ui::Checkbox("Only Sprinting", &velocity.settings.onlySprinting);
                }
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Combat Tab - Reach page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &reach.enabled, &reach.keybind);
                ImGui::Spacing();
                
                ui::Checkbox("Randomize", &reach.settings.randomize);
                
                if (reach.settings.randomize) {
                    ui::SliderFloat("Min Distance", &reach.settings.minDistance, 3.0f, 6.0f, "%.1f");
                    ui::SliderFloat("Max Distance", &reach.settings.maxDistance, 3.0f, 6.0f, "%.1f");
                } else {
                    ui::SliderFloat("Distance", &reach.settings.distance, 3.0f, 6.0f, "%.1f");
                }
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Conditions", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Weapons Only", &reach.settings.weaponsOnly);
                ui::Checkbox("Sprinting Only", &reach.settings.sprintingOnly);
                ui::Checkbox("Moving Only", &reach.settings.movingOnly);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Visuals Tab - NameTags page
    ui::add_page(1, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("Settings", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &nameTags.enabled, &nameTags.keybind);
                ImGui::Spacing();
                ui::SliderFloat("Scale", &nameTags.settings.scale, 0.5f, 2.0f, "%.1f");
                ui::Checkbox("Background", &nameTags.settings.background);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Display", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Show Health", &nameTags.settings.showHealth);
                ui::Checkbox("Show Distance", &nameTags.settings.showDistance);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Visuals Tab - ESP page (placeholder)
    ui::add_page(1, []() {
        ui::begin_child("Player ESP", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
        {
            static bool espEnabled = false;
            ui::Checkbox("Enabled", &espEnabled);
            static bool box2d = false;
            ui::Checkbox("Box 2D", &box2d);
        }
        ui::end_child();
    });
    
    // Visuals Tab - World page (placeholder)
    ui::add_page(1, []() {
        ui::begin_child("World", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
        {
            static bool timeChanger = false;
            ui::Checkbox("Time Changer", &timeChanger);
            static int time = 12000;
            if (timeChanger) ui::SliderInt("Time", &time, 0, 24000, "%d");
        }
        ui::end_child();
    });
    
    // Movement Tab - NoJumpDelay page
    ui::add_page(2, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &noJumpDelay.enabled, &noJumpDelay.keybind);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Info", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ImGui::TextWrapped("Removes jump delay for bhop.");
                ImGui::Spacing();
                ImGui::TextWrapped("Hold SPACE to auto-jump.");
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
    
    // Settings Tab
    ui::add_page(3, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("Theme", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::color("Accent Color", g_SchemeColor);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("HUD", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("ArrayList", &ArrayListRenderer::enabled);
                if (ArrayListRenderer::enabled) {
                    ui::Checkbox("  Background", &ArrayListRenderer::showBackground);
                    ui::Checkbox("  Sort by Length", &ArrayListRenderer::sortByLength);
                }
                ImGui::Spacing();
                ui::Checkbox("Notifications", &NotificationManager::enabled);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::Spacing();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Actions", { ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2, 0 });
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.08f, 0.15f, 1.0f));
                
                if (ImGui::Button("EJECT CLIENT", ImVec2(ImGui::CalcItemWidth(), 32))) {
                    cobble::context::is_running = false;
                    Menu::isOpen = false;
                }
                
                ImGui::PopStyleColor(3);
            }
            ui::end_child();
        }
        ImGui::EndGroup();
    });
}

void Menu::ApplyStyle() {
    ui::apply_style();
}

void Menu::Render() {
    // Animate open/close - always update animation even when closing
    float targetAnim = isOpen ? 1.f : 0.f;
    openAnim = ImLerp(openAnim, targetAnim, 15.f * ImGui::GetIO().DeltaTime);
    
    // Don't render if fully closed
    if (openAnim < 0.01f && !isOpen) {
        openAnim = 0.f;
        return;
    }
    
    // Continuously release movement keys while menu is open
    // This fights against Minecraft's native input handling which might re-enable them
    if (isOpen) {
        mc.releaseMovementKeys();
    }
    
    // Initialize pages once
    InitializePages();
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Update UI scale based on resolution
    ui::updateScale();
    
    // Sync global alpha modifier for manual rendering
    ui::alpha_mod = openAnim;
    
    // Fake blur: dark overlay behind menu
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0),
        io.DisplaySize,
        IM_COL32(0, 0, 0, (int)(115 * openAnim))  // Black with ~45% alpha
    );
    
    ui::size = ui::s(ui::baseSize);
    
    // Set window centered
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui::size);
    
    // Apply alpha for smooth fade in/out animation
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAnim);
    
    ImGui::Begin("Cobble", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    {
        // ===== LEFT NAV =====
        ImGui::BeginChild("nav", { ui::s(176), ImGui::GetWindowHeight() }, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        {
            // Brand area
            ImGui::BeginChild("brand", { ImGui::GetWindowWidth(), ui::s(42) }, 0, ImGuiWindowFlags_NoBackground);
            {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetWindowPos(), 
                    { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
                    ui::GetColor(ImGuiCol_ChildBg), 
                    GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersTop
                );
                ImGui::GetWindowDrawList()->AddRectFilled(
                    { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
                    { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() }, 
                    GetSchemeColor()
                );
                
                // Brand text "Cobble"
                const char* brand = "Cobble";
                ImVec2 brandSize = font(3)->CalcTextSizeA(font(3)->FontSize, FLT_MAX, 0.f, brand);
                ImGui::GetWindowDrawList()->AddText(
                    font(3), font(3)->FontSize,
                    { ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2 - brandSize.x / 2, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - brandSize.y / 2 - 2 },
                    GetSchemeColor(), brand
                );
            }
            ImGui::EndChild();
            
            // Tabs area - with padding from brand
            ImGui::SetCursorPosY(ui::s(50));
            ImGui::BeginChild("tabs", { ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - ui::s(50) }, 0, ImGuiWindowFlags_NoBackground);
            {
                // Background
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetWindowPos(), 
                    ImGui::GetWindowPos() + ImGui::GetWindowSize(), 
                    ui::GetColor(ImGuiCol_WindowBg, 1.0f), 
                    GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom
                );
                
                // Add top padding before first tab
                ImGui::Dummy({ 0, ui::s(6) });
                
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, 1 });
                ImGui::BeginGroup();
                {
                    for (size_t i = 0; i < ui::tabs.size(); ++i) {
                        ui::tab((int)i);
                    }
                }
                ImGui::EndGroup();
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        
        // ===== MAIN CONTENT =====
        ImGui::SetCursorPos({ ui::s(185), 0 });
        ImGui::BeginChild("main", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoBackground);
        {
            // Header
            ImGui::BeginChild("main header", { ImGui::GetWindowWidth(), ui::s(42) }, 0, ImGuiWindowFlags_NoBackground);
            {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetWindowPos(), 
                    { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
                    ui::GetColor(ImGuiCol_ChildBg), 
                    GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersTop
                );
                ImGui::GetWindowDrawList()->AddRectFilled(
                    { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
                    { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() }, 
                    GetSchemeColor()
                );
                
                // Tab icon
                ImGui::GetWindowDrawList()->AddText(
                    { ImGui::GetWindowPos().x + ui::s(15), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - ImGui::CalcTextSize(ui::tabs[ui::cur_tab].m_icon, 0, 1).y / 2 }, 
                    ImGui::GetColorU32(ImGuiCol_Text, ui::anim), 
                    ui::tabs[ui::cur_tab].m_icon, 
                    ImGui::FindRenderedTextEnd(ui::tabs[ui::cur_tab].m_icon)
                );
                
                // Tab name
                ImGui::GetWindowDrawList()->AddText(
                    font(2), font(2)->FontSize,
                    { ImGui::GetWindowPos().x + ui::s(40), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - font(2)->CalcTextSizeA(font(2)->FontSize, FLT_MAX, 0.f, ui::tabs[ui::cur_tab].m_name).y / 2 }, 
                    ImGui::GetColorU32(ui::tabs[ui::cur_tab].m_subtabs.size() > 0 ? ImGuiCol_TextDisabled : ImGuiCol_Text, ui::anim), 
                    ui::tabs[ui::cur_tab].m_name
                );
                
                // Subtab indicator
                if (ui::tabs[ui::cur_tab].m_subtabs.size() > 0) {
                    float nameWidth = font(2)->CalcTextSizeA(font(2)->FontSize, FLT_MAX, 0.f, ui::tabs[ui::cur_tab].m_name).x;
                    ImGui::RenderArrow(ImGui::GetWindowDrawList(), 
                        { ImGui::GetWindowPos().x + ui::s(47) + nameWidth, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - ui::s(10) }, 
                        ImGui::GetColorU32(ImGuiCol_Text, ui::anim * ui::content_anim), 
                        ImGuiDir_Right, ui::s(12)
                    );
                    
                    const char* subtabName = ui::tabs[ui::cur_tab].m_subtabs[ui::tabs[ui::cur_tab].cur_subtab];
                    ImGui::GetWindowDrawList()->AddText(
                        font(2), font(2)->FontSize,
                        { ImGui::GetWindowPos().x + ui::s(60) + nameWidth, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - font(2)->CalcTextSizeA(font(2)->FontSize, FLT_MAX, 0.f, subtabName).y / 2 }, 
                        GetSchemeColor(ui::anim * ui::content_anim), 
                        subtabName
                    );
                }
            }
            ImGui::EndChild();
            
            // Content background
            ImGui::GetWindowDrawList()->AddRectFilled(
                { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ui::s(42) }, 
                ImGui::GetWindowPos() + ImGui::GetWindowSize(), 
                ui::GetColor(ImGuiCol_WindowBg), 
                GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom
            );
            
            // Content
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ui::s(10), ui::s(10) });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { ui::s(15), ui::s(15) });
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ui::anim * ui::content_anim);
            ImGui::SetCursorPosY(ui::s(42));
            ImGui::BeginChild("content", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding);
            {
                ui::render_page();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(); // Alpha animation
    
    // Handle animations
    ui::handle_anims();
}

void Menu::Hotkey(const char* label, Module* module) {
    // Simplified hotkey display
    ImGui::SameLine();
    ImGui::PushID(label);
    
    std::string text = (moduleToBind == module) ? "..." : "[?]";
    
    if (ImGui::SmallButton(text.c_str())) {
        moduleToBind = module;
    }
    
    ImGui::PopID();
}
