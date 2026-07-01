#include "altmanager.h"

#include <cstring>
#include <algorithm>
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

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

void AltManager::onEnable() {
    addToHistory(settings.spoofedName);
}

void AltManager::addToHistory(const std::string& name) {
    if (!validateName(name.c_str()))
        return;
    nameHistory.erase(std::remove(nameHistory.begin(), nameHistory.end(), name), nameHistory.end());
    nameHistory.push_back(name);
    if (nameHistory.size() > MAX_HISTORY_LEN) {
        nameHistory.erase(nameHistory.begin());
    }
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
            bool enterPressed = ImGui::InputText("##altname", settings.spoofedName, sizeof(settings.spoofedName), ImGuiInputTextFlags_EnterReturnsTrue);
            bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ui::DescribeLastItem("Username", "Sets the offline-server username applied on your next connection.");
            ImGui::PopItemWidth();

            if (enterPressed || deactivated) {
                if (validateName(settings.spoofedName)) {
                    addToHistory(settings.spoofedName);
                }
            }

            if (!validateName(settings.spoofedName)) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid: 3-16 chars [A-Z a-z 0-9 _]");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("Only works on offline servers.");
            ImGui::Spacing();
            ImGui::TextWrapped("Applies on the next server connect.");
        },
        "History",
        [this]() {
            if (ImGui::Button("Clear All", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                nameHistory.clear();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (nameHistory.empty()) {
                ImGui::TextDisabled("No history yet");
            } else {
                ImGui::BeginChild("##althistorychild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
                // Render from most recent to oldest
                for (int i = static_cast<int>(nameHistory.size()) - 1; i >= 0; --i) {
                    const std::string& histName = nameHistory[i];
                    bool isSelected = (strcmp(settings.spoofedName, histName.c_str()) == 0);
                    if (ImGui::Selectable(histName.c_str(), isSelected)) {
                        size_t copyLen = (std::min)(histName.size(), (size_t)MAX_NAME_LEN);
                        memcpy(settings.spoofedName, histName.data(), copyLen);
                        settings.spoofedName[copyLen] = '\0';
                        addToHistory(histName); // Move to most recent
                    }
                }
                ImGui::EndChild();
            }
        });
}

std::string AltManager::getDisplayInfo() {
    if (!validateName(settings.spoofedName))
        return "invalid";
    return std::string(settings.spoofedName);
}



void AltManager::saveSettings(json::Value& node) const {
    node["spoofedName"] = std::string(settings.spoofedName);
    node["historyCount"] = static_cast<int>(nameHistory.size());
    for (size_t i = 0; i < nameHistory.size(); ++i) {
        std::string indexKey = std::to_string(i);
        node["history"][indexKey] = nameHistory[i];
    }
}

void AltManager::loadSettings(const json::Value& node) {
    std::string n = node["spoofedName"].as_string(settings.spoofedName);
    size_t copyLen = (std::min)(n.size(), (size_t)MAX_NAME_LEN);
    memcpy(settings.spoofedName, n.data(), copyLen);
    settings.spoofedName[copyLen] = '\0';

    nameHistory.clear();
    if (node.has("historyCount")) {
        int count = node["historyCount"].as_int(0);
        if (node.has("history")) {
            const auto& historyNode = node["history"];
            for (int i = 0; i < count; ++i) {
                std::string key = std::to_string(i);
                if (historyNode.has(key)) {
                    std::string val = historyNode[key].as_string("");
                    if (!val.empty()) {
                        nameHistory.push_back(val);
                    }
                }
            }
        }
    }
}
