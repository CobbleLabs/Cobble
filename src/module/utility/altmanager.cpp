#define _CRT_SECURE_NO_WARNINGS

#include "altmanager.h"

#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include <algorithm>
#include <cstring>


AltManager *altManager = nullptr;

AltManager::AltManager() : Module("AltManager") { keybind = 0; }

bool AltManager::validateName(const char *name) {
  if (!name)
    return false;
  size_t len = strlen(name);
  if (len < 3 || len > MAX_NAME_LEN)
    return false;
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_';
    if (!ok)
      return false;
  }
  return true;
}

void AltManager::onRenderMenu() {
  const ImU32 accent = GetAccentRGB();
  const ImVec4 accentVec = ImGui::ColorConvertU32ToFloat4(accent);
  const ImU32 textDim = ui::GetColor(ImGuiCol_TextDisabled);

  ui::section("Settings", [this, accentVec]() {
    bool enabledValue = enabled;
    if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
      if (enabledValue != enabled)
        setEnabled(enabledValue);
    }

    ImGui::Dummy(ImVec2(0, ui::s(8)));

    // ---- Username label with helper hint ----
    ImGui::PushFont(font(1));  // bold
    ImGui::TextUnformatted("Username");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("(3-16 chars, A-Z 0-9 _)");

    ImGui::Dummy(ImVec2(0, ui::s(4)));

    // ---- Username input inside a subtle bordered pill ----
    const float rowH = ui::s(34);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::s(8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::s(12), ui::s(8)));
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##altname", settings.spoofedName,
                     sizeof(settings.spoofedName));
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);

    const bool nameValid = validateName(settings.spoofedName);
    if (!nameValid) {
      ImGui::Dummy(ImVec2(0, ui::s(2)));
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Invalid name");
    }

    ImGui::Dummy(ImVec2(0, ui::s(10)));

    // ---- Save button (accent-colored when actionable) ----
    const bool alreadySaved = std::any_of(
        settings.savedNames.begin(), settings.savedNames.end(),
        [&](const std::string &n) { return n == settings.spoofedName; });
    const bool listFull =
        settings.savedNames.size() >= static_cast<size_t>(MAX_SAVED_NAMES);
    const bool canSave = nameValid && !alreadySaved && !listFull;

    if (canSave) {
      ImVec4 acc = accentVec;
      ImVec4 accHover = ImVec4(ImMin(acc.x * 1.15f, 1.f), ImMin(acc.y * 1.15f, 1.f),
                                ImMin(acc.z * 1.15f, 1.f), acc.w);
      ImGui::PushStyleColor(ImGuiCol_Button, acc);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accHover);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, acc);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    } else {
      ImGui::BeginDisabled();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::s(8));
    if (ImGui::Button("+  Save Nick", ImVec2(-1, ui::s(32)))) {
      if (canSave)
        settings.savedNames.emplace_back(settings.spoofedName);
    }
    ImGui::PopStyleVar();

    if (canSave) {
      ImGui::PopStyleColor(4);
    } else {
      ImGui::EndDisabled();
    }

    // Subtle status text below the button
    if (alreadySaved && nameValid) {
      ImGui::Dummy(ImVec2(0, ui::s(2)));
      ImGui::TextDisabled("Already in your list");
    } else if (listFull) {
      ImGui::Dummy(ImVec2(0, ui::s(2)));
      ImGui::TextDisabled("List full (max %d)", MAX_SAVED_NAMES);
    }
  });

  ImGui::Dummy(ImVec2(0, ui::s(14)));

  // -------------------- Saved Nicks --------------------
  char header[32];
  snprintf(header, sizeof(header), "Saved Nicks  (%d)",
           static_cast<int>(settings.savedNames.size()));

  ui::section(header, [this, accent, textDim]() {
    if (settings.savedNames.empty()) {
      // Empty-state card
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::GetColor(ImGuiCol_FrameBg, 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ui::s(10));
      ImGui::BeginChild("##altnicksempty", ImVec2(0, ui::s(66)), true,
                        ImGuiWindowFlags_NoScrollbar);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const char *msg = "No saved nicks yet";
      const char *hint = "Type a name and press Save Nick";
      ImVec2 msgSize = ImGui::CalcTextSize(msg);
      ImVec2 hintSize = ImGui::CalcTextSize(hint);
      ImGui::SetCursorPosX((avail.x - msgSize.x) * 0.5f);
      ImGui::TextDisabled("%s", msg);
      ImGui::SetCursorPosX((avail.x - hintSize.x) * 0.5f);
      ImGui::TextDisabled("%s", hint);
      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
      return;
    }

    // Scrollable list of cards.
    const int visibleRows =
        std::min<int>(6, static_cast<int>(settings.savedNames.size()));
    const float cardH = ui::s(38);
    const float gap = ui::s(6);
    const float listH = visibleRows * cardH + (visibleRows - 1) * gap +
                        ui::s(12);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##altnicklist", ImVec2(0, listH), false);

    int toDelete = -1;
    for (size_t i = 0; i < settings.savedNames.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const std::string &n = settings.savedNames[i];
      const bool isCurrent = (n == settings.spoofedName);

      const ImVec2 cardPos = ImGui::GetCursorScreenPos();
      const float cardW = ImGui::GetContentRegionAvail().x;
      const ImVec2 cardMax(cardPos.x + cardW, cardPos.y + cardH);
      ImDrawList *dl = ImGui::GetWindowDrawList();

      // Full-row invisible button covers the card and drives Use action.
      ImGui::SetCursorScreenPos(cardPos);
      const bool useClicked = ImGui::InvisibleButton("##use", ImVec2(cardW, cardH));
      const bool hovered = ImGui::IsItemHovered();

      // Card background
      ImU32 bg = ui::GetColor(ImGuiCol_FrameBg, hovered ? 0.9f : 0.6f);
      dl->AddRectFilled(cardPos, cardMax, bg, ui::s(9));
      if (isCurrent) {
        // Accent stripe on the left for the currently-selected nick.
        ImVec2 stripeMax(cardPos.x + ui::s(3), cardMax.y);
        dl->AddRectFilled(cardPos, stripeMax, accent, ui::s(2));
      }

      // Nick label, vertically centered
      ImVec2 textSize = ImGui::CalcTextSize(n.c_str());
      ImVec2 textPos(cardPos.x + ui::s(14),
                     cardPos.y + (cardH - textSize.y) * 0.5f);
      ImU32 textCol = ui::GetColor(ImGuiCol_Text, isCurrent ? 1.0f : 0.92f);
      dl->AddText(textPos, textCol, n.c_str());

      // Hover hint on the right ("Click to use")
      if (hovered && !isCurrent) {
        const char *hint = "Click to use";
        ImVec2 hintSize = ImGui::CalcTextSize(hint);
        ImVec2 hintPos(cardMax.x - ui::s(70) - hintSize.x,
                       cardPos.y + (cardH - hintSize.y) * 0.5f);
        dl->AddText(hintPos, textDim, hint);
      } else if (isCurrent) {
        const char *tag = "Active";
        ImVec2 tagSize = ImGui::CalcTextSize(tag);
        ImVec2 tagPos(cardMax.x - ui::s(70) - tagSize.x,
                      cardPos.y + (cardH - tagSize.y) * 0.5f);
        dl->AddText(tagPos, accent, tag);
      }

      // Delete X button on the far right, drawn as its own small item.
      ImGui::SetCursorScreenPos(
          ImVec2(cardMax.x - ui::s(34),
                 cardPos.y + (cardH - ui::s(22)) * 0.5f));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(1.0f, 0.35f, 0.35f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::s(6));
      if (ImGui::Button("X", ImVec2(ui::s(22), ui::s(22)))) {
        toDelete = static_cast<int>(i);
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);

      if (useClicked && !isCurrent) {
        std::strncpy(settings.spoofedName, n.c_str(),
                     sizeof(settings.spoofedName) - 1);
        settings.spoofedName[sizeof(settings.spoofedName) - 1] = '\0';
      }

      // Advance to next card position
      ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardMax.y + gap));
      ImGui::PopID();
    }

    if (toDelete >= 0 &&
        toDelete < static_cast<int>(settings.savedNames.size())) {
      settings.savedNames.erase(settings.savedNames.begin() + toDelete);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, ui::s(6)));
    ImGui::TextDisabled(
        "Applies on next server connect. Offline servers only.");
  });
}

std::string AltManager::getDisplayInfo() {
  if (!validateName(settings.spoofedName))
    return "invalid";
  return std::string(settings.spoofedName);
}

void AltManager::saveSettings(json::Value &node) const {
  node["spoofedName"] = std::string(settings.spoofedName);
  json::Value arr;
  arr.array();  // force type = Array so an empty list still serializes as []
  for (const auto &n : settings.savedNames) {
    arr.push_back(json::Value(n));
  }
  node["savedNames"] = arr;
}

void AltManager::loadSettings(const json::Value &node) {
  std::string current = node["spoofedName"].as_string("");
  if (!current.empty()) {
    std::strncpy(settings.spoofedName, current.c_str(),
                 sizeof(settings.spoofedName) - 1);
    settings.spoofedName[sizeof(settings.spoofedName) - 1] = '\0';
  }
  settings.savedNames.clear();
  if (node.has("savedNames") && node["savedNames"].is_array()) {
    for (const auto &v : node["savedNames"].array()) {
      if (!v.is_string())
        continue;
      std::string s = v.as_string("");
      if (s.empty() || !validateName(s.c_str()))
        continue;
      settings.savedNames.push_back(std::move(s));
      if (settings.savedNames.size() >=
          static_cast<size_t>(MAX_SAVED_NAMES))
        break;
    }
  }
}
