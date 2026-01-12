#pragma once

#include "../module.h"

class Sprint : public Module
{
public:
	Sprint();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;
};

extern Sprint* sprint;
