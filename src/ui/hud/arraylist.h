#pragma once

// ============================================================================
// ArrayList - Active modules HUD display
// ============================================================================

namespace ArrayListRenderer {
    inline bool enabled = true;
    inline bool showBackground = true;
    inline bool sortByLength = true;
    inline float yOffset = 5.0f;
    
    void render();
}
