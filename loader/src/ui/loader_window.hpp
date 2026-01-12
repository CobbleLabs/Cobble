#pragma once
#include <string>

namespace LoaderUI {
enum class State {
    INITIALIZING,       // Security checks
    WAITING_MINECRAFT,  // "No Minecraft found"
    LOGIN_REQUIRED,     // Username/password inputs
    AUTHENTICATING,     // "Authenticating..." spinner
    LICENSE_VALID,      // Show license info + "Waiting for Minecraft..."
    INJECTING_SUCCESS,  // Injection succeeded - show success message
    ERROR_STATE         // Red error message
};

// Lifecycle
bool Init();
void Show();  // Call after pre-rendering to show window
void Shutdown();
bool Render();  // Returns false if window closed

// State management
void SetState(State state);
void SetMessage(const std::string& msg);
void SetError(const std::string& error);
void SetLicenseInfo(const std::string& plan, const std::string& expiry);

// Input getters (for LOGIN_REQUIRED state)
std::string GetUsername();
std::string GetPassword();
bool IsLoginButtonPressed();
void ClearLoginButton();  // Reset button state after handling
}  // namespace LoaderUI
