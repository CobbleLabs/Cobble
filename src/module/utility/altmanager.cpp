#include "altmanager.h"

#include <cstring>
#include "../../ui/widgets/widgets.hpp"

AltManager* altManager = nullptr;

AltManager::AltManager() : Module("AltManager") {
    keybind = 0;
}

bool AltManager::validateName(const char* name) {
    if (!name)
        return false;
    size_t len = strlen(name);
    if (len < 3 || len > MAX_NAME_LEN)
        return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

void AltManager::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }

            ImGui::Spacing();

            ImGui::Text("Username");
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##altname", settings.spoofedName, sizeof(settings.spoofedName));
            ImGui::PopItemWidth();

            if (!validateName(settings.spoofedName)) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid: 3-16 chars [A-Z a-z 0-9 _]");
            }
        },
        "Info",
        []() {
            ImGui::TextWrapped("Only works on offline servers.");
            ImGui::Spacing();
            ImGui::TextWrapped("Applies on the next server connect.");
        });
}

std::string AltManager::getDisplayInfo() {
    if (!validateName(settings.spoofedName))
        return "invalid";
    return std::string(settings.spoofedName);
}
