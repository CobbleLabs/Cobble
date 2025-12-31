#include "module_manager.h"
#include "../sdk/minecraft.h"
#include "../ui/menu/menu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>

ModuleManager* moduleManager = nullptr;

// Check if Minecraft/Lunar window is the foreground window
bool ModuleManager::isMinecraftFocused()
{
	HWND foreground = GetForegroundWindow();
	if (!foreground) return false;
	
	// Get the process ID of the foreground window
	DWORD foregroundPid = 0;
	GetWindowThreadProcessId(foreground, &foregroundPid);
	
	// Compare with our process
	DWORD ourPid = GetCurrentProcessId();
	
	return foregroundPid == ourPid;
}

void ModuleManager::registerModule(Module* mod)
{
	modules.push_back(mod);
}

void ModuleManager::update()
{
	// Only process keybinds and modules when Minecraft is focused
	if (!isMinecraftFocused()) return;
	
	checkKeybinds();

	for (auto* mod : modules)
	{
		if (mod->enabled)
			mod->onUpdate();
	}
}

void ModuleManager::onRender()
{
	for (auto* mod : modules)
	{
		if (mod->enabled)
			mod->onRender();
	}
}

void ModuleManager::checkKeybinds()
{
	if (mc.isInGui()) return;
	if (Menu::isOpen) return;
	
	for (auto* mod : modules)
	{
		if (mod->keybind == 0) continue;

		bool pressed = (GetAsyncKeyState(mod->keybind) & 0x8000) != 0;

		if (pressed && !keyStates[mod->keybind])
		{
			mod->toggle();
		}

		keyStates[mod->keybind] = pressed;
	}
}
