#include "item_textures.h"
#include "embedded_textures.hpp"

#include <Windows.h>
#include <gl/GL.h>
#include <cstring>

ItemTextureManager& ItemTextureManager::get() {
    static ItemTextureManager instance;
    return instance;
}

void ItemTextureManager::init(const std::string& texturesPath) {
    if (initialized) return;
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

// Load texture from embedded data
unsigned int ItemTextureManager::loadTexture(const std::string& name) {
    // Find texture in embedded data
    const EmbeddedTexture* tex = nullptr;
    for (int i = 0; embeddedTextures[i].name != nullptr; i++) {
        if (strcmp(embeddedTextures[i].name, name.c_str()) == 0) {
            tex = &embeddedTextures[i].tex;
            break;
        }
    }
    
    if (!tex || !tex->data || tex->width <= 0 || tex->height <= 0) {
        return 0;
    }
    
    // Create OpenGL texture
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) {
        return 0;
    }
    
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
        
        // Potions - use damage value to determine type
        case 373: return getPotionTextureName(damage, false);
        case 438: return getPotionTextureName(damage, true);
        
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
    auto it = textureCache.find(cacheKey);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    // Get texture name for this item
    std::string name = getTextureFilename(itemId, damage);
    if (name.empty()) {
        textureCache[cacheKey] = 0;
        return 0;
    }
    
    // Load texture from embedded data
    unsigned int texId = loadTexture(name);
    
    textureCache[cacheKey] = texId;
    return texId;
}
