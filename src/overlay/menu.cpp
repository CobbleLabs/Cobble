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
#include "../module/combat/hitselect.h"
#include "../module/visual/nametags.h"
#include "../cobble.hpp"

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
                ui::SliderFloat("H Speed", &aimAssist.settings.horizontalSpeed, 1.0f, 100.0f, "%.0f");
                ui::SliderFloat("V Speed", &aimAssist.settings.verticalSpeed, 1.0f, 100.0f, "%.0f");
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
    
    // Combat Tab - HitSelect page
    ui::add_page(0, []() {
        ImGui::BeginGroup();
        {
            ui::begin_child("General", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::CheckboxBind("Enabled", &hitSelect.enabled, &hitSelect.keybind);
                ImGui::Spacing();
                
                // Mode selector
                const char* modes[] = { "Burst", "Criticals" };
                ImGui::Text("Mode");
                ImGui::SameLine();
                if (ImGui::BeginCombo("##mode", modes[hitSelect.settings.mode], ImGuiComboFlags_NoArrowButton)) {
                    for (int i = 0; i < 2; i++) {
                        bool selected = (hitSelect.settings.mode == i);
                        if (ImGui::Selectable(modes[i], selected))
                            hitSelect.settings.mode = i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                
                ImGui::Spacing();
                int cancelRate = hitSelect.settings.cancelRateInCombat;
                if (ui::SliderInt("Cancel Rate", &cancelRate, 0, 100, "%d%%"))
                    hitSelect.settings.cancelRateInCombat = cancelRate;
                    
                int missedRate = hitSelect.settings.cancelRateMissedSwings;
                if (ui::SliderInt("Missed Swings", &missedRate, 0, 100, "%d%%"))
                    hitSelect.settings.cancelRateMissedSwings = missedRate;
            }
            ui::end_child();
        }
        ImGui::EndGroup();
        
        ImGui::SameLine();
        
        ImGui::BeginGroup();
        {
            ui::begin_child("Options", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
            {
                ui::Checkbox("Fake Swing", &hitSelect.settings.fakeSwing);
                
                if (hitSelect.settings.mode == 0) {
                    // Burst mode options
                    int pauseDur = hitSelect.settings.pauseDuration;
                    if (ui::SliderInt("Pause (ms)", &pauseDur, 100, 600, "%d"))
                        hitSelect.settings.pauseDuration = pauseDur;
                } else {
                    // Criticals mode options
                    ui::Checkbox("Disable During KB", &hitSelect.settings.disableDuringKnockback);
                }
                
                ImGui::Spacing();
                
                // Humanization settings
                int humanErr = hitSelect.settings.humanErrorRate;
                if (ui::SliderInt("Human Error", &humanErr, 0, 20, "%d%%"))
                    hitSelect.settings.humanErrorRate = humanErr;
                    
                int timingRand = hitSelect.settings.timingRandomization;
                if (ui::SliderInt("Timing Rand", &timingRand, 0, 100, "%d ms"))
                    hitSelect.settings.timingRandomization = timingRand;
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
    
    // Settings Tab
    ui::add_page(2, []() {
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
            ui::begin_child("Actions", { (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0 });
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
    
    // Initialize pages once
    InitializePages();
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Update UI scale based on resolution
    ui::updateScale();
    
    // Sync global alpha modifier for manual rendering
    ui::alpha_mod = openAnim;
    
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
                    ui::GetColor(ImGuiCol_WindowBg, 0.9f), 
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
