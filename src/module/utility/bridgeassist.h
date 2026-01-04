#pragma once
#include "../module.h"
#include "../../sdk/core/minecraft.h"

class BridgeAssist : public Module
{
public:
	BridgeAssist() : Module("Bridge Assist") {}

	void onUpdate() override;
	void onRenderMenu() override;

	struct Settings {
		bool onlyWhenSneaking = true;
		bool onlyLookingDown = true;
	} settings;

private:
	Minecraft::Vec3 m_from = {};

	bool m_isEdge = false;
	bool m_wasSneaking = false;
	bool m_shiftInitiated = false;

	void Sneak();
	void UnSneak();
};

extern BridgeAssist* bridgeAssist;
