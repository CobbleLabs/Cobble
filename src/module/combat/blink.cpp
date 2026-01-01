#include "blink.h"
#include "../../sdk/minecraft.h"
#include "../../sdk/render_data.h"
#include "../../hooks/winsock_hooks.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"

#include <imgui.h>
#include <cmath>

Blink* blink = nullptr;

Blink::Blink() : Module("Blink")
{
    keybind = 0;
    locked = true; 
}

void Blink::onEnable()
{
    if (!player.isValid()) {
        log_err("Blink: Player not valid!");
        enabled = false;
        return;
    }
    
    if (!WinsockHooks::IsInitialized()) {
        log_err("Blink: Network hooks not initialized!");
        enabled = false;
        return;
    }
    
    startTime = std::chrono::steady_clock::now();
    wasAttacking = false;
    tickCounter = 0;
    
    // Capture current position as frozen position (where server thinks we are)
    serverX = player.getPosX();
    serverY = player.getPosY();
    serverZ = player.getPosZ();
    
    // Reset packet counters
    WinsockHooks::ResetBlockedCount();
    
    // Enable network blocking via NtDeviceIoControlFile hook (catches Java NIO)
    WinsockHooks::StopOutbound = true;
    
    log_ok("Blink ON - Blocking at (%.1f, %.1f, %.1f)", serverX, serverY, serverZ);
}

void Blink::onDisable()
{
    // Get packet stats before releasing
    int wsaBlocked = WinsockHooks::GetBlockedCount();
    int sendCalls = WinsockHooks::GetSendCount();
    int afdBlocked = WinsockHooks::GetAfdSendBlockedCount();
    
    // Calculate how long we were blinking
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - startTime).count();
    
    // Calculate distance moved
    double dx = player.getPosX() - serverX;
    double dy = player.getPosY() - serverY;
    double dz = player.getPosZ() - serverZ;
    double dist = sqrt(dx*dx + dy*dy + dz*dz);
    
    // Disable network blocking - all blocked packets will now flow
    WinsockHooks::StopOutbound = false;
    
    log_ok("Blink OFF - %.2fs, %.1fm | WSA:%d send:%d AFD:%d", 
           elapsed, dist, wsaBlocked, sendCalls, afdBlocked);
}

void Blink::onUpdate()
{
    if (!enabled) return;
    if (Menu::isOpen) return;
    
    tickCounter++;
    
    if (!player.isValid()) {
        log_warn("Blink: Player became invalid, disabling");
        setEnabled(false);
        return;
    }
    
    // Log periodic updates with all packet counts
    if (tickCounter % 40 == 0) {
        double dx = player.getPosX() - serverX;
        double dy = player.getPosY() - serverY;
        double dz = player.getPosZ() - serverZ;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);
        
        int afdBlocked = WinsockHooks::GetAfdSendBlockedCount();
        log_debug("Blink: Tick %d, moved %.1fm, AFD blocked: %d", 
            tickCounter, dist, afdBlocked);
    }
    
    // Check max duration
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - startTime).count();
    
    if (elapsed >= settings.maxDuration) {
        log_warn("Blink: Max duration (%.1fs) reached", settings.maxDuration);
        setEnabled(false);
        return;
    }
    
    // Check disable on attack
    if (settings.disableOnAttack) {
        bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        
        if (isAttacking && !wasAttacking) {
            if (!mc.isTargetingBlock() && player.isValid()) {
                log_debug("Blink: Attack detected, disabling");
                setEnabled(false);
                return;
            }
        }
        wasAttacking = isAttacking;
    }
}

void Blink::onRender()
{
    if (!enabled) return;
    if (!settings.showDebug) return;
    if (!player.isValid()) return;
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    
    // Draw server position (red box each frame)
    {
        float minX = (float)serverX - 0.3f;
        float maxX = (float)serverX + 0.3f;
        float minY = (float)serverY;
        float maxY = (float)serverY + 1.8f;
        float minZ = (float)serverZ - 0.3f;
        float maxZ = (float)serverZ + 0.3f;
        
        Vector3 corners[8] = {
            {minX, minY, minZ}, {maxX, minY, minZ},
            {maxX, minY, maxZ}, {minX, minY, maxZ},
            {minX, maxY, minZ}, {maxX, maxY, minZ},
            {maxX, maxY, maxZ}, {minX, maxY, maxZ}
        };
        
        Vector2 screen[8];
        bool allValid = true;
        for (int i = 0; i < 8; i++) {
            if (!CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screen[i])) {
                allValid = false;
                break;
            }
        }
        
        if (allValid) {
            ImU32 red = IM_COL32(255, 50, 50, 180);
            int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},
                {4,5},{5,6},{6,7},{7,4},
                {0,4},{1,5},{2,6},{3,7}
            };
            for (int i = 0; i < 12; i++) {
                draw->AddLine(
                    ImVec2(screen[edges[i][0]].x, screen[edges[i][0]].y),
                    ImVec2(screen[edges[i][1]].x, screen[edges[i][1]].y),
                    red, 2.0f
                );
            }
        }
    }
    
    // Draw distance info
    double dx = player.getPosX() - serverX;
    double dy = player.getPosY() - serverY;
    double dz = player.getPosZ() - serverZ;
    double dist = sqrt(dx*dx + dy*dy + dz*dz);
    
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - startTime).count();
    
    int afdBlocked = WinsockHooks::GetAfdSendBlockedCount();
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Blink: %.1fm | %.1fs | AFD blocked: %d", 
        dist, elapsed, afdBlocked);
    
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    
    draw->AddText(
        ImVec2(screenSize.x / 2 - textSize.x / 2, screenSize.y / 2 + 50),
        IM_COL32(255, 255, 255, 255),
        buf
    );
}

void Blink::onRenderMenu()
{
    ui::two_column(
        "Settings", [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            
            ui::SliderFloat("Max Duration", &settings.maxDuration, 1.0f, 15.0f, "%.1fs");
        },
        "Options", [this]() {
            ui::Checkbox("Disable on Attack", &settings.disableOnAttack);
            ui::Checkbox("Show Debug Visual", &settings.showDebug);
        }
    );
}
