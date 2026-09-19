#pragma once

// Port of Documents/newui/browser (index.html + menu.css).
// px -> dp, border without 'solid', ease-out -> quadratic-out,
// linear-gradient -> vertical-gradient decorator. .stage is the 1024x512 layout
// root only (no mock backdrop).

static const char kMenuDocument[] = R"RML(<rml>
<head>
<style>
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
    font-size: 12dp;
    font-weight: 400;
    color: var(--text);
    background-color: #00000080;
}

.stage {
    position: relative;
    width: 1024dp;
    height: 512dp;
}

.pw { position: absolute; }

.panel {
    overflow: hidden;
    border-radius: 10dp;
    background-color: var(--panel);
    border: 1dp #ffffff0a;
}

.glow {
    height: 13dp;
    margin: 0 5dp;
    decorator: vertical-gradient(#1ee3db40 #1ee3db00);
}

.accent {
    height: 2dp;
    background-color: var(--accent);
    border-radius: 1dp;
}

.p-main    { left: 52dp;  top: 72dp; width: 176dp; }
.p-combat  { left: 264dp; top: 44dp; width: 182dp; }
.p-world   { left: 462dp; top: 46dp; width: 178dp; }
.p-render  { left: 644dp; top: 48dp; width: 178dp; }
.p-utility { left: 836dp; top: 50dp; width: 178dp; }

.p-head {
    display: flex;
    align-items: center;
    padding: 9dp 10dp;
    background-color: var(--panel-head);
    border-bottom: 1dp var(--hairline);
}

.p-title {
    flex-grow: 1;
    font-size: 12dp;
    font-weight: 600;
    letter-spacing: 0.2dp;
    color: var(--text);
}

.tag {
    font-size: 9dp;
    font-weight: 500;
    letter-spacing: 0.3dp;
    color: var(--text-off);
    margin-left: 6dp;
}

.p-foot {
    display: flex;
    align-items: center;
    padding: 8dp 11dp;
    border-top: 1dp var(--hairline);
}

.grow { flex-grow: 1; }

.p-body { padding: 4dp 0 6dp 0; }

.nav {
    display: flex;
    align-items: center;
    padding: 7dp 11dp;
    transition: background-color 0.12s quadratic-out;
}
.nav:hover { background-color: var(--row-hover); }

.nl {
    flex-grow: 1;
    font-size: 12dp;
    font-weight: 500;
    color: var(--text);
}
.nav.dim .nl { color: var(--text-off); font-weight: 400; }

.sep {
    height: 1dp;
    margin: 6dp 11dp;
    background-color: var(--hairline);
}

.mod {
    display: flex;
    align-items: center;
    padding: 6dp 8dp 6dp 12dp;
    transition: background-color 0.12s quadratic-out;
}
.mod:hover { background-color: var(--row-hover); }

.mn {
    flex-grow: 1;
    font-size: 11.5dp;
    font-weight: 400;
    letter-spacing: 0.1dp;
    color: var(--text-off);
}
.mod.on .mn { color: var(--text); font-weight: 500; }

.key {
    font-size: 9dp;
    font-weight: 500;
    color: var(--text-off);
    border: 1dp var(--hairline);
    border-radius: 3dp;
    padding: 1dp 4dp;
    margin-right: 4dp;
}

.dots {
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 3dp 3dp 1dp 3dp;
}
.d {
    width: 2dp;
    height: 2dp;
    border-radius: 1dp;
    margin-bottom: 2dp;
    background-color: var(--dot);
    transition: background-color 0.12s quadratic-out;
}
.mod:hover .d { background-color: var(--text-mid); }

.chev {
    width: 4dp;
    height: 4dp;
    border-right: 1dp var(--text-off);
    border-bottom: 1dp var(--text-off);
}
.chev-r { transform: rotate(-45deg); }
.chev-u { transform: rotate(-135deg); }

.ic {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 13dp;
    height: 13dp;
    margin-right: 9dp;
}

.ic-logo {
    width: 11dp;
    height: 11dp;
    border-radius: 2dp;
    transform: rotate(45deg);
    decorator: vertical-gradient(#6df6ef #0fb2ac);
}

.i-diamond {
    width: 7dp;
    height: 7dp;
    border-radius: 1dp;
    transform: rotate(45deg);
    background-color: var(--accent);
}
.i-diamond.sm { width: 5dp; height: 5dp; }

.ic-render, .ic-world, .ic-gear {
    border: 1dp var(--accent);
    border-radius: 7dp;
}
.i-dot {
    width: 4dp;
    height: 4dp;
    border-radius: 2dp;
    background-color: var(--accent);
}
.i-bar {
    width: 11dp;
    height: 1dp;
    background-color: var(--accent);
}

.ic-utility {
    border: 1dp var(--accent);
    border-radius: 4dp;
}
.i-square {
    width: 5dp;
    height: 5dp;
    border-radius: 1dp;
    background-color: var(--accent);
}

.ic-gear {
    width: 12dp;
    height: 12dp;
    margin-right: 0;
    border-color: var(--text-off);
}
.ic-gear .i-dot { width: 3dp; height: 3dp; background-color: var(--text-mid); }

.ic-pin { width: 12dp; height: 12dp; margin-right: 0; }
.ic-pin .i-diamond { background-color: var(--accent); }
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
        <div class="nav" id="cat-combat"><div class="ic ic-combat"><div class="i-diamond"></div></div><div class="nl">Combat</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-render"><div class="ic ic-render"><div class="i-dot"></div></div><div class="nl">Render</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-utility"><div class="ic ic-utility"><div class="i-square"></div></div><div class="nl">Utility</div><div class="chev chev-r"></div></div>
        <div class="nav" id="cat-world"><div class="ic ic-world"><div class="i-bar"></div></div><div class="nl">World</div><div class="chev chev-r"></div></div>
        <div class="sep"></div>
        <div class="nav dim" id="cat-friends"><div class="nl">Friends</div><div class="chev chev-r"></div></div>
        <div class="nav dim" id="cat-profiles"><div class="nl">Profiles</div><div class="chev chev-r"></div></div>
        <div class="nav dim" id="cat-macros"><div class="nl">Macros</div><div class="chev chev-r"></div></div>
      </div>
      <div class="p-foot">
        <div class="ic ic-gear" id="btn-settings"><div class="i-dot"></div></div>
        <div class="grow"></div>
        <div class="ic ic-pin" id="btn-pin"><div class="i-diamond sm"></div></div>
      </div>
      <div class="accent"></div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-combat">
    <div class="panel">
      <div class="p-head">
        <div class="ic ic-combat"><div class="i-diamond"></div></div>
        <div class="p-title">Combat</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="mod on" id="m-killaura"><div class="mn">KillAura</div><div class="key">R</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-aimassist"><div class="mn">AimAssist</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-reach"><div class="mn">Reach</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-velocity"><div class="mn">Velocity</div><div class="key">V</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-triggerbot"><div class="mn">TriggerBot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-autoclicker"><div class="mn">AutoClicker</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-antibot"><div class="mn">AntiBot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-hitselect"><div class="mn">HitSelect</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-blockhit"><div class="mn">BlockHit</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
      </div>
      <div class="accent"></div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-world">
    <div class="panel">
      <div class="p-head">
        <div class="ic ic-world"><div class="i-bar"></div></div>
        <div class="p-title">World</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="mod" id="m-autotool"><div class="mn">AutoTool</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-blockin"><div class="mn">Block-In</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-safewalk"><div class="mn">SafeWalk</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-mlg"><div class="mn">MLG</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-sprint"><div class="mn">Sprint</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
      </div>
      <div class="accent"></div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-render">
    <div class="panel">
      <div class="p-head">
        <div class="ic ic-render"><div class="i-dot"></div></div>
        <div class="p-title">Render</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="mod on" id="m-esp"><div class="mn">ESP</div><div class="key">G</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-tracers"><div class="mn">Tracers</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-chestesp"><div class="mn">ChestESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-xray"><div class="mn">X-Ray</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-chams"><div class="mn">Chams</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-nametags"><div class="mn">NameTags</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-itemesp"><div class="mn">ItemESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod on" id="m-storageesp"><div class="mn">StorageESP</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
      </div>
      <div class="accent"></div>
    </div>
    <div class="glow"></div>
  </div>

  <div class="pw p-utility">
    <div class="panel">
      <div class="p-head">
        <div class="ic ic-utility"><div class="i-square"></div></div>
        <div class="p-title">Utility</div>
        <div class="chev chev-u"></div>
      </div>
      <div class="p-body">
        <div class="mod on" id="m-fly"><div class="mn">Fly</div><div class="key">F</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-scaffold"><div class="mn">Scaffold</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-timer"><div class="mn">Timer</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-blink"><div class="mn">Blink</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-phase"><div class="mn">Phase</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-nofall"><div class="mn">NoFall</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-autoarmor"><div class="mn">AutoArmor</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-bhop"><div class="mn">BHop</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-fastplace"><div class="mn">FastPlace</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-autopot"><div class="mn">AutoPot</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-cheststeal"><div class="mn">ChestSteal</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-antiafk"><div class="mn">Anti-AFK</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-freecam"><div class="mn">Freecam</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
        <div class="mod" id="m-parkour"><div class="mn">Parkour</div><div class="dots"><div class="d"></div><div class="d"></div><div class="d"></div></div></div>
      </div>
      <div class="accent"></div>
    </div>
    <div class="glow"></div>
  </div>

</div>
</body>
</rml>)RML";
