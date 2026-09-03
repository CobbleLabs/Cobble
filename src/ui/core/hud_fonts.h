#pragma once

// ============================================================================
// HUD font registry
//
// The six fixed atlas slots (font(0)..font(5)) are addressed by hardcoded index
// all over the UI, so they must never move. Anything the user can *pick* for a
// HUD element goes through this registry instead.
//
// Families come from two sources:
//   - the two embedded Inter faces already in the atlas (Regular = slot 5,
//     Bold = slot 2), which are always present, and
//   - system faces loaded at startup from %WINDIR%\Fonts, so the repo carries no
//     extra font binaries and nothing is redistributed. A candidate that isn't
//     installed is simply skipped.
//
// Settings persist the stable candidate ID (`Id`), never the combo row, because
// the set of installed faces differs per machine and a row index would silently
// re-point a saved config at a different font. IDs 0 and 1 are the two Inter
// faces, which is what makes the old ArrayList `fontStyle` values (0 = Regular,
// 1 = Bold) keep resolving to the same thing.
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"

namespace hud_fonts {

// Stable IDs for the always-present embedded faces. System faces get IDs from
// kFirstSystemId upward, fixed by the candidate table in hud_fonts.cpp's owner
// (overlay.cpp) — never by load order.
constexpr int kInterRegular = 0;
constexpr int kInterBold = 1;

// Loads the system candidates into `atlas`. Must run while the atlas is still
// unbuilt, after the six fixed slots have been added.
void Register(ImFontAtlas* atlas);

// Number of selectable families (>= 2).
int Count();

// Display names, one per combo row, for ui::Combo. Stable for the process.
const char* const* Names();

// combo row <-> stable ID.
int IdAt(int comboIndex);
int ComboIndexOf(int id);

// Font for a stable ID. Falls back to Inter Regular (then Bold) if the ID names
// a family this machine doesn't have, so a config from another PC still draws.
ImFont* GetById(int id);

}  // namespace hud_fonts
