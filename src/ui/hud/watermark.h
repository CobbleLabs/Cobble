#pragma once

// ============================================================================
// Watermark - a fully composable HUD badge.
//
// The watermark is a strip of ITEMS drawn inside an optional card. Nothing
// about the content is fixed: the strip is built out of text, an FPS readout, a
// clock, the session timer, a local image/GIF, separators and spacers, and each
// item carries its own font weight, colour source and spacing. The card
// (background, gradient, border, glow), the accent bar (on any side), the text
// effect, the layout axis and the placement are all independent settings, so
// "Cobble v1.7.0 | 120 FPS" is just the default arrangement rather than the
// only one.
//
// Everything is read from the render thread, and every item kind is backed by
// data that is safe there -- RenderData::gameFps, the wall clock, a monotonic
// session timer and the user's own text. No item touches JNI.
//
// The image path can be chosen with a native file dialog. That dialog is modal
// and pumps its own messages, so it runs on a worker thread and publishes the
// result back; see browseForImage() / pollPicker().
// ============================================================================

#include <cstddef>

namespace Watermark {

// ---- Item model ----------------------------------------------------------

// What an item draws. Text is the general case; the dynamic kinds render a
// value plus the item's own text as a unit label ("120" + "FPS").
enum Kind {
    KindText = 0,
    KindFps,
    KindClock,
    KindUptime,
    KindImage,
    KindSeparator,
    KindSpacer,
    KindCount
};

// Where an item's colour comes from. Primary/Secondary follow the two shared
// text colours, so a whole watermark can be recoloured from one place.
enum ColorMode { ColorPrimary = 0, ColorSecondary, ColorAccent, ColorRainbow, ColorCustom, ColorModeCount };

// Which of the two configured faces/sizes an item is drawn with.
enum Weight { WeightPrimary = 0, WeightSecondary };

enum LayoutAxis { LayoutHorizontal = 0, LayoutVertical };

// Cross-axis alignment of the content inside the card.
enum Alignment { AlignStart = 0, AlignCenter, AlignEnd };

enum BarSide { BarLeft = 0, BarRight, BarTop, BarBottom };

// Contrast treatment for text, for readability without a card behind it.
enum TextEffect { EffectNone = 0, EffectShadow, EffectOutline };

constexpr int kMaxItems = 12;
constexpr int kTextLen = 64;
constexpr int kPathLen = 512;

struct Item {
    bool enabled = true;
    int kind = KindText;
    int colorMode = ColorPrimary;
    int weight = WeightPrimary;
    // Dynamic kinds: draw `text` before the value instead of after it.
    bool labelBefore = false;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Trailing gap in unscaled pixels. Negative means "use the shared itemGap",
    // so changing the overall spacing stays a single slider.
    float gap = -1.0f;
    // Spacer length, in unscaled pixels. Unused by the other kinds.
    float size = 12.0f;
    // Literal text (KindText) or the unit label (KindFps / Clock / Uptime).
    char text[kTextLen] = {};
};

inline Item items[kMaxItems];
inline int itemCount = 0;
// Set by resetItems() and by a config load that carried an item list. render()
// seeds the default strip when it is still false, so a fresh install draws
// something even if the config never loaded.
inline bool itemsInitialized = false;

// ---- Global look ---------------------------------------------------------

inline bool enabled = true;
inline float scale = 1.0f;
inline float opacity = 1.0f;  // multiplies every colour the watermark draws

// Stable hud_fonts IDs (see ui/core/hud_fonts.h), not atlas slots. The default
// pairing is Inter Bold for values and Inter Regular for labels.
inline int fontPrimary = 1;
inline int fontSecondary = 0;
inline float fontSize = 19.0f;        // primary text size in unscaled pixels
inline float secondaryRatio = 0.85f;  // secondary size as a fraction of primary

inline int rainbowType = 0;  // 0 Flow, 1 Solid, 2 Fast, 3 Wide
inline float rainbowSaturation = 0.7f;

inline int layout = LayoutHorizontal;
inline int align = AlignCenter;
inline float itemGap = 7.0f;
// Gap between a dynamic item's value and its own unit label ("120" / "FPS").
inline float labelGap = 4.0f;

inline float primaryColor[4] = {0.9569f, 0.9608f, 0.9765f, 1.0f};
inline float secondaryColor[4] = {0.5490f, 0.5725f, 0.6196f, 1.0f};

// ---- Card ----------------------------------------------------------------

inline bool showBackground = true;
inline float backgroundColor[4] = {0.0f, 0.0f, 0.0f, 0.53f};
inline bool backgroundGradient = false;
inline float backgroundColor2[4] = {0.0784f, 0.0784f, 0.1098f, 0.53f};
inline bool gradientVertical = true;
inline float rounding = 0.0f;
inline float paddingX = 10.0f;
inline float paddingY = 6.0f;
inline bool showBorder = true;
inline float borderColor[4] = {1.0f, 1.0f, 1.0f, 0.06f};
inline float borderWidth = 1.0f;
// Soft outer glow, drawn as concentric rounded rects. Doubles as a drop shadow
// with a dark colour.
inline bool showGlow = false;
inline bool glowUseAccent = false;
inline float glowColor[4] = {0.0f, 0.0f, 0.0f, 0.45f};
inline float glowSize = 6.0f;

// ---- Accent bar ----------------------------------------------------------

inline bool showAccentBar = true;
inline int barSide = BarLeft;
inline int barColorMode = ColorAccent;
inline float barColor[4] = {0.9412f, 0.4667f, 0.0549f, 1.0f};
inline float barWidth = 3.0f;
inline float barGap = 9.0f;
inline float barInset = 0.0f;  // 0..0.45 of the bar trimmed off each end
inline float barRounding = 0.0f;

// ---- Text effect ---------------------------------------------------------

inline int textEffect = EffectNone;
inline float effectColor[4] = {0.0f, 0.0f, 0.0f, 0.70f};
inline float effectSize = 1.0f;

// ---- Separator items -----------------------------------------------------

inline float separatorColor[4] = {1.0f, 1.0f, 1.0f, 0.18f};
inline float separatorThickness = 1.0f;
inline float separatorLength = 0.36f;  // fraction of the content's cross extent

// ---- Clock / uptime formatting -------------------------------------------

inline bool clock24h = true;
inline bool clockSeconds = false;
inline bool uptimeHours = false;  // force h:mm:ss instead of m:ss under an hour

// ---- Image ---------------------------------------------------------------
//
// Read only while the watermark renders, so a missing or malformed asset never
// affects the rest of the overlay. Drawn by a KindImage item, which is what
// decides whether it is shown and where in the strip it sits.
inline char imagePath[kPathLen] = {};
inline float imageScale = 1.0f;
inline float imageOpacity = 1.0f;
inline float imageRounding = 0.0f;
inline float imageTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
inline float imageAnimationSpeed = 1.0f;
inline int imageReload = 0;  // bumped to force a re-decode

// ---- Placement -----------------------------------------------------------
//
// Stored as the screen fraction of the anchored corner (see hud_place.hpp), so
// the badge lands in the same relative spot at any resolution -- including the
// case where the DLL is injected while the game is already fullscreen, where
// absolute pixels saved from a windowed session described a different screen.
inline int anchor = 0;  // 0 TopLeft, 1 TopRight, 2 BottomLeft, 3 BottomRight
inline float relX = 0.0f;
inline float relY = 0.0f;
inline bool userPlaced = false;  // true only after an actual drag
inline float margin = 6.0f;      // corner inset used while auto-placed

// Drag state (same as the ArrayList): grab the box with the menu open, it
// follows the mouse while the menu is visually hidden, drop to place it.
inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;

// Last drawn box, in screen pixels (0 width = not drawn this frame). The
// ArrayList's auto-placement reads it so an auto-positioned list doesn't land
// on top of the watermark; one frame of staleness is invisible.
inline float lastX = 0.0f;
inline float lastY = 0.0f;
inline float lastW = 0.0f;
inline float lastH = 0.0f;

// ---- Media status --------------------------------------------------------

bool imageLoaded();
bool imageAnimated();
void requestImageReload();
// One short line for the settings page: dimensions, frame count, or why the
// asset didn't load.
const char* imageStatus();

// ---- Native file picker --------------------------------------------------
//
// browseForImage() only raises a request; a worker thread runs the dialog and
// pollPicker() applies the result on a later frame. pickerBusy() is true while
// the dialog is up -- the overlay uses it to keep the menu from auto-closing
// when the dialog takes focus away from the game window.
void browseForImage();
void revealImageFolder();
bool pickerBusy();
// True on the frame a newly picked path was applied, so the caller can persist
// the config exactly once instead of polling imagePath for changes.
bool pollPicker();

// ---- Item list editing ---------------------------------------------------
//
// The settings page drives the strip through these, so it never has to shuffle
// the array itself.
bool addItem(int kind);
void removeItem(int index);
void moveItem(int index, int delta);
const char* kindName(int kind);
bool hasKind(int kind);

// ---- Presets / resets ----------------------------------------------------

void resetItems();     // the default "Cobble v1.7.0 | 120 FPS" strip
void resetPosition();   // back to the anchored corner
void applyPreset(int preset);
int presetCount();
const char* const* presetNames();

void render();
}  // namespace Watermark
