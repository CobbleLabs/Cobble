#pragma once

#include <string>
#include <vector>

class ArrayListRenderer
{
public:
	static void render();
	
	// Settings
	static inline bool enabled = true;
	static inline bool showBackground = true;
	static inline bool sortByLength = true;  // Sort modules by name length
	static inline float yOffset = 10.0f;     // Distance from top
};
