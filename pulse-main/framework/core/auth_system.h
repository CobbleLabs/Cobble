#pragma once

#include <string>

namespace auth
{
    enum class AuthStatus {
        Success,
        KeyNotFound,
        HwidMismatch,
        Expired,
        Banned,
        InvalidFormat,
        ServerError
    };

    // HWID del Sistema y Portapapeles
    std::string get_system_hwid();
    bool copy_to_clipboard(const std::string& text);

    // Sesión del Cliente
    void initialize();
    bool is_authenticated();
    AuthStatus login(const std::string& inputKey);
    void logout();

    // Remember Me
    bool get_remember_me();
    void set_remember_me(bool value);

    // Info de sesión
    std::string get_current_key();
    std::string get_license_type();
    std::string get_expires_at();
    std::string get_status_message();

    // Helpers
    std::string status_to_string(AuthStatus status);
}
