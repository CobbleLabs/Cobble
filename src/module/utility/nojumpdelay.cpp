#include "nojumpdelay.h"
#include "../../sdk/minecraft.h"
#include "../../overlay/menu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

NoJumpDelay noJumpDelay;

NoJumpDelay::NoJumpDelay() : Module("NoJumpDelay")
{
	keybind = 0;
}

void NoJumpDelay::onEnable()
{
}

void NoJumpDelay::onDisable()
{
}

void NoJumpDelay::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	// When holding space and not on ground, force ground state and jump
	// This produces bhop-like effect (same as spamming jump key)
	if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && !player.isOnGround()) {
		player.setOnGround(true);
		player.jump();
	}
}
