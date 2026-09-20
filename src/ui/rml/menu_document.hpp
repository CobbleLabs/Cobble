#pragma once

// Split: MSVC C2026 rejects a single string literal over ~16KB.
static const char kMenuDocument[] =
    R"RML(<rml>
<head>
<style>
/* Cobble menu stylesheet. Loaded as-is by the browser preview and by RmlUi.
   Port to Cobble: paste this into <style> in src/ui/rml/menu_document.hpp together
   with the body of menu.rml. Do not use grid, calc(), or JS.

   A few properties are doubled so both engines accept the file:
     decorator / background-image, cubic-out / ease-out, border-style (browser only).

   Scale: everything is px at a 1.2x pass over the first draft. If you want to
   resize the whole menu without touching this file, switch the units to dp and
   call Context::SetDensityIndependentPixelRatio() in C++ (dp is invalid in the
   browser preview, so that trade-off is yours to make).
*/

* { box-sizing: border-box; }

body {
    --accent: #1ee3db;
    --accent-soft: #1ee3db24;
    --accent-glow: #1ee3db40;
    --panel: #0a0b10f2;
    --panel-head: #12141cf7;
    --row-hover: #ffffff0f;
    --hairline: #ffffff14;
    --text: #eef2f9;
    --text-mid: #aeb6c4;
    --text-off: #5c6474;
    --dot: #6b7383;

    display: flex;
    justify-content: center;
    align-items: center;
    width: 100%;
    height: 100%;
    margin: 0;
    padding: 0;
    font-family: Inter;
    font-size: 14px;
    font-weight: 400;
    color: var(--text);
    background-color: #00000080;
}

.stage {
    position: relative;
    width: 1216px;
    height: 616px;
}

.pw { position: absolute; }

.panel {
    overflow: hidden;
    border-radius: 12px;
    background-color: var(--panel);
    border-width: 1px;
    border-color: #ffffff0a;
    border-style: solid;
    border-bottom-width: 2px;
    border-bottom-color: var(--accent);
}

.glow {
    height: 0;
    padding-top: 16px;
    margin: 0 6px;
    background-color: #1ee3db18;
    background-image: linear-gradient(to bottom, #1ee3db40, #1ee3db00);
    decorator: vertical-gradient(#1ee3db40 #1ee3db00);
}

.p-main    { left: 48px;  top: 84px; width: 212px; }
.p-combat  { left: 292px; top: 52px; width: 218px; }
.p-world   { left: 530px; top: 54px; width: 214px; }
.p-render  { left: 764px; top: 56px; width: 214px; }
.p-utility { left: 990px; top: 36px; width: 214px; }

.p-head {
    display: flex;
    align-items: center;
    padding: 11px 12px 11px 14px;
    background-color: var(--panel-head);
    border-bottom-width: 1px;
    border-bottom-color: var(--hairline);
    border-bottom-style: solid;
}

.p-title {
    flex-grow: 1;
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0.2px;
    color: var(--text);
}

.tag {
    font-size: 11px;
    font-weight: 500;
    letter-spacing: 0.3px;
    color: var(--text-off);
    margin-left: 7px;
}

.p-foot {
    display: flex;
    align-items: center;
    padding: 10px 14px;
    border-top-width: 1px;
    border-top-color: var(--hairline);
    border-top-style: solid;
}

.grow { flex-grow: 1; }

.p-body { padding: 5px 0 7px 0; }

.nav {
    display: flex;
    align-items: center;
    padding: 9px 14px;
    transition: background-color 0.12s ease-out;
    transition: background-color 0.12s cubic-out;
}
.nav:hover { background-color: var(--row-hover); }

.nl {
    flex-grow: 1;
    font-size: 14px;
    font-weight: 500;
    color: var(--text);
}
.nav.dim .nl { color: var(--text-off); font-weight: 400; }

.sep {
    height: 1px;
    margin: 7px 14px;
    background-color: var(--hairline);
}

/* ============================ module rows ============================
   .mw wraps one module: the row itself plus its (hidden) settings drawer.
   The id lives on .mw, so C++ does GetElementById("m-killaura")->SetClass("open", ...).
*/

.mod {
    display: flex;
    align-items: center;
    padding: 7px 10px 7px 14px;
    transition: background-color 0.12s ease-out;
    transition: background-color 0.12s cubic-out;
}
.mod:hover { background-color: var(--row-hover); }

.mn {
    flex-grow: 1;
    font-size: 13.5px;
    font-weight: 400;
    letter-spacing: 0.1px;
    color: var(--text-off);
}
.mod.on .mn { color: var(--text); font-weight: 500; }

.key {
    font-size: 11px;
    font-weight: 500;
    color: var(--text-off);
    border-width: 1px;
    border-color: var(--hairline);
    border-style: solid;
    border-radius: 3px;
    padding: 2px 5px;
    margin-right: 6px;
}

.dots {
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 4px 4px 2px 4px;
}
.d {
    width: 3px;
    height: 3px;
    border-radius: 2px;
    margin-bottom: 2px;
    background-color: var(--dot);
    transition: background-color 0.12s ease-out;
    transition: background-color 0.12s cubic-out;
}
.mod:hover .d { background-color: var(--text-mid); }

/* ============================ settings drawer ============================
   Opened by right-clicking the row or clicking the three dots. Both are wired
   in C++ (menu_events.cpp) because RmlUi only emits `click` for the left button.
*/

.drawer {
    display: none;
    margin-left: 14px;
    padding: 5px 14px 11px 13px;
    background-color: #ffffff05;
}
.mw.open .drawer { display: block; }
.mw.open .mod { background-color: var(--row-hover); }
.mw.open .d { background-color: var(--accent); }

.st { padding: 6px 0 6px 0; }

.st-r {
    display: flex;
    align-items: center;
}

.st-h {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
}

.sn {
    flex-grow: 1;
    font-size: 12.5px;
    color: var(--text-mid);
}

.sv {
    font-size: 12.5px;
    font-weight: 500;
    color: var(--accent);
}

/* slider — track is 11px tall so the knob isn't clipped by flex/overflow */
.track {
    display: flex;
    align-items: center;
    height: 11px;
}
.bar {
    width: 100%;
    height: 4px;
    border-radius: 2px;
    background-color: #ffffff1a;
}
.fill {
    position: relative;
    height: 4px;
    border-radius: 2px;
    background-color: var(--accent);
}
.knob {
    position: absolute;
    right: -5px;
    top: 50%;
    margin-top: -5px;
    width: 11px;
    height: 11px;
    border-radius: 6px;
    background-color: #ffffff;
}

/* toggle */
.sw {
    display: flex;
    flex-shrink: 0;
    align-items: center;
    width: 28px;
    height: 16px;
    padding: 2px;
    border-radius: 8px;
    background-color: #ffffff1a;
    transition: background-color 0.12s ease-out;
    transition: background-color 0.12s cubic-out;
}
.nub {
    flex-shrink: 0;
    width: 12px;
    height: 12px;
    border-radius: 6px;
    background-color: var(--text-off);
}
.sw.on { background-color: var(--accent); justify-content: flex-end; }
.sw.on .nub { background-color: #05181b; }

/* enum / mode picker — label is a real element; RmlUi drops anonymous flex text */
.pick {
    display: flex;
    flex-shrink: 0;
    align-items: center;
    padding: 3px 8px;
    border-radius: 5px;
    background-color: #ffffff0f;
}
.pl {
    font-size: 12.5px;
    color: var(--text);
}
.pick .chev { margin-left: 9px; }

/* ============================ chevrons ============================ */

.chev {
    width: 5px;
    height: 5px;
    border-style: solid;
    border-width: 0;
    border-right-width: 1px;
    border-bottom-width: 1px;
    border-color: var(--text-off);
}
.chev-r { transform: rotate(-45deg); }
.chev-u { transform: rotate(-135deg); }
.chev-d { transform: rotate(45deg); }

/* ============================ icons ============================
   Categories are text-only. What is left: the logo gem in the main header and
   the two footer buttons.
*/

.ic {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 15px;
    height: 15px;
    margin-right: 10px;
}

.ic-logo {
    width: 13px;
    height: 13px;
    border-radius: 2px;
    transform: rotate(45deg);
    background-image: linear-gradient(to bottom, #6df6ef, #0fb2ac);
    decorator: vertical-gradient(#6df6ef #0fb2ac);
}

.i-diamond {
    width: 6px;
    height: 6px;
    border-radius: 1px;
    transform: rotate(45deg);
    background-color: var(--accent);
}

.ic-gear {
    width: 14px;
    height: 14px;
    margin-right: 0;
    border-width: 1px;
    border-color: var(--text-off);
    border-style: solid;
    border-radius: 7px;
}
.i-dot {
    width: 4px;
    height: 4px;
    border-radius: 2px;
    background-color: var(--text-mid);
}

.ic-pin { width: 14px; height: 14px; margin-right: 0; }
.st-r .key { margin-right: 0; }

</style>
</head>
<body>
<div class="stage">

  <div class="pw p-main">
    <div class="panel">
      <div class="p-head">
        <div class="ic ic-logo"></div>
        <div class="p-title">Cobble<span class="tag">b1</span></div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="nav" id="cat-combat"><div class="nl">Combat</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-render"><div class="nl">Render</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-utility"><div class="nl">Utility</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-world"><div class="nl">World</div><div class="chev chev-r"></div></div>
        <div class="sep"></div>
        <div class="nav dim" id="cat-friends"><div class="nl">Friends</div><div class="chev chev-r"></div></div>
        <div class="nav dim" id="cat-profiles"><div class="nl">Profiles</div><div class="chev chev-r"></div></div>
        <div class="nav dim" id="cat-macros"><div class="nl">Macros</div><div class="chev chev-r"></div></div>
      </div>
      <div class="p-foot">
        <div class="ic ic-gear" id="btn-settings"><div class="i-dot"></div></div>
        <div class="grow"></div>
        <div class="ic ic-pin" id="btn-pin"><div class="i-diamond"></div></div>
      </div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-combat">
    <div class="panel">
      <div class="p-head">
        <div class="p-title">Combat</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">

        <!-- a module with settings: right-click the row or click the dots to fold it open -->
        <div class="mw open" id="m-killaura">
          <div class="mod on"><div class="mn">KillAura</div><div class="key">R</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
          <div class="drawer">
            <div class="st">
              <div class="st-h"><div class="sn">Range</div><div class="sv">3.4</div></div>
              <div class="track"><div class="bar"><div class="fill" style="width: 62%;"><div class="knob"></div></div></div></div>
            </div>
            <div class="st st-r"><div class="sn">Mode</div><div class="pick"><div class="pl">Switch</div><div class="chev chev-d"></div></div></div>
            <div class="st st-r"><div class="sn">Through Walls</div><div class="sw on"><div class="nub"></div></div></div>
            <div class="st st-r"><div class="sn">Target Players</div><div class="sw on"><div class="nub"></div></div></div>
            <div class="st st-r"><div class="sn">Keybind</div><div class="key">R</div></div>
          </div>
        </div>

        <div class="mw" id="m-aimassist"><div class="mod on"><div class="mn">AimAssist</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>

        <div class="mw" id="m-reach">
          <div class="mod on"><div class="mn">Reach</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
          <div class="drawer">
            <div class="st">
              <div class="st-h"><div class="sn">Distance</div><div class="sv">3.2</div></div>
              <div class="track"><div class="bar"><div class="fill" style="width: 48%;"><div class="knob"></div></div></div></div>
            </div>
            <div class="st st-r"><div class="sn">Only While Combat</div><div class="sw"><div class="nub"></div></div></div>
          </div>
        </div>

        <div class="mw" id="m-velocity"><div class="mod on"><div class="mn">Velocity</div><div class="key">V</div><div class="dots"><div class="d"></div)RML"
    R"RML(><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-triggerbot"><div class="mod"><div class="mn">TriggerBot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-autoclicker"><div class="mod"><div class="mn">AutoClicker</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-antibot"><div class="mod"><div class="mn">AntiBot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-hitselect"><div class="mod"><div class="mn">HitSelect</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-blockhit"><div class="mod"><div class="mn">BlockHit</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
      </div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-world">
    <div class="panel">
      <div class="p-head">
        <div class="p-title">World</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="mw" id="m-autotool"><div class="mod"><div class="mn">AutoTool</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-blockin"><div class="mod"><div class="mn">Block-In</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-safewalk"><div class="mod"><div class="mn">SafeWalk</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-mlg"><div class="mod"><div class="mn">MLG</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>

        <div class="mw" id="m-sprint">
          <div class="mod on"><div class="mn">Sprint</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
          <div class="drawer">
            <div class="st st-r"><div class="sn">Mode</div><div class="pick"><div class="pl">Always</div><div class="chev chev-d"></div></div></div>
            <div class="st st-r"><div class="sn">Omni Sprint</div><div class="sw on"><div class="nub"></div></div></div>
            <div class="st st-r"><div class="sn">Keep Sprint</div><div class="sw"><div class="nub"></div></div></div>
          </div>
        </div>
      </div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-render">
    <div class="panel">
      <div class="p-head">
        <div class="p-title">Render</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">

        <div class="mw" id="m-esp">
          <div class="mod on"><div class="mn">ESP</div><div class="key">G</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
          <div class="drawer">
            <div class="st st-r"><div class="sn">Mode</div><div class="pick"><div class="pl">Box</div><div class="chev chev-d"></div></div></div>
            <div class="st">
              <div class="st-h"><div class="sn">Opacity</div><div class="sv">0.80</div></div>
              <div class="track"><div class="bar"><div class="fill" style="width: 80%;"><div class="knob"></div></div></div></div>
            </div>
            <div class="st st-r"><div class="sn">Players</div><div class="sw on"><div class="nub"></div></div></div>
            <div class="st st-r"><div class="sn">Mobs</div><div class="sw"><div class="nub"></div></div></div>
            <div class="st st-r"><div class="sn">Keybind</div><div class="key">G</div></div>
          </div>
        </div>

        <div class="mw" id="m-tracers"><div class="mod"><div class="mn">Tracers</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-chestesp"><div class="mod"><div class="mn">ChestESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-xray"><div class="mod on"><div class="mn">X-Ray</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-chams"><div class="mod"><div class="mn">Chams</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-nametags"><div class="mod"><div class="mn">NameTags</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-itemesp"><div class="mod"><div class="mn">ItemESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-storageesp"><div class="mod on"><div class="mn">StorageESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
      </div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-utility">
    <div class="panel">
      <div class="p-head">
        <div class="p-title">Utility</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">

        <div class="mw" id="m-fly">
          <div class="mod on"><div class="mn">Fly</div><div class="key">F</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
          <div class="drawer">
            <div class="st">
              <div class="st-h"><div class="sn">Speed</div><div class="sv">1.8</div></div>
              <div class="track"><div class="bar"><div class="fill" style="width: 45%;"><div class="knob"></div></div></div></div>
            </div>
            <div class="st st-r"><div class="sn">Mode</div><div class="pick"><div class="pl">Vanilla</div><div class="chev chev-d"></div></div></div>
            <div class="st st-r"><div class="sn">Anti Kick</div><div class="sw on"><div class="nub"></div></div></div>
          </div>
        </div>

        <div class="mw" id="m-scaffold"><div class="mod"><div class="mn">Scaffold</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-timer"><div class="mod"><div class="mn">Timer</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-blink"><div class="mod"><div class="mn">Blink</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-phase"><div class="mod"><div class="mn">Phase</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-nofall"><div class="mod"><div class="mn">NoFall</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-autoarmor"><div class="mod"><div class="mn">AutoArmor</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-bhop"><div class="mod"><div class="mn">BHop</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-fastplace"><div class="mod"><div class="mn">FastPlace</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-autopot"><div class="mod"><div class="mn">AutoPot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-cheststeal"><div class="mod"><div class="mn">ChestSteal</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-antiafk"><div class="mod"><div class="mn">Anti-AFK</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-freecam"><div class="mod"><div class="mn">Freecam</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
        <div class="mw" id="m-parkour"><div class="mod"><div class="mn">Parkour</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div></div>
      </div>
    </div>
    <div class="glow"></div>
  </div>

</div>
</body>
</rml>
)RML";
