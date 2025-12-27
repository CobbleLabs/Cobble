#pragma once

#include <string>

struct Module
{
	const char* name;
	bool enabled = false;
	int keybind = 0; // Virtual key code, 0 = no keybind

	Module(const char* name) : name(name) {}
	virtual ~Module() = default;

	// Called when module is enabled
	virtual void onEnable() {}

	// Called when module is disabled
	virtual void onDisable() {}

	// Called every tick while enabled
	virtual void onUpdate() {}
	
	// Called every frame for rendering (visual modules)
	virtual void onRender() {}

	void toggle()
	{
		enabled = !enabled;
		if (enabled)
			onEnable();
		else
			onDisable();
	}

	void setEnabled(bool value)
	{
		if (enabled == value) return;
		toggle();
	}
};
