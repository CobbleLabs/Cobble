#pragma once

#include <string>
#include "../ui/hud/notifications.h"

struct Module
{
	const char* name;
	bool enabled = false;
	int keybind = 0; // Virtual key code, 0 = no keybind
	bool locked = false;  // If true, module is hidden/grayed in UI until unlocked
	
	// Global unlock for experimental modules (Velocity, Reach, Blink)
	static inline bool experimentalUnlocked = false;
	static inline const char* unlockCode = "barcelon12";  // Type this in menu to unlock

	Module(const char* name) : name(name) {}
	virtual ~Module() = default;
	
	// Check if module is accessible in UI (not locked OR experiments unlocked)
	bool isAccessible() const {
		return !locked || experimentalUnlocked;
	}

	// Called when module is enabled
	virtual void onEnable() {}

	// Called when module is disabled
	virtual void onDisable() {}

	// Called every tick while enabled
	virtual void onUpdate() {}
	
	// Called every frame for rendering (visual modules)
	virtual void onRender() {}
	
	// Called to render this module's settings in the menu
	// Each module implements its own UI using ui:: widgets
	virtual void onRenderMenu() {}
	
	// Returns extra info to display in ArrayList (e.g., "Normal 17.0 cps")
	// Override in modules that want to show status info
	virtual std::string getDisplayInfo() { return ""; }
	
	// Check if module can be toggled (override to block toggle in certain states)
	virtual bool canToggle() { return true; }

	void toggle()
	{
		// Don't allow toggling locked modules unless unlocked
		if (locked && !experimentalUnlocked) return;
		
		// Check if module allows toggle in current state
		if (!canToggle()) return;
		
		enabled = !enabled;
		if (enabled)
			onEnable();
		else
			onDisable();
		
		// Send notification
		NotificationManager::add(name, enabled);
	}

	void setEnabled(bool value)
	{
		if (enabled == value) return;
		toggle();
	}
};
