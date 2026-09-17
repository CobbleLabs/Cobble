#include "bridgeassist.h"
#include <Windows.h>
#include <cmath>
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

BridgeAssist* bridgeAssist = nullptr;

int BridgeAssist::findBlockInInventory(int preferredId) const {
    const int currentSlot = player.getCurrentSlot();

    if (preferredId > 0 && preferredId <= 255) {
        for (int slot = 0; slot < 9; ++slot) {
            if (slot == currentSlot)
                continue;
            if (player.getItemInSlot(slot) == preferredId)
                return slot;
        }
    }

    for (int slot = 0; slot < 9; ++slot) {
        if (slot == currentSlot)
            continue;
        int id = player.getItemInSlot(slot);
        if (id > 0 && id <= 255)
            return slot;
    }

    for (int slot = 9; slot <= 35; ++slot) {
        int id = player.getItemInSlot(slot);
        if (id > 0 && id <= 255)
            return slot;
    }

    return -1;
}

void BridgeAssist::Sneak() {
    m_isEdge = true;
    if (!m_wasSneaking) {
        mc.setKeyBindState(mc.getSneakKey(), true);
        m_wasSneaking = true;
    }
}

void BridgeAssist::UnSneak() {
    m_isEdge = false;
    if (m_wasSneaking) {
        mc.setKeyBindState(mc.getSneakKey(), false);
        m_wasSneaking = false;
    }
}

void BridgeAssist::onUpdate() {
    bool isSneaking = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!enabled) {
        if (m_wasSneaking)
            UnSneak();
        m_shiftInitiated = false;
        return;
    }

    if (Menu::isOpen || mc.isInGui()) {
        if (m_wasSneaking)
            UnSneak();
        m_shiftInitiated = false;
        return;
    }

    if (!player.isValid())
        return;

    // Only activate if holding a block (IDs 1-255)
    int heldItemId = player.getHeldItemId();
    if (heldItemId <= 0 || heldItemId > 255) {
        if (settings.autoRefillBlocks && (m_shiftInitiated || m_wasSneaking)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRefillTime).count();
            if (elapsed >= 200) {
                int refillSlot = findBlockInInventory(m_lastBlockId);
                if (refillSlot >= 0) {
                    if (refillSlot < 9) {
                        player.setCurrentSlot(refillSlot);
                    } else {
                        mc.windowClickSwap(refillSlot, player.getCurrentSlot());
                    }
                    m_lastRefillTime = now;
                    Sneak();
                    return;
                }
            } else {
                Sneak();
                return;
            }
        }

        if (m_wasSneaking)
            UnSneak();
        m_shiftInitiated = false;
        return;
    }

    m_lastBlockId = heldItemId;

    // Detect sneak initiation
    if (settings.onlyWhenSneaking) {
        if (!m_shiftInitiated && isSneaking)
            m_shiftInitiated = true;
    }

    // Pitch check (looking down)
    float pitch = player.getPitch();
    float pitchThreshold = settings.onlyLookingDown ? 60.0f : 45.0f;

    if (pitch < pitchThreshold) {
        if (m_wasSneaking)
            UnSneak();
        m_shiftInitiated = false;
        return;
    }

    // Requires sneak initiation
    if (settings.onlyWhenSneaking && !m_shiftInitiated)
        return;

    // Raycast downward
    m_from = Minecraft::Vec3((float)player.getPosX(), (float)player.getPosY() + 1.0f, (float)player.getPosZ());

    Minecraft::Vec3 m_to = Minecraft::Vec3(m_from.x, m_from.y - 100.0f, m_from.z);
    Minecraft::Vec3 hitBlockPos = {};

    auto res = mc.rayTraceBlocks(m_from, m_to, hitBlockPos, false);

    // Height difference between player and detected block
    float diffY = (float)player.getPosY() - hitBlockPos.y - 1.0f;

    // Jump handling
    static bool jumped = false;

    m_wasSneaking = m_isEdge;
    m_isEdge = false;

    if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        jumped = true;

    if (jumped) {
        UnSneak();
        double motionY = player.getMotionY();
        if (motionY < -0.4 || std::abs(diffY) < 0.001f)
            jumped = false;
        return;
    }

    // Don't act if falling too fast
    double motionY = player.getMotionY();
    if (std::abs(motionY) > 0.5) {
        UnSneak();
        return;
    }

    // No block below or block is too far -> Sneak
    if (static_cast<int>(diffY) != 0 || res == Minecraft::RAYTRACE_BLOCKS_RESULT::NO_HIT) {
        Sneak();
    } else {
        UnSneak();
    }
}

void BridgeAssist::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
        },
        "Mode",
        [this]() {
            ui::Checkbox("Only When Sneaking", &settings.onlyWhenSneaking);
            ui::Checkbox("Only When Looking Down", &settings.onlyLookingDown);
            ui::Checkbox("Auto Refill Blocks", &settings.autoRefillBlocks);
        });
}

void BridgeAssist::saveSettings(json::Value& node) const {
    node["onlyLookingDown"] = settings.onlyLookingDown;
    node["onlyWhenSneaking"] = settings.onlyWhenSneaking;
    node["autoRefillBlocks"] = settings.autoRefillBlocks;
}

void BridgeAssist::loadSettings(const json::Value& node) {
    settings.onlyLookingDown = node["onlyLookingDown"].as_bool(settings.onlyLookingDown);
    settings.onlyWhenSneaking = node["onlyWhenSneaking"].as_bool(settings.onlyWhenSneaking);
    settings.autoRefillBlocks = node["autoRefillBlocks"].as_bool(settings.autoRefillBlocks);
}
