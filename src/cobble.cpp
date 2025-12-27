#include "web/config_server.h"

#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/autoclicker.h"
#include "module/aimassist.h"
#include "module/nametags.h"
#include "web/config.h"
#include "overlay/overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <atomic>

// Flag for shutdown signaling
static std::atomic<bool> shutdownRequested{ false };

// Console Ctrl handler - MUST return quickly
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			// Signal shutdown, don't block
			shutdownRequested = true;
			cobble::context::is_running = false;
			return TRUE; // Handled - don't kill process
		default:
			return FALSE;
	}
}

void cobble::context::initialize()
{
	logger.initialize();
	
	// Install console control handler (Ctrl+C, window close, etc)
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	log_debug("starting cobble...");
	
	Java::Init();

	if (!Java::Initialized) {
		log_err("java init failed, staying alive for debug");
		while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
	}

	SDK_Init();

	if (!mc.isValid()) {
		log_err("sdk init failed");
	}

	// Register modules
	moduleManager.registerModule(&autoClicker);
	moduleManager.registerModule(&aimAssist);
	moduleManager.registerModule(&nameTags);
	
	// Initialize overlay with delay - OpenGL context needs time to be ready
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(3));
		Overlay::Init();
	}).detach();

	// Start WebSocket server for web config
	configServer.onMessage([](const std::string& msg) {
		Config::apply(msg);
	});
	Web_Init();

	log_ok("cobble initialized");
	logger.writeRaw("\n");
	log_prompt("R = toggle autoclicker");
	log_prompt("V = toggle aimassist");
	log_prompt("N = toggle nametags");
	log_prompt("Ctrl+C = unload client");
	logger.writeRaw("\n");
	log_debug("dashboard: open config/index.html");

	is_running = true;

	while (is_running && !shutdownRequested)
	{
		// Refresh player state each tick
		player.refresh();
		
		// Update all modules
		moduleManager.update();

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Only shutdown if we're still in control (not being force-killed)
	if (!shutdownRequested) {
		shutdown();
	} else {
		// Fast shutdown for Ctrl+C/close - don't do heavy cleanup
		fastShutdown();
	}
}

void cobble::context::shutdown()
{
	logger.writeRaw("\n");
	log_warn("unloading cobble...");
	
	// Disable all modules first
	autoClicker.setEnabled(false);
	aimAssist.setEnabled(false);
	nameTags.setEnabled(false);
	
	Overlay::Shutdown();
	Web_Shutdown();
	Java::Kill();
	
	log_ok("cobble unloaded cleanly");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
	logger.shutdown();
}

void cobble::context::fastShutdown()
{
	// Quick cleanup - console is closing, don't log
	autoClicker.setEnabled(false);
	aimAssist.setEnabled(false);
	nameTags.setEnabled(false);
	
	Overlay::Shutdown();
	// Stop websocket
	Web_Shutdown();
}
