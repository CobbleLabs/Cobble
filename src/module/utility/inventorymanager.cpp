#include "inventorymanager.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../ui/hud/notifications.h"
#include "../../util/textures/item_textures.h"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

InventoryManager* inventoryManager = nullptr;

static void sendKey(WORD vk) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// LWJGL key code to Windows scan code mapping
// LWJGL uses scancodes directly for most keys
static void sendLwjglKey(int lwjglKeyCode) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = (WORD)lwjglKeyCode;
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = (WORD)lwjglKeyCode;
    inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

InventoryManager::InventoryManager() : Module("InvManager") {
    keybind = 0;
}

bool InventoryManager::canToggle() {
    return state == State::Idle;
}

bool InventoryManager::hasSavedLayout() const {
    return presets[settings.activePreset].saved;
}

int InventoryManager::inventoryToContainerSlot(int invSlot) {
    // mainInventory: 0-8 = hotbar, 9-35 = main inventory
    // Container slots: 5-8 = armor, 9-35 = main inventory, 36-44 = hotbar
    if (invSlot >= 0 && invSlot <= 8) {
        return 36 + invSlot;  // Hotbar: inv 0-8 -> container 36-44
    } else if (invSlot >= 9 && invSlot <= 35) {
        return invSlot;  // Main inventory: same slot numbers
    }
    return -1;
}

void InventoryManager::saveHotbar() {
    if (!mc.isValid() || !player.isValid()) {
        NotificationManager::addSystem("Cannot save, not in game");
        return;
    }
    
    auto slots = mc.getInventorySlots();
    
    if (slots.empty()) {
        NotificationManager::addSystem("Cannot save, inventory empty");
        return;
    }

    Preset& preset = presets[settings.activePreset];
    
    for (int i = 0; i < 9; i++) {
        preset.slots[i].itemId = 0;
        preset.slots[i].damage = 0;
    }

    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 8) {
            preset.slots[slot.slotId].itemId = slot.itemId;
            preset.slots[slot.slotId].damage = slot.damage;
        }
    }

    preset.saved = true;
    char msg[32];
    snprintf(msg, sizeof(msg), "Preset %d saved", settings.activePreset + 1);
    NotificationManager::addSystem(msg);
}

void InventoryManager::onEnable() {
    if (!mc.isValid() || !player.isValid()) {
        enabled = false;
        return;
    }
    
    // Safety check - don't start if already processing
    if (state != State::Idle) {
        enabled = false;
        return;
    }
    
    // Don't start if already in a GUI
    if (mc.isInGui()) {
        enabled = false;
        return;
    }

    if (!presets[settings.activePreset].saved) {
        saveHotbar();
        enabled = false;
        return;
    }

    scanForSwaps();

    if (pendingSwaps.empty()) {
        NotificationManager::addSystem("Hotbar already matches");
        enabled = false;
        return;
    }

    // Open inventory using user's inventory key
    sendLwjglKey(mc.getInventoryKey());
    state = State::OpeningInventory;
    stateStartTime = std::chrono::steady_clock::now();
}

void InventoryManager::onDisable() {
    state = State::Idle;
    pendingSwaps.clear();
}

void InventoryManager::finishRestore() {
    if (mc.isInventoryOpen()) {
        sendKey(VK_ESCAPE);
    }
    state = State::Idle;
    pendingSwaps.clear();
    enabled = false;
}

void InventoryManager::onUpdate() {
    if (!player.isValid())
        return;

    if (state == State::Idle)
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();

    switch (state) {
        case State::Idle:
            break;

        case State::OpeningInventory: {
            if (mc.isPlayerInventoryOpen()) {
                state = State::Swapping;
                stateStartTime = now;
                lastSwapTime = now - std::chrono::milliseconds(effectiveDelay());
            } else if (elapsed > TIMEOUT_MS) {
                finishRestore();
            }
            break;
        }

        case State::Swapping: {
            if (!mc.isPlayerInventoryOpen()) {
                finishRestore();
                return;
            }

            if (pendingSwaps.empty()) {
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            auto swapElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSwapTime).count();
            int delay = effectiveDelay();
            if (swapElapsed < delay)
                return;

            SwapTask task = pendingSwaps.front();
            pendingSwaps.erase(pendingSwaps.begin());

            int containerSlot = inventoryToContainerSlot(task.sourceSlot);
            
            // Safety check - skip if slot is invalid
            if (containerSlot < 0 || task.targetHotbar < 0 || task.targetHotbar > 8) {
                lastSwapTime = now;
                break;
            }
            
            log_debug("[InvManager] Executing swap: container %d -> hotbar %d", containerSlot, task.targetHotbar);
            mc.windowClickSwap(containerSlot, task.targetHotbar);

            lastSwapTime = now;
            if (settings.retryOnFail) {
                lastSwap = task;
                state = State::Verifying;
                stateStartTime = now;
            }
            break;
        }

        case State::Verifying: {
            if (!mc.isPlayerInventoryOpen()) {
                finishRestore();
                return;
            }

            auto verifyElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();
            int verifyDelay = settings.retryOnFail ? (std::max)(VERIFY_DELAY_MS, effectiveDelay()) : VERIFY_DELAY_MS;
            if (verifyElapsed < verifyDelay)
                break;

            auto slots = mc.getInventorySlots();
            int currentId = 0;
            int currentDamage = 0;
            bool found = false;
            for (const auto& s : slots) {
                if (s.slotId == lastSwap.targetHotbar) {
                    currentId = s.itemId;
                    currentDamage = s.damage;
                    found = true;
                    break;
                }
            }

            bool ok = found && currentId == lastSwap.expectedId &&
                      (!lastSwap.compareDamage || currentDamage == lastSwap.expectedDamage);

            if (ok) {
                state = State::Swapping;
                stateStartTime = now;
            } else {
                if (lastSwap.retryCount < MAX_RETRIES) {
                    lastSwap.retryCount++;
                    log_debug("[InvManager] Swap FAILED, retry #%d", lastSwap.retryCount);
                    pendingSwaps.insert(pendingSwaps.begin(), lastSwap);
                    state = State::Swapping;
                    stateStartTime = now;
                    lastSwapTime = now - std::chrono::milliseconds(effectiveDelay());
                } else {
                    log_debug("[InvManager] Swap FAILED, max retries exceeded");
                    state = State::Swapping;
                    stateStartTime = now;
                }
            }
            break;
        }

        case State::Closing: {
            if (elapsed >= effectiveDelay()) {
                finishRestore();
            }
            break;
        }
    }
}

void InventoryManager::scanForSwaps() {
    pendingSwaps.clear();

    auto slots = mc.getInventorySlots();
    if (slots.empty())
        return;

    // Build lookup: what's currently where
    struct CurrentSlot {
        int itemId = 0;
        int damage = 0;
    };
    CurrentSlot current[36] = {};

    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId < 36) {
            current[slot.slotId].itemId = slot.itemId;
            current[slot.slotId].damage = slot.damage;
        }
    }

    const Preset& preset = presets[settings.activePreset];

    // PASS 1: Identify which hotbar slots already have the correct item in the correct position
    // These slots should NOT be used as source for other swaps
    bool correctlyPlaced[9] = {};
    for (int targetSlot = 0; targetSlot < 9; targetSlot++) {
        int wantedId = preset.slots[targetSlot].itemId;
        int wantedDamage = preset.slots[targetSlot].damage;
        
        if (wantedId == 0) {
            correctlyPlaced[targetSlot] = true;  // Empty slot is "correct"
            continue;
        }
        
        bool compareDamage = (wantedId == 373);
        
        if (current[targetSlot].itemId == wantedId) {
            if (!compareDamage || current[targetSlot].damage == wantedDamage) {
                correctlyPlaced[targetSlot] = true;
            }
        }
    }

    // Track which slots have been used as source (prevents using same item twice)
    bool usedAsSource[36] = {};
    
    // Mark correctly placed hotbar items as used so they won't be stolen
    for (int i = 0; i < 9; i++) {
        if (correctlyPlaced[i]) {
            usedAsSource[i] = true;
        }
    }

    // PASS 2: For each incorrectly placed slot, find the item and create swap tasks
    for (int targetSlot = 0; targetSlot < 9; targetSlot++) {
        if (correctlyPlaced[targetSlot])
            continue;
            
        int wantedId = preset.slots[targetSlot].itemId;
        int wantedDamage = preset.slots[targetSlot].damage;

        // Skip if slot should be empty
        if (wantedId == 0)
            continue;

        bool compareDamage = (wantedId == 373);

        // Find the item - Priority 1: main inventory, Priority 2: hotbar
        int sourceSlot = -1;
        
        // Priority 1: Search main inventory (9-35)
        for (int i = 9; i < 36; i++) {
            if (usedAsSource[i])
                continue;
            if (current[i].itemId == wantedId) {
                if (!compareDamage || current[i].damage == wantedDamage) {
                    sourceSlot = i;
                    break;
                }
            }
        }
        
        // Priority 2: Search hotbar (0-8) - but NOT correctly placed items
        if (sourceSlot == -1) {
            for (int i = 0; i < 9; i++) {
                if (i == targetSlot)
                    continue;
                if (usedAsSource[i])
                    continue;
                // Don't steal from correctly placed slots
                if (correctlyPlaced[i])
                    continue;
                if (current[i].itemId == wantedId) {
                    if (!compareDamage || current[i].damage == wantedDamage) {
                        sourceSlot = i;
                        break;
                    }
                }
            }
        }

        if (sourceSlot == -1)
            continue;  // Item not found in inventory

        SwapTask task{};
        task.sourceSlot = sourceSlot;
        task.targetHotbar = targetSlot;
        task.expectedId = wantedId;
        task.expectedDamage = wantedDamage;
        task.compareDamage = compareDamage;
        task.retryCount = 0;
        pendingSwaps.push_back(task);
        log_debug("[InvManager] Planned swap: slot %d -> hotbar %d (item %d)", sourceSlot, targetSlot, wantedId);
        
        // Mark source as used
        usedAsSource[sourceSlot] = true;
        
        // Update current state to reflect the swap (items exchange positions)
        std::swap(current[sourceSlot], current[targetSlot]);
        
        // If the source was in hotbar, the item that was in targetSlot is now at sourceSlot
        // This might be the correct item for another slot, so we need to track this
    }
}

// Helper to get short item name from ID
static const char* getItemShortName(int itemId) {
    switch (itemId) {
        // Swords
        case 268: return "WSw";
        case 272: return "StSw";
        case 267: return "ISw";
        case 276: return "DSw";
        case 283: return "GSw";
        
        // Axes
        case 271: return "WAx";
        case 275: return "StAx";
        case 258: return "IAx";
        case 279: return "DAx";
        case 286: return "GAx";
        
        // Pickaxes
        case 270: return "WPi";
        case 274: return "StPi";
        case 257: return "IPi";
        case 278: return "DPi";
        case 285: return "GPi";
        
        // Shovels
        case 269: return "WSh";
        case 273: return "StSh";
        case 256: return "ISh";
        case 277: return "DSh";
        case 284: return "GSh";
        
        // Diamond Armor
        case 310: return "DHel";
        case 311: return "DChst";
        case 312: return "DLeg";
        case 313: return "DBoot";
        
        // Iron Armor
        case 306: return "IHel";
        case 307: return "IChst";
        case 308: return "ILeg";
        case 309: return "IBoot";
        
        // Gold Armor
        case 314: return "GHel";
        case 315: return "GChst";
        case 316: return "GLeg";
        case 317: return "GBoot";
        
        // Leather Armor
        case 298: return "LHel";
        case 299: return "LChst";
        case 300: return "LLeg";
        case 301: return "LBoot";
        
        // Chain Armor
        case 302: return "CHel";
        case 303: return "CChst";
        case 304: return "CLeg";
        case 305: return "CBoot";
        
        // Potions
        case 373: return "Pot";
        case 438: return "SPot";  // Splash
        
        // Food
        case 260: return "Apl";
        case 322: return "Gap";
        case 297: return "Brd";
        case 320: return "Pork";
        case 364: return "Stk";
        case 366: return "Chk";
        case 360: return "Mel";
        case 357: return "Cke";
        case 391: return "Car";
        case 396: return "GCar";
        case 392: return "Pto";
        case 393: return "BPto";
        case 400: return "Pie";
        
        // Bow/Arrow
        case 261: return "Bow";
        case 262: return "Arr";
        
        // Ender items
        case 368: return "Prl";
        case 381: return "Eye";
        
        // Buckets
        case 325: return "Bkt";
        case 326: return "Wtr";
        case 327: return "Lva";
        case 335: return "Mlk";
        
        // Fishing rod
        case 346: return "Rod";
        
        // Projectiles
        case 344: return "Egg";
        case 332: return "Snw";
        case 385: return "Fire";
        
        // Materials
        case 264: return "Dia";
        case 265: return "IIng";
        case 266: return "GIng";
        case 388: return "Emr";
        case 280: return "Stk";
        case 287: return "Str";
        case 263: return "Coal";
        case 331: return "Red";
        case 352: return "Bone";
        case 377: return "BRod";
        case 288: return "Fth";
        case 334: return "Lea";
        
        // Tools
        case 259: return "F&S";
        case 345: return "Comp";
        case 359: return "Shr";
        case 384: return "XP";
        
        // Common Blocks
        case 1: return "Stn";
        case 2: return "Grs";
        case 3: return "Dir";
        case 4: return "Cob";
        case 5: return "Plk";
        case 7: return "Bed";
        case 12: return "Snd";
        case 13: return "Grv";
        case 17: return "Log";
        case 18: return "Lvs";
        case 20: return "Gls";
        case 24: return "Sstn";
        case 45: return "Brk";
        case 48: return "Mss";
        case 49: return "Obs";
        case 87: return "Nrk";
        case 98: return "StBr";
        case 112: return "NBrk";
        case 121: return "End";
        case 155: return "Qrz";
        case 170: return "Hay";
        
        // Misc
        case 54: return "Chst";
        case 61: return "Frnc";
        case 58: return "Crft";
        case 47: return "Book";
        case 116: return "Ench";
        case 130: return "EChst";
        case 145: return "Anvl";
        case 379: return "Brew";
        
        default:
            if (itemId > 0) return "?";
            return "";
    }
}

void InventoryManager::onRenderMenu() {
    ui::section("Settings", [this]() {
        ui::Binder("Keybind", &keybind);
        
        ImGui::Spacing();
        
        int delay = settings.swapDelay;
        if (ui::SliderInt("Click Delay (ms)", &delay, 0, 200, "%d"))
            settings.swapDelay = delay;

        bool retry = settings.retryOnFail;
        if (ui::Checkbox("Retry on Fail", &retry))
            settings.retryOnFail = retry;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use if swaps fail on some servers");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Preset selector
        ImGui::Text("PRESET");
        ImGui::Spacing();
        
        // 4 preset buttons in a row
        float buttonWidth = (ImGui::GetContentRegionAvail().x - ui::s(6)) / 4.0f;
        for (int p = 0; p < MAX_PRESETS; p++) {
            if (p > 0) ImGui::SameLine();
            
            bool isActive = (settings.activePreset == p);
            bool hasSaved = presets[p].saved;
            
            // Color based on state
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
            } else if (hasSaved) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.5f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            }
            
            char label[8];
            snprintf(label, sizeof(label), "%d", p + 1);
            if (ImGui::Button(label, ImVec2(buttonWidth, 0))) {
                settings.activePreset = p;
            }
            
            ImGui::PopStyleColor(2);
        }
        
        ImGui::Spacing();
        
        // Get current preset
        Preset& currentPreset = presets[settings.activePreset];
        
        // Save button
        if (ImGui::Button(currentPreset.saved ? "Overwrite" : "Save Hotbar", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            saveHotbar();
        }
        
        ImGui::Spacing();
        
        // Visual hotbar grid - 9 slots in a row
        float slotSize = ui::s(28);
        float spacing = ui::s(2);
        float totalWidth = slotSize * 9 + spacing * 8;
        float startX = (ImGui::GetContentRegionAvail().x - totalWidth) / 2;
        
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        cursor.x += startX;
        
        ImDrawList* draw = ImGui::GetWindowDrawList();
        
        for (int i = 0; i < 9; i++) {
            ImVec2 slotMin = ImVec2(cursor.x + i * (slotSize + spacing), cursor.y);
            ImVec2 slotMax = ImVec2(slotMin.x + slotSize, slotMin.y + slotSize);
            
            // Slot background
            if (currentPreset.saved && currentPreset.slots[i].itemId != 0) {
                // Filled slot
                draw->AddRectFilled(slotMin, slotMax, IM_COL32(40, 40, 40, 220), 4.0f);
                draw->AddRect(slotMin, slotMax, IM_COL32(80, 80, 80, 255), 4.0f);
                
                // Try to get item texture
                unsigned int texId = ItemTextureManager::get().getTexture(currentPreset.slots[i].itemId, currentPreset.slots[i].damage);
                
                if (texId != 0) {
                    // Render item texture
                    float padding = ui::s(2);
                    ImVec2 imgMin = ImVec2(slotMin.x + padding, slotMin.y + padding);
                    ImVec2 imgMax = ImVec2(slotMax.x - padding, slotMax.y - padding);
                    draw->AddImage((ImTextureID)(intptr_t)texId, imgMin, imgMax);
                } else {
                    // Fallback to text name
                    const char* name = getItemShortName(currentPreset.slots[i].itemId);
                    ImVec2 textSize = ImGui::CalcTextSize(name);
                    ImVec2 textPos = ImVec2(
                        slotMin.x + (slotSize - textSize.x) / 2,
                        slotMin.y + (slotSize - textSize.y) / 2
                    );
                    draw->AddText(textPos, IM_COL32(200, 255, 200, 255), name);
                }
            } else {
                // Empty slot
                draw->AddRectFilled(slotMin, slotMax, IM_COL32(30, 30, 30, 200), 4.0f);
                draw->AddRect(slotMin, slotMax, IM_COL32(60, 60, 60, 255), 4.0f);
            }
            
            // Slot number at bottom
            char numBuf[2];
            snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
            ImVec2 numSize = ImGui::CalcTextSize(numBuf);
            ImVec2 numPos = ImVec2(slotMin.x + (slotSize - numSize.x) / 2, slotMax.y + 2);
            draw->AddText(numPos, IM_COL32(100, 100, 100, 255), numBuf);
        }
        
        ImGui::Dummy(ImVec2(totalWidth, slotSize + ui::s(14)));
        
        if (!currentPreset.saved) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Press keybind to save current hotbar");
            ImGui::PopStyleColor();
        }
    });
}
