#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

// Simple WebSocket server for config sync
// Uses raw sockets - no external dependencies
// Only supports single client connection for simplicity
class ConfigServer
{
public:
	using MessageHandler = std::function<void(const std::string&)>;

	ConfigServer(int port = 8765);
	~ConfigServer();

	bool start();
	void stop();
	bool isRunning() const { return running; }

	void sendMessage(const std::string& message);
	void onMessage(MessageHandler handler) { messageHandler = handler; }

private:
	int port;
	std::atomic<bool> running{ false };
	std::thread serverThread;
	SOCKET serverSocket = INVALID_SOCKET;
	SOCKET clientSocket = INVALID_SOCKET;
	MessageHandler messageHandler;

	void serverLoop();
	bool acceptClient();
	bool handleWebSocketHandshake();
	void handleClient();
	
	std::string readFrame();
	void sendFrame(const std::string& data);
	
	static std::string base64Encode(const unsigned char* data, size_t len);
	static std::string sha1(const std::string& input);
};

extern ConfigServer configServer;

// Initialize web server for config
void Web_Init();
void Web_Shutdown();
