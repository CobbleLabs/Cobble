#include "item_textures.h"
#include "embedded_textures.hpp"

#include <Windows.h>
#include <gl/GL.h>
#include <cstring>
#include <vector>
#include <shlobj.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../../../infra/ext/stb/stb_image.h"

ItemTextureManager& ItemTextureManager::get() {
    static ItemTextureManager instance;
    return instance;
}

static std::string findTexturesPath() {
    // 1. Try AppData
    wchar_t* appData = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData) == S_OK) {
        std::wstring path(appData);
        CoTaskMemFree(appData);
        std::wstring appDataCobble = path + L"\\Cobble\\textures";
        DWORD dwAttrib = GetFileAttributesW(appDataCobble.c_str());
        if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            int size = WideCharToMultiByte(CP_UTF8, 0, appDataCobble.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string result(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, appDataCobble.c_str(), -1, &result[0], size, nullptr, nullptr);
            return result;
        }
    }

    // 2. Try hardcoded workspace path
    std::string wsPath = "c:\\Users\\kohler\\Downloads\\cobble-server-main\\Cobble\\textures";
    DWORD dwAttrib = GetFileAttributesA(wsPath.c_str());
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        return wsPath;
    }

    // 3. Try relative to working directory
    std::string relPath = ".\\textures";
    dwAttrib = GetFileAttributesA(relPath.c_str());
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        return relPath;
    }

    return "";
}

static std::string getFallbackEmbedded(const std::string& name) {
    if (name == "stone") return "cobblestone";
    if (name == "grass_side" || name == "grass_top") return "dirt";
    if (name == "gold_ore") return "gold_ingot";
    if (name == "iron_ore") return "iron_ingot";
    if (name == "coal_ore") return "coal";
    if (name == "lapis_ore" || name == "lapis_block") return "diamond";
    if (name == "emerald_ore" || name == "emerald_block") return "emerald";
    if (name == "redstone_ore" || name == "redstone_ore_lit") return "redstone_dust";
    // chest now has its own embedded texture
    if (name == "bedrock") return "obsidian";
    return name;
}

void ItemTextureManager::init(const std::string& texturesPath) {
    if (initialized) return;
    basePath = findTexturesPath();
    initialized = true;
}

void ItemTextureManager::shutdown() {
    for (auto& pair : textureCache) {
        if (pair.second != 0) {
            glDeleteTextures(1, &pair.second);
        }
    }
    textureCache.clear();
    initialized = false;
}

// Rotate pixel data 180° in-place (preserves dimensions).
static void rotate180(const unsigned char* src, int width, int height,
                      std::vector<unsigned char>& dst) {
    dst.resize(width * height * 4);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = (y * width + x) * 4;
            int dstX   = width  - 1 - x;
            int dstY   = height - 1 - y;
            int dstIdx = (dstY * width + dstX) * 4;
            dst[dstIdx + 0] = src[srcIdx + 0];
            dst[dstIdx + 1] = src[srcIdx + 1];
            dst[dstIdx + 2] = src[srcIdx + 2];
            dst[dstIdx + 3] = src[srcIdx + 3];
        }
    }
}

// Load texture from disk or fallback to embedded data
unsigned int ItemTextureManager::loadTexture(const std::string& name) {
    // Disk PNGs come out upside-down once uploaded via glTexImage2D (GL samples
    // the first row at t=0). Flip on load so the icons render upright with
    // standard ImGui UVs (0,0)-(1,1).
    stbi_set_flip_vertically_on_load(true);

    // 1. Try to load from disk first if basePath is set
    if (!basePath.empty()) {
        std::string paths[] = {
            basePath + "\\blocks\\" + name + ".png",
            basePath + "\\items\\"  + name + ".png"
        };
        for (const auto& path : paths) {
            DWORD dwAttrib = GetFileAttributesA(path.c_str());
            if (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
                int width = 0, height = 0, channels = 0;
                unsigned char* imgData = stbi_load(path.c_str(), &width, &height, &channels, 4);
                if (imgData) {
                    GLuint textureId = 0;
                    glGenTextures(1, &textureId);
                    if (textureId != 0) {
                        glBindTexture(GL_TEXTURE_2D, textureId);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData);
                    }
                    stbi_image_free(imgData);
                    if (textureId != 0) return textureId;
                }
            }
        }
    }

    // 2. Fallback to embedded textures — already in correct orientation, upload as-is
    std::string embeddedName = getFallbackEmbedded(name);
    const EmbeddedTexture* tex = nullptr;
    for (int i = 0; embeddedTextures[i].name != nullptr; i++) {
        if (strcmp(embeddedTextures[i].name, embeddedName.c_str()) == 0) {
            tex = &embeddedTextures[i].tex;
            break;
        }
    }

    if (!tex || !tex->data || tex->width <= 0 || tex->height <= 0)
        return 0;

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex->data);

    return textureId;
}

// Helper to get potion texture name from damage value

// Damage value format in MC 1.7/1.8: bits encode potion type, tier, extended, splash
static std::string getPotionTextureName(int damage, bool isSplashItem) {
    // Extract potion effect from damage value
    // Lower 4 bits (0-15) = base potion type
    // Bit 5 (32) = tier II
    // Bit 6 (64) = extended duration
    // Bit 14 (16384) = splash (for ID 373)
    
    int baseType = damage & 0x0F;
    bool tierII = (damage & 0x20) != 0;
    bool extended = (damage & 0x40) != 0;
    bool isSplash = isSplashItem || ((damage & 0x4000) != 0);
    
    std::string prefix = "potion_";
    std::string suffix = "";
    
    if (isSplash) suffix = "_splash";
    else if (tierII) suffix = "_2";
    else if (extended) suffix = "_ext";
    
    // Map base potion types
    switch (baseType) {
        case 0:  return "potion_bottle_empty";  // Water bottle
        case 1:  return prefix + "regen" + suffix;
        case 2:  return prefix + "speed" + suffix;
        case 3:  return prefix + "fire_res" + suffix;
        case 4:  return prefix + "poison" + suffix;
        case 5:  return prefix + "health" + suffix;
        case 6:  return prefix + "night_vision" + suffix;
        case 8:  return prefix + "weakness" + suffix;
        case 9:  return prefix + "strength" + suffix;
        case 10: return prefix + "slowness" + suffix;
        case 11: return prefix + "leaping" + suffix;
        case 12: return prefix + "harming" + suffix;
        case 13: return prefix + "water_breathing" + suffix;
        case 14: return prefix + "invisibility" + suffix;
        default: 
            // Unknown potion type - return generic
            return isSplash ? "potion_bottle_splash" : "potion_bottle_drinkable";
    }
}

std::string ItemTextureManager::getTextureFilename(int itemId, int damage) {
    switch (itemId) {
        // Swords
        case 268: return "wood_sword";
        case 272: return "stone_sword";
        case 267: return "iron_sword";
        case 276: return "diamond_sword";
        case 283: return "gold_sword";
        
        // Axes
        case 271: return "wood_axe";
        case 275: return "stone_axe";
        case 258: return "iron_axe";
        case 279: return "diamond_axe";
        case 286: return "gold_axe";
        
        // Pickaxes
        case 270: return "wood_pickaxe";
        case 274: return "stone_pickaxe";
        case 257: return "iron_pickaxe";
        case 278: return "diamond_pickaxe";
        case 285: return "gold_pickaxe";
        
        // Shovels
        case 269: return "wood_shovel";
        case 273: return "stone_shovel";
        case 256: return "iron_shovel";
        case 277: return "diamond_shovel";
        case 284: return "gold_shovel";
        
        // Armor - Diamond
        case 310: return "diamond_helmet";
        case 311: return "diamond_chestplate";
        case 312: return "diamond_leggings";
        case 313: return "diamond_boots";
        
        // Armor - Iron
        case 306: return "iron_helmet";
        case 307: return "iron_chestplate";
        case 308: return "iron_leggings";
        case 309: return "iron_boots";
        
        // Armor - Gold
        case 314: return "gold_helmet";
        case 315: return "gold_chestplate";
        case 316: return "gold_leggings";
        case 317: return "gold_boots";
        
        // Armor - Leather
        case 298: return "leather_helmet";
        case 299: return "leather_chestplate";
        case 300: return "leather_leggings";
        case 301: return "leather_boots";
        
        // Armor - Chain
        case 302: return "chainmail_helmet";
        case 303: return "chainmail_chestplate";
        case 304: return "chainmail_leggings";
        case 305: return "chainmail_boots";
        
        // Bow & Arrow
        case 261: return "bow_standby";
        case 262: return "arrow";
        
        // Food
        case 260: return "apple";
        case 322: return "apple_golden";
        case 297: return "bread";
        case 320: return "porkchop_cooked";
        case 319: return "porkchop_raw";
        case 364: return "beef_cooked";
        case 363: return "beef_raw";
        case 366: return "chicken_cooked";
        case 365: return "chicken_raw";
        case 360: return "melon";
        case 357: return "cookie";
        case 391: return "carrot";
        case 396: return "carrot_golden";
        case 392: return "potato";
        case 393: return "potato_baked";
        case 400: return "pumpkin_pie";
        
        // Potions - splash potion always shows splash bottle, regular potion shows drinkable bottle
        case 373: return "potion_bottle_drinkable";
        case 438: return "potion_bottle_splash";
        
        // Ender items
        case 368: return "ender_pearl";
        case 381: return "ender_eye";
        
        // Projectiles
        case 344: return "egg";
        case 332: return "snowball";
        case 385: return "fireball";
        
        // Buckets
        case 325: return "bucket_empty";
        case 326: return "bucket_water";
        case 327: return "bucket_lava";
        case 335: return "bucket_milk";
        
        // Fishing rod
        case 346: return "fishing_rod_uncast";
        
        // Materials
        case 264: return "diamond";
        case 265: return "iron_ingot";
        case 266: return "gold_ingot";
        case 388: return "emerald";
        case 280: return "stick";
        case 287: return "string";
        case 263: return "coal";
        case 331: return "redstone_dust";
        case 352: return "bone";
        case 377: return "blaze_rod";
        case 288: return "feather";
        case 334: return "leather";
        
        // Tools
        case 259: return "flint_and_steel";
        case 345: return "compass";
        case 359: return "shears";
        case 384: return "experience_bottle";
        
        // Block ID mappings for Block ESP UI (Fallback textures mapped to available embedded textures)
        case 1:  return "stone";
        case 2:  return "grass_side";
        case 3:  return "dirt";        // Dirt
        case 4:  return "cobblestone"; // Cobblestone
        case 5:  return "planks_oak";  // Oak Wood Planks
        case 6:  return "apple";       // Oak Sapling -> Apple
        case 7:  return "obsidian";    // Bedrock -> Obsidian
        case 8:  
        case 9:  return "bucket_water"; // Water -> Water Bucket
        case 10: 
        case 11: return "bucket_lava";  // Lava -> Lava Bucket
        case 12: return "sand";        // Sand
        case 13: return "gravel";      // Gravel
        case 14: return "gold_ore";
        case 15: return "iron_ore";
        case 16: return "coal_ore";
        case 17: return "wood_axe";    // Oak Wood -> Wood Axe
        case 18: return "shears";      // Oak Leaves -> Shears
        case 19: return "sponge";      // Sponge (if available, otherwise apple)
        case 20: return "glass";       // Glass
        case 21: return "lapis_ore";
        case 22: return "diamond";     // Lapis Lazuli Block -> Diamond
        case 23: return "bow_standby"; // Dispenser -> Bow
        case 24: return "sand";        // Sandstone -> Sand
        case 25: return "clock";       // Note Block -> Clock
        case 26: return "leather_boots"; // Bed Block -> Boots
        case 27: return "powered_rail"; // Powered Rail (if available, else stick)
        case 28: return "detector_rail"; 
        case 29: return "slimeball";   // Sticky Piston -> Slimeball
        case 30: return "string";      // Cobweb -> String
        case 31: return "shears";      // Tall Grass -> Shears
        case 32: return "stick";       // Dead Bush -> Stick
        case 33: return "iron_ingot";  // Piston -> Iron Ingot
        case 34: return "iron_ingot";  
        case 35: return "wool";        // Wool (if available, otherwise leather)
        case 37: return "dandelion";   
        case 38: return "poppy";       
        case 39: 
        case 40: return "red_mushroom"; // Mushrooms
        case 41: return "gold_ingot";  // Block of Gold -> Gold Ingot
        case 42: return "iron_ingot";  // Block of Iron -> Iron Ingot
        case 43: 
        case 44: return "stone_sword"; // Stone Slab -> Stone Sword
        case 45: return "brick";       
        case 46: return "tnt_side";    // TNT
        case 47: return "book";        
        case 48: return "cobblestone"; // Mossy Cobblestone -> Cobblestone
        case 49: return "obsidian";    // Obsidian
        case 50: return "torch";       
        case 51: return "fireball";    // Fire -> Fireball
        case 52: return "mob_spawner"; 
        case 53: return "wood_pickaxe"; // Wood Stairs -> Wood Pickaxe
        case 54: return "chest"; // Chest
        case 55: return "redstone_dust"; // Redstone Wire -> Redstone Dust
        case 56: return "diamond_ore";
        case 57: return "diamond";     // Block of Diamond -> Diamond
        case 58: return "crafting_table_front"; // Crafting Table
        case 59: return "bread";       // Wheat Crops -> Bread
        case 60: return "dirt";        // Farmland -> Dirt
        case 61: 
        case 62: return "furnace_front_off"; // Furnace
        case 63: return "stick";       // Sign Block -> Stick
        case 64: return "iron_door";   
        case 65: return "ladder";      
        case 66: return "rail";        
        case 67: return "stone_pickaxe"; // Cobblestone Stairs -> Stone Pickaxe
        case 68: return "stick";       
        case 69: return "stick";       // Lever -> Stick
        case 70: return "stone_sword"; // Pressure Plate -> Stone Sword
        case 71: return "iron_door";   
        case 72: return "wood_sword";  
        case 73: 
        case 74: return "redstone_ore";
        case 75: 
        case 76: return "redstone_dust"; 
        case 77: return "stone_sword"; 
        case 78: return "snowball";    // Snow -> Snowball
        case 79: return "packed_ice";  // Ice -> Packed Ice
        case 80: return "snowball";    
        case 81: return "cactus";      
        case 82: return "clay";        
        case 83: return "sugar_cane";  
        case 84: return "record_13";   // Jukebox -> Record
        case 85: return "stick";       
        case 86: return "pumpkin";     
        case 87: return "netherrack";  
        case 88: return "soul_sand";   
        case 89: return "glowstone_dust"; // Glowstone -> Glowstone Dust
        case 90: return "nether_portal"; 
        case 91: return "pumpkin";     
        case 92: return "cake";        
        case 93: 
        case 94: return "redstone_dust"; 
        case 95: return "glass";       // Stained Glass -> Glass
        case 96: return "wood_sword";  
        case 97: return "egg";         // Monster Egg -> Egg
        case 98: return "cobblestone"; // Stone Bricks -> Cobblestone
        case 99: 
        case 100: return "red_mushroom"; 
        case 101: return "iron_ingot"; // Iron Bars -> Iron Ingot
        case 102: return "glass";      // Glass Pane -> Glass
        case 103: return "melon";      // Melon Block -> Melon
        case 104: 
        case 105: return "melon";      
        case 106: return "shears";     // Vines -> Shears
        case 107: return "stick";      
        case 108: return "brick";      
        case 109: return "stone_pickaxe"; 
        case 110: return "dirt";       // Mycelium -> Dirt
        case 111: return "waterlily";  
        case 112: return "nether_brick"; 
        case 113: return "nether_brick"; 
        case 114: return "stone_pickaxe"; 
        case 115: return "nether_wart"; 
        case 116: return "diamond";    // Enchantment Table -> Diamond
        case 117: return "brewing_stand"; 
        case 118: return "cauldron";   
        case 119: 
        case 120: return "ender_eye";  // End Portal -> Ender Eye
        case 121: return "obsidian";   // End Stone -> Obsidian
        case 122: return "dragon_egg"; 
        case 123: 
        case 124: return "redstone_dust"; // Redstone Lamp -> Redstone Dust
        case 125: 
        case 126: return "wooden_slab"; 
        case 127: return "cocoa";      
        case 128: return "stone_pickaxe"; 
        case 129: return "emerald_ore";
        case 130: return "ender_pearl"; // Ender Chest -> Ender Pearl
        case 131: return "tripwire_hook"; 
        case 132: return "string";     
        case 133: return "emerald";    // Block of Emerald -> Emerald
        case 134: 
        case 135: 
        case 136: return "wood_pickaxe"; 
        case 137: return "command_block"; 
        case 138: return "beacon";     
        case 139: return "cobblestone"; 
        case 140: return "flower_pot"; 
        case 141: return "carrot";     // Carrots -> Carrot
        case 142: return "potato";     // Potatoes -> Potato
        case 143: return "stick";      
        case 144: return "skull";      
        case 145: return "anvil";      
        case 146: return "chest"; // Trapped Chest
        case 147: 
        case 148: return "iron_ingot"; 
        case 149: 
        case 150: return "redstone_dust"; 
        case 151: return "clock";      
        case 152: return "redstone_dust"; // Block of Redstone -> Redstone Dust
        case 153: return "quartz";     // Nether Quartz Ore -> Quartz
        case 154: return "hopper";     
        case 155: return "quartz";     // Block of Quartz -> Quartz
        case 156: return "stone_pickaxe"; 
        case 157: return "rail";       
        case 158: return "bow_standby"; 
        case 159: return "clay";       
        case 160: return "glass";      
        case 161: return "shears";     
        case 162: return "wood_axe";   
        case 163: 
        case 164: return "wood_pickaxe"; 
        case 165: return "slimeball";  // Slime Block -> Slimeball
        case 166: return "barrier";    
        case 167: return "iron_ingot"; 
        case 168: return "prismarine"; 
        case 169: return "sea_lantern"; 
        case 170: return "hay_bale";   
        case 171: return "wool";       
        case 172: return "dirt";       
        case 173: return "coal";       // Block of Coal -> Coal
        case 174: return "packed_ice"; 
        case 175: return "poppy";      
        case 176: 
        case 177: return "banner";     
        case 178: return "clock";      
        case 179: return "sand";       
        case 180: return "stone_pickaxe"; 
        case 181: 
        case 182: return "stone_sword"; 
        case 183: 
        case 184: 
        case 185: 
        case 186: 
        case 187: return "stick";      
        case 188: 
        case 189: 
        case 190: 
        case 191: 
        case 192: return "stick";      
        case 193: 
        case 194: 
        case 195: 
        case 196: 
        case 197: return "iron_door";  
        
        default:
            return "";
    }
}

unsigned int ItemTextureManager::getTexture(int itemId, int damage) {
    if (!initialized) return 0;
    
    // Use combined key for items that use damage for variants (potions: 373, 438)
    // For most items, damage is durability and shouldn't affect texture
    int cacheKey = itemId;
    if (itemId == 373 || itemId == 438) {
        // Potions use damage for type - combine into unique key
        cacheKey = itemId * 10000 + damage;
    }
    
    // Check cache first
    for (const auto& pair : textureCache) {
        if (pair.first == cacheKey) {
            return pair.second;
        }
    }
    
    // Get texture name for this item
    std::string name = getTextureFilename(itemId, damage);
    if (name.empty()) {
        textureCache.push_back({cacheKey, 0});
        return 0;
    }
    
    // Load texture from disk or fallback to embedded data
    unsigned int texId = loadTexture(name);
    
    textureCache.push_back({cacheKey, texId});
    return texId;
}
