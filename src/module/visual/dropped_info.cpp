#include "dropped_info.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../ui/menu/menu.h"
#include "../../sdk/network/lunar_sender.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../sdk/player/render_data.h"

DroppedInfo* droppedInfo = nullptr;

DroppedInfo::DroppedInfo() : Module("DroppedInfo") {
    keybind = 0;
    enabled = true; // Enabled by default as requested logic
    renders = true; // Must be true for onRender to be called
    settings.waypointColor[0] = 1.0f; // Red default
    settings.waypointColor[1] = 0.0f;
    settings.waypointColor[2] = 0.0f;
    settings.waypointColor[3] = 1.0f;
}

void DroppedInfo::onEnable() {
    potionCount = 0;
}

void DroppedInfo::onUpdate() {
    if (!Java::Env() || !Java::Initialized)
        return;
    
    // Scan items
    // We reuse logic similar to ItemESP but focusing only on potions
    std::vector<ItemEntityInfo> items = mc.getItemEntityList();
    

    
    int total = 0;
    bool foundOne = false;
    double firstX = 0, firstY = 0, firstZ = 0;
    
    for (const auto& item : items) {
        // Potion ID is 373
        if (item.itemId == 373) {
            int dmg = item.damage;
            // Check for Splash Health II Potions ONLY
            // 16421: Splash Health II
            // 16453: Splash Health II (Alt)
            if (dmg == 16421 || dmg == 16453) {
                total += item.stackSize;
                
                // Track first one found for Waypoint
                if (!foundOne) {
                    firstX = item.posX;
                    firstY = item.posY;
                    firstZ = item.posZ;
                    foundOne = true;
                }
            }
        }
    }
    

        std::lock_guard<std::mutex> lock(mutex);
        potionCount = total;
        
        // Handle Lunar Waypoint
        if (settings.showWaypoint) {
            if (foundOne && !hasTarget) {
                 // New target found
                 int color = (int)(settings.waypointColor[0] * 255) << 16 | 
                             (int)(settings.waypointColor[1] * 255) << 8 | 
                             (int)(settings.waypointColor[2] * 255);
                             
                 LunarSender::SendWaypoint("Potions", (int)firstX, (int)firstY, (int)firstZ, color, true);
                 hasTarget = true;
                 targetX = firstX;
                 targetY = firstY;
                 targetZ = firstZ;
            } else if (!foundOne && hasTarget) {
                 // Lost target
                 LunarSender::RemoveWaypoint("Potions");
                 hasTarget = false;
            } else if (foundOne && hasTarget) {
                 // Update position if moved significantly? 
                 // Waypoints usually stick. If position changes (e.g. pushed), remove and re-add.
                 if (abs(firstX - targetX) > 1.0 || abs(firstY - targetY) > 1.0 || abs(firstZ - targetZ) > 1.0) {
                      LunarSender::RemoveWaypoint("Potions");
                      
                      int color = (int)(settings.waypointColor[0] * 255) << 16 | 
                                  (int)(settings.waypointColor[1] * 255) << 8 | 
                                  (int)(settings.waypointColor[2] * 255);
                      LunarSender::SendWaypoint("Potions", (int)firstX, (int)firstY, (int)firstZ, color, true);
                      
                      targetX = firstX;
                      targetY = firstY;
                      targetZ = firstZ;
                 }
            }
        } else if (hasTarget) {
             // Disabled via settings
             LunarSender::RemoveWaypoint("Potions");
             hasTarget = false;
        }
    }
}

void DroppedInfo::onRender() {
    bool open = Menu::isOpen;
    if (!enabled && !open) return;
    
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw) return;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Dropped Pots: %d", potionCount);
    
    // Always render text if enabled, or if menu is open (for positioning)
    if (!enabled && open) {
        snprintf(buf, sizeof(buf), "Dropped Pots: 0"); // Example text
    } else if (!enabled) {
        return;
    }
    
    float scale = settings.scale;
    ImGui::SetWindowFontScale(scale);
    
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    float x = settings.x;
    float y = settings.y;
    float padding = 5.0f * scale;
    
    // Draggable Logic
    if (open) {
         ImVec2 mousePos = ImGui::GetMousePos();
         bool hover = mousePos.x >= x - padding && mousePos.x <= x + textSize.x + padding &&
                      mousePos.y >= y - padding && mousePos.y <= y + textSize.y + padding;
         
         if (hover) {
             draw->AddRect(ImVec2(x - padding, y - padding), 
                           ImVec2(x + textSize.x + padding, y + textSize.y + padding), 
                           IM_COL32(255, 255, 255, 255));
                           
             if (ImGui::IsMouseClicked(0)) {
                 dragging = true;
                 dragOffsetX = mousePos.x - x;
                 dragOffsetY = mousePos.y - y;
             }
         }
         
         if (dragging) {
             if (ImGui::IsMouseDown(0)) {
                 settings.x = mousePos.x - dragOffsetX;
                 settings.y = mousePos.y - dragOffsetY;
                 x = settings.x; // Update local for current frame render
                 y = settings.y;
             } else {
                 dragging = false;
             }
         }
    }
    
    if (settings.background) {
        draw->AddRectFilled(ImVec2(x - padding, y - padding), 
                            ImVec2(x + textSize.x + padding, y + textSize.y + padding), 
                            IM_COL32(0, 0, 0, 160), 4.0f);
    }
    
    draw->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 255), buf);
    draw->AddText(ImVec2(x, y), IM_COL32(255, 85, 85, 255), buf); // Red for Health
    
    ImGui::SetWindowFontScale(1.0f);
    
    // Render Waypoint - REMOVED custom rendering as we are using Lunar Native Waypoints now
    // But we keep the Draggable HUD logic above
}

void DroppedInfo::onRenderMenu() {
    ui::CheckboxBind("Enabled", &enabled, &keybind);
    ui::SliderFloat("Scale", &settings.scale, 0.5f, 3.0f, "%.1f");
    ui::SliderFloat("X", &settings.x, 0.0f, 1000.0f, "%.0f");
    ui::SliderFloat("Y", &settings.y, 0.0f, 1000.0f, "%.0f");
    ui::Checkbox("Background", &settings.background);
    ImGui::Spacing();
    ui::Checkbox("Waypoint", &settings.showWaypoint);
    ui::color("Waypoint Color", settings.waypointColor);
}
