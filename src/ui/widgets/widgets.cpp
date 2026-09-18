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
    {"", "SilentAura", "", "Attacks valid players using a separate server-side rotation without moving your camera."},
    {"", "Velocity", "", "Controls how much enemy knockback affects your movement."},
    {"", "Reach", "", "Extends the distance at which attacks can connect."},
    {"", "HitSelect", "", "Times attacks around combo state to improve hit selection."},
    {"", "NoHitDelay", "", "Removes the client-side delay between valid attacks."},
    {"", "Criticals", "", "Automates critical-hit movement when its conditions are met."},
    {"", "WTap", "", "Briefly resets forward movement or sprint after attacks to control spacing."},
    {"", "AutoBlock", "",
     "Automatically blocks with a sword during combat with Slinky-style state machine, lag phase and visual "
     "animations."},
    {"", "Backtrack", "", "Targets recent server positions to compensate for movement and latency."},
    {"", "Piercing", "", "Finds and attacks valid entities through obstructing blocks."},
    {"", "AntiBot", "", "Decides what counts as a fake player. Every combat module asks it before choosing a target."},
    {"", "NameTags", "", "Renders readable, customizable information above players."},
    {"", "ESP", "", "Highlights players through terrain with configurable boxes and health."},
    {"", "Chams", "", "Re-renders nearby players through terrain with a configurable material."},
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
    {"", "InventoryMove", "", "Lets you keep walking, jumping and sprinting while a screen is open."},
    {"", "ArmorSwitch", "", "Switches between configured armor sets with one action."},
    {"", "AutoRefill", "",
     "Moves replacement soup or potions into empty hotbar slots, on human timing: a reaction pause, jittered gaps, and "
     "a real cursor reach when Legit mode draws one."},
    {"", "ThrowPots", "",
     "One-shot splash-heal burst on a keybind: counts how many pots the missing health needs, flicks down on human "
     "timing, and gives your slot and angle back."},
    {"", "FastPlace", "", "Reduces the delay between right-click item placements."},
    {"", "NoItemRelease", "", "Keeps supported item-use actions active according to the selected mode."},
    {"", "BridgeAssist", "", "Helps you sneak safely at block edges while bridging."},
    {"", "Scaffold", "", "Auto-bridges blocks beneath you along the direction you look as you move."},
    {"", "Clutch", "", "Emergency save: places a block only when your predicted path would fall into the void."},
    {"", "Breaker", "", "Finds nearby beds and breaks them through surrounding blocks."},
    {"", "AutoTool", "", "Selects the best hotbar tool for the block or target."},
    {"", "InvManager", "", "Organizes the inventory and hotbar using your saved preferences."},
    {"", "AltManager", "", "Sets the offline-mode username used on the next server connection."},
    {"", "FastBreak", "", "Removes the cooldown between block mining hits so blocks break faster."},
    {"", "Teams", "", "Decides who counts as a teammate, from server team data, nametag colors and the scoreboard."},
    {"", "AutoPot", "",
     "Throws splash healing potions at your feet when your health drops, counting the heal already in the air so a "
     "burst spends exactly as many potions as the missing health calls for."},
    {"", "FastStop", "", "Cancels leftover momentum instantly when you release the movement keys."},
};

static const DescriptionEntry kOptionDescriptions[] = {
    {"", "Enabled", "", "Turns this module on or off. The key box sets its toggle bind."},
    {"", "Weapon Only", "", "Runs only while a supported weapon is held."},
    {"", "Weapons Only", "", "Runs only while a supported weapon is held."},
    {"", "Mouse Pressed", "", "Runs only while the required mouse button is held."},
    {"", "Break Blocks", "", "Lets normal block breaking take priority over this feature."},
    {"", "Target Invisibles", "", "Allows invisible players to be considered valid targets."},
    {"", "Draw Invisibles", "", "Includes invisible players in the rendered highlight."},
    {"", "Custom Color", "", "Uses a flat configurable color instead of the player texture."},
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
    {"", "Allow Heal I", "", "Uses Instant Health I when no stronger splash healing potion is available."},
    {"", "Target Health (%)", "", "Uses potions only below this percentage of the player's maximum health."},
    {"", "Max Throws", "", "Caps the number of potions used by one smart-heal activation."},
    {"", "Random Slots", "", "Randomizes the order of eligible healing-potion slots for each activation."},
    {"", "Switch Delay (ms)", "", "Waits after selecting a potion before trying to throw it."},
    {"", "Throw Delay (ms)", "", "Waits for consumption and the server health update before the next potion."},
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
    {"", "Prediction Time", "", "Clutch mode: how far ahead the trajectory is simulated to detect a void fall."},
    {"", "Search Radius", "", "Clutch mode: how far to search for a reachable solid anchor."},
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
    {"SilentAura", "Aim Speed", "", "Controls how quickly the server-side rotation converges on the selected target."},
    {"SilentAura", "Extra Swing Distance", "", "Adds to the normal three-block acquisition distance."},
    {"SilentAura", "Max Angle", "", "Limits targets to half this total angle around your visible view."},
    {"SilentAura", "Target Mode", "Distance", "Targets the nearest valid player."},
    {"SilentAura", "Target Mode", "Yaw", "Targets the valid player closest to your visible crosshair."},
    {"SilentAura", "Target Mode", "Armor", "Targets the valid player with the lowest armor value."},
    {"SilentAura", "Target Mode", "Threat", "Targets the valid player with the strongest weapon and armor."},
    {"SilentAura", "Target Mode", "Health", "Targets the valid player with the lowest health."},
    {"SilentAura", "Target Area", "Center", "Aims at the center of the target hitbox."},
    {"SilentAura", "Target Area", "Closest", "Aims at the closest horizontal point inside the target hitbox."},
    {"SilentAura", "Switch", "", "Prefers another valid player while the current target is in hit cooldown."},
    {"SilentAura", "Require Mouse Down", "", "Runs only while the attack button is held."},
    {"AutoClicker", "Randomization", "Random", "Uses the standard pseudo-random generator for each CPS interval."},
    {"AutoClicker", "Randomization", "Natural", "Uses a separately seeded 64-bit generator for CPS and click timing."},
    {"AutoClicker", "Randomization", "Adaptive",
     "Uses secure draws with changing CPS clusters, random walks, and asymmetric press timing."},
    {"Velocity", "Mode", "Regular", "Directly scales horizontal and vertical knockback by the configured percentages."},
    {"Velocity", "Mode", "Jump", "Attempts a timed jump after taking knockback to preserve movement control."},
    {"Velocity", "Mode", "Reduce",
     "Reduces knockback after the configured tick delay instead of applying it immediately."},
    {"Velocity", "Mode", "Ignore", "Randomly ignores eligible knockback while using the selected vertical rule."},
    {"Velocity", "Vertical", "Never", "Never suppresses the vertical component while Ignore mode activates."},
    {"Velocity", "Vertical", "Only Air", "Suppresses vertical knockback only while you are already airborne."},
    {"Velocity", "Vertical", "Always", "Suppresses vertical knockback whenever Ignore mode activates."},
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
    {"InventoryMove", "Movement", "", "Passes WASD through to the game while a screen is open."},
    {"InventoryMove", "Jump", "", "Passes the jump key through while a screen is open."},
    {"InventoryMove", "Sneak", "", "Passes the sneak key through while a screen is open."},
    {"InventoryMove", "Sprint", "", "Passes the sprint key through while a screen is open."},
    {"InventoryMove", "Containers", "", "Keeps you moving inside chests, furnaces and your own inventory."},
    {"InventoryMove", "Text Screens", "",
     "Also moves while chat, signs, books and the anvil are open — careful, WASD are letters there."},
    {"InventoryMove", "Other Screens", "", "Also moves in the pause menu, options and any other non-container screen."},
    {"Backtrack", "Mode", "Always", "Engages whenever the crosshair is on a player — the classic behaviour."},
    {"Backtrack", "Mode", "Only on Hit",
     "Engages only after you actually land a hit. The ghost appears mid-fight and expires after the hold window."},
    {"Backtrack", "Hold Time (ms)", "",
     "How far into the past the ghost sits. Movement packets are withheld for this long, so the server keeps the "
     "target where the box is drawn."},
    {"Backtrack", "Packet Limit", "",
     "Hard stop on how many of the target's movement packets may be withheld at once. Reaching it releases the whole "
     "queue immediately."},
    {"Backtrack", "Whole Stream", "",
     "Delays every player's movement, not just your target's. A real connection is not late only for whoever you are "
     "looking at, so leaving this on is what makes the hold read as lag."},
    {"Backtrack", "Min Range", "",
     "Ignores targets closer than this, since backtracking point-blank pushes the hitbox behind you."},
    {"Backtrack", "Max Range", "",
     "Ignores targets past this distance, so the hold only opens inside your real reach."},
    {"Backtrack", "Only While Attacking", "",
     "Holds packets only while you are actually swinging — the mouse held down, or AutoClicker running."},
    {"Backtrack", "Color", "",
     "Color of the ghost box drawn where the server still believes the target is. Its alpha is the overall opacity."},

    {"AntiBot", "Name Tag", "",
     "Flags a player whose name gives it away: an explicit [NPC] tag, an empty or one-letter name, or a name made only "
     "of color codes. Safe to leave on."},
    {"AntiBot", "NPC UUID", "",
     "Flags version-2 UUIDs, which the Citizens NPC plugin hands out, and the all-zero UUID. Real accounts never have "
     "either, so this costs you nothing."},
    {"AntiBot", "Fake Profile", "",
     "Flags a game profile that is not a name Mojang could issue — wrong length or illegal characters. Skipped when "
     "the profile name cannot be read."},
    {"AntiBot", "No Health", "",
     "Flags an entity the server never gave health attributes to. Real players always have a max health."},
    {"AntiBot", "Invisible", "",
     "Flags every invisible player as a bot. Aggressive: on any server with invisibility potions this will hide real "
     "enemies."},
    {"AntiBot", "Reset Counters", "",
     "Zeroes the per-check detection counts. They also reset on their own when you change world or server."},
    {"Teams", "Enabled", "",
     "Master switch for team awareness. Off means every player is an enemy, including the ones the vanilla scoreboard "
     "puts on your team."},
    {"Teams", "Lunar API", "",
     "Trusts the roster the server sends over the Lunar Client channel. This is the only source that states outright "
     "who your teammates are, so while it reports anyone the weaker ones are ignored."},
    {"Teams", "Apollo Nametags", "",
     "Treats a green Apollo nametag as a teammate, once it has been seen enough times to rule out cosmetic color "
     "cycling."},
    {"Teams", "Scoreboard", "",
     "Uses Cobble's own scoreboard tracking: the S3EPacketTeams roster, matched by name. Turning this off makes combat "
     "modules hit players the server put on your team."},
    {"Teams", "Own Nametag Color", "", "Counts players whose nametag color matches yours as teammates."},
    {"Teams", "Manual Color", "", "Picks one color yourself instead of reading it from your own nametag."},
    {"Teams", "Team Color", "", "The nametag color that counts as friendly while Manual Color is on."},
    {"Teams", "Confirm Hits", "",
     "How many green nametag signals are needed before a player is trusted. 1 reacts instantly but falls for "
     "cosmetics; higher is slower and certain."},
    {"Teams", "Confirm Window (ms)", "",
     "How close together those signals have to arrive. A longer gap than this restarts the count, so a tag that only "
     "flashes green now and then never adds up to a teammate."},
    {"Teams", "Grace Time (ms)", "",
     "How long a confirmed teammate keeps the status after losing the color, so a temporary recolor does not turn a "
     "teammate back into a target."},
    {"Teams", "Require Team Tag", "",
     "Counts a green nametag only when it arrives with the team-tag payload. Lobby and party tags do not carry one, so "
     "this is the fix for servers that hand green out too freely."},
    {"Teams", "Lunar Overrides Tags", "",
     "While the Lunar roster names anyone in this world, green nametags are ignored entirely. Turn it off where the "
     "roster is only your party and the rest of the team is marked by color."},
    {"Teams", "Vanilla Teams", "",
     "Obeys the game's own same-team check. This is the flag the server sets with /team, independent of anything "
     "Cobble "
     "detects; off means a scoreboard team alone will not protect anyone."},
    {"Teams", "Quick Add", "",
     "Adds whoever you are looking at to your friends with one key. The friend list itself lives in Settings."},
    {"Teams", "Clear Detected", "",
     "Forgets every auto-detected teammate so the next packets rebuild the list. Your manual friends stay."},
    {"AutoPot", "Pot Below (%)", "",
     "Health at which a burst starts, as a share of your maximum. 55% is roughly where a good player reaches for a "
     "potion: low enough not to waste them, high enough to survive the next combo."},
    {"AutoPot", "Heal Until (%)", "",
     "The burst stops once predicted health reaches this. Predicted, not current, so potions still in the air already "
     "count and you never overheal by two throws."},
    {"AutoPot", "Max Pots", "", "Hard ceiling on one burst, whatever the health maths asks for."},
    {"AutoPot", "Prefer Heal II", "",
     "Spends the strong potions first, ending the burst in fewer throws. Turn it off to save Heal II and pay with Heal "
     "I."},
    {"AutoPot", "Allow Heal I", "", "Lets plain Instant Health count. Off means only Instant Health II is ever used."},
    {"AutoPot", "Only On Ground", "",
     "Skips throws while airborne, where the potion lands somewhere other than under you."},
    {"AutoPot", "Only In Combat", "",
     "Waits for recent damage before potting, so you do not spend potions topping off between fights."},
    {"AutoPot", "Combat Window (ms)", "", "How long after taking a hit you still count as in combat."},
    {"AutoPot", "Preset", "",
     "Named timing bundles. Instant is as fast as the protocol allows, Human is what a good player actually does, "
     "Legit is visibly unhurried. Moving any timing slider switches this to Custom instead of mislabelling itself."},
    {"AutoPot", "Reaction (ms)", "",
     "Delay between health dropping and your hand moving. Zero is machine-fast; humans sit near 140. If health has "
     "already been low for a moment this is shortened, because a player watching the bar reacts sooner."},
    {"AutoPot", "Jitter (%)", "",
     "Spread applied to every delay, drawn so most throws land near the base and the extremes stay rare. Zero makes "
     "every burst identically timed, which is the easiest thing in the world to spot."},
    {"AutoPot", "Switch (ms)", "", "Gap between selecting the hotbar slot and the right click."},
    {"AutoPot", "Throw Gap (ms)", "", "Pause between potions of one burst."},
    {"AutoPot", "Restore (ms)", "", "Pause after the last throw before the sword comes back."},
    {"AutoPot", "Fatigue", "",
     "A long fight slows the delays slightly, the way real hands do, and resets once you stop potting."},
    {"AutoPot", "Aim", "",
     "Silent points the outgoing packet down and leaves your camera alone. Snap moves the real pitch and jumps back. "
     "Smooth pulls it down over a few frames like a mouse. None throws wherever you already look."},
    {"AutoPot", "Pitch", "", "Degrees down for the throw. 90 is straight at your feet."},
    {"AutoPot", "Pitch Jitter", "",
     "Degrees of variation per throw. Invisible inside the splash radius, very visible in a rotation log if absent."},
    {"AutoPot", "Flick Time (ms)", "", "How long the Smooth pull down takes."},
    {"AutoPot", "Switch Back", "", "Returns to the slot you were holding once the burst ends."},
    {"AutoPot", "Retry On Fail", "",
     "If a throw does not register, tries again with the same potion instead of abandoning the burst."},
    {"AutoPot", "Heal Credit (ms)", "",
     "How long a thrown potion's heal is assumed to be on its way. The server reports it a tick or two late, and "
     "without this window the same missing health gets potted twice."},
    {"AutoPot", "Misclick Chance (%)", "",
     "Chance of grabbing a neighbouring hotbar slot first, noticing, and correcting. Costs one switch delay. Zero by "
     "default because it is a liability in a real fight."},
    {"AutoRefill", "Mode", "Packet",
     "No screen and no cursor: the inventory is opened, swapped and closed over packets alone. Nothing is drawn, so "
     "this is the one to use in a fight."},
    {"AutoRefill", "Mode", "Legit",
     "Presses your real inventory key and moves the real cursor to each stack, so anything watching the client sees an "
     "ordinary refill."},
    {"AutoRefill", "On Inventory Open", "",
     "Stops refilling on a keybind and instead waits for you to open the inventory yourself, then fills the gaps while "
     "you are already in there. The screen stays yours — it is never closed for you."},
    {"AutoRefill", "Preset", "",
     "Named timing bundles. Instant is the old flat machine timing, Human is what a good player actually produces, "
     "Careful is visibly unhurried. Moving any slider switches this to Custom instead of mislabelling itself."},
    {"AutoRefill", "Gap (ms)", "",
     "Base pause between item moves. Never used as-is: every gap is jittered, scaled by fatigue, and now and then "
     "extended by a hesitation, so no two refills share a rhythm."},
    {"AutoRefill", "Reaction (ms)", "",
     "Delay between the trigger and anything at all happening. Zero opens and clicks on the same frame, which no hand "
     "can do; a person sits near 190."},
    {"AutoRefill", "Jitter (%)", "",
     "Spread applied to every delay, drawn so most land near the base and the extremes stay rare. Zero gives a "
     "perfectly periodic click train, which is the easiest pattern in the world to pick out of a log."},
    {"AutoRefill", "Fatigue", "",
     "A long refill slows slightly as it goes, the way real hands do across nine slots, and resets for the next one."},
    {"AutoRefill", "Hesitate (%)", "",
     "Chance per item of an extra pause — and in Legit mode a short cursor drift onto the wrong stack before "
     "correcting. Costs time and nothing else, since the decoy is never clicked."},
    {"AutoRefill", "Nearest Source First", "",
     "Takes whichever stack sits closest to the last one instead of sweeping strictly left to right, which is both "
     "shorter for the cursor and what a person actually does."},
    {"AutoRefill", "Human Mouse Path", "",
     "Replaces the straight constant-speed slide with a real reach: eased acceleration, a sideways arc, an occasional "
     "overshoot and correction, hand tremor, and a duration that grows with distance. Legit mode only."},
    {"AutoRefill", "Mouse Move (ms)", "",
     "Reference time for one cursor move. Short hops finish quicker and long crossings take longer, so this sets the "
     "scale rather than the exact duration."},

    {"ThrowPots", "Mode", "Dynamic", "Throws as many pots as it takes to reach the target health, up to Max Throws."},
    {"ThrowPots", "Mode", "Single", "Throws exactly one pot, whatever your health is."},
    {"ThrowPots", "Mode", "Double", "Throws up to two pots - the usual manual panic-pot."},
    {"ThrowPots", "Aim", "Current", "Throws wherever you are already looking. No rotation, nothing to hide."},
    {"ThrowPots", "Aim", "Down", "Aims your view at the ground and back up again, so what you see is what is sent."},
    {"ThrowPots", "Aim", "Silent Down",
     "Aims down only inside the outgoing packet. Your view never moves, but the server sees a downward flick."},
    {"ThrowPots", "Target Health (%)", "",
     "Health the burst is trying to reach. Dynamic keeps throwing until you are at or above it."},
    {"ThrowPots", "Max Throws", "", "Hard ceiling on one Dynamic burst, so a bad heal roll cannot empty the hotbar."},
    {"ThrowPots", "Random Slots", "",
     "Picks pot slots in a random order instead of strongest first, so the same hotbar does not always drain the same "
     "way."},
    {"ThrowPots", "Allow Heal I", "", "Lets weak Healing I splashes into the plan when no Healing II is left."},
    {"ThrowPots", "Aim Pitch", "",
     "How far down the throw aims. 90 is straight at your feet; a couple of degrees short of that is what a real flick "
     "lands on."},
    {"ThrowPots", "Pitch Jitter", "",
     "Random spread on that pitch, in degrees. Without it every throw reports the exact same angle, which no hand ever "
     "does."},
    {"ThrowPots", "Aim Smoothing (ms)", "",
     "Sweeps the pitch down over this long instead of teleporting to it. The throw only fires once the sweep has "
     "landed. 0 snaps."},
    {"ThrowPots", "Switch Back", "", "Returns to the slot, and the angle, you were on before the burst."},
    {"ThrowPots", "Retry on Fail", "",
     "Re-throws a pot that never left your hand, up to twice, instead of silently skipping it."},
    {"ThrowPots", "Preset", "",
     "Timing bundle. Instant is the old flat behaviour, Human is a fast player under pressure, Careful trades speed "
     "for the safest pattern."},
    {"ThrowPots", "Reaction (ms)", "",
     "Pause between the keybind and the first slot switch. Reacting instantly is the clearest sign a program pressed "
     "the key."},
    {"ThrowPots", "Switch Delay (ms)", "",
     "Base wait after selecting a pot slot before throwing, and again before switching back."},
    {"ThrowPots", "Throw Delay (ms)", "", "Base wait between one pot landing and the next being selected."},
    {"ThrowPots", "Jitter (%)", "",
     "Spread applied to every delay above. Real gaps scatter around an average; identical gaps are a fingerprint."},
    {"ThrowPots", "Fatigue", "",
     "Widens the gaps slightly as the burst runs. The fourth pot never comes out as fast as the first."},
    {"Settings", "Freelook", "",
     "Turns Freelook on in toggle mode. The key box assigns the key used by either activation mode."},
    {"Settings", "Hold Key", "",
     "Keeps Freelook active only while its assigned key is held, then restores the previous perspective."},
    {"Settings", "Perspective", "Back", "Places the detached camera behind your player."},
    {"Settings", "Perspective", "Front", "Places the detached camera in front of your player."},
    {"AutoBlock", "Range", "", "Maximum distance to an enemy for AutoBlock to activate."},
    {"AutoBlock", "Maximum Hurt Time", "", "Threshold of hurt ticks to trigger blocking."},
    {"AutoBlock", "Hold Duration", "", "Duration in ticks to hold the block."},
    {"AutoBlock", "Require Left Click", "", "Only blocks while holding attack (LMB)."},
    {"AutoBlock", "Require Right Click", "", "Only blocks while holding use/block (RMB)."},
    {"AutoBlock", "Require Damage", "", "Only activates if the player has recently taken damage."},
    {"AutoBlock", "Fake Animation", "", "Renders the first-person blocking pose without server slowdown."},
    {"AutoBlock", "Only Within Range", "", "Only animates the fake block if a target is in range."},
    {"AutoBlock", "Chance", "", "Probability of triggering the lag/delay phase."},
    {"AutoBlock", "Duration", "", "Maximum duration of the lag phase."},
    {"AutoBlock", "Flush On Attack", "", "Clears the lag delay immediately on attack or swing."},
    {"AutoBlock", "Reblock Instantly", "", "Removes re-entry cooldown to re-block immediately."},
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
    // A combo always asks with its selected value, so a table row written for the option as a
    // whole -- {context, key, ""} -- would never have matched one, and every such combo fell
    // through to the generated "Adjusts X" line instead of the text somebody wrote for it.
    if (value && *value)
        if (const char* byKey = findDescription(kOptionDescriptions, IM_ARRAYSIZE(kOptionDescriptions),
                                                g_descriptionContext.c_str(), key, ""))
            return byKey;
    if (const char* generic = findDescription(kOptionDescriptions, IM_ARRAYSIZE(kOptionDescriptions), "", key, ""))
        return generic;

    static std::string fallback;
    fallback = "Adjusts ";
    fallback += key ? key : "this option";
    fallback += " for ";
    fallback += g_descriptionContext.empty() ? "the current feature." : g_descriptionContext + ".";
    return fallback.c_str();
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
     {"AutoClicker", "AimAssist", "Velocity", "Reach", "HitSelect", "NoHitDelay", "Criticals", "WTap", "Piercing",
      "AutoBlock", "AntiBot", "SilentAura"}},
    {ICON_FA_EYE,
     "Visuals",
     {"NameTags", "ESP", "ItemESP", "Tracers", "BlockESP", "Trajectories", "AntiDebuff", "ArrayList", "DroppedInfo",
      "Notifications", "Watermark", "Pings", "Chams"}},
    {ICON_FA_RUNNING,
     "Movement",
     {"Timer", "TimerRange", "NoSlowDown", "NoJumpDelay", "Sprint", "InventoryMove", "FastStop"}},
    {ICON_FA_KEYBOARD,
     "Utility",
     {"ArmorSwitch", "AutoRefill", "ThrowPots", "AutoPot", "FastPlace", "NoItemRelease", "BridgeAssist", "Scaffold",
      "Clutch", "Breaker", "AutoTool", "InvManager", "FastBreak", "AltManager", "Teams"}},
    {ICON_FA_WIFI, "Network", {"Backtrack", "Blink", "FakeLag"}},
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

// ==================== HOVER DESCRIPTIONS ====================
// One tip at a time, typed out letter by letter. HoverDescription() only *records* the
// request — widgets call it from all over the frame — and the text is drawn once, from
// handle_anims(), after everything else has been laid out. That ordering is what lets it
// go into the foreground draw list (nothing can cover it) and what makes "whichever
// widget is hovered wins" fall out for free.
//
// Bare text, no card: a panel next to the cursor covers the very rows you are comparing,
// and its border/fill fights the widget it is describing for attention. Legibility comes
// from a two-step drop shadow instead, the same trick the notification stack uses.
namespace {

// How many glyph-slots wide the arrival fade is. Narrow on purpose: the trail should read
// as motion, not as a queue you have to wait behind.
constexpr float kTipFadeBand = 3.5f;
// Glyphs per second. The reveal is a flourish, not a gate on reading — a full sentence is
// on screen inside a few frames, so the eye never has to track the cursor to keep up.
constexpr float kTipGlyphsPerSecond = 195.0f;
// Text height. Smaller than the notification row (ui::s(14)) so it stays subordinate to
// the setting it is describing.
constexpr float kTipTextSize = 12.5f;

struct DescriptionTip {
    std::string text;      // the sentence currently being revealed
    bool rainbow = false;  // locked-module tips cycle hue instead of using the text color
    ImVec2 anchor{0, 0};   // cursor position recorded with the request
    ImVec2 pos{0, 0};      // eased text origin, so moving between settings glides
    float reveal = 0.0f;   // glyphs revealed so far, fractional
    float alpha = 0.0f;    // overall fade
    bool placed = false;   // pos is meaningful (don't slide in from the screen corner)
    int requestFrame = -1;
};

// Set once per menu frame by SetDescriptionContext. Tips are a module-page affordance:
// the module list, the Settings page and the general chrome all get none, so hovering
// your way around the menu doesn't trail a sentence behind the cursor everywhere.
bool g_descriptionAllowed = false;

DescriptionTip g_tip;

struct TipLine {
    const char* begin;
    const char* end;
    float width;
};

// Cubic ease-out: quick off the mark, settles gently.
float easeOut(float t) {
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Byte length of the UTF-8 sequence at p, never running past end.
int glyphBytes(const char* p, const char* end) {
    int len = 1;
    while (p + len < end && (static_cast<unsigned char>(p[len]) & 0xC0) == 0x80)
        ++len;
    return len;
}

// ImGui's own tooltips (the tick-timing hint) claim the space just under the cursor.
bool imguiTooltipVisible() {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx)
        return false;
    for (ImGuiWindow* window : ctx->Windows)
        if ((window->Flags & ImGuiWindowFlags_Tooltip) && window->WasActive)
            return true;
    return false;
}

void drawDescriptionTip() {
    ImGuiIO& io = ImGui::GetIO();
    // A hitch (chunk build, world load) shouldn't dump the whole sentence in one frame.
    const float dt = ImMin(io.DeltaTime, 1.0f / 30.0f);

    const bool live = !g_tip.text.empty() && g_tip.requestFrame == ImGui::GetFrameCount();
    g_tip.alpha = ImClamp(g_tip.alpha + (live ? dt * 17.0f : -dt * 23.0f), 0.0f, 1.0f);
    if (g_tip.alpha <= 0.0f) {
        g_tip.placed = false;
        return;
    }

    ImFont* tipFont = (io.Fonts && io.Fonts->Fonts.Size > 0) ? font(0) : ImGui::GetFont();
    if (!tipFont || !tipFont->IsLoaded())
        return;

    const float fontSize = ui::s(kTipTextSize);
    // The atlas glyphs were baked at FontSize; every width query has to be told what
    // fraction of that we are actually drawing at, or the wrap runs long.
    const float fontScale = tipFont->FontSize > 0.0f ? fontSize / tipFont->FontSize : 1.0f;
    const float lineHeight = fontSize * 1.26f;
    const float wrapWidth = ImMax(ui::s(140.0f), ImMin(ui::s(260.0f), io.DisplaySize.x * 0.30f));

    // Wrap once per frame. Paragraphs are split by hand because CalcWordWrapPositionA
    // treats '\n' as "reset the running width" and keeps going.
    static std::vector<TipLine> lines;
    lines.clear();
    float widest = 0.0f;
    const char* text = g_tip.text.c_str();
    const char* textEnd = text + g_tip.text.size();
    for (const char* paragraph = text; paragraph <= textEnd;) {
        const char* hardEnd = paragraph;
        while (hardEnd < textEnd && *hardEnd != '\n')
            ++hardEnd;
        for (const char* runner = paragraph;;) {
            const char* lineEnd = tipFont->CalcWordWrapPositionA(fontScale, runner, hardEnd, wrapWidth);
            if (lineEnd <= runner)
                lineEnd = ImMin(runner + 1, hardEnd);  // a single glyph wider than the panel
            const float width = tipFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, runner, lineEnd).x;
            lines.push_back({runner, lineEnd, width});
            widest = ImMax(widest, width);
            runner = lineEnd;
            while (runner < hardEnd && *runner == ' ')  // the wrap point sits on the space
                ++runner;
            if (runner >= hardEnd)
                break;
        }
        if (hardEnd >= textEnd)
            break;
        paragraph = hardEnd + 1;
    }
    if (lines.empty())
        return;

    int glyphTotal = 0;
    for (const char* p = text; p < textEnd; ++p)
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80)
            ++glyphTotal;
    g_tip.reveal = ImMin(g_tip.reveal + dt * kTipGlyphsPerSecond, static_cast<float>(glyphTotal) + kTipFadeBand);

    // Only used to keep the block on screen — there is no panel to size any more.
    const ImVec2 textSize(widest, lineHeight * static_cast<float>(lines.size()));

    const float margin = ui::s(8.0f);
    ImVec2 target(g_tip.anchor.x + ui::s(15.0f), g_tip.anchor.y + ui::s(19.0f));
    if (imguiTooltipVisible())
        target.y = g_tip.anchor.y - textSize.y - ui::s(10.0f);
    target.x = ImMax(margin, ImMin(target.x, io.DisplaySize.x - textSize.x - margin));
    target.y = ImMax(margin, ImMin(target.y, io.DisplaySize.y - textSize.y - margin));
    if (!g_tip.placed) {
        g_tip.pos = target;
        g_tip.placed = true;
    } else {
        // Trails the cursor, and glides when you move from one setting to the next. Quicker
        // than the old panel: with no card to carry there is no weight to sell.
        g_tip.pos = ImLerp(g_tip.pos, target, ImMin(1.0f, dt * 32.0f));
    }

    const float ease = easeOut(g_tip.alpha);
    const ImVec2 origin = g_tip.pos + ImVec2(0.0f, (1.0f - ease) * ui::s(4.0f));  // rises as it appears
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(GetAccentRGB());

    // The reveal itself. Every glyph lands on its own clock: it fades up, drifts the last
    // couple of pixels into place, and cools from the accent color to the text color. The
    // glyph advances come from the full string, so nothing ever shifts sideways as it types.
    // Each glyph carries its own two-step shadow — with the card gone, that is the only
    // thing separating the sentence from whatever is behind the menu.
    const float time = static_cast<float>(ImGui::GetTime());
    const ImVec4 landed = ImGui::ColorConvertU32ToFloat4(ui::GetColor(ImGuiCol_Text));
    const float lift = ImMax(1.0f, ui::s(1.0f));
    int index = 0;
    float y = origin.y;
    for (const TipLine& line : lines) {
        float x = origin.x;
        for (const char* p = line.begin; p < line.end;) {
            const int bytes = glyphBytes(p, line.end);
            const float raw = ImClamp((g_tip.reveal - static_cast<float>(index)) / kTipFadeBand, 0.0f, 1.0f);
            // Spaces cost three draw calls and show nothing.
            if (raw > 0.0f && *p != ' ') {
                const float t = easeOut(raw);
                ImVec4 color;
                if (g_tip.rainbow)
                    ImGui::ColorConvertHSVtoRGB(fmodf(time * 0.55f + static_cast<float>(index) * 0.03f, 1.0f), 0.72f,
                                                1.0f, color.x, color.y, color.z);
                else
                    color = ImLerp(accent, landed, t);
                color.w = t * ease;
                const ImVec2 at(x, y + (1.0f - t) * ui::s(2.5f));
                draw->AddText(tipFont, fontSize, ImVec2(at.x + lift * 2.0f, at.y + lift * 2.0f),
                              IM_COL32(0, 0, 0, static_cast<int>(color.w * 58.0f)), p, p + bytes);
                draw->AddText(tipFont, fontSize, ImVec2(at.x + lift, at.y + lift),
                              IM_COL32(0, 0, 0, static_cast<int>(color.w * 155.0f)), p, p + bytes);
                draw->AddText(tipFont, fontSize, at, ImGui::ColorConvertFloat4ToU32(color), p, p + bytes);
            }
            x += tipFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, p + bytes).x;
            p += bytes;
            ++index;
        }
        y += lineHeight;
    }
}

}  // namespace

void ui::SetDescriptionContext(const char* context, bool allowTips) {
    g_descriptionContext = context ? context : "";
    g_descriptionAllowed = allowTips;
}

const char* ui::GetModuleDescription(const char* module) {
    if (const char* description =
            findDescription(kModuleDescriptions, IM_ARRAYSIZE(kModuleDescriptions), "", module, ""))
        return description;
    return "Configures this module's behavior.";
}

void ui::HoverDescription(const char* key, bool hovered, const char* explicitText, bool rainbow) {
    if (!hovered)
        return;
    // Tips belong to module pages. Everywhere else — the module grid, Settings, Configs,
    // the tab strip — a sentence chasing the cursor is noise, so the page has to opt in.
    if (!g_descriptionAllowed)
        return;
    const char* text = (explicitText && *explicitText) ? explicitText : resolveOptionDescription(key);
    if (!text || !*text)
        return;
    // Last request in the frame wins. Only one widget can really be hovered, and when two
    // overlap (a control inside a row) the inner one is drawn later and is the specific one.
    if (g_tip.text != text) {
        g_tip.text = text;
        g_tip.reveal = 0.0f;  // a new sentence types from its first letter
    }
    g_tip.rainbow = rainbow;
    g_tip.anchor = ImGui::GetMousePos();
    g_tip.requestFrame = ImGui::GetFrameCount();
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

    // Last thing in the menu frame: every widget has already registered whether it is
    // hovered, so the tip knows what to say and lands on top of everything.
    drawDescriptionTip();

    // Opt-in per frame. A page that renders without announcing a context gets no tips
    // rather than inheriting the last module page's permission.
    g_descriptionAllowed = false;
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
        ImFont* f = font(2);                         // bold, bigger than the 18px body font
        const float headerFS = f->FontSize * 1.18f;  // enlarge "Settings"/"Options" titles
        const float rowRef = ui::s(26.f);            // matches the Back control row height
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

    // Handle text input mode - render input and return early.
    //
    // The flag is re-checked against allowDirectInput on purpose: anim_obj keys its
    // storage by LABEL, and labels like "Scale" or "Font Size" are shared with
    // sliders on other pages that never opted in. Editing ends on Enter, Escape or
    // a click elsewhere -- none of which happens when the menu is closed by its
    // keybind mid-edit -- so a left-over flag would otherwise turn one of those
    // other sliders into a text box the next time it is drawn.
    if (allowDirectInput && inputState.editing) {
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

        // Position input at value location. Wide enough for the text it replaces:
        // a unit suffix ("19 px") needs more room than a bare multiplier ("1.00x").
        float inputWidth = ImMax(s(25.f), value_size.x + s(10.f));
        ImVec2 inputPos(total_bb.Max.x - inputWidth, total_bb.Min.y);
        SetCursorScreenPos(inputPos);
        PushItemWidth(inputWidth);

        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(2), s(1)));
        PushStyleColor(ImGuiCol_FrameBg, GetColor(ImGuiCol_FrameBgActive));

        // Focus on the first frame only. The flag is cleared at the very end of the
        // branch, not here: on this frame the input is not the active item yet, and
        // the "focus went elsewhere" test below would read that as a click away and
        // shut the editor before it ever opened.
        if (inputState.justStarted)
            SetKeyboardFocusHere();

        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
        bool enterPressed = InputText("##valueinput", inputState.buffer, sizeof(inputState.buffer), flags);
        const bool inputActive = IsItemActive();

        // A typed value is deliberately NOT clamped to [v_min, v_max]. The slider's
        // range is what the grab can reach comfortably, not a hard limit -- typing
        // is how you ask for a value past the end of it (a 10x watermark image from a
        // slider that stops at 4x). Whoever reads the value is what bounds it.
        auto commit = [&]() {
            float newVal = 0.0f;
            if (sscanf_s(inputState.buffer, "%f", &newVal) == 1 && std::isfinite(newVal)) {
                *v = newVal;
                MarkItemEdited(id);
            }
            inputState.editing = false;
        };

        if (enterPressed) {
            commit();
        } else if (IsKeyPressed(ImGuiKey_Escape)) {
            inputState.editing = false;  // discard
        } else if (!inputActive && !inputState.justStarted) {
            // Focus left the box -- clicked away, or it never took focus at all.
            // Accept what is in it, the way a text field does, and close. Keying off
            // the active item rather than a hit-test on the click means a click that
            // lands a pixel off the input box can no longer swallow the whole edit.
            commit();
        }
        inputState.justStarted = false;

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

    // Hand the selected row's rect to the sidebar so it can spring one shared rail
    // to it. Published from the row itself because the pinned-Settings layout means
    // the caller cannot derive row positions from the index alone.
    if (selected) {
        nav_sel_min = bb.Min;
        nav_sel_max = bb.Max;
        nav_sel_valid = true;
    }

    // Hover-only pill. Selection is drawn by the sidebar rail on the parent window's
    // draw list (so it sits behind this row's icon and label); duplicating it here
    // would give the selected row two overlapping highlights.
    float pillT = obj.hover * 0.55f;
    if (pillT > 0.01f) {
        ImVec2 pillMin{bb.Min.x + s(8), bb.Min.y + s(3)};
        ImVec2 pillMax{bb.Max.x - s(8), bb.Max.y - s(3)};
        window->DrawList->AddRectFilled(pillMin, pillMax, GetSchemeColor(0.14f * pillT), s(6));
    }

    bool showIcons = ImGui::GetIO().DisplaySize.y >= 600.0f;
    float iconX = bb.Min.x + s(15);
    float textOffsetX = showIcons ? (iconX - bb.Min.x + icon_size.x + s(10)) : s(15);

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

void ui::apply_style() {
    theme::apply();
}
