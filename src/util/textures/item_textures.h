#pragma once

#include <string>
#include <unordered_map>

// Manages loading and caching of Minecraft item textures
// Textures are loaded from PNG files and converted to OpenGL textures
class ItemTextureManager {
public:
    static ItemTextureManager& get();

    // Initialize texture system - call after OpenGL context is ready
    void init(const std::string& texturesPath);

    // Shutdown - release all textures
    void shutdown();

    // Get OpenGL texture ID for an item, returns 0 if not found
    unsigned int getTexture(int itemId, int damage = 0);

    // Check if textures are loaded
    bool isInitialized() const { return initialized; }

private:
    ItemTextureManager() = default;
    ~ItemTextureManager() = default;

    // Load a PNG file and create GL texture
    unsigned int loadTexture(const std::string& name);

    // Get texture filename for item ID
    std::string getTextureFilename(int itemId, int damage = 0);

    std::string basePath;
    std::unordered_map<int, unsigned int> textureCache;  // cacheKey -> GL texture ID
    bool initialized = false;
};
