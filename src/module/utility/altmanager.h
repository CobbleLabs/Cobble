#pragma once

#include "../module.h"

#include <string>
#include <vector>

class AltManager : public Module {
public:
    AltManager();

    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Account panel body, shared by the module page inside the click GUI and the
    // standalone window bound to the menu key on the multiplayer screen.
    void renderAccountPanel();

    // Compact variant used by the standalone window: header, username row and the
    // account list only.
    void renderCompactPanel();

    // Exact height renderCompactPanel() will draw at, content only. The window
    // asks for this before Begin() instead of letting ImGui auto-fit, which sizes
    // off the *previous* frame and clips the bottom row for one frame every time
    // the panel changes shape.
    float compactPanelHeight() const;

    static constexpr int MAX_NAME_LEN = 16;
    static constexpr int MAX_SAVED_NAMES = 16;  // legacy config key
    static constexpr int MAX_SAVED_ACCOUNTS = 64;

    // Mirrors accountmanager's AccountType. CRACKED is name-only (offline
    // servers); the rest carry a real session and can join online-mode servers.
    // The stored values are persisted in the config, so never renumber them.
    enum class AccountType {
        CRACKED = 0,
        MICROSOFT = 1,
        TOKEN = 2,
        REFRESH = 3,
        COOKIE = 4,
    };

    // Login method currently selected in the panel. Kept out of AccountType so
    // the UI can offer "offline" without it being an account kind of its own.
    enum class LoginMethod {
        OFFLINE = 0,
        MICROSOFT,
        TOKEN,
        REFRESH,
        COOKIE,
        COUNT,
    };

    struct AltAccount {
        std::string username;
        std::string uuid;         // real playerID, only filled for the original one
        std::string accessToken;  // real token, only filled for the original one
        // Microsoft refresh token, or the serialized cookie jar for COOKIE
        // accounts. Lets a stale accessToken be renewed without a new browser trip.
        std::string secret;
        AccountType type = AccountType::CRACKED;

        bool isPremium() const { return type != AccountType::CRACKED; }
    };

    static const char* typeTag(AccountType type);

    struct Settings {
        char spoofedName[MAX_NAME_LEN + 1] = "Player";
        std::vector<AltAccount> savedAccounts;
        int activeIndex = -1;  // -1 = no saved account in use
    } settings;

    bool isActive() const { return enabled && validateName(settings.spoofedName); }

    static bool validateName(const char* name);

    // ---- account list ----
    bool addCrackedAccount(const char* username);
    // Adds (or refreshes) an account that came out of a real login. Returns the
    // index it landed on, or -1 when the list is full.
    int addPremiumAccount(const std::string& username, const std::string& uuid, const std::string& accessToken,
                          const std::string& secret, AccountType type);
    void useAccount(int index);
    void removeAccount(int index);

    // ---- original (real) Minecraft session ----
    // Snapshotted the first time the JVM hands us a Session, before anything is
    // spoofed, so "Reset" can put the real account back.
    void captureOriginalSession();
    bool hasOriginalSession() const { return originalCaptured_; }
    const std::string& originalUsername() const { return originalSession_.username; }
    bool resetToOriginalAccount();

private:
    // Pushes spoofedName into the live Session. Retried across frames because the
    // SDK/JNI mappings may not be resolved yet on the first menu render.
    void syncSessionIfNeeded();

    // Login-method selector plus the inputs for whichever method is picked.
    // Shared by both panels; `compact` picks the tighter metrics used by the
    // standalone window. Returns the height it consumed.
    void renderLoginMethodUI(bool compact);

    // Height renderLoginMethodUI() will take, so the fixed-height click GUI panel
    // can size its child before drawing it.
    float loginMethodHeight(bool compact) const;

    // Moves anything the auth worker finished into the account list. Called at
    // the top of both panels.
    void pollAuthResults();

    LoginMethod loginMethod_ = LoginMethod::OFFLINE;

    AltAccount originalSession_;
    std::string originalSessionType_ = "mojang";
    bool originalCaptured_ = false;

    bool sessionDirty_ = false;
    int sessionAttempts_ = 0;
};

extern AltManager* altManager;

// Standalone Alt Manager window. Bound to the same key as the click GUI
// (INSERT by default) but only reachable from the multiplayer menu, where the
// key opens this window instead of the full menu.
namespace AltManagerWindow {
inline bool isOpen = false;
inline float openAnim = 0.f;

// True while the multiplayer menu is up, i.e. while the menu key should open
// this window instead of the click GUI. Result is cached for a few frames.
bool isAvailable();

void Close();
void Render();
}  // namespace AltManagerWindow
