#include "widgets.hpp"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include "../../module/module.h"
#include "../hud/hud_color.hpp"
#include "../menu/menu.h"
#include "icons.hpp"
#include "theme.hpp"

using namespace ImGui;

namespace {
struct DescriptionEntry {
    const char* context;
    const char* key;
    const char* value;
    const char* text;
};

static std::string g_descriptionContext;

static const DescriptionEntry kModuleDescriptions[] = {
    {"", "AutoClicker", "", "Clicks automatically while you hold the attack button."},
    {"", "AimAssist", "", "Smoothly guides your aim toward a valid player target."},
    {"", "AimBow", "", "Aims the bow at the nearest player, solving the arrow arc and lead."},
    {"", "Velocity", "", "Controls how much enemy knockback affects your movement."},
    {"", "Reach", "", "Extends the distance at which attacks can connect."},
    {"", "HitSelect", "", "Times attacks around combo state to improve hit selection."},
    {"", "NoHitDelay", "", "Removes the client-side delay between valid attacks."},
    {"", "Criticals", "", "Automates critical-hit movement when its conditions are met."},
    {"", "WTap", "", "Briefly resets forward movement or sprint after attacks to control spacing."},
    {"", "Backtrack", "", "Targets recent server positions to compensate for movement and latency."},
    {"", "Piercing", "", "Finds and attacks valid entities through obstructing blocks."},
    {"", "NameTags", "", "Renders readable, customizable information above players."},
    {"", "ESP", "", "Highlights players through terrain with configurable boxes and health."},
    {"", "ItemESP", "", "Highlights dropped items so they remain visible through terrain."},
    {"", "Tracers", "", "Draws directional lines from your screen to nearby players."},
    {"", "BlockESP", "", "Highlights selected ores and containers inside the configured range."},
    {"", "Trajectories", "", "Predicts the flight path and landing point of projectiles."},
    {"", "AntiDebuff", "", "Removes disruptive visual effects such as blindness and nausea."},
    {"", "ArrayList", "", "Controls the on-screen list of currently enabled modules."},
    {"", "Watermark", "", "Shows the Cobble client tag, version and FPS in the HUD."},
    {"", "DroppedInfo", "", "Shows extra information about the item you just dropped."},
    {"", "Notifications", "", "Controls pop-up messages for module and client events."},
    {"", "Timer", "", "Changes the client game timer to speed up or slow down actions."},
    {"", "TimerRange", "", "Applies a random timer multiplier before, after, per hit, or constantly."},
    {"", "NoSlowDown", "", "Prevents item use from applying the normal movement slowdown."},
    {"", "NoJumpDelay", "", "Removes the short delay between consecutive jumps."},
    {"", "FakeLag", "", "Adds calculated movement latency from your measured RTT and configured target ping."},
    {"", "Blink", "", "Queues movement packets temporarily, then releases them together."},
    {"", "Sprint", "", "Keeps sprint active automatically while you move."},
    {"", "ArmorSwitch", "", "Switches between configured armor sets with one action."},
    {"", "AutoRefill", "", "Moves replacement soup or potions into empty hotbar slots."},
    {"", "FastPlace", "", "Reduces the delay between right-click item placements."},
    {"", "NoItemRelease", "", "Keeps supported item-use actions active according to the selected mode."},
    {"", "BridgeAssist", "", "Helps you sneak safely at block edges while bridging."},
    {"", "Clutch", "", "Places reachable blocks only when your predicted path would fall into the void."},
    {"", "Breaker", "", "Finds nearby beds and breaks them through surrounding blocks."},
    {"", "AutoTool", "", "Selects the best hotbar tool for the block or target."},
    {"", "InvManager", "", "Organizes the inventory and hotbar using your saved preferences."},
    {"", "TCP Optimizer", "", "Optimizes Windows TCP settings, packet priority and adapter tuning for Minecraft."},
    {"", "FastBreak", "", "Removes the cooldown between block mining hits so blocks break faster."},
};

static const DescriptionEntry kOptionDescriptions[] = {
    {"", "Enabled", "", "Turns this module on or off. The key box sets its toggle bind."},
    {"", "Weapon Only", "", "Runs only while a supported weapon is held."},
    {"", "Weapons Only", "", "Runs only while a supported weapon is held."},
    {"", "Mouse Pressed", "", "Runs only while the required mouse button is held."},
    {"", "Break Blocks", "", "Lets normal block breaking take priority over this feature."},
    {"", "Target Invisibles", "", "Allows invisible players to be considered valid targets."},
    {"", "Range", "", "Sets the maximum distance at which this option can operate."},
    {"", "Distance", "", "Sets the maximum distance at which this option can operate."},
    {"", "FOV", "", "Limits activation to targets inside this horizontal field of view."},
    {"", "Chance", "", "Sets the probability that the action is applied."},
    {"", "Delay", "", "Sets how long the module waits before performing its action."},
    {"", "Speed", "", "Controls how quickly this feature reacts or operates."},
    {"", "Scale", "", "Changes the rendered size without changing its information."},
    {"", "Background", "", "Draws a subtle background behind the rendered content."},
    {"", "Filled", "", "Fills the inside of the rendered shape."},
    {"", "Outline", "", "Draws an outline to improve visibility against the world."},
    {"", "Color", "", "Sets the color used by this rendered element."},
    {"", "Thickness", "", "Controls the width of the rendered line."},
    {"", "Line Thickness", "", "Controls the width of the predicted trajectory line."},
    {"", "Box Color", "", "Sets the color used for the outer player or item box."},
    {"", "Fill Color", "", "Sets the translucent color drawn inside highlighted boxes."},
    {"", "Outline Color", "", "Sets the border color used to separate highlights from the world."},
    {"", "Line Color", "", "Sets the color of the predicted trajectory line."},
    {"", "Tracer Color", "", "Sets the fixed color used by tracer lines."},
    {"", "Enemy Color", "", "Sets the highlight color used for players detected as enemies."},
    {"", "Shadow Color", "", "Sets the text-shadow color used behind rendered labels."},
    {"", "Only Sprinting", "", "Runs only while your player is sprinting."},
    {"", "Sprinting Only", "", "Runs only while your player is sprinting."},
    {"", "Moving Only", "", "Runs only while your player is moving."},
    {"", "Moving Forward", "", "Runs only while forward movement is being pressed."},
    {"", "Looking at Player", "", "Runs only while your crosshair is aimed at a player."},
    {"", "Looking At Player", "", "Runs only while your crosshair is aimed at a player."},
    {"", "Holding Weapon", "", "Runs only while a supported weapon is held."},
    {"", "Only With Sword", "", "Restricts the action to swords."},
    {"", "Only Attacking", "", "Runs only while an attack is being performed."},
    {"", "Only When Sneaking", "", "Runs only while the sneak key is held."},
    {"", "Only When Looking Down", "", "Runs only while looking toward the edge below you."},
    {"", "Ignore Friends", "", "Excludes players saved in your friends list from this module."},
    {"", "Ignore Teams", "", "Excludes players detected as teammates by scoreboard, color, or server team data."},
    {"", "Silent Mode", "", "Performs inventory changes without visibly opening or moving the selected slot."},
    {"", "Retry on Fail", "", "Retries the operation when the server rejects or misses a change."},
    {"", "On Inventory Open", "", "Runs automatically whenever the inventory screen is open."},
    {"", "Switch Back", "", "Returns to the previously selected slot after the action."},
    {"", "Stop On Hitbox", "", "Stops aim correction once the crosshair is already inside the target hitbox."},
    {"", "Entity Lock", "", "Keeps the acquired entity until it dies, disappears, or fails the target filters."},
    {"", "Always Aim", "", "Allows aim assistance without requiring the attack button."},
    {"", "Aim Closest Point", "", "Aims at the nearest point of the target hitbox instead of its center."},
    {"", "Reaction Time", "", "Adds a delay before aim assistance begins tracking a newly acquired target."},
    {"", "Jitter", "", "Adds configurable randomized micro-movement to AimAssist output."},
    {"", "Randomization", "", "Selects how AutoClicker distributes clicks inside the configured CPS range."},
    {"", "Click Delay (ms)", "", "Sets the delay in milliseconds before the automated click is sent."},
    {"", "Delay (ms)", "", "Sets how many milliseconds the module waits before acting."},
    {"", "Speed (ms)", "", "Sets the interval in milliseconds between repeated actions."},
    {"", "Horizontal", "", "Sets the percentage of horizontal knockback retained."},
    {"", "Vertical", "", "Sets the percentage or behavior of vertical knockback."},
    {"", "Jump Chance", "", "Sets how often a jump reset is attempted after taking a hit."},
    {"", "Double Clicks", "", "Allows an additional click when the selected velocity behavior requires it."},
    {"", "Variance", "", "Adds small variation to avoid applying an identical value every time."},
    {"", "Tick Spread", "", "Spreads the adjustment across multiple game ticks."},
    {"", "Micro Delay", "", "Adds a very short delay before the adjustment is applied."},
    {"", "Min CPS", "", "Sets the lowest click rate used by the click generator."},
    {"", "Max CPS", "", "Sets the highest click rate used by the click generator."},
    {"", "CPS", "", "Sets the target clicks per second."},
    {"", "Stable CPS", "", "Reduces random variation to keep the click rate more consistent."},
    {"", "Inventory", "", "Allows clicking while an inventory screen is open."},
    {"", "Hit Chance", "", "Sets the percentage of eligible attacks that receive extended reach."},
    {"", "Tick Delay", "", "Adds game ticks between reach attempts."},
    {"", "Min Combo Hits", "", "Requires this many successful combo hits before selection logic activates."},
    {"", "Combo Detection", "", "Sets how long recent hits remain part of the same combo."},
    {"", "Wait For First Hit", "", "Lets the first hit pass before beginning the selection delay."},
    {"", "Wait Duration", "", "Sets how long HitSelect waits before allowing the next selected hit."},
    {"", "Ticks", "", "Sets how many previous movement ticks are available for backtracking."},
    {"", "Latency", "", "Sets the simulated history window in milliseconds."},
    {"", "Render Previous Ticks", "", "Draws stored historical positions for visual feedback."},
    {"", "Render Server Pos", "", "Draws the position currently associated with the server-side target."},
    {"", "Show Box", "", "Renders a box around the selected historical position."},
    {"", "Show Health", "", "Adds the player's current health to the nametag."},
    {"", "Show Distance", "", "Adds the distance from you to the nametag."},
    {"", "Show Armor", "", "Displays the player's equipped armor beside the nametag."},
    {"", "Show Effects", "", "Displays active potion effects in the nametag."},
    {"", "Normal Color", "", "Sets the nametag color used for ordinary players."},
    {"", "Friend Color", "", "Sets the nametag color used for players in your friends list."},
    {"", "Health Bar", "", "Draws a bar that reflects the player's current health."},
    {"", "Team Colors", "", "Uses detected team colors instead of one fixed ESP color."},
    {"", "Box 2D", "", "Draws a flat screen-aligned box around each valid target."},
    {"", "Box 3D", "", "Draws a world-space box around each valid target."},
    {"", "Show Quantity", "", "Displays the stack count for dropped items."},
    {"", "Color by Distance", "", "Changes tracer color based on how far away the player is."},
    {"", "Color by Health", "", "Changes tracer color based on the player's health."},
    {"", "Diamond Ore", "", "Highlights diamond ore blocks."},
    {"", "Emerald Ore", "", "Highlights emerald ore blocks."},
    {"", "Iron Ore", "", "Highlights iron ore blocks."},
    {"", "Gold Ore", "", "Highlights gold ore blocks."},
    {"", "Lapis Ore", "", "Highlights lapis ore blocks."},
    {"", "Redstone Ore", "", "Highlights redstone ore blocks."},
    {"", "Coal Ore", "", "Highlights coal ore blocks."},
    {"", "Chests", "", "Highlights chest blocks."},
    {"", "Opacity", "", "Controls how transparent the block highlight is."},
    {"", "Fade with Distance", "", "Gradually fades highlights as they approach the range limit."},
    {"", "Bow", "", "Shows predicted trajectories for arrows fired from bows."},
    {"", "Ender Pearl", "", "Shows predicted trajectories for thrown ender pearls."},
    {"", "Egg", "", "Shows predicted trajectories for thrown eggs."},
    {"", "Snowball", "", "Shows predicted trajectories for thrown snowballs."},
    {"", "Healing II", "", "Shows predicted trajectories for splash healing potions."},
    {"", "Poison", "", "Shows predicted trajectories for splash poison potions."},
    {"", "Slowness", "", "Shows predicted trajectories for splash slowness potions."},
    {"", "Blindness", "", "Prevents the blindness overlay and darkness from affecting your view."},
    {"", "Nausea", "", "Prevents the nausea screen distortion from affecting your view."},
    {"", "Rainbow", "", "Cycles the rendered color continuously through the hue spectrum."},
    {"", "Color Bar", "", "Draws an accent bar beside each enabled module."},
    {"", "Sort by Length", "", "Orders enabled modules by rendered text width."},
    {"", "Show Info", "", "Shows each module's active mode or status beside its name."},
    {"", "Size", "", "Changes the scale of the on-screen module list."},
    {"", "High Saturation", "", "Uses stronger rainbow colors instead of softer pastel tones."},
    {"", "Minimum Speed", "", "Sets the lowest timer multiplier TimerRange may choose."},
    {"", "Maximum Speed", "", "Sets the highest timer multiplier TimerRange may choose."},
    {"", "Offset", "", "Sets the delay after a hit before TimerRange starts."},
    {"", "Duration", "", "Sets how long the selected TimerRange speed remains active."},
    {"", "Prediction Time", "", "Sets how far ahead Clutch simulates the current trajectory."},
    {"", "Search Radius", "", "Sets how far Clutch searches for a reachable solid anchor."},
    {"", "Place Delay", "", "Sets the minimum time between automatic block placements."},
    {"Breaker", "Legit", "", "Breaks up to two blocks covering the bed before targeting the bed itself."},
    {"", "Release Time", "", "Sets how long WTap releases the selected movement input."},
    {"", "Timer Limit", "", "Sets the maximum time packets may remain queued."},
    {"", "Show Timer", "", "Displays how long Blink has been queuing packets."},
    {"", "Show Breadcrumbs", "", "Draws the path traveled while packets are queued."},
    {"", "Moving Only", "", "Applies the feature only while horizontal movement is detected."},
    {"", "Render Server Player", "", "Draws a player-sized ghost at the position estimated from measured latency."},
    {"", "Filled Render", "", "Adds a translucent fill inside the server-position ghost."},
    {"", "Render Line", "", "Connects your current position to the rendered server position."},
    {"", "Show Status", "", "Shows measured round-trip and estimated one-way latency."},
    {"", "Blocks Only", "", "Applies the placement delay reduction only to blocks."},
    {"", "Refill Soup", "", "Refills empty hotbar slots with mushroom stew."},
    {"", "Refill Potions", "", "Refills empty hotbar slots with supported healing potions."},
    {"", "Auto Sword (Attack)", "", "Selects the strongest sword when attacking an entity."},
    {"", "Prefer Protection", "", "Prioritizes armor with stronger protection enchantments."},
    {"", "Set 1", "", "Chooses the armor material used by the first switch set."},
    {"", "Set 2", "", "Chooses the armor material used by the second switch set."},
    {"", "Line From", "", "Chooses the screen position where tracer lines begin."},
    {"", "Position", "", "Chooses the screen corner used to anchor the module list."},
    {"", "Keybind", "", "Sets the keyboard or mouse key used to toggle this module."},
    {"", "Lock", "", "Keeps the selected behavior attached to its current target until switching is required."},
    {"", "X", "", "Sets the horizontal screen position."},
    {"", "Y", "", "Sets the vertical screen position."},

    {"AimAssist", "Target Mode", "Lowest Health", "Targets the valid player with the lowest current health."},
    {"AimAssist", "Target Mode", "Closest to Player", "Targets the valid player nearest to your position."},
    {"AimAssist", "Target Mode", "Closest to Crosshair", "Targets the player nearest to the center of your view."},
    {"AimAssist", "Target Mode", "Switch", "Continuously evaluates targets using the selected sorting rule."},
    {"AimAssist", "Sort By", "Distance", "Ranks switch targets by nearest distance."},
    {"AimAssist", "Sort By", "Health", "Ranks switch targets by lowest health."},
    {"AimAssist", "Sort By", "FOV", "Ranks switch targets by smallest angle from your crosshair."},
    {"AimAssist", "Sort By", "Hurt Time", "Ranks targets by when they can receive the next hit."},
    {"AutoClicker", "Randomization", "Random", "Uses the standard pseudo-random generator for each CPS interval."},
    {"AutoClicker", "Randomization", "Natural", "Uses a separately seeded 64-bit generator for CPS and click timing."},
    {"AutoClicker", "Randomization", "Adaptive", "Uses secure draws with changing CPS clusters, random walks, and asymmetric press timing."},
    {"Velocity", "Mode", "Regular", "Directly scales horizontal and vertical knockback by the configured percentages."},
    {"Velocity", "Mode", "Jump", "Attempts a timed jump after taking knockback to preserve movement control."},
    {"Velocity", "Mode", "Reduce", "Reduces knockback after the configured tick delay instead of applying it immediately."},
    {"Velocity", "Mode", "Ignore", "Randomly ignores eligible knockback while using the selected vertical rule."},
    {"Velocity", "Vertical", "Never", "Never suppresses the vertical component while Ignore mode activates."},
    {"Velocity", "Vertical", "Only Air", "Suppresses vertical knockback only while you are already airborne."},
    {"Velocity", "Vertical", "Always", "Suppresses vertical knockback whenever Ignore mode activates."},
    {"Backtrack", "Mode", "Ticks", "Stores a fixed number of previous target positions."},
    {"Backtrack", "Mode", "Latency", "Stores positions using a time window measured in milliseconds."},
    {"TimerRange", "Mode", "Before Hit", "Starts the timer window as the attack input begins."},
    {"TimerRange", "Mode", "After Hit", "Starts the timer window after the configured delay from a hit."},
    {"TimerRange", "Mode", "Per Hit", "Starts a new timer window for every confirmed outgoing hit."},
    {"TimerRange", "Mode", "Constant", "Continuously applies a timer value while the selected conditions are met."},
    {"WTap", "Mode", "Forward", "Briefly releases forward movement after an attack."},
    {"WTap", "Mode", "Sprint", "Briefly resets sprint after an attack."},
    {"WTap", "Mode", "Hybrid", "Resets forward movement and sprint together after an attack."},
    {"NoItemRelease", "Mode", "Consumable", "Prevents release packets for food, potions, and placeable blocks."},
    {"NoItemRelease", "Mode", "Sword", "Prevents release packets while sword blocking."},
    {"NoItemRelease", "Mode", "All", "Prevents release packets for every supported item except bows and fishing rods."},
    {"Tracers", "Line From", "Bottom", "Starts tracer lines at the bottom-center of the screen."},
    {"Tracers", "Line From", "Center", "Starts tracer lines at the center of the screen."},
};

static const char* findDescription(const DescriptionEntry* entries, size_t count, const char* context, const char* key,
                                   const char* value = "") {
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (strcmp(entry.context, context ? context : "") == 0 && strcmp(entry.key, key ? key : "") == 0 &&
            strcmp(entry.value, value ? value : "") == 0)
            return entry.text;
    }
    return nullptr;
}

static const char* resolveOptionDescription(const char* key, const char* value = "") {
    if (const char* exact = findDescription(kOptionDescriptions, IM_ARRAYSIZE(kOptionDescriptions),
                                            g_descriptionContext.c_str(), key, value))
        return exact;
    if (const char* generic =
            findDescription(kOptionDescriptions, IM_ARRAYSIZE(kOptionDescriptions), "", key, ""))
        return generic;

    static std::string fallback;
    fallback = "Adjusts ";
    fallback += key ? key : "this option";
    fallback += " for ";
    fallback += g_descriptionContext.empty() ? "the current feature." : g_descriptionContext + ".";
    return fallback.c_str();
}

static void drawRainbowText(ImDrawList* draw, ImVec2 pos, const char* text, float alpha) {
    const float time = static_cast<float>(ImGui::GetTime());
    const char* cursor = text;
    while (cursor && *cursor) {
        char glyph[2] = {*cursor, '\0'};
        const float hue = fmodf(time * 1.8f + pos.x * 0.006f, 1.0f);
        ImVec4 color;
        ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.0f, color.x, color.y, color.z);
        color.w = alpha;
        draw->AddText(pos, ImGui::ColorConvertFloat4ToU32(color), glyph);
        pos.x += ImGui::CalcTextSize(glyph).x;
        ++cursor;
    }
}
}  // namespace

// ==================== BINDER STATE ====================
static bool g_BinderActive = false;

bool ui::IsBinderActive() {
    return g_BinderActive;
}

void ui::ResetBinderState() {
    g_BinderActive = false;
}

// ==================== GLOBAL SCHEME COLOR ====================
float (&g_SchemeColor)[4] = ui::theme::accentColor;

ImU32 GetAccentRGB() {
    if (ui::theme::accentRainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return hud::GetRainbowColorEx(0, 0.85f, ms, ui::theme::rainbowType);
    }
    return IM_COL32((int)(ui::theme::accentColor[0] * 255.0f), (int)(ui::theme::accentColor[1] * 255.0f),
                    (int)(ui::theme::accentColor[2] * 255.0f), 255);
}

inline ImU32 GetSchemeColor(float alpha) {
    ImVec4 col = ImGui::ColorConvertU32ToFloat4(GetAccentRGB());
    col.w = ui::theme::accentColor[3];
    col.w *= alpha * ImGui::GetStyle().Alpha * ui::alpha_mod;
    return ImGui::ColorConvertFloat4ToU32(col);
}

static void DrawSliderGradient(ImDrawList* draw, const ImRect& bounds, float width, float rounding) {
    width = (std::clamp)(width, 0.0f, bounds.GetWidth());
    if (!draw || width <= 0.0f)
        return;

    // Solid accent fill (no gradient) for the active portion of the slider track.
    const ImVec2 maximum(bounds.Min.x + width, bounds.Max.y);
    const ImU32 accent = GetSchemeColor();
    draw->PushClipRect(bounds.Min, maximum, true);
    draw->AddRectFilled(bounds.Min, maximum, accent, rounding);
    draw->PopClipRect();
}

// ==================== TAB DEFINITIONS ====================
std::vector<ui::tab_t> ui::tabs = {
    {ICON_FA_CROSSHAIRS,
     "Combat",
     {"AutoClicker", "AimAssist", "Velocity", "Reach", "HitSelect", "NoHitDelay", "Criticals", "WTap",
      "Piercing", "AimBow", "BlockHit"}},
    {ICON_FA_EYE,
     "Visuals",
     {"NameTags", "ESP", "ItemESP", "Tracers", "BlockESP", "Trajectories", "AntiDebuff", "ArrayList",
      "DroppedInfo", "Notifications", "Watermark", "TargetHUD", "ClickGUI"}},
    {ICON_FA_RUNNING, "Movement", {"Timer", "TimerRange", "NoSlowDown", "NoJumpDelay", "Sprint"}},
    {ICON_FA_KEYBOARD,
     "Utility",
     {"ArmorSwitch", "AutoRefill", "FastPlace", "NoItemRelease", "BridgeAssist", "Clutch", "Breaker", "AutoTool",
      "InvManager", "FastBreak"}},
    {ICON_FA_WIFI, "Network", {"Backtrack", "Blink", "FakeLag", "TCP Optimizer"}},
#if COBBLE_SPECTATE_ENABLED
    {ICON_FA_EYE, "Spectate", {}},
#endif
    {ICON_FA_SAVE, "Configs", {}},
    {ICON_FA_GEAR, "Settings", {}}};

// ==================== SCALING ====================

void ui::updateScale() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float widthScale = (display.x - 32.0f) / baseSize.x;
    const float heightScale = (display.y - 32.0f) / baseSize.y;
    scale = (std::clamp)((std::min)(widthScale, heightScale), 0.65f, 1.35f);
}

float ui::s(float value) {
    return value * scale;
}

ImVec2 ui::s(const ImVec2& v) {
    return {v.x * scale, v.y * scale};
}

ImU32 ui::GetColor(ImGuiCol idx, float alpha_mult) {
    ImVec4 col = ImGui::GetStyle().Colors[idx];
    col.w *= alpha_mult * ImGui::GetStyle().Alpha * alpha_mod;
    return ImGui::ColorConvertFloat4ToU32(col);
}

void ui::SetDescriptionContext(const char* context) {
    g_descriptionContext = context ? context : "";
}

const char* ui::GetModuleDescription(const char* module) {
    if (const char* description =
            findDescription(kModuleDescriptions, IM_ARRAYSIZE(kModuleDescriptions), "", module, ""))
        return description;
    return "Configures this module's behavior.";
}

void ui::HoverDescription(const char* key, bool hovered, const char* explicitText, bool rainbow) {
    (void)key;
    (void)hovered;
    (void)explicitText;
    (void)rainbow;
}

void ui::DescribeLastItem(const char* key, const char* explicitText, bool rainbow) {
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    HoverDescription(key, hovered, explicitText, rainbow);
}

// ==================== ANIMATION SYSTEM ====================

float ui::animate(float current, float min, float max, bool state) {
    float target = state ? max : min;
    return ImLerp(current, target, 21.f * ImGui::GetIO().DeltaTime);
}

ImColor ui::col_anim(ImU32 from, ImU32 to, float t) {
    ImVec4 f = ImGui::ColorConvertU32ToFloat4(from);
    ImVec4 tt = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(f, tt, t));
}

void ui::handle_anims() {
    anim = ImLerp(anim, anim_dst, 21.f * ImGui::GetIO().DeltaTime);
    if (abs(anim - anim_dst) < 0.01f) {
        anim_dst = 1.f;
        cur_tab = next_tab;
    }

    content_anim = ImLerp(content_anim, content_anim_dst, 21.f * ImGui::GetIO().DeltaTime);
    if (abs(content_anim - content_anim_dst) < 0.01f) {
        content_anim_dst = 1.f;
        if (cur_tab < tabs.size()) {
            tabs[cur_tab].cur_subtab = tabs[cur_tab].next_subtab;
            tabs[cur_tab].cur_view = tabs[cur_tab].next_view;
        }
    }
}

// ==================== LAYOUT HELPERS ====================

// Inline section: a lightweight header (label + accent underline) followed by
// its content flowing directly in the current window -- no bordered box, no
// fixed height. Sections stack continuously one after another so the config
// reads as a single scrolling column instead of separate cards.
static void inline_section(const char* title, const std::function<void()>& content) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* draw = window->DrawList;

    if (title && ImGui::CalcTextSize(title, 0, 1).x > 0) {
        // Section header: clearly larger/bolder than body text, no separator line.
        // The header occupies a reference row the same height as the Back control
        // (s(26)) with its text vertically centered, so a top-row header lines up
        // with Back instead of sitting a few pixels higher (the larger header font
        // would otherwise start above Back's centered label).
        ImFont* f = font(2);                          // bold, bigger than the 18px body font
        const float headerFS = f->FontSize * 1.18f;   // enlarge "Settings"/"Options" titles
        const float rowRef = ui::s(26.f);             // matches the Back control row height
        const float offsetY = ImMax(0.f, (rowRef - headerFS) * 0.5f);
        ImVec2 pos = window->DC.CursorPos;
        draw->AddText(f, headerFS, {pos.x, pos.y + offsetY}, ui::GetColor(ImGuiCol_Text), title,
                      ImGui::FindRenderedTextEnd(title));
        ImGui::Dummy({0, rowRef + ui::s(10)});
    }

    content();
}

void ui::two_column(const char* leftTitle, std::function<void()> leftContent, const char* rightTitle,
                    std::function<void()> rightContent) {
    float spacing = ui::s(40.f);  // gap between the two config columns
    float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    ImGui::BeginGroup();
    ImGui::PushItemWidth(halfWidth);
    inline_section(leftTitle, leftContent);
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0, spacing);

    ImGui::BeginGroup();
    ImGui::PushItemWidth(halfWidth);
    inline_section(rightTitle, rightContent);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
}

void ui::section(const char* title, std::function<void()> content) {
    inline_section(title, content);
}

void ui::full_width(const char* title, std::function<void()> content) {
    inline_section(title, content);
}

// ==================== CHILD CONTAINERS ====================

void ui::begin_child(const char* name, ImVec2 size) {
    float headerHeight = s(33.f);

    ImGui::BeginChild(std::string(name).append("decor").c_str(),
                      CalcItemSize(size,
                                   (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 -
                                       GImGui->Style.ItemSpacing.x / 2,
                                   0),
                      0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                                              GetColor(ImGuiCol_ChildBg), GImGui->Style.ChildRounding);

    if (ImGui::CalcTextSize(name, 0, 1).x > 0) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(),
            {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight},
            GetColor(ImGuiCol_Header), GImGui->Style.ChildRounding, ImDrawFlags_RoundCornersTop);
        ImGui::GetWindowDrawList()->AddLine(
            {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + headerHeight},
            {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight},
            GetSchemeColor(0.5f));
        ImGui::GetWindowDrawList()->AddText(
            font(1), font(1)->FontSize,
            {ImGui::GetWindowPos().x + s(13),
             ImGui::GetWindowPos().y + headerHeight / 2 -
                 font(1)->CalcTextSizeA(font(1)->FontSize, FLT_MAX, 0.f, name, ImGui::FindRenderedTextEnd(name)).y / 2},
            GetColor(ImGuiCol_Text), name, ImGui::FindRenderedTextEnd(name));
        ImGui::SetCursorPosY(headerHeight + 1);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {s(13), s(15)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {s(8), s(14)});
    ImGui::BeginChild(name, {-1, size.y == 0 ? size.y : -1}, 0,
                      ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoBackground);
}

void ui::end_child() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

// ==================== CHECKBOX ====================

bool ui::Checkbox(const char* label, bool* v) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    auto name_size = CalcTextSize(label, 0, 1);
    auto draw = window->DrawList;
    auto pos = window->DC.CursorPos;

    float square_sz = s(18.f);
    float checkSize = s(9.f);
    ImRect bb(pos, {pos.x + square_sz, pos.y + square_sz});
    ImRect total_bb(pos, {pos.x + square_sz + name_size.x + GImGui->Style.ItemInnerSpacing.x, pos.y + square_sz});
    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        MarkItemEdited(id);
    }

    struct cb_state {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, cb_state{0.f, 0.f, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, *v);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered && !(*v));
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover);

    draw->AddText({total_bb.Max.x - name_size.x, bb.GetCenter().y - name_size.y / 2}, GetColor(ImGuiCol_Text), label,
                  FindRenderedTextEnd(label));
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(obj.anim), style.FrameRounding);
    RenderCheckMark(draw,
                    {bb.GetCenter().x - (checkSize * obj.anim) / 2, bb.GetCenter().y - (checkSize * obj.anim) / 2},
                    GetColor(ImGuiCol_Text, obj.anim), checkSize * obj.anim);

    HoverDescription(label, hovered);
    return pressed;
}

// ==================== CHECKBOX WITH BIND ====================

bool ui::CheckboxBind(const char* label, bool* v, int* key) {
    auto window = GetCurrentWindow();
    auto style = GetStyle();
    auto draw = window->DrawList;

    float availWidth = CalcItemWidth();
    float bindBoxWidth = s(50.0f);
    float spacing = s(8.0f);
    float checkboxWidth = availWidth - bindBoxWidth - spacing;

    ImVec2 pos = window->DC.CursorPos;
    float height = s(20.f);
    float square_sz = s(18.f);

    // Checkbox part
    auto cbId = window->GetID(label);
    auto name_size = CalcTextSize(label, 0, 1);

    ImRect cbBox(pos, {pos.x + square_sz, pos.y + square_sz});
    ImRect cbTotal(pos, {pos.x + checkboxWidth, pos.y + height});

    bool cbHovered, cbHeld;
    ItemAdd(cbTotal, cbId);
    bool cbPressed = ButtonBehavior(cbTotal, cbId, &cbHovered, &cbHeld);
    if (cbPressed) {
        *v = !(*v);
        MarkItemEdited(cbId);
    }

    struct cb_s {
        float anim;
        float hover;
        ImColor col;
    };
    auto& cbObj = anim_obj(label, 0, cb_s{0.f, 0.f, 0});
    cbObj.anim = animate(cbObj.anim, 0.f, 1.f, *v);
    cbObj.hover = animate(cbObj.hover, 0.f, 1.f, cbHovered && !(*v));
    cbObj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), cbObj.hover);

    draw->AddRectFilled(cbBox.Min, cbBox.Max, cbObj.col, style.FrameRounding);
    draw->AddRect(cbBox.Min, cbBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(cbBox.Min, cbBox.Max, GetSchemeColor(cbObj.anim), style.FrameRounding);
    RenderCheckMark(draw, {cbBox.GetCenter().x - (9 * cbObj.anim) / 2, cbBox.GetCenter().y - (9 * cbObj.anim) / 2},
                    GetColor(ImGuiCol_Text, cbObj.anim), 9 * cbObj.anim);
    draw->AddText({cbBox.Max.x + style.ItemInnerSpacing.x, cbBox.GetCenter().y - name_size.y / 2},
                  GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));

    // Binder part
    std::string bindId = std::string(label) + "##bind";
    auto bId = window->GetID(bindId.c_str());

    ImRect bindBox(pos + ImVec2(availWidth - bindBoxWidth, (height - s(18.f)) / 2),
                   pos + ImVec2(availWidth, (height + s(18.f)) / 2));
    ItemAdd(bindBox, bId);

    bool bindHovered = ItemHoverable(bindBox, bId, 0);
    bool bindActive = GetActiveID() == bId;

    // Update global binder state for ESC handling in overlay
    if (bindActive)
        g_BinderActive = true;

    struct bind_s {
        float anim;
        float hover;
        ImColor col;
        ImColor textCol;
        int framesSinceActive;
    };
    auto& bindObj = anim_obj(bindId.c_str(), 0, bind_s{0.f, 0.f, 0, 0, 0});
    bindObj.anim = animate(bindObj.anim, 0.f, 1.f, bindActive);
    bindObj.hover = animate(bindObj.hover, 0.f, 1.f, bindHovered || bindActive);
    bindObj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), bindObj.hover),
                           GetColor(ImGuiCol_FrameBgActive), bindObj.anim);
    bindObj.textCol = col_anim(GetColor(ImGuiCol_Text), GetSchemeColor(), bindObj.anim);

    if (bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(bId, window);
        FocusWindow(window);
        bindObj.framesSinceActive = 0;
    }

    if (bindActive && !bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    bool changed = false;
    if (bindActive) {
        bindObj.framesSinceActive++;
        if (bindObj.framesSinceActive > 5) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                *key = 0;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                *key = VK_RBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                *key = VK_MBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                *key = VK_XBUTTON1;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                *key = VK_XBUTTON2;
                ClearActiveID();
                changed = true;
            } else {
                // Check all common key ranges
                static const int keyRanges[][2] = {
                    {0x08, 0x2F},  // Control keys, arrows, etc.
                    {0x30, 0x39},  // Numbers 0-9
                    {0x41, 0x5A},  // Letters A-Z
                    {0x60, 0x6F},  // Numpad
                    {0x70, 0x87},  // F1-F24
                    {0x90, 0x91},  // NUMLOCK, SCROLL
                    {0xA0, 0xB7},  // Shift variants, browser/media keys
                    {0xBA, 0xC0},  // OEM keys: ;=,-./`
                    {0xDB, 0xDF},  // OEM keys: [\]'
                };
                bool found = false;
                for (const auto& range : keyRanges) {
                    for (int vk = range[0]; vk <= range[1] && !found; vk++) {
                        if (vk == VK_ESCAPE)
                            continue;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            *key = vk;
                            ClearActiveID();
                            changed = true;
                            found = true;
                        }
                    }
                    if (found)
                        break;
                }
            }
        }
    }

    const char* displayText = bindActive ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bindBox.Min, bindBox.Max, bindObj.col, style.FrameRounding);
    draw->AddRect(bindBox.Min, bindBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({bindBox.GetCenter().x - textSize.x / 2, bindBox.GetCenter().y - textSize.y / 2}, bindObj.textCol,
                  displayText);

    ItemSize(ImVec2(availWidth, height));
    HoverDescription(label, cbHovered || bindHovered || bindActive);
    return cbPressed || changed;
}

// ==================== SLIDER FLOAT ====================

bool ui::SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, bool allowDirectInput) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, 0, 1);
    ImVec2 frame_size = ImVec2(CalcItemWidth(), s(6));
    ImRect total_bb(pos, pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y * 1.25f + frame_size.y));
    ImRect bb(ImVec2(total_bb.Min.x, total_bb.Max.y - frame_size.y), ImVec2(total_bb.Max.x, total_bb.Max.y));

    // Calculate value text area for direct input detection
    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);
    ImVec2 value_size = CalcTextSize(value_buf);
    ImRect value_bb(ImVec2(total_bb.Max.x - value_size.x - s(4), total_bb.Min.y - s(2)),
                    ImVec2(total_bb.Max.x + s(4), total_bb.Min.y + value_size.y + s(2)));

    // Track editing state with persistent storage
    struct direct_input_state {
        bool editing;
        char buffer[32];
        bool justStarted;
    };
    auto& inputState = anim_obj(label, 1, direct_input_state{false, "", false});

    // Check for click on value text - only if allowDirectInput is true
    if (allowDirectInput && !inputState.editing) {
        if (IsMouseHoveringRect(value_bb.Min, value_bb.Max) && IsMouseClicked(ImGuiMouseButton_Left)) {
            inputState.editing = true;
            inputState.justStarted = true;
            snprintf(inputState.buffer, sizeof(inputState.buffer), format, *v);
        }
    }

    // Handle text input mode - render input and return early
    if (inputState.editing) {
        ItemAdd(total_bb, id);
        ItemSize(total_bb);
        const ImVec2 postItemCursor = window->DC.CursorPos;

        // Draw slider background while editing
        struct slider_state {
            float grab;
            float anim;
            ImColor col;
        };
        auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});
        float t = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
        obj.grab = t * bb.GetWidth();

        window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColor(ImGuiCol_FrameBg), 100);
        DrawSliderGradient(window->DrawList, bb, obj.grab * anim, 100.0f);
        float grab_radius = s(6.f);
        window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                          GetColor(ImGuiCol_Text), 64);
        window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));

        // Position input at value location
        float inputWidth = s(25.f);
        ImVec2 inputPos(total_bb.Max.x - inputWidth, total_bb.Min.y);
        SetCursorScreenPos(inputPos);
        PushItemWidth(inputWidth);

        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(2), s(1)));
        PushStyleColor(ImGuiCol_FrameBg, GetColor(ImGuiCol_FrameBgActive));

        // Set focus only on first frame
        if (inputState.justStarted) {
            SetKeyboardFocusHere();
            inputState.justStarted = false;
        }

        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
        bool enterPressed = InputText("##valueinput", inputState.buffer, sizeof(inputState.buffer), flags);

        // Only close on Enter key press
        if (enterPressed) {
            float newVal = 0.0f;
            if (sscanf_s(inputState.buffer, "%f", &newVal) == 1 && std::isfinite(newVal)) {
                *v = newVal;
                MarkItemEdited(id);
            }
            inputState.editing = false;
        }

        // Cancel on escape
        if (IsKeyPressed(ImGuiKey_Escape)) {
            inputState.editing = false;
        }

        // Close if clicked outside the input box
        if (IsMouseClicked(ImGuiMouseButton_Left) && !IsItemHovered()) {
            float newVal = 0.0f;
            if (sscanf_s(inputState.buffer, "%f", &newVal) == 1 && std::isfinite(newVal)) {
                *v = newVal;
                MarkItemEdited(id);
            }
            inputState.editing = false;
        }

        PopStyleColor();
        PopStyleVar();
        PopItemWidth();

        SetCursorScreenPos(postItemCursor);
        HoverDescription(label, true);
        return true;
    }

    // Normal slider logic
    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    const bool hovered = ItemHoverable(bb, id, 0);
    const bool clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
    const bool active = GetActiveID() == id;

    if (clicked) {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
    }

    if (active && IsMouseReleased(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    struct slider_state {
        float grab;
        float anim;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});

    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    float grab_pos = t * bb.GetWidth();

    if (active) {
        float mouse_rel = ImClamp((GetMousePos().x - bb.Min.x) / bb.GetWidth(), 0.0f, 1.0f);
        *v = v_min + mouse_rel * (v_max - v_min);
        MarkItemEdited(id);
        t = (*v - v_min) / (v_max - v_min);
        grab_pos = t * bb.GetWidth();
        obj.grab = ImLerp(obj.grab, grab_pos, 24.f * GetIO().DeltaTime);
    } else {
        obj.grab = grab_pos;
    }

    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || active);
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.anim);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    DrawSliderGradient(window->DrawList, bb, obj.grab * anim, 100.0f);

    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                      GetColor(ImGuiCol_Text), 64);

    window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y}, GetColor(ImGuiCol_Text),
                              value_buf);

    HoverDescription(label, hovered || active || IsMouseHoveringRect(value_bb.Min, value_bb.Max));
    return active;
}

// ==================== SLIDER FLOAT WITH CLAMPING ====================
// Same as SliderFloat but clamps value to [*clamp_min, *clamp_max] DURING drag
// This prevents visual overshoot when linking min/max sliders

bool ui::SliderFloat(const char* label, float* v, float v_min, float v_max, float* clamp_min, float* clamp_max,
                     const char* format) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, 0, 1);
    ImVec2 frame_size = ImVec2(CalcItemWidth(), s(6));
    ImRect total_bb(pos, pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y * 1.25f + frame_size.y));
    ImRect bb(ImVec2(total_bb.Min.x, total_bb.Max.y - frame_size.y), ImVec2(total_bb.Max.x, total_bb.Max.y));

    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);

    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    const bool hovered = ItemHoverable(bb, id, 0);
    const bool clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
    const bool active = GetActiveID() == id;

    if (clicked) {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
    }

    if (active && IsMouseReleased(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    struct slider_state {
        float grab;
        float anim;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});

    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    float grab_pos = t * bb.GetWidth();

    if (active) {
        float mouse_rel = ImClamp((GetMousePos().x - bb.Min.x) / bb.GetWidth(), 0.0f, 1.0f);
        float new_val = v_min + mouse_rel * (v_max - v_min);

        // Apply linked clamping DURING drag
        if (clamp_min && new_val < *clamp_min)
            new_val = *clamp_min;
        if (clamp_max && new_val > *clamp_max)
            new_val = *clamp_max;

        *v = new_val;
        MarkItemEdited(id);
        t = (*v - v_min) / (v_max - v_min);
        grab_pos = t * bb.GetWidth();
        obj.grab = ImLerp(obj.grab, grab_pos, 24.f * GetIO().DeltaTime);
    } else {
        obj.grab = grab_pos;
    }

    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || active);
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.anim);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    DrawSliderGradient(window->DrawList, bb, obj.grab * anim, 100.0f);

    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                      GetColor(ImGuiCol_Text), 64);

    window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y}, GetColor(ImGuiCol_Text),
                              value_buf);

    HoverDescription(label, hovered || active);
    return active;
}

// ==================== SLIDER INT ====================

bool ui::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format) {
    float val = (float)*v;
    bool changed = ui::SliderFloat(label, &val, (float)v_min, (float)v_max, "%.0f");
    *v = (int)val;
    return changed;
}

// ==================== KEY NAME LOOKUP ====================

const char* ui::GetKeyName(int vk) {
    switch (vk) {
        // Mouse buttons
        case 0x00:
            return "NONE";
        case 0x01:
            return "LMB";
        case 0x02:
            return "RMB";
        case 0x03:
            return "BREAK";
        case 0x04:
            return "MMB";
        case 0x05:
            return "MB4";
        case 0x06:
            return "MB5";

        // Control keys
        case 0x08:
            return "BACK";
        case 0x09:
            return "TAB";
        case 0x0C:
            return "CLEAR";
        case 0x0D:
            return "ENTER";
        case 0x10:
            return "SHIFT";
        case 0x11:
            return "CTRL";
        case 0x12:
            return "ALT";
        case 0x13:
            return "PAUSE";
        case 0x14:
            return "CAPS";
        case 0x1B:
            return "ESC";
        case 0x20:
            return "SPACE";
        case 0x21:
            return "PGUP";
        case 0x22:
            return "PGDN";
        case 0x23:
            return "END";
        case 0x24:
            return "HOME";
        case 0x25:
            return "LEFT";
        case 0x26:
            return "UP";
        case 0x27:
            return "RIGHT";
        case 0x28:
            return "DOWN";
        case 0x29:
            return "SELECT";
        case 0x2A:
            return "PRINT";
        case 0x2B:
            return "EXEC";
        case 0x2C:
            return "PRTSC";
        case 0x2D:
            return "INS";
        case 0x2E:
            return "DEL";
        case 0x2F:
            return "HELP";

        // Numbers
        case 0x30:
            return "0";
        case 0x31:
            return "1";
        case 0x32:
            return "2";
        case 0x33:
            return "3";
        case 0x34:
            return "4";
        case 0x35:
            return "5";
        case 0x36:
            return "6";
        case 0x37:
            return "7";
        case 0x38:
            return "8";
        case 0x39:
            return "9";

        // Letters
        case 0x41:
            return "A";
        case 0x42:
            return "B";
        case 0x43:
            return "C";
        case 0x44:
            return "D";
        case 0x45:
            return "E";
        case 0x46:
            return "F";
        case 0x47:
            return "G";
        case 0x48:
            return "H";
        case 0x49:
            return "I";
        case 0x4A:
            return "J";
        case 0x4B:
            return "K";
        case 0x4C:
            return "L";
        case 0x4D:
            return "M";
        case 0x4E:
            return "N";
        case 0x4F:
            return "O";
        case 0x50:
            return "P";
        case 0x51:
            return "Q";
        case 0x52:
            return "R";
        case 0x53:
            return "S";
        case 0x54:
            return "T";
        case 0x55:
            return "U";
        case 0x56:
            return "V";
        case 0x57:
            return "W";
        case 0x58:
            return "X";
        case 0x59:
            return "Y";
        case 0x5A:
            return "Z";

        // Windows keys
        case 0x5B:
            return "LWIN";
        case 0x5C:
            return "RWIN";
        case 0x5D:
            return "APPS";
        case 0x5F:
            return "SLEEP";

        // Numpad
        case 0x60:
            return "NUM0";
        case 0x61:
            return "NUM1";
        case 0x62:
            return "NUM2";
        case 0x63:
            return "NUM3";
        case 0x64:
            return "NUM4";
        case 0x65:
            return "NUM5";
        case 0x66:
            return "NUM6";
        case 0x67:
            return "NUM7";
        case 0x68:
            return "NUM8";
        case 0x69:
            return "NUM9";
        case 0x6A:
            return "NUM*";
        case 0x6B:
            return "NUM+";
        case 0x6C:
            return "NUMSEP";
        case 0x6D:
            return "NUM-";
        case 0x6E:
            return "NUM.";
        case 0x6F:
            return "NUM/";

        // Function keys
        case 0x70:
            return "F1";
        case 0x71:
            return "F2";
        case 0x72:
            return "F3";
        case 0x73:
            return "F4";
        case 0x74:
            return "F5";
        case 0x75:
            return "F6";
        case 0x76:
            return "F7";
        case 0x77:
            return "F8";
        case 0x78:
            return "F9";
        case 0x79:
            return "F10";
        case 0x7A:
            return "F11";
        case 0x7B:
            return "F12";
        case 0x7C:
            return "F13";
        case 0x7D:
            return "F14";
        case 0x7E:
            return "F15";
        case 0x7F:
            return "F16";
        case 0x80:
            return "F17";
        case 0x81:
            return "F18";
        case 0x82:
            return "F19";
        case 0x83:
            return "F20";
        case 0x84:
            return "F21";
        case 0x85:
            return "F22";
        case 0x86:
            return "F23";
        case 0x87:
            return "F24";

        // Lock keys
        case 0x90:
            return "NUMLOCK";
        case 0x91:
            return "SCROLL";

        // Shift variants
        case 0xA0:
            return "LSHIFT";
        case 0xA1:
            return "RSHIFT";
        case 0xA2:
            return "LCTRL";
        case 0xA3:
            return "RCTRL";
        case 0xA4:
            return "LALT";
        case 0xA5:
            return "RALT";

        // Browser keys
        case 0xA6:
            return "BBACK";
        case 0xA7:
            return "BFWD";
        case 0xA8:
            return "BREF";
        case 0xA9:
            return "BSTOP";
        case 0xAA:
            return "BSRCH";
        case 0xAB:
            return "BFAV";
        case 0xAC:
            return "BHOME";

        // Media keys
        case 0xAD:
            return "MUTE";
        case 0xAE:
            return "VOLDN";
        case 0xAF:
            return "VOLUP";
        case 0xB0:
            return "NEXT";
        case 0xB1:
            return "PREV";
        case 0xB2:
            return "STOP";
        case 0xB3:
            return "PLAY";
        case 0xB4:
            return "MAIL";
        case 0xB5:
            return "MEDIA";
        case 0xB6:
            return "APP1";
        case 0xB7:
            return "APP2";

        // OEM keys (punctuation/symbols)
        case 0xBA:
            return ";";  // OEM_1: ;:
        case 0xBB:
            return "=";  // OEM_PLUS: =+
        case 0xBC:
            return ",";  // OEM_COMMA: ,<
        case 0xBD:
            return "-";  // OEM_MINUS: -_
        case 0xBE:
            return ".";  // OEM_PERIOD: .>
        case 0xBF:
            return "/";  // OEM_2: /?
        case 0xC0:
            return "`";  // OEM_3: `~
        case 0xDB:
            return "[";  // OEM_4: [{
        case 0xDC:
            return "\\";  // OEM_5: \|
        case 0xDD:
            return "]";  // OEM_6: ]}
        case 0xDE:
            return "'";  // OEM_7: '"
        case 0xDF:
            return "OEM8";
        case 0xE2:
            return "<>";  // OEM_102: additional key on non-US keyboards

        default:
            return "NONE";
    }
}

// ==================== BINDER ====================

bool ui::Binder(const char* label, int* key) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();
    auto draw = window->DrawList;

    ImVec2 labelSize = CalcTextSize(label, 0, 1);
    float boxWidth = s(50.0f);
    float boxHeight = s(18.f);

    ImVec2 pos = window->DC.CursorPos;
    float availWidth = CalcItemWidth();
    float rowHeight = s(20.f);

    ImRect bb(pos + ImVec2(availWidth - boxWidth, (rowHeight - boxHeight) / 2),
              pos + ImVec2(availWidth, (rowHeight + boxHeight) / 2));
    ImRect totalBb(pos, pos + ImVec2(availWidth, rowHeight));

    ItemAdd(bb, id);
    ItemSize(totalBb);

    bool hovered = ItemHoverable(bb, id, 0);
    bool active = GetActiveID() == id;

    // Update global binder state for ESC handling in overlay
    if (active)
        g_BinderActive = true;

    struct binder_state {
        float anim;
        float hover;
        ImColor col;
        ImColor textCol;
        int framesSinceActive;
    };
    auto& obj = anim_obj(label, 0, binder_state{0.f, 0.f, 0, 0, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, active);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered || active);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover),
                       GetColor(ImGuiCol_FrameBgActive), obj.anim);
    obj.textCol = col_anim(GetColor(ImGuiCol_Text), GetSchemeColor(), obj.anim);

    if (hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(id, window);
        FocusWindow(window);
        obj.framesSinceActive = 0;
    }

    if (active && !hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    bool changed = false;
    if (active) {
        obj.framesSinceActive++;
        if (obj.framesSinceActive > 5) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                *key = 0;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                *key = VK_RBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                *key = VK_MBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                *key = VK_XBUTTON1;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                *key = VK_XBUTTON2;
                ClearActiveID();
                changed = true;
            } else {
                // Check all common key ranges
                static const int keyRanges[][2] = {
                    {0x08, 0x2F},  // Control keys, arrows, etc.
                    {0x30, 0x39},  // Numbers 0-9
                    {0x41, 0x5A},  // Letters A-Z
                    {0x60, 0x6F},  // Numpad
                    {0x70, 0x87},  // F1-F24
                    {0x90, 0x91},  // NUMLOCK, SCROLL
                    {0xA0, 0xB7},  // Shift variants, browser/media keys
                    {0xBA, 0xC0},  // OEM keys: ;=,-./`
                    {0xDB, 0xDF},  // OEM keys: [\]'
                };
                bool found = false;
                for (const auto& range : keyRanges) {
                    for (int vk = range[0]; vk <= range[1] && !found; vk++) {
                        if (vk == VK_ESCAPE)
                            continue;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            *key = vk;
                            ClearActiveID();
                            changed = true;
                            found = true;
                        }
                    }
                    if (found)
                        break;
                }
            }
        }
    }

    draw->AddText({pos.x, pos.y + rowHeight / 2 - labelSize.y / 2}, GetColor(ImGuiCol_Text), label,
                  FindRenderedTextEnd(label));

    const char* displayText = active ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({bb.GetCenter().x - textSize.x / 2, bb.GetCenter().y - textSize.y / 2}, obj.textCol, displayText);

    HoverDescription(label, hovered || active);
    return changed;
}

// ==================== COLOR PICKER ====================

bool ui::color(const char* name, float col[4], bool alpha) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(name);
    auto draw = window->DrawList;
    auto pos = window->DC.CursorPos;

    ImVec2 size{s(16), s(16)};
    auto label_size = CalcTextSize(name, 0, 1);

    ImRect bb(pos, {pos.x + CalcItemWidth() - size.x, pos.y + size.y});
    ItemAdd(bb, id);
    ItemSize(bb);

    draw->AddText({bb.Min.x, bb.GetCenter().y - label_size.y / 2}, GetColor(ImGuiCol_Text), name,
                  FindRenderedTextEnd(name));

    bool value_changed = false;
    if (col) {
        pos = window->DC.CursorPos;
        window->DC.CursorPos = ImVec2{bb.Max.x, bb.Min.y};

        ImGuiColorEditFlags flags =
            alpha ? (ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs |
                     ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar)
                  : (ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                     ImGuiColorEditFlags_NoAlpha);

        value_changed = ColorEdit4(std::string(name).append("##col").c_str(), col, flags);
        window->DC.CursorPos = pos;
    }

    HoverDescription(name, ImGui::IsMouseHoveringRect(bb.Min, ImVec2(bb.Max.x + size.x, bb.Max.y)));
    return value_changed;
}

// ==================== COMBO ====================

bool ui::Combo(const char* label, int* current, const char* const items[], int count) {
    ImVec2 labelPos = ImGui::GetCursorPos();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GetColor(ImGuiCol_TextDisabled)), "%s", label);
    
    std::string hidden_label = std::string("##") + label;
    const bool changed = ImGui::Combo(hidden_label.c_str(), current, items, count);
    
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    // Also consider it hovered if mouse is over the label text
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    if (ImGui::IsMouseHoveringRect(ImGui::GetWindowPos() + labelPos, ImGui::GetWindowPos() + labelPos + labelSize)) {
        hovered = true;
    }
    
    const char* selectedValue = current && *current >= 0 && *current < count ? items[*current] : "";
    HoverDescription(label, hovered, resolveOptionDescription(label, selectedValue));
    return changed;
}

// ==================== TAB WIDGET ====================

static int rotation_start_index;

static void rotate_start() {
    rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

static ImVec2 rotation_center() {
    ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);
    const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);
    return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
}

static void rotate_end(float rad, ImVec2 center) {
    float s = sin(rad), c = cos(rad);
    center = ImRotate(center, s, c) - center;
    auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

void ui::tab(int num) {
    auto icon = tabs[num].m_icon;
    auto name = tabs[num].m_name;

    bool selected = ui::next_tab == num;

    auto window = ImGui::GetCurrentWindow();
    auto id = window->GetID(name);

    ImVec2 icon_size = CalcTextSize(icon, 0, 1);
    ImVec2 name_size =
        font(2 + (selected ? 1 : 0))
            ->CalcTextSizeA(font(2 + (selected ? 1 : 0))->FontSize, FLT_MAX, 0.f, name, FindRenderedTextEnd(name));

    ImVec2 tab_size{window->Size.x, s(38)};
    ImRect bb{window->DC.CursorPos, window->DC.CursorPos + tab_size};
    ItemAdd(bb, id);
    ItemSize(bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    if (pressed) {
        if (!selected) {
            // Selecting a different category: animate the whole content area.
            ui::next_tab = num;
            ui::anim_dst = 0.f;
        } else if (num == ui::cur_tab && tabs[num].cur_view == ui::CategoryView::Config) {
            // Re-selecting the already-active module category while it is showing a
            // module config issues a Back-equivalent: return to its module list.
            tabs[num].next_view = ui::CategoryView::List;
            content_anim_dst = 0.f;
        }
    }

    struct tab_state {
        float cur;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, tab_state{0.f, 0.f, 0});

    obj.cur = animate(obj.cur, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text, 0.75f), obj.hover),
                       GetColor(ImGuiCol_Text), obj.cur);

    // Rounded selection/hover highlight pill behind the row (drawn first so the
    // icon + label sit on top). Cleaner than per-row separator lines.
    float pillT = ImMax(obj.cur, obj.hover * 0.55f);
    if (pillT > 0.01f) {
        ImVec2 pillMin{bb.Min.x + s(8), bb.Min.y + s(3)};
        ImVec2 pillMax{bb.Max.x - s(8), bb.Max.y - s(3)};
        window->DrawList->AddRectFilled(pillMin, pillMax, GetSchemeColor(0.14f * pillT), s(6));
    }

    // Hide icons on small resolutions
    bool showIcons = ImGui::GetIO().DisplaySize.y >= 600.0f;

    // Icon position and text spacing
    float iconX = bb.Min.x + s(15);
    float iconTextGap = s(10);  // consistent gap between icon and text
    float textOffsetX = showIcons ? (iconX - bb.Min.x + icon_size.x + iconTextGap) : s(15);

    if (showIcons) {
        window->DrawList->AddText({iconX, bb.GetCenter().y - icon_size.y / 2}, obj.col, icon,
                                  FindRenderedTextEnd(icon));
    }
    window->DrawList->AddText(font(2 + (selected ? 1 : 0)), font(2 + (selected ? 1 : 0))->FontSize,
                              {bb.Min.x + textOffsetX, bb.GetCenter().y - name_size.y / 2}, obj.col, name,
                              FindRenderedTextEnd(name));

    // Category rows are select-only: no expansion arrow, no module rows, and no
    // per-row separator line (the selection pill above conveys state instead).
}

void ui::subtab(int num, int tab_num) {
    SetDescriptionContext("__modules__");
    auto name = tabs[tab_num].m_subtabs[num];
    auto name_size = ImGui::CalcTextSize(name, 0, 1);

    // Check if module is locked (experimental feature)
    Module* mod = getModuleForSubtab(tab_num, num);
    bool isLocked = mod && mod->showsDeveloperOnly();

    bool selected = tabs[tab_num].next_subtab == num;

    auto window = ImGui::GetCurrentWindow();
    auto id = window->GetID(name);

    // Larger subtab row with proper padding
    float rowHeight = s(20);
    float availWidth = window->Size.x - GetCursorPosX();
    ImRect bb{window->DC.CursorPos, window->DC.CursorPos + ImVec2(availWidth, rowHeight)};
    ItemAdd(bb, id);
    ItemSize(bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    // Allow selection even for locked modules - renderPage shows unlock UI
    if (pressed && !selected) {
        tabs[tab_num].next_subtab = num;
        content_anim_dst = 0.f;
    }

    struct subtab_state {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, subtab_state{0.f, 0.f, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);

    // Hover background
    if (obj.hover > 0.01f && !selected) {
        window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColor(ImGuiCol_FrameBgHovered, obj.hover * 0.3f), s(4));
    }

    // Center text vertically
    float textY = bb.GetCenter().y - name_size.y / 2;

    if (isLocked) {
        // Grayed out look for locked modules
        ImU32 grayColor = IM_COL32(100, 100, 100, 150);
        window->DrawList->AddText(ImVec2(bb.Min.x, textY), grayColor, name, FindRenderedTextEnd(name));
    } else {
        obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text), obj.hover),
                           GetSchemeColor(), obj.anim);
        window->DrawList->AddText(ImVec2(bb.Min.x, textY), obj.col, name, FindRenderedTextEnd(name));
    }

    HoverDescription(name, hovered, isLocked ? "Developer only (for now.)" : GetModuleDescription(name), isLocked);
}

// ==================== APPLY STYLE ====================

void ui::apply_style() {
    theme::apply();
}
