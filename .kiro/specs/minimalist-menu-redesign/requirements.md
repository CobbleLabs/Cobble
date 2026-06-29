# Requirements Document

## Introduction

This feature redesigns the existing ImGui overlay menu (the injectable DLL's OpenGL2 overlay) to present a cleaner, more minimalist navigation model, and refines that redesign with a softer, more card-based look and more direct module interactions.

Today the left navigation sidebar lists top-level categories (Combat, Visuals, Movement, Utility, Configs, Settings) AND expands each category into per-module subtab rows directly in the sidebar. Selecting a subtab renders that module's configuration in the main content area.

The redesign moves module selection out of the sidebar and into the main content area:

- The sidebar shows only the top-level categories.
- Selecting a category shows that category's modules as a selectable list in the main content area.
- A back control (and re-selecting the active category) returns to the module list.

The refined look and interactions layered on top of that model are:

- **Softer corners.** The menu window, its containers, and the module cards/rows use a larger corner rounding for a softer, more minimalist appearance.
- **Click name to toggle.** In the module list, clicking a module's name toggles that module's enabled state on or off. This replaces the previous behavior where selecting the entry opened the module's configuration.
- **Gear opens configuration.** Each module entry shows a gear (cog) control that opens the module's configuration page. The gear replaces the chevron/arrow affordance previously shown on the entry.
- **Module cards.** Each module entry is rendered as a separated, rounded card with visible spacing between modules instead of flush rows.
- **Two-column grid.** The module list arranges modules in a two-column grid (for example, Combat's ten modules render five cards in the left column and five in the right column).
- **Centered categories.** The category items in the sidebar are vertically centered as a single group, and Settings sits together with the other categories instead of being pinned to the bottom.

The redesign is intended to be incremental and low-risk. Existing module configuration rendering (`Module::onRenderMenu`), centralized routing (`getModuleForSubtab`), the special pages (ArrayList, Notifications, DroppedInfo, Configs, Settings), the menu fade animation, and config persistence MUST all continue to work unchanged.

## Glossary

- **Menu**: The ImGui overlay window rendered on the OpenGL render thread (`Menu::Render` in `src/ui/menu/menu.cpp`).
- **Navigation_Sidebar**: The left-hand panel that lists top-level categories.
- **Category**: A top-level grouping shown in the Navigation_Sidebar. Categories are Combat, Visuals, Movement, Utility, Configs, and Settings.
- **Module_Category**: A Category whose entries are modules. Module_Categories are Combat, Visuals, Movement, and Utility.
- **Special_Category**: A Category that has no module list and renders a dedicated page directly. Special_Categories are Configs and Settings.
- **Category_Group**: The single contiguous set of all Category rows rendered in the Navigation_Sidebar, including Settings.
- **Module_Entry**: A selectable item shown in the Module_List_View that represents one module (or one special module page) belonging to the active Module_Category. A Module_Entry is composed of a Name_Region and a Gear_Control.
- **Module_Card**: The separated, rounded container that renders a single Module_Entry in the Module_List_View, with spacing around it that visually separates it from adjacent Module_Entry items.
- **Name_Region**: The clickable area of a Module_Entry that displays the module name. Activating the Name_Region toggles the module's enabled state.
- **Gear_Control**: The gear (cog) icon control on a Module_Entry. Activating the Gear_Control opens the Module_Config_View for that module. The Gear_Control replaces the chevron/arrow affordance used by the earlier redesign.
- **Module_Toggle**: The act of flipping a module's enabled state through the existing `Module::toggle()` / `Module::setEnabled()` path.
- **Module_List_View**: The main content area state that displays the Module_Entry items for the active Module_Category.
- **Two_Column_Grid**: The layout used by the Module_List_View that arranges Module_Card items into a left column and a right column.
- **Module_Config_View**: The main content area state that displays the configuration of a single selected module.
- **Special_Module_Page**: An entry that has no backing `Module*` (resolves to `nullptr` through Module_Routing) and is rendered with dedicated inline UI. Special_Module_Pages are ArrayList and Notifications within the Visuals category. DroppedInfo is a regular module (it has a backing `Module*`) and is not a Special_Module_Page.
- **Back_Control**: The control in the Module_Config_View that returns the Menu to the Module_List_View.
- **View_Transition**: The animation played when the Menu changes between the Module_List_View and the Module_Config_View.
- **Module_Routing**: The existing `getModuleForSubtab(tabIdx, subtabIdx)` mechanism in `menu.cpp` that maps a category index and an entry index to a `Module*`.
- **Corner_Rounding**: The corner radius applied to the Menu window, its containers, and the Module_Card items.
- **UI_Wrappers**: The `ui::` widget and layout helpers in `src/ui/widgets/widgets.hpp` (for example `ui::Checkbox`, `ui::SliderFloat`, `ui::Combo`, `ui::two_column`, `ui::s`).

## Requirements

### Requirement 1: Category-only navigation sidebar

**User Story:** As a user, I want the sidebar to list only top-level categories, so that the navigation looks clean and uncluttered.

#### Acceptance Criteria

1. THE Navigation_Sidebar SHALL render each Category as a single selectable row.
2. WHILE a Category is selected, THE Navigation_Sidebar SHALL keep that Category's row collapsed without rendering any Module_Entry rows beneath it.
3. THE Navigation_Sidebar SHALL render the Combat, Visuals, Movement, Utility, Configs, and Settings categories.
4. THE Navigation_Sidebar SHALL render the Settings Category as part of the Category_Group together with the other categories, without pinning the Settings Category to the bottom of the panel.
5. THE Navigation_Sidebar SHALL provide no expansion affordance on a Category row, so that a Category can only be selected and never expanded to reveal Module_Entry rows.
6. THE Navigation_Sidebar SHALL position the Category_Group so that the group is vertically centered within the Navigation_Sidebar content area rather than top-aligned.

### Requirement 2: Module list in the main content area

**User Story:** As a user, I want to see a category's modules in the main content area, so that I can pick a module after choosing a category.

#### Acceptance Criteria

1. WHEN a Module_Category is selected in the Navigation_Sidebar, THE Module_List_View SHALL display one Module_Entry for every module that belongs to that Module_Category in the main content area.
2. THE Module_List_View SHALL order the Module_Entry items using the same order defined by the existing category entry list for that Module_Category.
3. THE Module_List_View SHALL render the displayed name of each Module_Entry in its Name_Region.
4. WHEN a different Module_Category is selected, THE Module_List_View SHALL replace its Module_Entry items with the modules of the newly selected Module_Category.
5. IF the Module_List_View cannot resolve the modules of a newly selected Module_Category, THEN THE Module_List_View SHALL continue to display the previously resolved Module_Entry items until the resolution succeeds.

### Requirement 3: Toggle a module from the list

**User Story:** As a user, I want to enable or disable a module by clicking its name in the list, so that I can switch modules on and off quickly without opening a page.

#### Acceptance Criteria

1. WHEN the Name_Region of a Module_Entry is activated in the Module_List_View, THE Menu SHALL perform a Module_Toggle on the corresponding module.
2. WHEN a Module_Toggle changes a module's enabled state, THE Module_List_View SHALL render the Module_Entry in a presentation that reflects the new enabled state.
3. THE Menu SHALL resolve the activated Module_Entry to its module through Module_Routing using the active category index and the entry index.
4. WHERE a module is locked and not unlocked, IF the Name_Region of that module's Module_Entry is activated, THEN THE Menu SHALL leave the module's enabled state unchanged.
5. WHEN a Module_Toggle changes a module's enabled state from the Module_List_View, THE Menu SHALL persist the resulting enabled state through the existing configuration persistence path.

### Requirement 4: Open a module configuration via the gear control

**User Story:** As a user, I want to open a module's settings by clicking its gear icon, so that toggling the module and opening its configuration are separate actions.

#### Acceptance Criteria

1. THE Module_List_View SHALL render a Gear_Control on each Module_Entry in place of the chevron/arrow affordance used by the earlier redesign.
2. WHEN the Gear_Control of a Module_Entry is activated, THE Menu SHALL transition to the Module_Config_View for the corresponding module.
3. WHILE the Module_Config_View is active for a module that has a backing `Module*`, THE Module_Config_View SHALL render that module's `onRenderMenu` content.
4. THE Menu SHALL resolve the selected Module_Entry to its module through Module_Routing using the active category index and the entry index.
5. WHILE the Module_Config_View is active, THE Menu SHALL render exactly one module configuration at a time.

### Requirement 5: Module cards with spacing

**User Story:** As a user, I want each module shown as its own separated card, so that the list looks distinct and easy to scan.

#### Acceptance Criteria

1. THE Module_List_View SHALL render each Module_Entry inside a Module_Card with a rounded background.
2. THE Module_List_View SHALL render spacing around each Module_Card so that adjacent Module_Card items are visually separated rather than flush.
3. THE Module_Card SHALL contain both the Name_Region and the Gear_Control of its Module_Entry.

### Requirement 6: Two-column grid layout

**User Story:** As a user, I want the module cards arranged in two columns, so that I can see more modules at once with less scrolling.

#### Acceptance Criteria

1. WHILE a Module_Category is displayed in the Module_List_View, THE Module_List_View SHALL arrange the Module_Card items into a Two_Column_Grid with a left column and a right column.
2. THE Module_List_View SHALL fill the left column with the leading Module_Card items in category entry order before placing the remaining Module_Card items in the right column.
3. WHERE a Module_Category contains an odd number of Module_Entry items, THE Module_List_View SHALL place the additional Module_Card in the left column.
4. THE Module_List_View SHALL render horizontal spacing between the left column and the right column of the Two_Column_Grid.

### Requirement 7: Increased corner rounding

**User Story:** As a user, I want softer, more rounded corners throughout the menu, so that the interface feels more minimalist and polished.

#### Acceptance Criteria

1. THE Menu SHALL render the menu window with rounded corners.
2. THE Menu SHALL render the Navigation_Sidebar, the header, and the content containers with rounded corners.
3. THE Module_List_View SHALL render each Module_Card with rounded corners.
4. THE Menu SHALL apply a Corner_Rounding radius to the window, the containers, and the Module_Card items that is larger than the corresponding radius used before this refinement.

### Requirement 8: Animated transition between list and configuration

**User Story:** As a user, I want a smooth animation when opening and closing a module page, so that the interface feels polished.

#### Acceptance Criteria

1. WHEN the Menu transitions from the Module_List_View to the Module_Config_View, THE View_Transition SHALL animate the change.
2. WHEN the Menu transitions from the Module_Config_View to the Module_List_View, THE View_Transition SHALL animate the change.
3. WHILE a View_Transition is in progress, THE Menu SHALL apply the existing menu alpha and fade behavior so that the transitioning content fades consistently with the rest of the Menu.
4. IF a View_Transition animation fails to start or complete, THEN THE Menu SHALL still complete the change between the Module_List_View and the Module_Config_View so that navigation always succeeds.

### Requirement 9: Return to the module list

**User Story:** As a user, I want to go back to the module list, so that I can choose a different module without leaving the category.

#### Acceptance Criteria

1. WHILE the Module_Config_View is active, THE Module_Config_View SHALL display a Back_Control.
2. WHEN the Back_Control is activated, THE Menu SHALL transition to the Module_List_View for the active Module_Category.
3. WHEN the active Module_Category is selected again in the Navigation_Sidebar while the Module_Config_View is active, THE Menu SHALL transition to the Module_List_View for that Module_Category.
4. WHEN the Menu returns to the Module_List_View, THE Module_List_View SHALL display the Module_Entry items of the active Module_Category.

### Requirement 10: Special categories render directly

**User Story:** As a user, I want the Configs and Settings pages to open directly, so that they keep working as before.

#### Acceptance Criteria

1. WHEN the Configs Category is selected, THE Menu SHALL render the existing Configs page in the main content area without displaying a Module_List_View.
2. WHEN the Settings Category is selected, THE Menu SHALL render the existing Settings page in the main content area without displaying a Module_List_View.
3. WHEN the Configs Category is selected, THE Menu SHALL preserve the existing one-shot configuration refresh behavior on entry to the Configs page.

### Requirement 11: Special module pages preserved

**User Story:** As a user, I want the ArrayList, Notifications, and DroppedInfo settings to remain available, so that no functionality is lost in the redesign.

#### Acceptance Criteria

1. THE Module_List_View for the Visuals Module_Category SHALL include a Module_Entry for ArrayList, a Module_Entry for DroppedInfo, and a Module_Entry for Notifications.
2. WHEN the Gear_Control of the ArrayList Module_Entry is activated, THE Module_Config_View SHALL render the existing ArrayList configuration content.
3. WHEN the Gear_Control of the Notifications Module_Entry is activated, THE Module_Config_View SHALL render the existing Notifications configuration content.
4. WHEN the Gear_Control of the DroppedInfo Module_Entry is activated, THE Module_Config_View SHALL render the DroppedInfo module configuration content.
5. IF the configuration content for a selected Module_Entry cannot be rendered, THEN THE Module_Config_View SHALL display a placeholder message indicating the content cannot be loaded while keeping the Module_Entry available in the Module_List_View.

### Requirement 12: Locked module handling

**User Story:** As a user, I want locked modules to behave the same as today, so that access control is preserved.

#### Acceptance Criteria

1. WHERE a module is marked developer-only, THE Module_List_View SHALL render its Module_Card in the existing locked (grayed) presentation.
2. IF a selected module is not accessible, THEN THE Module_Config_View SHALL render the existing unlock prompt instead of the module configuration.
3. WHEN a valid unlock code is entered in the unlock prompt, THE Module_Config_View SHALL render the module configuration content.

### Requirement 13: Header reflects the current view

**User Story:** As a user, I want the header to show where I am, so that I can tell whether I am viewing a list or a specific module.

#### Acceptance Criteria

1. WHILE the Module_List_View is active, THE Menu SHALL display the active Category name in the header.
2. WHILE the Module_Config_View is active, THE Menu SHALL display the active Category name and the selected module name in the header.
3. WHILE a Special_Category page is active, THE Menu SHALL display that Special_Category name in the header.

### Requirement 14: Preserve rendering, threading, and persistence constraints

**User Story:** As a developer, I want the redesign to follow existing UI and threading constraints, so that the menu fade and module persistence keep working and the render thread stays safe.

#### Acceptance Criteria

1. THE Module_List_View SHALL render its interactive elements, including the Name_Region and the Gear_Control, using UI_Wrappers.
2. THE Menu SHALL perform all redesigned rendering on the OpenGL render thread only.
3. WHEN a Module_Toggle is performed from the Module_List_View, THE Menu SHALL change the module enabled state only through the existing `Module::toggle()` / `Module::setEnabled()` path.
4. WHEN a Module_Toggle is performed from the Module_List_View, THE Menu SHALL introduce no direct JNI calls on the OpenGL render thread.
5. THE Menu SHALL keep Module_Routing centralized in `menu.cpp` rather than placing module-specific configuration UI in the Navigation_Sidebar or Module_List_View.
6. THE Menu SHALL preserve the existing configuration persistence behavior for all modules.
