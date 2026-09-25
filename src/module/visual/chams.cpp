#include "chams.h"

#include <algorithm>

#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

Chams* chams = nullptr;

Chams::Chams() : Module("Chams") {
    keybind = 0;
    usesPlayerCache = true;
}

void Chams::onRender3D() {
    if (!enabled || !mc.isValid())
        return;

    const auto snap = VisualCache::GetSnapshot();
    if (!snap.valid)
        return;

    const float partial = std::clamp(snap.partialTicks, 0.0f, 1.0f);
    const double localX = snap.localLastX + (snap.localX - snap.localLastX) * partial;
    const double localY = snap.localLastY + (snap.localY - snap.localLastY) * partial;
    const double localZ = snap.localLastZ + (snap.localZ - snap.localLastZ) * partial;
    const double maxDistanceSq = static_cast<double>(settings.range) * settings.range;

    const float r = settings.color[0];
    const float g = settings.color[1];
    const float b = settings.color[2];
    const float a = std::clamp(settings.color[3], 0.05f, 1.0f);

    for (const auto& target : snap.players) {
        if (target.entityId <= 0 || target.health <= 0.0f)
            continue;
        if (target.isInvisible && !settings.drawInvisiblePlayers)
            continue;

        const double x = target.lastPosX + (target.posX - target.lastPosX) * partial;
        const double y = target.lastPosY + (target.posY - target.lastPosY) * partial;
        const double z = target.lastPosZ + (target.posZ - target.lastPosZ) * partial;
        const double dx = x - localX;
        const double dy = y - localY;
        const double dz = z - localZ;
        if (dx * dx + dy * dy + dz * dz > maxDistanceSq)
            continue;

        // fill=false: keep the real textured skin and draw it through walls. The flat-colour
        // fill variant is gone, so the parameter is now a constant rather than a mode.
        mc.renderEntityChams(target.entityId, r, g, b, a, false);
    }
}

void Chams::onRenderMenu() {
    bool enabledValue = enabled;

    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ui::Checkbox("Draw Invisibles", &settings.drawInvisiblePlayers);
            ui::SliderFloat("Range", &settings.range, 16.0f, 255.0f, "%.0fm");
        },
        "Appearance",
        [this]() {
            ui::color("Color", settings.color, false);
            ui::SliderFloat("Opacity", &settings.color[3], 0.05f, 1.0f, "%.2f");
        });
}

void Chams::saveSettings(json::Value& node) const {
    node.remove("mode");  // the Fill/See Player choice is gone; See Player is the behaviour
    node["drawInvisiblePlayers"] = settings.drawInvisiblePlayers;
    node["range"] = settings.range;
    json::writeColor(node, "color", settings.color);
}

void Chams::loadSettings(const json::Value& node) {
    settings.drawInvisiblePlayers = node["drawInvisiblePlayers"].as_bool(settings.drawInvisiblePlayers);
    settings.range = std::clamp(node["range"].as_float(settings.range), 16.0f, 255.0f);
    json::readColor(node, "color", settings.color);
    settings.color[3] = std::clamp(settings.color[3], 0.05f, 1.0f);
}
