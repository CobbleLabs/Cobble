#pragma once

// Pure navigation transition model for the minimalist menu redesign.
//
// This header is deliberately free of any ImGui dependency and global state so
// that the navigation state machine can be unit- and property-tested in
// isolation. menu.cpp is the single owner that copies between the live `ui::`
// state and `nav::NavState`.
//
// See .kiro/specs/minimalist-menu-redesign/design.md ("Components and
// Interfaces" #2 MenuNav, and "Data Models").

namespace nav {

// Which page the main content area should render for a category.
enum class ViewKind { List, Config, SpecialPage };

// Navigation events produced by the sidebar / list / back control.
enum class Event { SelectCategory, SelectEntry, Back };

// Static facts about a category, supplied by the caller (menu.cpp owns the real
// tab table). Kept as plain data so tests can construct arbitrary cases.
struct CategoryInfo {
    bool isSpecial;   // Configs / Settings
    int  entryCount;  // number of Module_Entry items (m_subtabs.size())
};

// Mirrors the animated state committed in handle_anims(). The `next*` fields are
// the pending values applied by commit() at the animation midpoint.
struct NavState {
    int      category     = 0;
    int      entry        = 0;                 // selected module index within category
    ViewKind view         = ViewKind::List;
    int      nextCategory = 0;
    int      nextEntry     = 0;
    ViewKind nextView      = ViewKind::List;
};

// Pure: compute the pending transition for an event. Does not mutate cur_*.
inline NavState requestTransition(NavState s, Event e, int arg, const CategoryInfo& info) {
    switch (e) {
        case Event::SelectCategory: {
            if (arg != s.category) {
                // Selecting a different category always lands on its list (or
                // its direct page when special). entry/nextEntry unchanged.
                s.nextCategory = arg;
                s.nextView     = info.isSpecial ? ViewKind::SpecialPage : ViewKind::List;
            } else if (!info.isSpecial && s.view == ViewKind::Config) {
                // Re-selecting the active module category while in Config
                // returns to its list. Idempotent no-op when already in List.
                s.nextView = ViewKind::List;
            }
            break;
        }
        case Event::SelectEntry: {
            // Only meaningful for module categories. Clamp the requested index
            // into the valid range; if there are no entries, keep state.
            if (info.entryCount > 0) {
                int clamped = arg;
                if (clamped < 0) clamped = 0;
                if (clamped >= info.entryCount) clamped = info.entryCount - 1;
                s.nextEntry = clamped;
                s.nextView  = ViewKind::Config;
            }
            break;
        }
        case Event::Back: {
            // Return to the list; preserve the category and entry selection.
            s.nextView = ViewKind::List;
            break;
        }
    }
    return s;
}

// Pure: commit pending -> current (what handle_anims does at the midpoint).
inline NavState commit(NavState s) {
    s.category = s.nextCategory;
    s.entry    = s.nextEntry;
    s.view     = s.nextView;
    return s;
}

// Pure: the view that should currently be rendered for a category.
inline ViewKind resolveView(const NavState& s, const CategoryInfo& info) {
    if (info.isSpecial) return ViewKind::SpecialPage;
    return s.view;
}

// Two-column split: returns the index that partitions n ordered entries into
// left = entries[0, split) and right = entries[split, n). The left column gets
// the leading prefix; odd counts bias the extra card to the left column.
// left ++ right preserves the original order.
inline int columnSplit(int n) {
    if (n <= 0) return 0;
    return (n + 1) / 2;  // ceil(n/2)
}

} // namespace nav
