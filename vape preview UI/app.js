"use strict";

/*
 * Standalone visual mock only.
 * There are intentionally no imports, network requests, storage APIs,
 * Minecraft hooks, native calls or references to the original Java client.
 */

const ui = {
  client: document.querySelector("#client"),
  viewport: document.querySelector("#client-viewport"),
  content: document.querySelector("#content"),
  backdrop: document.querySelector("#backdrop"),
  sidecar: document.querySelector("#sidecar"),
  sidecarTitle: document.querySelector("#sidecar-title"),
  sidecarLeading: document.querySelector("#sidecar-leading"),
  sidecarContent: document.querySelector("#sidecar-content"),
  toast: document.querySelector("#toast")
};

const moduleFixtures = [
  { id: "aim-assist", name: "AimAssist", category: "combat", detail: "Smooth target tracking preview", favorite: true, tag: "BETA", icon: "sword" },
  { id: "auto-clicker", name: "AutoClicker", category: "combat", detail: "Click timing controls", favorite: true, icon: "sword" },
  { id: "reach", name: "Reach", category: "combat", detail: "Distance configuration", favorite: false, warning: true, tag: "UNSAFE", icon: "sword" },
  { id: "velocity", name: "Velocity", category: "combat", detail: "Horizontal and vertical values", favorite: true, icon: "shield" },
  { id: "esp", name: "ESP", category: "render", detail: "Entity highlight appearance", favorite: true, icon: "eye" },
  { id: "nametags", name: "NameTags", category: "render", detail: "Custom nametag layout", favorite: false, icon: "eye" },
  { id: "fullbright", name: "Fullbright", category: "render", detail: "Brightness display control", favorite: false, icon: "eye" },
  { id: "sprint", name: "Sprint", category: "utility", detail: "Movement state preview", favorite: true, icon: "wrench" },
  { id: "timer", name: "Timer", category: "utility", detail: "Client speed slider mock", favorite: false, tag: "INDEV", icon: "wrench" },
  { id: "scaffold", name: "Scaffold", category: "world", detail: "Placement settings display", favorite: false, warning: true, icon: "world" },
  { id: "chest-stealer", name: "ChestStealer", category: "inventory", detail: "Inventory rules preview", favorite: false, icon: "box" },
  { id: "inv-cleaner", name: "InventoryCleaner", category: "inventory", detail: "Filter rule editor preview", favorite: true, icon: "box" }
];

const categories = [
  { id: "favorites", label: "Favorites", icon: "star", favorite: true },
  { id: "combat", label: "Combat", icon: "sword" },
  { id: "render", label: "Render", icon: "eye" },
  { id: "utility", label: "Utility", icon: "wrench" },
  { id: "world", label: "World", icon: "world" },
  { id: "inventory", label: "Inventory", icon: "box" },
  { id: "macros", label: "Macros", icon: "macro" },
  { id: "legit", label: "Legit", icon: "shield" }
];

const macroFixtures = [
  { id: "macro-1", name: "Quick slot", key: "G", detail: "Visual fixture · no key hook" },
  { id: "macro-2", name: "Chat message", key: "H", detail: "Visual fixture · no game action" }
];

const friendFixtures = [
  { id: "friend-1", name: "PlayerOne", alias: "Builder", online: true },
  { id: "friend-2", name: "SkyRunner", alias: "", online: false },
  { id: "friend-3", name: "AlexDemo", alias: "Teammate", online: true }
];

const profileFixtures = [
  { id: "profile-1", name: "Default", active: true },
  { id: "profile-2", name: "Practice", active: false },
  { id: "profile-3", name: "Visual Test", active: false }
];

const state = {
  section: "modules",
  moduleMode: "favorites",
  moduleQuery: "",
  friendSource: "minecraft",
  friendQuery: "",
  selectedModule: null,
  selectedProfile: "profile-1",
  visualModuleStates: Object.fromEntries(moduleFixtures.map((module) => [module.id, false])),
  visualOverlayStates: { watermark: true, keystrokes: false, arraylist: true, radar: false },
  favoriteIds: new Set(moduleFixtures.filter((module) => module.favorite).map((module) => module.id)),
  friends: friendFixtures.map((friend) => ({ ...friend })),
  profiles: profileFixtures.map((profile) => ({ ...profile })),
  macros: macroFixtures.map((macro) => ({ ...macro })),
  creatingProfile: false,
  animations: true,
  compactCards: false,
  accent: "#25c69a",
  toastTimer: null,
  sidecar: null
};

function icon(name, className = "") {
  return `<svg class="${className}" aria-hidden="true"><use href="#icon-${escapeHtml(name)}"></use></svg>`;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function showToast(message) {
  window.clearTimeout(state.toastTimer);
  ui.toast.textContent = message;
  ui.toast.classList.add("is-visible");
  state.toastTimer = window.setTimeout(() => ui.toast.classList.remove("is-visible"), 1900);
}

function applyAccent(hex) {
  const normalized = /^#[0-9a-f]{6}$/i.test(hex) ? hex : "#25c69a";
  state.accent = normalized;
  const red = Number.parseInt(normalized.slice(1, 3), 16);
  const green = Number.parseInt(normalized.slice(3, 5), 16);
  const blue = Number.parseInt(normalized.slice(5, 7), 16);
  document.documentElement.style.setProperty("--accent", normalized);
  document.documentElement.style.setProperty("--accent-rgb", `${red}, ${green}, ${blue}`);
}

function fitClient() {
  const maxWidth = Math.max(280, window.innerWidth - 40);
  const maxHeight = Math.max(168, window.innerHeight - 118);
  const scale = Math.min(1.18, maxWidth / 800, maxHeight / 480);
  ui.client.style.transform = `scale(${scale})`;
  ui.viewport.style.width = `${800 * scale}px`;
  ui.viewport.style.height = `${480 * scale}px`;
}

function render() {
  document.querySelectorAll("[data-section]").forEach((tab) => {
    tab.classList.toggle("is-active", tab.dataset.section === state.section);
  });

  ui.client.classList.toggle("no-animations", !state.animations);
  ui.content.innerHTML = renderSection();
  renderSidecar();
}

function renderSection() {
  if (state.section === "friends") return renderFriendsPage();
  if (state.section === "profiles") return renderProfilesPage();
  return renderModulesPage();
}

function renderModulesPage() {
  const activeCategory = categories.find((category) => category.id === state.moduleMode) || categories[0];
  return `
    <div class="page page--modules">
      <aside class="sidebar">
        <div class="sidebar__header">Modules</div>
        <div class="sidebar__scroll">
          ${categories.map((category) => renderCategoryRow(category)).join("")}
        </div>
      </aside>
      <section class="main-pane" aria-label="${escapeHtml(activeCategory.label)}">
        ${renderModuleMain(activeCategory)}
      </section>
    </div>`;
}

function renderCategoryRow(category) {
  const count = category.id === "favorites"
    ? state.favoriteIds.size
    : moduleFixtures.filter((module) => module.category === category.id).length;
  const showCount = !["macros", "legit"].includes(category.id);
  return `
    <button class="nav-row ${category.favorite ? "is-favorite" : ""} ${state.moduleMode === category.id ? "is-active" : ""}"
      type="button" data-module-mode="${category.id}">
      <span class="nav-row__icon">${icon(category.icon)}</span>
      <span class="nav-row__label">${escapeHtml(category.label)}</span>
      ${showCount ? `<span class="nav-row__badge">${count}</span>` : "<span></span>"}
      <span class="nav-row__arrow">${icon("chevron")}</span>
    </button>`;
}

function renderModuleMain(category) {
  if (category.id === "legit") return renderLegitView();
  if (category.id === "macros") return renderMacrosView();

  const query = state.moduleQuery.trim().toLowerCase();
  const modules = moduleFixtures.filter((module) => {
    const categoryMatches = category.id === "favorites"
      ? state.favoriteIds.has(module.id)
      : module.category === category.id;
    const queryMatches = !query || module.name.toLowerCase().includes(query) || module.detail.toLowerCase().includes(query);
    return categoryMatches && queryMatches;
  });

  return `
    <label class="search-row">
      ${icon("search")}
      <input type="search" data-input="module-search" value="${escapeHtml(state.moduleQuery)}" placeholder="Search modules..." autocomplete="off">
      ${category.id === "favorites" ? '<button class="search-row__action" type="button" data-action="reorder">Edit</button>' : ""}
    </label>
    <div class="card-list ${state.compactCards ? "is-compact" : ""}">
      ${modules.length ? modules.map(renderModuleCard).join("") : '<div class="empty-state">No visual fixtures match this filter.</div>'}
    </div>`;
}

function renderModuleCard(module) {
  const visuallyOn = Boolean(state.visualModuleStates[module.id]);
  const selected = state.selectedModule === module.id;
  const dimmed = state.selectedModule && !selected;
  const favorite = state.favoriteIds.has(module.id);
  return `
    <article class="module-card ${visuallyOn ? "is-visually-on" : ""} ${selected ? "is-selected" : ""} ${dimmed ? "is-dimmed" : ""} ${module.warning ? "is-warning" : ""}"
      data-open-module="${module.id}">
      <div class="module-card__copy">
        <div class="module-card__name">
          ${escapeHtml(module.name)}
          ${module.tag ? `<span class="tag ${module.warning ? "tag--orange" : ""}">${escapeHtml(module.tag)}</span>` : ""}
        </div>
        <div class="module-card__detail">${escapeHtml(module.detail)}</div>
      </div>
      <button class="star-button ${favorite ? "is-active" : ""}" type="button" data-favorite="${module.id}" title="Favorito visual">${icon("star")}</button>
      <button class="dots-button" type="button" data-open-module="${module.id}" title="Abrir controles de muestra">${icon("dots")}</button>
      <button class="visual-switch ${visuallyOn ? "is-on" : ""}" type="button" data-toggle-module="${module.id}" aria-label="Alternar estado visual"></button>
    </article>`;
}

function renderLegitView() {
  const query = state.moduleQuery.trim().toLowerCase();
  const fixtures = moduleFixtures.filter((module) => !query || module.name.toLowerCase().includes(query)).slice(0, 6);
  return `
    <label class="search-row">
      ${icon("search")}
      <input type="search" data-input="module-search" value="${escapeHtml(state.moduleQuery)}" placeholder="Search legit modules..." autocomplete="off">
    </label>
    <div class="main-scroll">
      <div class="legit-grid">
        ${fixtures.map((module) => {
          const visuallyOn = state.visualModuleStates[module.id];
          const selected = state.selectedModule === module.id;
          return `<article class="legit-card ${selected ? "is-selected" : ""} ${state.selectedModule && !selected ? "is-dimmed" : ""}" data-open-module="${module.id}">
            <span class="legit-card__icon">${icon(module.icon)}</span>
            <span class="legit-card__name">${escapeHtml(module.name)}</span>
            <button class="visual-switch ${visuallyOn ? "is-on" : ""}" type="button" data-toggle-module="${module.id}" aria-label="Alternar estado visual"></button>
          </article>`;
        }).join("")}
      </div>
    </div>`;
}

function renderMacrosView() {
  return `
    <div class="search-row">
      ${icon("search")}
      <input type="search" placeholder="Search macros..." aria-label="Buscar macros visuales">
      <button class="search-row__action" type="button" data-action="new-macro">New macro</button>
    </div>
    <div class="card-list">
      ${state.macros.map((macro) => `<article class="macro-card">
        <span class="macro-card__key">${escapeHtml(macro.key)}</span>
        <div class="macro-card__copy">
          <div class="macro-card__name">${escapeHtml(macro.name)}</div>
          <div class="macro-card__meta">${escapeHtml(macro.detail)}</div>
        </div>
        <button class="friend-action" type="button" data-action="mock-only" title="Editar visualmente">${icon("edit")}</button>
        <button class="friend-action is-danger" type="button" data-delete-macro="${macro.id}" title="Eliminar fixture local">${icon("trash")}</button>
      </article>`).join("")}
    </div>`;
}

function renderFriendsPage() {
  return `
    <div class="page page--friends">
      <aside class="sidebar">
        <div class="sidebar__header">
          Friends<span class="spacer"></span>
          <button class="icon-button" type="button" data-action="settings">${icon("settings")}</button>
        </div>
        <div class="sidebar__scroll">
          ${renderFriendSource("minecraft", "Minecraft Friends")}
          ${renderFriendSource("online", "VAPE Friends")}
        </div>
        <button class="user-status" type="button" data-action="user-status">
          <span class="user-status__avatar">${icon("user")}</span>
          <span><span class="user-status__name">Preview User</span><span class="user-status__meta">Offline · local fixture</span></span>
        </button>
      </aside>
      <section class="main-pane">
        ${state.friendSource === "online" ? renderOnlinePlaceholder() : renderMinecraftFriends()}
      </section>
    </div>`;
}

function renderFriendSource(id, label) {
  return `<button class="nav-row ${state.friendSource === id ? "is-active" : ""}" type="button" data-friend-source="${id}">
    <span class="nav-row__icon">${icon(id === "online" ? "users" : "user")}</span>
    <span class="nav-row__label">${escapeHtml(label)}</span>
    <span class="nav-row__badge">${id === "online" ? "0" : state.friends.length}</span>
    <span class="nav-row__arrow">${icon("chevron")}</span>
  </button>`;
}

function renderOnlinePlaceholder() {
  return `<div class="info-card" style="margin-top:24px"><strong>PREVIEW</strong>Under construction. Use frames GUI mode to use Vape friends for now.<br><br>No online service is connected in this standalone view.</div>`;
}

function renderMinecraftFriends() {
  const query = state.friendQuery.trim().toLowerCase();
  const visibleFriends = state.friends.filter((friend) => {
    return !query || friend.name.toLowerCase().includes(query) || friend.alias.toLowerCase().includes(query);
  });
  return `
    <label class="search-row">
      ${icon("search")}
      <input type="search" data-input="friend-search" value="${escapeHtml(state.friendQuery)}" placeholder="Search friends..." autocomplete="off">
    </label>
    <div class="main-scroll">
      ${query ? `<div class="section-label">Results</div>${renderFriendSearchResult(state.friendQuery)}` : ""}
      <div class="section-label">Your Friends</div>
      ${visibleFriends.length ? visibleFriends.map(renderFriendCard).join("") : '<div class="info-card"><strong>INFO</strong>Enter a username in the search bar to add a friend.</div>'}
    </div>`;
}

function renderFriendSearchResult(query) {
  const exists = state.friends.some((friend) => friend.name.toLowerCase() === query.trim().toLowerCase());
  return `<article class="friend-card">
    <span class="friend-card__status"></span>
    <div class="friend-card__copy">
      <div class="friend-card__name">“${escapeHtml(query.trim())}”</div>
      <div class="friend-card__meta">Search Result · local only</div>
    </div>
    <button class="friend-action" type="button" ${exists ? "disabled" : `data-add-friend="${escapeHtml(query.trim())}"`} title="Añadir fixture local">${icon(exists ? "shield" : "plus")}</button>
  </article>`;
}

function renderFriendCard(friend) {
  const displayName = friend.alias ? `*${friend.alias}` : friend.name;
  return `<article class="friend-card">
    <span class="friend-card__status ${friend.online ? "is-online" : ""}"></span>
    <div class="friend-card__copy">
      <div class="friend-card__name">${escapeHtml(displayName)}</div>
      <div class="friend-card__meta">${escapeHtml(friend.name)} · ${friend.online ? "Nearby" : "Offline fixture"}</div>
    </div>
    <button class="friend-action" type="button" data-open-friend="${friend.id}" title="Ajustes locales">${icon("settings")}</button>
    <button class="friend-action is-danger" type="button" data-remove-friend="${friend.id}" title="Quitar fixture local">${icon("close")}</button>
  </article>`;
}

function renderProfilesPage() {
  const activeProfile = state.profiles.find((profile) => profile.active) || state.profiles[0];
  const selectedProfile = state.profiles.find((profile) => profile.id === state.selectedProfile) || activeProfile;
  return `
    <div class="page page--profiles">
      <aside class="sidebar">
        <div class="sidebar__header">
          Profiles<span class="spacer"></span>
          <button class="action-button" type="button" data-action="new-profile">${icon("plus")} New profile</button>
          <button class="icon-button" type="button" data-action="settings">${icon("settings")}</button>
        </div>
        <div class="profile-list">
          ${state.creatingProfile ? renderDraftProfileCard() : ""}
          ${state.profiles.map((profile) => renderProfileCard(profile, selectedProfile)).join("")}
        </div>
        <button class="public-profiles" type="button" data-action="public-profiles">${icon("profiles")} Public profiles <span class="badge">6</span></button>
      </aside>
      <section class="main-pane">
        ${state.creatingProfile ? renderCreateProfile(activeProfile) : renderProfileMain(activeProfile)}
      </section>
    </div>`;
}

function renderProfileCard(profile, selectedProfile) {
  const dimmed = selectedProfile && selectedProfile.id !== profile.id;
  return `<article class="profile-card ${profile.active ? "is-active" : ""} ${selectedProfile?.id === profile.id ? "is-selected" : ""} ${state.sidecar?.type === "profile" && dimmed ? "is-dimmed" : ""}" data-select-profile="${profile.id}">
    <span class="profile-card__marker"></span>
    <span class="profile-card__name">${escapeHtml(profile.name)}</span>
    ${profile.active ? '<span class="profile-card__badge">ACTIVE</span>' : ""}
    <button class="dots-button" type="button" data-open-profile="${profile.id}">${icon("dots")}</button>
  </article>`;
}

function renderDraftProfileCard() {
  return `<article class="profile-card is-selected"><span class="profile-card__marker"></span><span id="draft-name" class="profile-card__name">New Profile</span></article>`;
}

function renderProfileMain(profile) {
  const snapshots = ["Combat settings", "Render settings", "Utility settings", "HUD layout", "Keybinds"];
  return `<div class="profile-main">
    <header class="profile-header">
      <span class="profile-header__title">${escapeHtml(profile.name)}</span>
      <span class="profile-header__meta">Local snapshot preview</span>
      <button class="dots-button" type="button" data-open-profile="${profile.id}">${icon("dots")}</button>
    </header>
    <div class="snapshot-list">
      ${snapshots.map((name, index) => `<div class="snapshot-row"><span class="snapshot-row__name">${escapeHtml(name)}</span><span class="snapshot-row__state">${index < 3 ? "SAVED" : "DEFAULT"}</span></div>`).join("")}
    </div>
  </div>`;
}

function renderCreateProfile(activeProfile) {
  return `<div class="profile-main create-profile">
    <div class="create-profile__top">
      <input id="profile-name-input" class="small-input" type="text" maxlength="48" placeholder="Type name" autocomplete="off">
      <span class="tag">FROM ${escapeHtml(activeProfile.name)}</span>
    </div>
    <div class="create-profile__preview">
      <div class="snapshot-row"><span class="snapshot-row__name">Visual states</span><span class="snapshot-row__state">COPY</span></div>
      <div class="snapshot-row"><span class="snapshot-row__name">Theme appearance</span><span class="snapshot-row__state">COPY</span></div>
      <div class="snapshot-row"><span class="snapshot-row__name">Mock layout</span><span class="snapshot-row__state">COPY</span></div>
    </div>
    <div class="create-profile__actions">
      <button class="action-button action-button--neutral" type="button" data-action="cancel-profile">Cancel</button>
      <button id="create-profile-button" class="action-button action-button--green" type="button" data-action="create-profile" disabled>Create</button>
    </div>
  </div>`;
}

function openSidecar(config) {
  state.sidecar = config;
  if (config.type === "module") state.selectedModule = config.id;
  render();
}

function closeSidecar() {
  state.sidecar = null;
  state.selectedModule = null;
  render();
}

function renderSidecar() {
  const config = state.sidecar;
  ui.client.classList.toggle("is-sidecar-open", Boolean(config));
  ui.client.classList.toggle("has-backdrop", Boolean(config?.backdrop));
  ui.sidecar.classList.toggle("is-frame-overlay", Boolean(config?.frameOverlay));
  ui.sidecar.classList.toggle("is-wide", Boolean(config?.wide));
  ui.sidecar.setAttribute("aria-hidden", String(!config));

  if (!config) {
    ui.sidecarContent.innerHTML = "";
    return;
  }

  ui.sidecarTitle.textContent = config.title;
  ui.sidecarLeading.innerHTML = icon(config.icon || "settings");
  if (config.type === "settings") ui.sidecarContent.innerHTML = renderVisualSettings();
  if (config.type === "overlays") ui.sidecarContent.innerHTML = renderOverlaySettings();
  if (config.type === "module") ui.sidecarContent.innerHTML = renderModuleSidecar(config.id);
  if (config.type === "friend") ui.sidecarContent.innerHTML = renderFriendSidecar(config.id);
  if (config.type === "profile") ui.sidecarContent.innerHTML = renderProfileSidecar(config.id);
  if (config.type === "public") ui.sidecarContent.innerHTML = renderPublicProfiles();
}

function renderVisualSettings() {
  const swatches = ["#25c69a", "#2f7ae5", "#9b67e8", "#ec812c", "#e64e76", "#d1d1d1"];
  return `
    <div class="control-group">
      <span class="control-label">Accent color</span>
      <div class="swatches">${swatches.map((color) => `<button class="swatch ${state.accent === color ? "is-active" : ""}" style="--swatch:${color}" type="button" data-accent="${color}" aria-label="Color ${color}"></button>`).join("")}</div>
      <p class="control-description">Sólo cambia variables CSS de esta preview.</p>
    </div>
    <div class="control-group">
      <span class="control-label">Appearance</span>
      <div class="control-row"><span>Animations</span><button class="visual-switch ${state.animations ? "is-on" : ""}" type="button" data-action="toggle-animations"></button></div>
      <div class="control-row"><span>Compact cards</span><button class="visual-switch ${state.compactCards ? "is-on" : ""}" type="button" data-action="toggle-compact"></button></div>
      <div class="control-row"><span>GUI scale</span></div>
      <input class="range" type="range" min="80" max="118" value="100" data-input="preview-scale" aria-label="Escala de preview">
    </div>
    <div class="info-card"><strong>ISOLATED</strong>No settings are written to disk or sent to the original client.</div>`;
}

function renderOverlaySettings() {
  const labels = { watermark: "Watermark", keystrokes: "Keystrokes", arraylist: "Enabled modules", radar: "Radar" };
  return `
    <div class="control-group">
      <span class="control-label">HUD overlays</span>
      ${Object.entries(labels).map(([id, label]) => `<div class="control-row"><span>${escapeHtml(label)}</span><button class="visual-switch ${state.visualOverlayStates[id] ? "is-on" : ""}" type="button" data-toggle-overlay="${id}"></button></div>`).join("")}
      <p class="control-description">Estos switches no dibujan sobre ningún juego; sólo conservan estado en memoria.</p>
    </div>
    <div class="info-card"><strong>NO GAME HOOK</strong>This page has no access to a framebuffer, player or world.</div>`;
}

function renderModuleSidecar(id) {
  const module = moduleFixtures.find((item) => item.id === id);
  if (!module) return "";
  const visuallyOn = state.visualModuleStates[id];
  return `
    <div class="sidecar-module-hero">
      <span class="sidecar-module-hero__icon">${icon(module.icon)}</span>
      <span class="sidecar-module-hero__copy"><strong>${escapeHtml(module.name)}</strong><small>Pure visual fixture</small></span>
      <button class="visual-switch ${visuallyOn ? "is-on" : ""}" type="button" data-toggle-module="${id}"></button>
    </div>
    <div class="control-group">
      <span class="control-label">Mode</span>
      <button class="fake-select" type="button" data-action="mock-only"><span>Normal</span>${icon("chevron")}</button>
    </div>
    <div class="control-group">
      <span class="control-label">Strength</span>
      <input class="range" type="range" min="0" max="100" value="62" data-input="mock-range">
      <p class="control-description">Decorative value. It is not bound to a module.</p>
    </div>
    <div class="control-group">
      <div class="control-row"><span>Show indicator</span><button class="visual-switch is-on" type="button" data-action="mock-toggle"></button></div>
      <div class="control-row"><span>Only while moving</span><button class="visual-switch" type="button" data-action="mock-toggle"></button></div>
    </div>
    <div class="info-card"><strong>VISUAL ONLY</strong>Changing these controls cannot activate ${escapeHtml(module.name)} or any other client feature.</div>`;
}

function renderFriendSidecar(id) {
  const friend = state.friends.find((item) => item.id === id);
  if (!friend) return "";
  return `
    <div class="control-group">
      <label class="control-label" for="alias-input">Alias</label>
      <input id="alias-input" class="small-input" type="text" value="${escapeHtml(friend.alias)}" placeholder="Set alias..." data-friend-alias="${friend.id}" autocomplete="off">
      <p class="control-description">The alias exists only until this page is reloaded.</p>
    </div>
    <div class="control-row"><span>Active</span><button class="visual-switch ${friend.online ? "is-on" : ""}" type="button" data-toggle-friend="${friend.id}"></button></div>
    <div class="info-card" style="margin-top:12px"><strong>LOCAL FIXTURE</strong>No Minecraft friend list or online account is available.</div>`;
}

function renderProfileSidecar(id) {
  const profile = state.profiles.find((item) => item.id === id);
  if (!profile) return "";
  return `
    <div class="sidecar-module-hero">
      <span class="sidecar-module-hero__icon">${icon("profiles")}</span>
      <span class="sidecar-module-hero__copy"><strong>${escapeHtml(profile.name)}</strong><small>${profile.active ? "Active visual profile" : "Local profile fixture"}</small></span>
    </div>
    <div class="control-group">
      <span class="control-label">Snapshot</span>
      ${["Combat settings", "Render settings", "Utility settings", "HUD layout"].map((name) => `<div class="snapshot-row"><span class="snapshot-row__name">${name}</span><span class="snapshot-row__state">MOCK</span></div>`).join("")}
    </div>
    <button class="action-button" type="button" data-activate-profile="${profile.id}">Set active visually</button>
    <div class="info-card" style="margin-top:12px"><strong>NO PROFILE MANAGER</strong>This cannot load or save a real client profile.</div>`;
}

function renderPublicProfiles() {
  return `<div class="control-group"><span class="control-label">Public profiles</span>
    ${["Practice layout", "Minimal HUD", "Default visuals"].map((name) => `<div class="snapshot-row"><span class="snapshot-row__name">${name}</span><span class="tag">DEMO</span></div>`).join("")}
  </div><div class="info-card"><strong>OFFLINE</strong>No profile is downloaded and no API call exists in this preview.</div>`;
}

function toggleVisualModule(id) {
  if (!(id in state.visualModuleStates)) return;
  state.visualModuleStates[id] = !state.visualModuleStates[id];
  render();
  showToast(`Estado gráfico: ${state.visualModuleStates[id] ? "ON" : "OFF"} · ningún módulo real fue activado`);
}

function preserveInputFocus(selector, value) {
  window.requestAnimationFrame(() => {
    const input = document.querySelector(selector);
    if (!input) return;
    input.focus();
    input.setSelectionRange(value.length, value.length);
  });
}

function resetVisualState() {
  state.visualModuleStates = Object.fromEntries(moduleFixtures.map((module) => [module.id, false]));
  state.selectedModule = null;
  state.sidecar = null;
  state.moduleQuery = "";
  state.friendQuery = "";
  state.section = "modules";
  state.moduleMode = "favorites";
  render();
  showToast("Preview restablecida · todos los estados visuales están en OFF");
}

document.addEventListener("click", (event) => {
  const section = event.target.closest("[data-section]");
  if (section) {
    state.section = section.dataset.section;
    state.sidecar = null;
    state.selectedModule = null;
    render();
    return;
  }

  const moduleMode = event.target.closest("[data-module-mode]");
  if (moduleMode) {
    state.moduleMode = moduleMode.dataset.moduleMode;
    state.moduleQuery = "";
    state.selectedModule = null;
    state.sidecar = null;
    render();
    return;
  }

  const friendSource = event.target.closest("[data-friend-source]");
  if (friendSource) {
    state.friendSource = friendSource.dataset.friendSource;
    render();
    return;
  }

  const toggleModule = event.target.closest("[data-toggle-module]");
  if (toggleModule) {
    event.stopPropagation();
    toggleVisualModule(toggleModule.dataset.toggleModule);
    return;
  }

  const favorite = event.target.closest("[data-favorite]");
  if (favorite) {
    event.stopPropagation();
    const id = favorite.dataset.favorite;
    if (state.favoriteIds.has(id)) state.favoriteIds.delete(id);
    else state.favoriteIds.add(id);
    render();
    showToast("Favorito actualizado sólo en memoria");
    return;
  }

  const openModule = event.target.closest("[data-open-module]");
  if (openModule) {
    const id = openModule.dataset.openModule;
    const module = moduleFixtures.find((item) => item.id === id);
    openSidecar({ type: "module", id, title: module?.name || "Module", icon: module?.icon || "settings", backdrop: false });
    return;
  }

  const openFriend = event.target.closest("[data-open-friend]");
  if (openFriend) {
    const friend = state.friends.find((item) => item.id === openFriend.dataset.openFriend);
    if (friend) openSidecar({ type: "friend", id: friend.id, title: friend.name, icon: "settings", backdrop: true });
    return;
  }

  const removeFriend = event.target.closest("[data-remove-friend]");
  if (removeFriend) {
    state.friends = state.friends.filter((friend) => friend.id !== removeFriend.dataset.removeFriend);
    render();
    showToast("Fixture de amigo eliminado localmente");
    return;
  }

  const addFriend = event.target.closest("[data-add-friend]");
  if (addFriend && addFriend.dataset.addFriend) {
    const name = addFriend.dataset.addFriend.slice(0, 24);
    state.friends.push({ id: `friend-${Date.now()}`, name, alias: "", online: false });
    render();
    showToast("Fixture de amigo añadido localmente");
    return;
  }

  const openProfile = event.target.closest("[data-open-profile]");
  if (openProfile) {
    event.stopPropagation();
    const profile = state.profiles.find((item) => item.id === openProfile.dataset.openProfile);
    if (profile) {
      state.selectedProfile = profile.id;
      openSidecar({ type: "profile", id: profile.id, title: profile.name, icon: "profiles", wide: true, backdrop: false });
    }
    return;
  }

  const selectProfile = event.target.closest("[data-select-profile]");
  if (selectProfile) {
    state.selectedProfile = selectProfile.dataset.selectProfile;
    render();
    return;
  }

  const activateProfile = event.target.closest("[data-activate-profile]");
  if (activateProfile) {
    state.profiles.forEach((profile) => { profile.active = profile.id === activateProfile.dataset.activateProfile; });
    state.sidecar = null;
    render();
    showToast("Perfil marcado como activo sólo en la preview");
    return;
  }

  const toggleOverlay = event.target.closest("[data-toggle-overlay]");
  if (toggleOverlay) {
    const id = toggleOverlay.dataset.toggleOverlay;
    state.visualOverlayStates[id] = !state.visualOverlayStates[id];
    renderSidecar();
    showToast("Overlay simulado · no se dibuja sobre Minecraft");
    return;
  }

  const accent = event.target.closest("[data-accent]");
  if (accent) {
    applyAccent(accent.dataset.accent);
    renderSidecar();
    return;
  }

  const toggleFriend = event.target.closest("[data-toggle-friend]");
  if (toggleFriend) {
    const friend = state.friends.find((item) => item.id === toggleFriend.dataset.toggleFriend);
    if (friend) friend.online = !friend.online;
    render();
    showToast("Estado de amigo modificado sólo en memoria");
    return;
  }

  const deleteMacro = event.target.closest("[data-delete-macro]");
  if (deleteMacro) {
    state.macros = state.macros.filter((macro) => macro.id !== deleteMacro.dataset.deleteMacro);
    render();
    showToast("Macro de muestra eliminada · no existía ningún bind real");
    return;
  }

  const action = event.target.closest("[data-action]")?.dataset.action;
  if (!action) return;

  if (action === "close-sidecar") closeSidecar();
  if (action === "settings") openSidecar({ type: "settings", title: "Settings", icon: "settings", frameOverlay: true, backdrop: true });
  if (action === "overlays") openSidecar({ type: "overlays", title: "Overlays", icon: "layers", frameOverlay: true, backdrop: true });
  if (action === "sync") showToast("Guardado simulado · no hay red ni archivos de perfil");
  if (action === "brand") resetVisualState();
  if (action === "toggle-animations") { state.animations = !state.animations; render(); }
  if (action === "toggle-compact") { state.compactCards = !state.compactCards; render(); }
  if (action === "mock-only") showToast("Control decorativo · sin acción externa");
  if (action === "mock-toggle") {
    event.target.classList.toggle("is-on");
    showToast("Switch decorativo · sin conexión al cliente");
  }
  if (action === "reorder") showToast("Modo de edición visual · ningún favorito real cambia");
  if (action === "new-macro") {
    state.macros.push({ id: `macro-${Date.now()}`, name: "New visual macro", key: "—", detail: "Unsaved local fixture" });
    render();
  }
  if (action === "user-status") showToast("Usuario ficticio · no hay cuenta conectada");
  if (action === "public-profiles") openSidecar({ type: "public", title: "Public profiles", icon: "profiles", frameOverlay: true, backdrop: true });
  if (action === "new-profile") {
    state.creatingProfile = true;
    state.sidecar = null;
    render();
    window.requestAnimationFrame(() => document.querySelector("#profile-name-input")?.focus());
  }
  if (action === "cancel-profile") {
    state.creatingProfile = false;
    render();
  }
  if (action === "create-profile") {
    const input = document.querySelector("#profile-name-input");
    const name = input?.value.trim();
    if (!name) return;
    const id = `profile-${Date.now()}`;
    state.profiles.push({ id, name: name.slice(0, 48), active: false });
    state.selectedProfile = id;
    state.creatingProfile = false;
    render();
    showToast("Perfil de muestra creado sólo en memoria");
  }
});

ui.backdrop.addEventListener("click", closeSidecar);

document.addEventListener("input", (event) => {
  if (event.target.matches('[data-input="module-search"]')) {
    state.moduleQuery = event.target.value;
    render();
    preserveInputFocus('[data-input="module-search"]', state.moduleQuery);
  }

  if (event.target.matches('[data-input="friend-search"]')) {
    state.friendQuery = event.target.value;
    render();
    preserveInputFocus('[data-input="friend-search"]', state.friendQuery);
  }

  if (event.target.matches("#profile-name-input")) {
    const name = event.target.value;
    const button = document.querySelector("#create-profile-button");
    const draftName = document.querySelector("#draft-name");
    if (button) button.disabled = !name.trim();
    if (draftName) draftName.textContent = name.trim() || "New Profile";
  }

  if (event.target.matches("[data-friend-alias]")) {
    const friend = state.friends.find((item) => item.id === event.target.dataset.friendAlias);
    if (friend) friend.alias = event.target.value.slice(0, 24);
  }

  if (event.target.matches('[data-input="preview-scale"]')) {
    const scale = Number(event.target.value) / 100;
    ui.client.style.transform = `scale(${scale})`;
    ui.viewport.style.width = `${800 * scale}px`;
    ui.viewport.style.height = `${480 * scale}px`;
  }
});

document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && state.sidecar) closeSidecar();
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "f" && state.section === "modules") {
    event.preventDefault();
    document.querySelector('[data-input="module-search"]')?.focus();
  }
});

window.addEventListener("resize", fitClient);

applyAccent(state.accent);
render();
fitClient();
