#include "config_server.h"
#include "../util/logger.hpp"
#include <sstream>
#include <iomanip>

ConfigServer configServer(8765);

// Simple SHA-1 implementation for WebSocket handshake
// Only used for Accept key generation
namespace {
	uint32_t rotl32(uint32_t x, int n) {
		return (x << n) | (x >> (32 - n));
	}

	void sha1Transform(uint32_t state[5], const unsigned char block[64]) {
		uint32_t w[80];
		uint32_t a, b, c, d, e;

		for (int i = 0; i < 16; i++) {
			w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
				   (block[i * 4 + 2] << 8) | block[i * 4 + 3];
		}
		for (int i = 16; i < 80; i++) {
			w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		}

		a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

		for (int i = 0; i < 80; i++) {
			uint32_t f, k;
			if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6; }

			uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
		}

		state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
	}
}

std::string ConfigServer::sha1(const std::string& input) {
	uint32_t state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	std::vector<unsigned char> data(input.begin(), input.end());
	
	size_t origLen = data.size();
	data.push_back(0x80);
	while ((data.size() % 64) != 56) data.push_back(0);
	
	uint64_t bitLen = origLen * 8;
	for (int i = 7; i >= 0; i--) data.push_back((bitLen >> (i * 8)) & 0xFF);
	
	for (size_t i = 0; i < data.size(); i += 64) {
		sha1Transform(state, &data[i]);
	}
	
	std::string result;
	result.reserve(20);
	for (int i = 0; i < 5; i++) {
		result += (state[i] >> 24) & 0xFF;
		result += (state[i] >> 16) & 0xFF;
		result += (state[i] >> 8) & 0xFF;
		result += state[i] & 0xFF;
	}
	return result;
}

std::string ConfigServer::base64Encode(const unsigned char* data, size_t len) {
	static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	result.reserve((len + 2) / 3 * 4);
	
	for (size_t i = 0; i < len; i += 3) {
		unsigned int n = data[i] << 16;
		if (i + 1 < len) n |= data[i + 1] << 8;
		if (i + 2 < len) n |= data[i + 2];
		
		result += chars[(n >> 18) & 0x3F];
		result += chars[(n >> 12) & 0x3F];
		result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
		result += (i + 2 < len) ? chars[n & 0x3F] : '=';
	}
	return result;
}

ConfigServer::ConfigServer(int port) : port(port) {}

ConfigServer::~ConfigServer() {
	stop();
}

bool ConfigServer::start() {
	if (running) return true;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		log_err("WebSocket: WSAStartup failed");
		return false;
	}

	serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET) {
		log_err("WebSocket: socket creation failed");
		WSACleanup();
		return false;
	}

	// Allow address reuse
	int opt = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		log_err("WebSocket: bind failed");
		closesocket(serverSocket);
		WSACleanup();
		return false;
	}

	if (listen(serverSocket, 1) == SOCKET_ERROR) {
		log_err("WebSocket: listen failed");
		closesocket(serverSocket);
		WSACleanup();
		return false;
	}

	running = true;
	serverThread = std::thread(&ConfigServer::serverLoop, this);
	
	log_ok("WebSocket server started on port " + std::to_string(port));
	return true;
}

void ConfigServer::stop() {
	if (!running) return;
	
	running = false;
	
	if (clientSocket != INVALID_SOCKET) {
		closesocket(clientSocket);
		clientSocket = INVALID_SOCKET;
	}
	
	if (serverSocket != INVALID_SOCKET) {
		closesocket(serverSocket);
		serverSocket = INVALID_SOCKET;
	}
	
	if (serverThread.joinable()) {
		serverThread.join();
	}
	
	WSACleanup();
	log_ok("WebSocket server stopped");
}

void ConfigServer::serverLoop() {
	while (running) {
		if (acceptClient()) {
			if (handleWebSocketHandshake()) {
				handleClient();
			}
			closesocket(clientSocket);
			clientSocket = INVALID_SOCKET;
		}
	}
}

bool ConfigServer::acceptClient() {
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(serverSocket, &readfds);
	
	timeval timeout{ 1, 0 }; // 1 second timeout
	
	if (select(0, &readfds, nullptr, nullptr, &timeout) > 0) {
		clientSocket = accept(serverSocket, nullptr, nullptr);
		if (clientSocket != INVALID_SOCKET) {
			return true;
		}
	}
	return false;
}

bool ConfigServer::handleWebSocketHandshake() {
	char buffer[2048];
	int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
	if (received <= 0) return false;
	buffer[received] = '\0';

	std::string request(buffer);
	
	// Find Sec-WebSocket-Key
	size_t keyPos = request.find("Sec-WebSocket-Key: ");
	if (keyPos == std::string::npos) return false;
	
	keyPos += 19;
	size_t keyEnd = request.find("\r\n", keyPos);
	std::string key = request.substr(keyPos, keyEnd - keyPos);

	// Generate accept key
	std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	std::string hash = sha1(acceptKey);
	std::string encoded = base64Encode((unsigned char*)hash.data(), hash.size());

	// Send handshake response
	std::string response = 
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: " + encoded + "\r\n\r\n";

	send(clientSocket, response.c_str(), (int)response.size(), 0);
	// Handshake complete - silent
	return true;
}

void ConfigServer::handleClient() {
	while (running && clientSocket != INVALID_SOCKET) {
		std::string message = readFrame();
		if (message.empty()) break;
		
		if (messageHandler) {
			messageHandler(message);
		}
	}
}

std::string ConfigServer::readFrame() {
	unsigned char header[2];
	int received = recv(clientSocket, (char*)header, 2, 0);
	if (received != 2) return "";

	bool fin = (header[0] & 0x80) != 0;
	int opcode = header[0] & 0x0F;
	bool masked = (header[1] & 0x80) != 0;
	uint64_t payloadLen = header[1] & 0x7F;

	// Handle close frame
	if (opcode == 8) return "";

	// Extended payload length
	if (payloadLen == 126) {
		unsigned char ext[2];
		recv(clientSocket, (char*)ext, 2, 0);
		payloadLen = (ext[0] << 8) | ext[1];
	} else if (payloadLen == 127) {
		unsigned char ext[8];
		recv(clientSocket, (char*)ext, 8, 0);
		payloadLen = 0;
		for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | ext[i];
	}

	// Read mask key
	unsigned char mask[4] = { 0 };
	if (masked) {
		recv(clientSocket, (char*)mask, 4, 0);
	}

	// Read payload
	std::string payload(payloadLen, '\0');
	size_t totalReceived = 0;
	while (totalReceived < payloadLen) {
		int r = recv(clientSocket, &payload[totalReceived], (int)(payloadLen - totalReceived), 0);
		if (r <= 0) return "";
		totalReceived += r;
	}

	// Unmask
	if (masked) {
		for (size_t i = 0; i < payloadLen; i++) {
			payload[i] ^= mask[i % 4];
		}
	}

	return payload;
}

void ConfigServer::sendFrame(const std::string& data) {
	if (clientSocket == INVALID_SOCKET) return;

	std::vector<unsigned char> frame;
	frame.push_back(0x81); // FIN + text frame

	if (data.size() <= 125) {
		frame.push_back((unsigned char)data.size());
	} else if (data.size() <= 65535) {
		frame.push_back(126);
		frame.push_back((data.size() >> 8) & 0xFF);
		frame.push_back(data.size() & 0xFF);
	} else {
		frame.push_back(127);
		for (int i = 7; i >= 0; i--) {
			frame.push_back((data.size() >> (i * 8)) & 0xFF);
		}
	}

	frame.insert(frame.end(), data.begin(), data.end());
	::send(clientSocket, (char*)frame.data(), (int)frame.size(), 0);
}

void ConfigServer::sendMessage(const std::string& message) {
	sendFrame(message);
}

void Web_Init() {
	configServer.start();
}

void Web_Shutdown() {
	configServer.stop();
}
