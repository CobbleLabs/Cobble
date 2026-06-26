# Implementation Plan: Minimalist Menu Redesign

## Overview

This plan implements the menu redesign incrementally and test-first, in C++20 (the
language the design uses). It starts from the pure, ImGui-free navigation model
(`nav::`) so the transition logic can be locked down with property-based tests before
any rendering changes are made. It then layers the view state onto the existing tab
system, extends the single animation commit point, simplifies the sidebar, and finally
splits the main content area into the `Module_List_View` and `Module_Config_View`,
updates the header, and verifies routing parity.

All rendering work stays on the OpenGL render thread, uses `ui::` wrappers only, keeps
routing centralized in `menu.cpp` (`getModuleForSubtab`), and is cosmetic-only — no
validation, handshake, integrity, threading, JNI, or persistence paths are touched
(CLAUDE.md constraints; Requirements 10.1–10.4).

## Tasks

- [x] 1. Create the pure navigation model
  - [x] 1.1 Implement `nav::` model in `src/ui/menu/menu_nav.hpp`
    - Create a header-only, ImGui-free file with `enum class ViewKind { List, Config, SpecialPage }`, `enum class Event { SelectCategory, SelectEntry, Back }`, and the plain-data structs `CategoryInfo { bool isSpecial; int entryCount; }` and `NavState { int category; int entry; ViewKind view; int nextCategory; int nextEntry; ViewKind nextView; }`.
    - Implement `NavState requestTransition(NavState s, Event e, int arg, const CategoryInfo& info)` with the design's rules: `SelectCategory(c)` to a different category sets `nextCategory=c` and `nextView=SpecialPage` if special else `List`; `SelectCategory(c)` where `c==category` on a module category in `Config` sets `nextView=List` (idempotent no-op when already `List`); `SelectEntry(i)` clamps `i` to `[0, entryCount)`, sets `nextEntry` and `nextView=Config`; `Back` sets `nextView=List` preserving `nextCategory`/`nextEntry`.
    - Implement `NavState commit(NavState s)` that copies `next* -> cur*` (mirrors the `handle_anims` midpoint swap) and `ViewKind resolveView(const NavState& s, const CategoryInfo& info)` returning `SpecialPage` for special categories, otherwise `s.view`.
    - Keep the file free of ImGui includes and global state; no `#include "imgui.h"`.
    - _Requirements: 2.4, 3.1, 3.3, 3.4, 4.4, 5.2, 5.3, 5.4, 6.1, 6.2, 10.3_

- [x] 2. Stand up property-based tests for the navigation model
  - [x] 2.1 Add a C++ property-test target wired to RapidCheck
    - Create a standalone test target (e.g., `tests/menu_nav/` with its own MSBuild project added to the solution, or a CMake test target) that compiles `menu_nav.hpp` and links RapidCheck. Do not implement PBT from scratch — depend on the RapidCheck library.
    - Provide RapidCheck generators for arbitrary `CategoryInfo` (mix of module and special categories, including `entryCount == 0` unresolved/empty cases) and arbitrary `Event` sequences with valid and invalid `arg` indices.
    - Configure each property to run a minimum of 100 iterations.
    - _Requirements: 10.3_

  - [ ]* 2.2 Write property test for category landing view (its own test file)
    - **Property 1: Selecting a category determines the landing view**
    - **Validates: Requirements 2.4, 6.1, 6.2**
    - Tag: `// Feature: minimalist-menu-redesign, Property 1: Selecting a category determines the landing view`

  - [ ]* 2.3 Write property test for list contents and order (its own test file)
    - **Property 2: List view renders exactly the active category's entries in order**
    - **Validates: Requirements 2.1, 2.2, 2.3, 2.5, 5.4**
    - Tag: `// Feature: minimalist-menu-redesign, Property 2: List view renders exactly the active category's entries in order`

  - [ ]* 2.4 Write property test for entry selection routing key (its own test file)
    - **Property 3: Selecting an entry opens its config with a single routing key**
    - **Validates: Requirements 3.1, 3.3, 3.4**
    - Tag: `// Feature: minimalist-menu-redesign, Property 3: Selecting an entry opens its config with a single routing key`

  - [ ]* 2.5 Write property test for Back returning to the list (its own test file)
    - **Property 4: Back returns to the list for the same category**
    - **Validates: Requirements 5.2, 5.4**
    - Tag: `// Feature: minimalist-menu-redesign, Property 4: Back returns to the list for the same category`

  - [ ]* 2.6 Write property test for re-selecting the active category (its own test file)
    - **Property 5: Re-selecting the active category in config returns to its list**
    - **Validates: Requirements 5.3**
    - Tag: `// Feature: minimalist-menu-redesign, Property 5: Re-selecting the active category in config returns to its list`

  - [ ]* 2.7 Write property test for special-category direct rendering (its own test file)
    - **Property 6: Special categories always render their direct page**
    - **Validates: Requirements 6.1, 6.2**
    - Tag: `// Feature: minimalist-menu-redesign, Property 6: Special categories always render their direct page`

  - [ ]* 2.8 Write property test for pending transitions completing (its own test file)
    - **Property 7: Pending transitions always complete**
    - **Validates: Requirements 4.4**
    - Tag: `// Feature: minimalist-menu-redesign, Property 7: Pending transitions always complete`

- [x] 3. Checkpoint - navigation model verified
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Add per-category view state to the tab system
  - [x] 4.1 Extend `tab_t` with view state in `src/ui/widgets/widgets.hpp`
    - Add `enum class CategoryView { List, Config };` to the `ui` namespace.
    - Add `CategoryView cur_view = CategoryView::List;` and `CategoryView next_view = CategoryView::List;` to `struct tab_t`, leaving the existing constructor and `cur_subtab`/`next_subtab` fields unchanged.
    - _Requirements: 3.1, 5.2, 5.3_

- [x] 5. Commit the view at the animation midpoint
  - [x] 5.1 Extend `ui::handle_anims()` in `src/ui/widgets/widgets.cpp`
    - At the existing `content_anim` midpoint commit (where `tabs[cur_tab].cur_subtab = tabs[cur_tab].next_subtab`), also swap `tabs[cur_tab].cur_view = tabs[cur_tab].next_view` under the same `cur_tab < tabs.size()` guard.
    - Keep `content_anim_dst` reset to `1.f` so the swap is guaranteed on a subsequent frame even if the fade is degenerate (backs Req 4.4); do not add any new animation timers.
    - _Requirements: 4.1, 4.2, 4.3, 4.4_

- [x] 6. Simplify the sidebar to select-only categories
  - [x] 6.1 Rework `ui::tab(int)` in `src/ui/widgets/widgets.cpp`
    - Remove the block that renders `ui::subtab(...)` rows when a category is selected (no module rows in the sidebar) and remove the rotating expansion-arrow affordance for tabs with subtabs.
    - Keep the category row's icon + label + selected/hover animation (`anim_obj` + `animate`) and the bottom-pinned Settings tab + separator layout unchanged.
    - Click behavior: clicking a different category keeps `next_tab = num; anim_dst = 0.f;`. Clicking the already-active module category while `tabs[num].cur_view == CategoryView::Config` issues a Back-equivalent: `tabs[num].next_view = CategoryView::List; content_anim_dst = 0.f;`.
    - Do not add module-specific logic here; keep routing in `menu.cpp`.
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 5.3, 10.1, 10.3_

- [x] 7. Checkpoint - sidebar and view state wired
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Render the module list view
  - [x] 8.1 Implement `renderModuleList(int tab)` in `src/ui/menu/menu.cpp`
    - Iterate `ui::tabs[tab].m_subtabs` in order, rendering one selectable `Module_Entry` per item with `ui::` wrappers inside a `ui::section`/`ui::begin_child` container so it fades with the menu. Display name is the existing `m_subtabs[i]` string.
    - For each entry resolve `Module* m = getModuleForSubtab(tab, i)`; if `m && m->showsDeveloperOnly()` render it in the existing locked (grayed) presentation, and keep it selectable.
    - On selecting entry `i`, set `ui::tabs[tab].next_subtab = i; ui::tabs[tab].next_view = CategoryView::Config; ui::content_anim_dst = 0.f;` (the `SelectEntry(i)` mapping).
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 3.1, 8.1, 10.1, 10.3_

- [x] 9. Render the module config view
  - [x] 9.1 Implement `renderModuleConfig(int tab)` in `src/ui/menu/menu.cpp`
    - Render a `Back_Control` at the top using a `ui::` control; activating it sets `ui::tabs[tab].next_view = CategoryView::List; ui::content_anim_dst = 0.f;`.
    - Move the existing per-module body in as-is: `Module* mod = getModuleForSubtab(ui::cur_tab, tab.cur_subtab)`; `mod && !mod->isAccessible()` renders the existing unlock prompt verbatim; accessible `mod` calls `mod->onRenderMenu()`; `mod == nullptr` with Visuals subtab 7 renders the inline ArrayList page and subtab 9 the inline Notifications page; set `SetDescriptionContext` per branch exactly as today.
    - For any other unresolved `(category, entry)` (null module, not a known inline page), render a neutral placeholder ("Couldn't load this page.") using `ui::` text while leaving the list entry selectable.
    - _Requirements: 3.2, 5.1, 5.2, 7.2, 7.3, 7.4, 7.5, 8.2, 8.3, 10.1, 10.3, 10.4_

- [x] 10. Dispatch list vs config in `renderPage`
  - [x] 10.1 Update `renderPage()` branching in `src/ui/menu/menu.cpp`
    - Keep the special-category handling unchanged: Settings (`settingsIdx`) renders `renderSettingsPage()`; Configs (`configsIdx`) preserves the one-shot `g_configsNeedsRefresh` behavior and renders `renderConfigsPage()`.
    - For a module category, branch on `ui::tabs[ui::cur_tab].cur_view`: `List` calls `renderModuleList(ui::cur_tab)`, `Config` calls `renderModuleConfig(ui::cur_tab)`.
    - Preserve the existing `cur_tab < tabs.size()` and `cur_subtab < m_subtabs.size()` bounds guards.
    - _Requirements: 3.4, 6.1, 6.2, 6.3, 10.3_

- [x] 11. Reflect the current view in the header
  - [x] 11.1 Update header rendering in `src/ui/menu/menu.cpp`
    - `Module_List_View`: draw the active category name only (no breadcrumb).
    - `Module_Config_View`: draw category name + arrow + selected module name (`m_subtabs[cur_subtab]`), gating the existing breadcrumb visuals on `tabs[cur_tab].cur_view == CategoryView::Config`.
    - `Special_Category`: draw the category name only. Keep the breadcrumb fading with `ui::anim * ui::content_anim`.
    - _Requirements: 9.1, 9.2, 9.3_

- [x] 12. Checkpoint - rendering complete
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 13. Routing parity and config-dispatch example tests
  - [ ]* 13.1 Write routing-parity example test
    - Table-driven test asserting `getModuleForSubtab(category, entry)` returns identical mappings for all `(category, entry)` pairs, including the `nullptr` sentinels at (1,7) ArrayList and (1,9) Notifications and the real `droppedInfo` at (1,8).
    - _Requirements: 3.2, 3.3, 7.1, 7.2, 7.3, 7.4_

  - [ ]* 13.2 Write config-dispatch and placeholder example tests
    - With a mock `Module`, assert the config body calls `onRenderMenu()` for a resolvable accessible module, renders the unlock prompt for a locked/inaccessible module, and renders the placeholder for an unknown null entry; assert the header shows category-only for list/special and category+module for config.
    - _Requirements: 7.5, 8.1, 8.2, 8.3, 9.1, 9.2, 9.3_

- [x] 14. Final checkpoint - build and verify
  - [x] 14.1 Build the dev configuration and resolve any compile/link issues
    - Build via the project's dev path (`.\build.ps1 -Build`) and fix any errors introduced by the changes; confirm `menu_nav.hpp` and the modified `widgets.cpp`/`menu.cpp` compile cleanly.
    - Ensure all property tests and example tests pass.
    - _Requirements: 10.1, 10.2, 10.3, 10.4_

## Revision: Card-Based UI Refinements

The base redesign above (tasks 1–14) is implemented and complete. The tasks below
layer the six refinements from the updated design onto it: a single-sourced larger
corner rounding, a name-toggle / gear-config split, rounded spaced cards, a two-column
grid, and a vertically centered sidebar group. All work stays cosmetic, on the GL render
thread, uses `ui::` wrappers / the drawlist + `ButtonBehavior` pattern, reuses the
existing `Module::toggle()` path (no new JNI), and keeps routing centralized in
`menu.cpp` (CLAUDE.md constraints).

- [ ] 15. Increase corner rounding from a single source (`theme.hpp` + `theme.cpp`)
  - [ ] 15.1 Add `ui::theme::rounding` base-radius constants and consume them in `theme::apply()`
    - In `src/ui/widgets/theme.hpp`, add `namespace ui::theme::rounding` with `inline constexpr float` base radii: `Window = 10.f`, `Child = 8.f`, `Frame = 6.f`, `Grab = 6.f`, `Popup = 10.f`, `Scrollbar = 6.f`, `ModuleCard = 8.f` (single place to tune softness).
    - In `src/ui/widgets/theme.cpp::apply()`, replace the hardcoded `s(4)/s(3)/...` rounding literals so each style field reads its constant scaled by `ui::s`: `style.WindowRounding = s(rounding::Window)`, `ChildRounding = s(rounding::Child)`, `FrameRounding = s(rounding::Frame)`, `GrabRounding = s(rounding::Grab)`, `PopupRounding = s(rounding::Popup)`, `ScrollbarRounding = s(rounding::Scrollbar)`.
    - Do not touch the existing draw calls in `menu.cpp`/`widgets.cpp` — they already read `GImGui->Style.*Rounding`, so they inherit the larger radii automatically. Only `ModuleCard` is consumed at draw time (task 17.1).
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [ ]* 15.2 Write rounding-constants example test
    - Assert each new base radius is strictly greater than its pre-revision counterpart (Window 10 > 4, Child 8 > 3, Frame 6 > 3, Grab 6 > 3, Popup 10 > 4, Scrollbar 6 > 3, ModuleCard 8 > old row radius 4). Pure compile-time/value check; no ImGui context required.
    - _Requirements: 7.4_

- [ ] 16. Add the pure two-column split helper
  - [ ] 16.1 Implement `columnSplit(int n)` as a header-only, ImGui-free helper
    - Add `inline int columnSplit(int n) { return (n + 1) / 2; }` (== `ceil(n/2)`) so the left column gets the leading prefix and odd counts bias the extra card left. Place it where it is testable and free of any ImGui include — e.g. in `src/ui/menu/menu_nav.hpp` (already pure and on the test target's include path) or a small `layout.hpp` header next to it.
    - Document the partition contract in a comment: `left = entries[0, split)`, `right = entries[split, n)`; `left ++ right` preserves original order.
    - _Requirements: 6.2, 6.3_

  - [ ]* 16.2 Write property test for the two-column split (its own test file in `tests/menu_nav/`)
    - **Property 8: Two-column split preserves order and biases the extra card left**
    - **Validates: Requirements 6.2, 6.3**
    - RapidCheck over arbitrary `n >= 0` (include n = 0, 1, even, odd), minimum 100 iterations; assert (a) `left ++ right` equals the original sequence, (b) `left` is the leading prefix, (c) `|left| - |right|` is 1 for odd `n` and 0 for even `n`. Include `generators.hpp`; add the new `.cpp` to `menu_nav_tests.vcxproj`.
    - Tag: `// Feature: minimalist-menu-redesign, Property 8: Two-column split preserves order and biases the extra card left`

- [ ] 17. Rebuild the module list as a two-column grid of rounded cards (`menu.cpp::renderModuleList`)
  - [ ] 17.1 Rebuild `renderModuleList(int tab)` into a `Two_Column_Grid` of `Module_Card`s
    - Keep the `ui::section`/`ui::begin_child` container so the grid fades with the menu, and stay on the drawlist + `ButtonBehavior` pattern (no raw interactive `ImGui::` widgets).
    - Geometry: `n = t.m_subtabs.size()`, `split = columnSplit(n)` (left = `[0, split)` = `ceil(N/2)` cards, right = `[split, n)`); `cardW = (avail - gapX) / 2`, with a horizontal `gapX` between columns and a vertical `cardGapY` between cards. Render left and right as two side-by-side `BeginGroup`/`EndGroup` columns.
    - Each `Module_Card`: draw a rounded rect background using `ui::s(ui::theme::rounding::ModuleCard)`. Resolve `Module* m = getModuleForSubtab(tab, i)`; locked (`m && m->showsDeveloperOnly()`) cards render grayed; enabled cards (`m && m->isEnabled()`) show the accent presentation reflecting the live enabled state.
    - Two disjoint hit areas with distinct ImGui IDs over non-overlapping sub-rects: a `Name_Region` (left portion, id `#name`) whose press calls `m->toggle()` guarded by `if (m)` — locked modules early-return so state is unchanged; and a `Gear_Control` (right portion, id `#name##gear`, drawing `ICON_FA_COG`) whose press sets `t.next_subtab = i; t.next_view = ui::CategoryView::Config; ui::content_anim_dst = 0.f;`. Register the gear sub-rect first so it owns its region unambiguously, then the name sub-rect over the remaining area.
    - Null-module entries (Visuals ArrayList = subtab 7, Notifications = subtab 9): `Name_Region` press is a no-op (the `if (m)` guard skips), `Gear_Control` opens the inline config as usual; render neutral (never accent/enabled) presentation. DroppedInfo (subtab 8) is a real module and behaves like any other card.
    - _Requirements: 2.1, 2.2, 2.3, 3.1, 3.2, 3.3, 3.4, 4.1, 4.2, 5.1, 5.2, 5.3, 6.1, 6.2, 6.3, 6.4, 7.3, 11.1, 11.2, 11.3, 12.1, 14.1, 14.3_

  - [ ]* 17.2 Write name-toggle wiring example test
    - With a mock `Module`, simulate a `Name_Region` activation and assert `toggle()`/`setEnabled()` is invoked exactly once on `getModuleForSubtab(category, entry)`, nav state (category/entry/view) is unchanged, and a locked mock leaves `enabled` unchanged; a `nullptr` entry produces no toggle.
    - _Requirements: 3.1, 3.4, 14.3_

  - [ ]* 17.3 Write hit-area separation example test
    - Assert the `Name_Region` and `Gear_Control` sub-rects are disjoint: a press inside the name rect fires only a toggle (no view change); a press inside the gear rect fires only the `SelectEntry`/config transition (no toggle). Assert mutual exclusivity for any point.
    - _Requirements: 5.3_

- [ ] 18. Vertically center the sidebar category group (`menu.cpp::Menu::Render` "tabs" child)
  - [ ] 18.1 Center the whole `Category_Group` (Settings inline) instead of bottom-pinning Settings
    - In the "tabs" child, remove the bottom-pin block (the `Dummy` spacer sized from `availableSpace`, the separator line above Settings, the `s(4)` `Dummy`, and the separate `ui::tab(tabs.size()-1)` call). Render every category, Settings included, in one group.
    - Compute `rowH = ui::s(38)` (the `ui::tab` row height), `tabCount = ui::tabs.size()`, `groupH = rowH * tabCount`, `availH = ImGui::GetContentRegionAvail().y`, `topOffset = max(0.f, (availH - groupH) * 0.5f)`. Emit `ImGui::Dummy({0, topOffset})`, then `BeginGroup()` / loop `ui::tab(i)` for `i in [0, tabCount)` / `EndGroup()`.
    - Clamp `topOffset` to `>= 0` so a short panel top-aligns rather than clipping. Leave the brand area, the tabs-child background fill, and per-tab rendering unchanged.
    - _Requirements: 1.4, 1.6_

  - [ ]* 18.2 Write centering-offset example test
    - Unit-check the pure offset formula `topOffset = max(0, (availH - rowH*tabCount)/2)`: centers for tall panels (positive offset) and clamps to 0 for short panels (`availH < groupH`).
    - _Requirements: 1.6_

- [ ] 19. Final checkpoint - build and verify the revision
  - [ ] 19.1 Build the dev configuration and run the navigation tests
    - Build via `.\build.ps1 -Build` and fix any compile/link issues introduced by the rounding, card grid, and sidebar changes; confirm `theme.cpp`, `menu.cpp`, and the split helper compile cleanly.
    - Run the `menu_nav` test target and ensure all property tests (including Property 8) and example tests pass.
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 14.1, 14.2, 14.5, 14.6_

## Notes

- Tasks marked with `*` are optional test sub-tasks and can be skipped for a faster MVP; core implementation tasks are never optional.
- Property tests (2.2–2.8) target only the pure `nav::` model and each implements exactly one design property over a minimum of 100 RapidCheck iterations.
- ImGui-dependent behavior (sidebar, header, list/config dispatch, fades, special pages) is verified with example tests and visual review, not property tests.
- Every task references specific requirements for traceability and keeps routing centralized in `menu.cpp` per CLAUDE.md.
- Checkpoints provide incremental validation between the model, sidebar, and rendering stages.
- Revision tasks 15–19 layer the card-based refinements on top of the completed base redesign (tasks 1–14). Property 8 covers the new pure `columnSplit`; the name-toggle, hit-area separation, rounding constants, and centering offset are covered by example tests.

## Task Dependency Graph

The graph below schedules only the new, not-started revision tasks (15–19). The base
redesign tasks (1.1–14.1) are already implemented and are intentionally excluded so they
are not re-run. Tasks 17.1 and 18.1 both edit `menu.cpp`, so they are placed in separate
waves to avoid write conflicts.

```json
{
  "waves": [
    { "id": 0, "tasks": ["15.1", "16.1", "17.1"] },
    { "id": 1, "tasks": ["18.1", "15.2", "16.2", "17.2", "17.3"] },
    { "id": 2, "tasks": ["18.2"] },
    { "id": 3, "tasks": ["19.1"] }
  ]
}
```
