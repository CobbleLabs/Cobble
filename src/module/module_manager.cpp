#include "module_manager.h"

ModuleManager moduleManager;

void ModuleManager::registerModule(Module* mod)
{
	modules.push_back(mod);
}

void ModuleManager::update()
{
	checkKeybinds();

	for (auto* mod : modules)
	{
		if (mod->enabled)
			mod->onUpdate();
	}
}

void ModuleManager::checkKeybinds()
{
	for (auto* mod : modules)
	{
		if (mod->keybind == 0) continue;

		bool pressed = (GetAsyncKeyState(mod->keybind) & 0x8000) != 0;

		// Detect key down edge (was not pressed, now pressed)
		if (pressed && !keyStates[mod->keybind])
		{
			mod->toggle();
		}

		keyStates[mod->keybind] = pressed;
	}
}
