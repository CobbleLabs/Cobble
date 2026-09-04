#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "item_esp.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/textures/item_textures.h"

ItemESP* itemEsp = nullptr;

struct MinecraftItem {
    int id;
    const char* name;
};

static const MinecraftItem ALL_MINECRAFT_ITEMS[] = {
    // ── Swords ─────────────────────────────────────────────
    { 268, "Wooden Sword" },
    { 272, "Stone Sword" },
    { 267, "Iron Sword" },
    { 276, "Diamond Sword" },
    { 283, "Golden Sword" },
    // ── Axes ───────────────────────────────────────────────
    { 271, "Wooden Axe" },
    { 275, "Stone Axe" },
    { 258, "Iron Axe" },
    { 279, "Diamond Axe" },
    { 286, "Golden Axe" },
    // ── Bow & Arrows ───────────────────────────────────────
    { 261, "Bow" },
    { 262, "Arrow" },
    // ── Helmets ────────────────────────────────────────────
    { 298, "Leather Helmet" },
    { 302, "Chainmail Helmet" },
    { 306, "Iron Helmet" },
    { 310, "Diamond Helmet" },
    { 314, "Golden Helmet" },
    // ── Chestplates ────────────────────────────────────────
    { 299, "Leather Chestplate" },
    { 303, "Chainmail Chestplate" },
    { 307, "Iron Chestplate" },
    { 311, "Diamond Chestplate" },
    { 315, "Golden Chestplate" },
    // ── Leggings ───────────────────────────────────────────
    { 300, "Leather Leggings" },
    { 304, "Chainmail Leggings" },
    { 308, "Iron Leggings" },
    { 312, "Diamond Leggings" },
    { 316, "Golden Leggings" },
    // ── Boots ──────────────────────────────────────────────
    { 301, "Leather Boots" },
    { 305, "Chainmail Boots" },
    { 309, "Iron Boots" },
    { 313, "Diamond Boots" },
    { 317, "Golden Boots" },
    // ── Potions ────────────────────────────────────────────
    { 373, "Potion" },
    { 438, "Splash Potion" },
    // ── Throwables ─────────────────────────────────────────
    { 368, "Ender Pearl" },
    { 344, "Egg" },
    { 332, "Snowball" },
    { 385, "Fire Charge" },
    // ── Food ───────────────────────────────────────────────
    { 260, "Apple" },
    { 322, "Golden Apple" },
    { 297, "Bread" },
    { 364, "Cooked Beef" },
    { 366, "Cooked Chicken" },
    { 320, "Cooked Porkchop" },
    { 391, "Carrot" },
    { 396, "Golden Carrot" },
    { 360, "Melon Slice" },
    { 393, "Baked Potato" },
    // ── Pickaxes ───────────────────────────────────────────
    { 270, "Wooden Pickaxe" },
    { 274, "Stone Pickaxe" },
    { 257, "Iron Pickaxe" },
    { 278, "Diamond Pickaxe" },
    // ── Other PvP Tools ────────────────────────────────────
    { 259, "Flint and Steel" },
    { 346, "Fishing Rod" },
    { 359, "Shears" },
    { 403, "Enchanted Book" },
    { 384, "Bottle o' Enchanting" },
    // ── Materials ──────────────────────────────────────────
    { 264, "Diamond" },
    { 265, "Iron Ingot" },
    { 266, "Gold Ingot" },
    { 388, "Emerald" },
    { 263, "Coal" },
    { 331, "Redstone" },
};

// Look up a human-readable name for an item id from the known-items table.
// Falls back to a generic label so unknown drops still show something.
static const char* itemNameForId(int id) {
    for (const auto& it : ALL_MINECRAFT_ITEMS) {
        if (it.id == id)
            return it.name;
    }
    return "Item";
}

// A run of text painted in one color/style, produced by splitting a Minecraft
// display name on its § color+format codes.
struct ColoredRun {
    std::string text;
    ImU32 color;
    bool bold = false;
};

// Map a Minecraft color code char (after §) to an RGB color. Returns false for
// formatting codes (k-o, r) and unknown chars so the caller can handle them.
static bool mcColorForCode(char c, ImU32& out) {
    switch (c) {
        case '0': out = IM_COL32(0, 0, 0, 255); return true;
        case '1': out = IM_COL32(0, 0, 170, 255); return true;
        case '2': out = IM_COL32(0, 170, 0, 255); return true;
        case '3': out = IM_COL32(0, 170, 170, 255); return true;
        case '4': out = IM_COL32(170, 0, 0, 255); return true;
        case '5': out = IM_COL32(170, 0, 170, 255); return true;
        case '6': out = IM_COL32(255, 170, 0, 255); return true;
        case '7': out = IM_COL32(170, 170, 170, 255); return true;
        case '8': out = IM_COL32(85, 85, 85, 255); return true;
        case '9': out = IM_COL32(85, 85, 255, 255); return true;
        case 'a': out = IM_COL32(85, 255, 85, 255); return true;
        case 'b': out = IM_COL32(85, 255, 255, 255); return true;
        case 'c': out = IM_COL32(255, 85, 85, 255); return true;
        case 'd': out = IM_COL32(255, 85, 255, 255); return true;
        case 'e': out = IM_COL32(255, 255, 85, 255); return true;
        case 'f': out = IM_COL32(255, 255, 255, 255); return true;
        default:  return false;  // k-o (formatting), r (reset), or unknown
    }
}

// Split a display name into colored runs. The § sign arrives as the UTF-8 pair
// 0xC2 0xA7 (modified UTF-8 from JNI); a bare 0xA7 is handled too. Text before
// any code, and after a reset/unknown code, uses `defaultCol`.
static std::vector<ColoredRun> parseColoredName(const std::string& s, ImU32 defaultCol) {
    std::vector<ColoredRun> runs;
    ColoredRun cur{"", defaultCol, false};
    // Emit the current run (if any) before changing color/style state.
    auto flush = [&]() {
        if (!cur.text.empty()) {
            runs.push_back(cur);
            cur.text.clear();
        }
    };
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        bool isSection = false;
        size_t codeIdx = 0;
        if (ch == 0xC2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0xA7) {
            isSection = true;
            codeIdx = i + 2;
        } else if (ch == 0xA7 && i + 1 < s.size()) {
            isSection = true;
            codeIdx = i + 1;
        }

        if (isSection) {
            char code = (char)std::tolower((unsigned char)s[codeIdx]);
            ImU32 col;
            if (mcColorForCode(code, col)) {
                // A color code sets the color AND resets formatting (vanilla rule),
                // so "§l§cX" is red non-bold while "§c§lX" is red bold.
                flush();
                cur.color = col;
                cur.bold = false;
            } else if (code == 'r') {
                flush();
                cur.color = defaultCol;
                cur.bold = false;
            } else if (code == 'l') {
                // Bold: keep the current color, just enable the bold font.
                flush();
                cur.bold = true;
            }
            // Other formatting codes (k, m, n, o) are ignored (no visual change).
            i = codeIdx;  // skip the code char (loop's ++i steps past it)
            continue;
        }
        cur.text.push_back((char)ch);
    }
    flush();
    return runs;
}

ItemESP::ItemESP() : Module("ItemESP") {
    keybind = 0;
    // Mark as a rendering module so the overlay keeps drawing ImGui frames while
    // the menu is closed. Without this, hasRenderWork() returns false and the
    // overlay skips the whole frame, so the ESP box only showed with the GUI open.
    renders = true;
    worldVisual = true;
    settings.box = true;
    settings.filled = true;
    settings.outline = true;
    settings.box3D = false;
    settings.showQuantity = true;
    std::memset(enabledItemIds, 0, sizeof(enabledItemIds));
}

void ItemESP::onEnable() {
    log_ok("ItemESP enabled");
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedItems.clear();
}

void ItemESP::onDisable() {
    log_ok("ItemESP disabled");
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedItems.clear();
}

void ItemESP::onUpdate() {
    lastUpdateTick = GetTickCount64();

    if (!Java::Env() || !Java::Initialized)
        return;

    // Fetch items
    std::vector<ItemEntityInfo> items = mc.getItemEntityList();

    // Fetch render pos directly from MC (since we are on game thread)
    RenderPos rPos = mc.getRenderPos();
    float pTicks = mc.getPartialTicks();
    
    // Fetch local player for fallback interpolation logic (optional, but good for consistency)
    jobject localPlayer = mc.getPlayer();
    double lx = 0, ly = 0, lz = 0;
    double llx = 0, lly = 0, llz = 0;
    
    if (localPlayer) {
        if (JniCache::fid_posX) lx = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posX);
        if (JniCache::fid_posY) ly = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posY);
        if (JniCache::fid_posZ) lz = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posZ);
        if (JniCache::fid_lastTickPosX) llx = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosX);
        if (JniCache::fid_lastTickPosY) lly = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosY);
        if (JniCache::fid_lastTickPosZ) llz = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosZ);
        Java::Env()->DeleteLocalRef(localPlayer);
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedItems = std::move(items);
        cachedRenderX = rPos.x;
        cachedRenderY = rPos.y;
        cachedRenderZ = rPos.z;
        cachedPartialTicks = pTicks;
        cachedLocalX = lx;
        cachedLocalY = ly;
        cachedLocalZ = lz;
        cachedLocalLastX = llx;
        cachedLocalLastY = lly;
        cachedLocalLastZ = llz;
    }
}

// Render 3D wireframe box (Adapted from esp.cpp)
static void Render3DBox(ImDrawList* draw, float x, float y, float z, double renderX, double renderY, double renderZ,
                        ImU32 boxCol, ImU32 outlineCol, ImU32 fillCol, bool drawOutline, bool drawFilled) {
    // Item Frame dimensions - Items are small 0.25 x 0.25 usually
    // Item Frame dimensions - Items are small 0.25 x 0.25 usually
    // User requested taller square box
    static constexpr float HALF_WIDTH = 0.25f;  // Width 0.5
    static constexpr float HEIGHT = 0.5f;       // Height 0.5 (Cube-ish/Tall)

    // 8 corners of the hitbox
    Vector3 corners[8] = {
        {x - HALF_WIDTH, y, z - HALF_WIDTH},           // 0: bottom back-left
        {x + HALF_WIDTH, y, z - HALF_WIDTH},           // 1: bottom back-right
        {x + HALF_WIDTH, y, z + HALF_WIDTH},           // 2: bottom front-right
        {x - HALF_WIDTH, y, z + HALF_WIDTH},           // 3: bottom front-left
        {x - HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH},  // 4: top back-left
        {x + HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH},  // 5: top back-right
        {x + HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH},  // 6: top front-right
        {x - HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH},  // 7: top front-left
    };

    // Project all corners to screen space
    Vector2 screenCorners[8];
    bool visible[8];
    int visibleCount = 0;
    for (int i = 0; i < 8; i++) {
        Vector3 v = {corners[i].x - (float)renderX, corners[i].y - (float)renderY, corners[i].z - (float)renderZ};
        visible[i] = CWorldToScreen::WorldToScreen(v, RenderData::modelView, RenderData::projection,
                                                   RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
        if (visible[i])
            visibleCount++;
    }

    // Need at least 3 visible corners to render faces
    if (visibleCount < 3)
        return;

    static constexpr int faces[6][4] = {
        {0, 1, 2, 3}, {7, 6, 5, 4}, {3, 2, 6, 7}, {1, 0, 4, 5}, {0, 3, 7, 4}, {2, 1, 5, 6},
    };

    if (drawFilled) {
        for (int f = 0; f < 6; f++) {
            int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
            if (!visible[i0] || !visible[i1] || !visible[i2] || !visible[i3]) continue;

            float ax = screenCorners[i1].x - screenCorners[i0].x;
            float ay = screenCorners[i1].y - screenCorners[i0].y;
            float bx = screenCorners[i2].x - screenCorners[i0].x;
            float by = screenCorners[i2].y - screenCorners[i0].y;
            float cross = ax * by - ay * bx;

            if (cross > 0) {
                ImVec2 points[4] = {
                    ImVec2(screenCorners[i0].x, screenCorners[i0].y),
                    ImVec2(screenCorners[i1].x, screenCorners[i1].y),
                    ImVec2(screenCorners[i2].x, screenCorners[i2].y),
                    ImVec2(screenCorners[i3].x, screenCorners[i3].y),
                };
                draw->AddConvexPolyFilled(points, 4, fillCol);
            }
        }
    }

    static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, 
                                         {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    if (drawOutline) {
        for (int i = 0; i < 12; i++) {
            int a = edges[i][0], b = edges[i][1];
            if (visible[a] && visible[b]) {
                draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                              ImVec2(screenCorners[b].x, screenCorners[b].y), outlineCol, 2.0f);
            }
        }
    }

    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        if (visible[a] && visible[b]) {
            draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                          ImVec2(screenCorners[b].x, screenCorners[b].y), boxCol, 1.0f);
        }
    }
}

void ItemESP::onRender() {
    if (!RenderData::updated) return;
    if (GetTickCount64() - lastUpdateTick > 300) return;

    // Background draw list renders every frame regardless of whether an ImGui
    // window is active. GetWindowDrawList() only draws reliably while the menu
    // is open, which made the boxes flicker on/off when the GUI was closed.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    std::vector<ItemEntityInfo> localCache;
    double renderX, renderY, renderZ;
    float partialTicks;
    double localX, localY, localZ;
    double localLastX, localLastY, localLastZ;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localCache = cachedItems;
        renderX = cachedRenderX;
        renderY = cachedRenderY;
        renderZ = cachedRenderZ;
        partialTicks = cachedPartialTicks;
        localX = cachedLocalX;
        localY = cachedLocalY;
        localZ = cachedLocalZ;
        localLastX = cachedLocalLastX;
        localLastY = cachedLocalLastY;
        localLastZ = cachedLocalLastZ;
    }
    
    // Fallback
    bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);
    if (useFallback) {
        renderX = localLastX + (localX - localLastX) * partialTicks;
        renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;
        renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
    }

    auto outlineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.outlineColor[0], settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3]));
    auto boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3]));
    auto fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.filledColor[0], settings.filledColor[1], settings.filledColor[2], settings.filledColor[3]));
    auto nameCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.nameColor[0], settings.nameColor[1], settings.nameColor[2], settings.nameColor[3]));

    for (const auto& item : localCache) {
        // Distance check
        double dx = item.posX - localX;
        double dy = item.posY - localY;
        double dz = item.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > settings.range) continue;

        if (item.itemId >= 0 && item.itemId < 512 && !enabledItemIds[item.itemId]) {
            continue;
        }

        // Entities don't have lastTickPos in our struct yet, so we just use current pos
        // Or we could add lastTickPos to ItemEntityInfo if we want smooth interpolation
        // For items, they usually don't move fast unless sliding. 
        // For now, use static pos.
        float interpX = (float)item.posX; 
        float interpY = (float)item.posY;
        float interpZ = (float)item.posZ;

        if (settings.box3D) {
            Render3DBox(draw, interpX, interpY, interpZ, renderX, renderY, renderZ, boxCol, outlineCol, fillCol, settings.outline, settings.filled);
        }

        bool needs2DBox = settings.box;
        bool needs2DFilled = settings.filled && !settings.box3D;
        
        // Project center for Quantity Text
        // Item roughly 0.25 height
        Vector3 topPos = {interpX - (float)renderX, interpY + 0.5f - (float)renderY, interpZ - (float)renderZ};
        Vector3 bottomPos = {interpX - (float)renderX, interpY - (float)renderY, interpZ - (float)renderZ};
        
        Vector2 sTop, sBot;
        bool topVis = CWorldToScreen::WorldToScreen(topPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, sTop);
        bool botVis = CWorldToScreen::WorldToScreen(bottomPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, sBot);

        if (!topVis && !botVis) continue;

        if (needs2DBox || needs2DFilled) {
             float height = sBot.y - sTop.y;
             if (height < 1.0f) height = 1.0f;
             if (height > 500.0f) height = 500.0f; // clamp

             float width = height; // Square for items usually
             float left = sBot.x - width * 0.5f;
             float right = sBot.x + width * 0.5f;
             float top = sTop.y;
             float bottom = sBot.y;

             if (needs2DFilled) {
                 draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), fillCol);
             }
             if (needs2DBox) {
                 if (settings.outline) {
                     draw->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1), outlineCol);
                     draw->AddRect(ImVec2(left + 1, top + 1), ImVec2(right - 1, bottom - 1), outlineCol);
                 }
                 draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol);
             }
        }

        // Track how far above the box we've drawn so stacked labels don't overlap.
        float labelBaseY = sTop.y - 2.0f;

        // Large item name, centered above the box (big text with a shadow so it
        // reads over any terrain). A server-set custom name keeps its § colors
        // (e.g. "Espada DEMON"); otherwise the generic type name is drawn in the
        // box color.
        if (settings.showName) {
            ImFont* regular = ImGui::GetFont();
            ImFont* boldFont = font(2);  // bold face (same one the ArrayList uses)
            if (!boldFont) boldFont = regular;
            float nameFS = regular->FontSize * settings.nameScale;
            float sh = (std::max)(1.0f, settings.nameScale);  // shadow offset scales with text

            std::vector<ColoredRun> runs;
            if (item.hasCustomName)
                runs = parseColoredName(item.customName, nameCol);
            else
                runs.push_back({itemNameForId(item.itemId), nameCol, false});

            // §l segments render in the bold font; the rest in the regular one.
            auto runFont = [&](const ColoredRun& r) { return r.bold ? boldFont : regular; };

            // Total width to center the whole label; row height from the tallest run.
            float totalW = 0.0f, rowH = 0.0f;
            for (const auto& run : runs) {
                ImVec2 sz = runFont(run)->CalcTextSizeA(nameFS, FLT_MAX, 0.0f, run.text.c_str());
                totalW += sz.x;
                if (sz.y > rowH) rowH = sz.y;
            }

            float nX = sBot.x - totalW * 0.5f;
            float nY = labelBaseY - rowH;
            float cursorX = nX;
            for (const auto& run : runs) {
                ImFont* rf = runFont(run);
                draw->AddText(rf, nameFS, ImVec2(cursorX + sh, nY + sh), IM_COL32(0, 0, 0, 220), run.text.c_str());
                draw->AddText(rf, nameFS, ImVec2(cursorX, nY), run.color, run.text.c_str());
                cursorX += rf->CalcTextSizeA(nameFS, FLT_MAX, 0.0f, run.text.c_str()).x;
            }
            labelBaseY = nY - 2.0f;  // push the quantity above the name
        }

        // Quantity (Show Quantity)
        if (settings.showQuantity && item.isStackable()) {
             std::string qty = "x" + std::to_string(item.stackSize);

             ImVec2 textSize = ImGui::CalcTextSize(qty.c_str());
             float tX = sBot.x - textSize.x * 0.5f; // Center horizontally
             float tY = labelBaseY - textSize.y;    // Above the box / name

             draw->AddText(ImVec2(tX + 1, tY + 1), IM_COL32(0, 0, 0, 255), qty.c_str()); // Shadow
             draw->AddText(ImVec2(tX, tY), IM_COL32(255, 255, 255, 255), qty.c_str());
        }
    }
}

void ItemESP::onRenderMenu() {
    bool enabledValue = enabled;

    // Settings + Colors as two columns, exactly like the ESP module (all ui::
    // wrappers, which respect the column width). The item list can't share a
    // column -- its listbox/filter use full width -- so it goes full-width below.
    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();
            // Box 2D / Box 3D act like radio buttons: enabling one disables the
            // other. Done inline so the switch is immediate and consistent.
            if (ui::Checkbox("Box 2D", &settings.box)) {
                if (settings.box) settings.box3D = false;
            }
            if (ui::Checkbox("Box 3D", &settings.box3D)) {
                if (settings.box3D) settings.box = false;
            }
            ui::Checkbox("Filled", &settings.filled);
            ui::Checkbox("Outline", &settings.outline);
            ui::Checkbox("Show Quantity", &settings.showQuantity);
            ui::Checkbox("Show Name", &settings.showName);
            if (settings.showName)
                ui::SliderFloat("Name Size", &settings.nameScale, 1.0f, 3.0f, "%.1fx");
            ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
        },
        "Colors",
        [this]() {
            ui::color("Box Color", settings.boxColor);
            ui::color("Fill Color", settings.filledColor);
            ui::color("Outline Color", settings.outlineColor);
            if (settings.showName)
                ui::color("Name Color", settings.nameColor);
        });

    ui::section("Target Items", [this]() {
        ImGui::Text("Presets:");

        auto togglePreset = [this](const char* label, int id) {
            bool val = enabledItemIds[id];
            std::string idStr = "preset_" + std::to_string(id);
            ImGui::PushID(idStr.c_str());
            unsigned int texId = ItemTextureManager::get().getTexture(id);
            if (texId > 0) {
                ImGui::Image((ImTextureID)(intptr_t)texId, ImVec2(18, 18), ImVec2(0, 0), ImVec2(1, 1));
                ImGui::SameLine();
            } else {
                ImGui::Dummy(ImVec2(18, 18));
                ImGui::SameLine();
            }
            if (ui::Checkbox(label, &val)) {
                enabledItemIds[id] = val;
            }
            ImGui::PopID();
        };

        togglePreset("Diamond Sword", 276);
        togglePreset("Golden Apple", 322);
        togglePreset("Bow", 261);
        togglePreset("Ender Pearl", 368);
        togglePreset("Potion", 373);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Search & Customize:");

        static char searchFilter[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ItemFilter", "Filter items...", searchFilter, sizeof(searchFilter));

        if (ImGui::BeginListBox("##ItemSelectionList", ImVec2(-1, ui::s(220)))) {
            std::string filterStr = searchFilter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            for (const auto& item : ALL_MINECRAFT_ITEMS) {
                std::string itemName = item.name;
                std::string itemNameLower = itemName;
                std::transform(itemNameLower.begin(), itemNameLower.end(), itemNameLower.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (!filterStr.empty() && itemNameLower.find(filterStr) == std::string::npos) {
                    continue;
                }

                std::string idStr = "list_" + std::to_string(item.id);
                ImGui::PushID(idStr.c_str());
                unsigned int texId = ItemTextureManager::get().getTexture(item.id);
                if (texId > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)texId, ImVec2(18, 18), ImVec2(0, 0), ImVec2(1, 1));
                    ImGui::SameLine();
                } else {
                    ImGui::Dummy(ImVec2(18, 18));
                    ImGui::SameLine();
                }

                bool isEnabled = enabledItemIds[item.id];
                if (ImGui::Checkbox(itemName.c_str(), &isEnabled)) {
                    enabledItemIds[item.id] = isEnabled;
                }
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
    });
}

std::string ItemESP::getDisplayInfo() {
    return "";
}

void ItemESP::saveSettings(json::Value& node) const {
    std::string customStr = "";
    for (int i = 0; i < 512; i++) {
        if (enabledItemIds[i]) {
            if (!customStr.empty()) customStr += ",";
            customStr += std::to_string(i);
        }
    }
    node["customItems"] = customStr;
    node["box"] = settings.box;
    node["box3D"] = settings.box3D;
    node["filled"] = settings.filled;
    node["outline"] = settings.outline;
    node["showQuantity"] = settings.showQuantity;
    node["showName"] = settings.showName;
    node["nameScale"] = settings.nameScale;
    node["range"] = settings.range;
    json::writeColor(node, "boxColor", settings.boxColor);
    json::writeColor(node, "filledColor", settings.filledColor);
    json::writeColor(node, "outlineColor", settings.outlineColor);
    json::writeColor(node, "nameColor", settings.nameColor);
}

void ItemESP::loadSettings(const json::Value& node) {
    if (node.has("customItems")) {
        std::memset(enabledItemIds, 0, sizeof(enabledItemIds));
        std::string s = node["customItems"].as_string("");
        if (!s.empty()) {
            size_t start = 0, end;
            while ((end = s.find(',', start)) != std::string::npos) {
                std::string num = s.substr(start, end - start);
                if (!num.empty()) {
                    int id = std::stoi(num);
                    if (id >= 0 && id < 512) enabledItemIds[id] = true;
                }
                start = end + 1;
            }
            std::string num = s.substr(start);
            if (!num.empty()) {
                int lastId = std::stoi(num);
                if (lastId >= 0 && lastId < 512) enabledItemIds[lastId] = true;
            }
        }
    } else {
        // No saved config — keep items disabled by default (user must opt-in)
        std::memset(enabledItemIds, 0, sizeof(enabledItemIds));
    }
    settings.box = node["box"].as_bool(settings.box);
    settings.box3D = node["box3D"].as_bool(settings.box3D);
    settings.filled = node["filled"].as_bool(settings.filled);
    settings.outline = node["outline"].as_bool(settings.outline);
    settings.showQuantity = node["showQuantity"].as_bool(settings.showQuantity);
    settings.showName = node["showName"].as_bool(settings.showName);
    settings.nameScale = node["nameScale"].as_float(settings.nameScale);
    settings.range = node["range"].as_float(settings.range);
    json::readColor(node, "boxColor", settings.boxColor);
    json::readColor(node, "filledColor", settings.filledColor);
    json::readColor(node, "outlineColor", settings.outlineColor);
    json::readColor(node, "nameColor", settings.nameColor);
}
