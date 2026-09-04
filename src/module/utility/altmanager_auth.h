#pragma once

#include <string>
#include <vector>

// Alt Manager authentication back-end.
//
// Ports the four login methods of the accountmanager Forge mod to native code:
//
//   MICROSOFT - full OAuth code flow. Opens the system browser and catches the
//               redirect on a one-shot HTTP listener at 127.0.0.1:25575.
//   REFRESH   - Microsoft OAuth refresh tokens (the "M.C..." desktop ones).
//   TOKEN     - a minecraftservices JWT access token pasted directly, single or
//               bulk (one per line).
//   COOKIE    - browser cookies exported as Netscape .txt or JSON, replayed
//               against the silent (prompt=none) OAuth endpoints.
//
// Everything runs on one background worker thread; the UI polls Status() every
// frame and drains finished logins with TakeResults(). Nothing here touches
// JNI or ImGui, so it is safe to call from the render thread.

namespace AltAuth {

enum class Kind {
    MICROSOFT = 0,
    TOKEN,
    REFRESH,
    COOKIE,
};

struct Account {
    std::string username;
    std::string uuid;
    std::string accessToken;
    std::string secret;  // MS refresh token, or the serialized cookie jar
    Kind kind = Kind::TOKEN;
};

// ---- worker state (all cheap, safe to poll every frame) ----
bool IsBusy();
std::string Status();     // one-line progress, e.g. "Xbox Live..."
std::string LastError();  // last failure message, "" when none
void ClearError();
void Cancel();

// Logins finished since the last call. Queued, so closing the window mid-flight
// does not lose them.
std::vector<Account> TakeResults();

// ---- flows: queue work on the worker and return immediately ----
// All four return false when a login is already in flight or the input is
// obviously unusable (LastError() then says why).
bool BeginMicrosoft();
bool BeginRefresh(const std::string& refreshToken);
bool BeginTokens(const std::string& rawTokenBlob);
bool BeginCookies(const std::string& rawCookieData);

// URL the browser was sent to; shown so the user can copy it if the browser
// failed to open. Empty until BeginMicrosoft() runs.
std::string AuthUrl();

// How many access tokens a bulk paste actually contains. Lets the UI show
// "3 tokens found" before the user commits.
int CountTokens(const std::string& raw);

// Reads an import off disk (a token list, or a cookie export as Netscape .txt
// or JSON). Returns "" on error and fills errorOut.
std::string ReadFile(const std::string& path, std::string& errorOut);

// ---- native "open file" dialog ----
// The Win32 picker pumps its own modal message loop, which would freeze the
// game if it ran on the render thread, so it gets a thread of its own. Ask for
// it with BrowseForFile(), then poll TakeBrowsedFile() once a frame: it returns
// true exactly once, with the path the user picked.
bool BrowseForFile();  // false when a dialog is already up
bool IsBrowsing();
bool TakeBrowsedFile(std::string& pathOut);

// Stops the worker. Called on DLL teardown.
void Shutdown();

}  // namespace AltAuth
