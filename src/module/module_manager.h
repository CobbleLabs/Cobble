#pragma once

#include "module.h"
#include <vector>
#include <Windows.h>

// Handles keybinds and update loop for all registered modules
class ModuleManager
{
public:
	void registerModule(Module* mod);
	void update();
	void onRender();  // Called from overlay for visual modules
	void checkKeybinds();
	void broadcastModuleState(Module* mod);  // Send state to web UI
	
	const std::vector<Module*>& getModules() const { return modules; }

	template<typename T>
	T* get()
	{
		for (auto* mod : modules)
		{
			if (auto* casted = dynamic_cast<T*>(mod))
				return casted;
		}
		return nullptr;
	}

private:
	std::vector<Module*> modules;
	
	// Keybind state tracking to detect key press (not hold)
	bool keyStates[256] = { false };
	
	// Check if Minecraft window is focused
	bool isMinecraftFocused();
};

extern ModuleManager* moduleManager;
