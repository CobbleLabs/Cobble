#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "watermark.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_internal.h"  // ShadeVertsLinearColorGradientKeepAlpha
#include "../../sdk/player/render_data.h"
#include "../core/hud_fonts.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "hud_drag.hpp"
#include "hud_place.hpp"
#include "generated/version.h"  // COBBLE_VERSION
#include "../../../infra/ext/stb/stb_image.h"

#include "../../util/logger.hpp"

#include <Windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <gl/GL.h>

// Linked here rather than in the project file: these three are only needed by
// the watermark's file picker, and keeping the reference next to the code that
// uses it means the dependency is visible where it matters.
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// The version shown on the watermark is deliberately NOT COBBLE_VERSION.
//
// COBBLE_VERSION comes from cobble.json and is the version handed to the auth
// server: the licence check, the telemetry payload, the updater check and the
// session validator all send it, so it has to name a version the server has
// actually released or login fails. The watermark, on the other hand, should say
// what build this really is. Keep this string as the source-of-truth build number
// and bump cobble.json only once that build ships.
#define COBBLE_DISPLAY_VERSION "1.7.0"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Watermark;

namespace {

// UTF-8 -> UTF-16. Paths come either from the ImGui text field (UTF-8) or from
// the file dialog (UTF-16 converted to UTF-8 on the way in), so every path in
// this file is UTF-8 and only widens at the point it reaches the OS. Passing the
// narrow string straight to std::filesystem would leave the encoding up to the
// runtime, which silently mangles non-ASCII folder names.
std::wstring widen(const std::string& utf8) {
    if (utf8.empty())
        return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (needed <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.data(), needed);
    return out;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty())
        return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// Local image / GIF asset
// ---------------------------------------------------------------------------

struct WatermarkImage {
    std::string path;
    int revision = -1;
    HGLRC context = nullptr;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    int frameCount = 0;
    int frame = 0;
    float frameElapsedMs = 0.0f;
    std::vector<unsigned char> pixels;
    std::vector<int> delaysMs;
    char status[96] = {};
};

WatermarkImage g_image;

void deleteTextureIfCurrentContext() {
    if (g_image.texture != 0 && g_image.context == wglGetCurrentContext())
        glDeleteTextures(1, &g_image.texture);
    g_image.texture = 0;
}

void clearImageData(bool keepPath = false) {
    deleteTextureIfCurrentContext();
    if (!keepPath)
        g_image.path.clear();
    g_image.revision = -1;
    g_image.context = nullptr;
    g_image.width = 0;
    g_image.height = 0;
    g_image.frameCount = 0;
    g_image.frame = 0;
    g_image.frameElapsedMs = 0.0f;
    g_image.pixels.clear();
    g_image.delaysMs.clear();
}

bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(widen(path).c_str(), std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(64 * 1024 * 1024))
        return false;
    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool isGifPath(const std::string& path) {
    if (path.size() < 4)
        return false;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".gif";
}

bool uploadCurrentFrame() {
    if (!g_image.context || g_image.width <= 0 || g_image.height <= 0 || g_image.frameCount <= 0 ||
        g_image.pixels.empty())
        return false;

    const size_t frameBytes = static_cast<size_t>(g_image.width) * static_cast<size_t>(g_image.height) * 4u;
    const size_t offset = static_cast<size_t>(g_image.frame) * frameBytes;
    if (offset > g_image.pixels.size() || frameBytes > g_image.pixels.size() - offset)
        return false;

    if (g_image.texture == 0)
        glGenTextures(1, &g_image.texture);
    if (g_image.texture == 0)
        return false;

    glBindTexture(GL_TEXTURE_2D, g_image.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_image.width, g_image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_image.pixels.data() + offset);
    return true;
}

void setStatus(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_image.status, sizeof(g_image.status), fmt, args);
    va_end(args);
}

// Rebind the asset to the requested path, keeping the identity fields so
// ensureImageAsset() doesn't retry the same broken file every frame.
void failLoad(const std::string& path, int revision, const char* why) {
    clearImageData(true);
    g_image.path = path;
    g_image.revision = revision;
    g_image.context = wglGetCurrentContext();
    setStatus("%s", why);
}

bool loadImageAsset(const std::string& path, int revision) {
    clearImageData(true);
    g_image.path = path;
    g_image.revision = revision;
    g_image.context = wglGetCurrentContext();
    g_image.status[0] = '\0';
    if (path.empty())
        return false;
    if (!g_image.context) {
        setStatus("No GL context");
        return false;
    }

    std::vector<unsigned char> bytes;
    if (!readFileBytes(path, bytes)) {
        failLoad(path, revision, "File not found or too large");
        return false;
    }

    // Do NOT flip. ImGui maps uv.y = 0 to the FIRST row handed to glTexImage2D
    // and draws it at the top of the quad, and stb_image already decodes top row
    // first -- so flipping here is exactly what drew every watermark image upside
    // down. The flag is a process-global that stb keeps between calls and that
    // util/textures/item_textures.cpp sets to true for its own loads, so it has to
    // be cleared explicitly on every load rather than left at whatever ran last.
    stbi_set_flip_vertically_on_load(false);

    int width = 0, height = 0, frames = 0, channels = 0;
    int* delays = nullptr;
    stbi_uc* decoded = nullptr;
    if (isGifPath(path)) {
        decoded = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &width, &height,
                                            &frames, &channels, 4);
    }

    if (decoded && frames > 0) {
        const size_t framePixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t totalPixels = framePixels * static_cast<size_t>(frames);
        if (width <= 0 || height <= 0 || totalPixels == 0 || totalPixels > 16u * 1024u * 1024u) {
            stbi_image_free(decoded);
            if (delays)
                stbi_image_free(delays);
            failLoad(path, revision, "GIF too large to decode");
            return false;
        }
        g_image.width = width;
        g_image.height = height;
        g_image.frameCount = frames;
        g_image.pixels.assign(decoded, decoded + totalPixels * 4u);
        g_image.delaysMs.resize(static_cast<size_t>(frames), 100);
        for (int i = 0; i < frames; ++i)
            g_image.delaysMs[static_cast<size_t>(i)] = (delays && delays[i] > 0) ? delays[i] * 10 : 100;
        stbi_image_free(decoded);
        if (delays)
            stbi_image_free(delays);
        if (!uploadCurrentFrame()) {
            failLoad(path, revision, "Upload failed");
            return false;
        }
        setStatus("GIF %dx%d, %d frames", width, height, frames);
        return true;
    }
    if (decoded)
        stbi_image_free(decoded);
    if (delays)
        stbi_image_free(delays);

    decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    const bool tooBig = decoded && static_cast<size_t>(width) * static_cast<size_t>(height) > 16u * 1024u * 1024u;
    if (!decoded || width <= 0 || height <= 0 || tooBig) {
        if (decoded)
            stbi_image_free(decoded);
        failLoad(path, revision, tooBig ? "Image too large" : "Unsupported or corrupt image");
        return false;
    }

    g_image.width = width;
    g_image.height = height;
    g_image.frameCount = 1;
    g_image.pixels.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    stbi_image_free(decoded);
    if (!uploadCurrentFrame()) {
        failLoad(path, revision, "Upload failed");
        return false;
    }
    setStatus("Image %dx%d", width, height);
    return true;
}

void ensureImageAsset() {
    const std::string requested(imagePath);
    const int revision = imageReload;
    const HGLRC context = wglGetCurrentContext();
    if (requested != g_image.path || revision != g_image.revision) {
        loadImageAsset(requested, revision);
        return;
    }
    if (!requested.empty() && context != g_image.context) {
        // The overlay recreates ImGui when Minecraft changes GL contexts. The
        // decoded pixels remain reusable, but the old GL texture does not.
        g_image.context = context;
        g_image.texture = 0;
        uploadCurrentFrame();
    }
}

void advanceImage(float dtMs) {
    if (g_image.frameCount <= 1 || g_image.texture == 0 || g_image.delaysMs.empty())
        return;
    g_image.frameElapsedMs += dtMs * (std::max)(0.05f, imageAnimationSpeed);
    // Bound the catch-up: a long stall (loading screen, alt-tab) would otherwise
    // spin through thousands of frames and re-upload the texture for each one.
    int guard = 0;
    while (g_image.frameElapsedMs >= g_image.delaysMs[static_cast<size_t>(g_image.frame)] && guard++ < 64) {
        g_image.frameElapsedMs -= static_cast<float>(g_image.delaysMs[static_cast<size_t>(g_image.frame)]);
        g_image.frame = (g_image.frame + 1) % g_image.frameCount;
        uploadCurrentFrame();
    }
    if (guard >= 64)
        g_image.frameElapsedMs = 0.0f;
}

// ---------------------------------------------------------------------------
// Native file picker
//
// The overlay draws from inside the wglSwapBuffers hook, i.e. on the game's
// render thread. GetOpenFileNameW is modal and runs its own message loop, so
// calling it from the settings page would stall presentation and let that loop
// re-enter the hook mid-frame. It runs on a detached worker instead: the button
// raises a request, the worker publishes a path, and the render thread applies it
// on a later frame.
// ---------------------------------------------------------------------------

std::atomic<bool> g_pickerBusy{false};
std::atomic<bool> g_pickerReady{false};
std::mutex g_pickerMutex;
std::string g_pickerResult;

void pickerWorker() {
    // STA for the shell's common item dialog. GetOpenFileNameW without a custom
    // template or hook is redirected to it on Vista+, which is what makes this
    // the real Explorer browser rather than the Win3.1-era one.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    wchar_t buffer[2048] = {};
    // Seed with the current path so re-opening the dialog lands in the same folder.
    {
        std::lock_guard<std::mutex> lock(g_pickerMutex);
        const std::wstring current = widen(g_pickerResult);
        if (!current.empty() && current.size() < 1024)
            std::memcpy(buffer, current.c_str(), (current.size() + 1) * sizeof(wchar_t));
        g_pickerResult.clear();
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    // No owner window on purpose: an owned modal dialog disables input on the
    // game window for as long as it is up, and the owner lives on another thread.
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Images (png, jpg, bmp, gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrTitle = L"Choose a watermark image";
    // OFN_NOCHANGEDIR matters: the dialog would otherwise move the *process*
    // working directory, and Minecraft resolves its own resources relative to it.
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;

    const bool picked = GetOpenFileNameW(&ofn) && buffer[0] != L'\0';
    if (picked) {
        std::lock_guard<std::mutex> lock(g_pickerMutex);
        g_pickerResult = narrow(buffer);
        g_pickerReady.store(true);
    }

    if (SUCCEEDED(hr))
        CoUninitialize();
    g_pickerBusy.store(false);
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

ImU32 colorFromFloats(const float color[4]) {
    const auto channel = [](float value) { return (int)((std::clamp)(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(channel(color[0]), channel(color[1]), channel(color[2]), channel(color[3]));
}

ImU32 mulAlpha(ImU32 color, float f) {
    const int a = (int)(((color >> IM_COL32_A_SHIFT) & 0xFF) * (std::clamp)(f, 0.0f, 1.0f));
    return (color & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

ImU32 setAlpha(ImU32 color, float a01) {
    const int a = (int)((std::clamp)(a01, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

// Fill a rounded rect with a two-colour gradient. AddRectFilledMultiColor has no
// rounding, so the shape is filled flat and its vertices are then shaded, which
// is ImGui's own supported way of gradient-filling an arbitrary convex path.
// Alpha follows the first colour (that is what "KeepAlpha" means) -- the gradient
// blends RGB only.
void fillGradient(ImDrawList* draw, ImVec2 p1, ImVec2 p2, ImU32 c1, ImU32 c2, float round, bool vertical) {
    const int first = draw->VtxBuffer.Size;
    draw->AddRectFilled(p1, p2, c1, round);
    const ImVec2 g0 = p1;
    const ImVec2 g1 = vertical ? ImVec2(p1.x, p2.y) : ImVec2(p2.x, p1.y);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, first, draw->VtxBuffer.Size, g0, g1, c1, c2);
}

// ---------------------------------------------------------------------------
// Live values (render-thread safe: no JNI)
// ---------------------------------------------------------------------------

float g_fpsSmooth = 0.0f;
long long g_sessionStartMs = 0;

long long nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void formatClock(char* out, size_t n) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    int hour = static_cast<int>(st.wHour);
    const char* suffix = "";
    if (!clock24h) {
        suffix = hour >= 12 ? " PM" : " AM";
        hour %= 12;
        if (hour == 0)
            hour = 12;
    }
    if (clockSeconds)
        std::snprintf(out, n, "%d:%02d:%02d%s", hour, (int)st.wMinute, (int)st.wSecond, suffix);
    else
        std::snprintf(out, n, "%d:%02d%s", hour, (int)st.wMinute, suffix);
}

void formatUptime(char* out, size_t n) {
    if (g_sessionStartMs == 0)
        g_sessionStartMs = nowMs();
    const long long elapsed = nowMs() - g_sessionStartMs;
    const int total = static_cast<int>((std::max)(0LL, elapsed) / 1000);
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int seconds = total % 60;
    if (hours > 0 || uptimeHours)
        std::snprintf(out, n, "%d:%02d:%02d", hours, minutes, seconds);
    else
        std::snprintf(out, n, "%d:%02d", minutes, seconds);
}

// ---------------------------------------------------------------------------
// Layout
//
// One measured cell per visible item. `w`/`h` are pixel extents; which of the two
// is the layout axis depends on `layout`, so the same cell list drives both the
// horizontal strip and the vertical stack.
// ---------------------------------------------------------------------------

struct Cell {
    const Item* item = nullptr;
    int index = 0;  // item index, used as the rainbow phase offset
    int kind = KindText;
    float w = 0.0f;
    float h = 0.0f;
    float gapAfter = 0.0f;

    bool isText = false;
    char value[48] = {};
    const char* label = nullptr;
    ImFont* valueFont = nullptr;
    ImFont* labelFont = nullptr;
    float valueFS = 0.0f;
    float labelFS = 0.0f;
    float valueW = 0.0f;
    float labelW = 0.0f;
    float valueAscent = 0.0f;
    float labelAscent = 0.0f;
    bool labelFirst = false;
    float labelGap = 0.0f;
};

float ascentPx(ImFont* f, float size) {
    if (!f || f->FontSize <= 0.0f)
        return size * 0.8f;
    return f->Ascent * (size / f->FontSize);
}

ImU32 modeColor(int colorMode, const float custom[4], int phase, long long timeMs) {
    switch (colorMode) {
        case ColorSecondary: return colorFromFloats(secondaryColor);
        case ColorAccent: return GetAccentRGB();
        case ColorRainbow: return hud::GetRainbowColorEx(phase, rainbowSaturation, timeMs, rainbowType);
        case ColorCustom: return colorFromFloats(custom);
        default: return colorFromFloats(primaryColor);
    }
}

// Draw one text run and return its advance. Rainbow runs are drawn glyph by
// glyph so the colour flows along the word (the signature watermark look);
// everything else is a single AddText. The contrast effect is drawn first, for
// the whole run at once, so it never shows between glyphs.
float drawRun(ImDrawList* draw, ImFont* f, float fs, float x, float y, const char* txt, ImU32 flat, bool rainbowRun,
              int phase, long long timeMs, float effectPx, ImU32 effectCol) {
    if (!draw || !f || !txt || !*txt)
        return 0.0f;

    if (effectPx > 0.0f && ((effectCol >> IM_COL32_A_SHIFT) & 0xFF) != 0) {
        if (textEffect == EffectShadow) {
            draw->AddText(f, fs, ImVec2(x + effectPx, y + effectPx), effectCol, txt);
        } else if (textEffect == EffectOutline) {
            const float taps[4][2] = {{-effectPx, 0.0f}, {effectPx, 0.0f}, {0.0f, -effectPx}, {0.0f, effectPx}};
            for (const auto& t : taps)
                draw->AddText(f, fs, ImVec2(x + t[0], y + t[1]), effectCol, txt);
        }
    }

    if (!rainbowRun) {
        draw->AddText(f, fs, ImVec2(x, y), flat, txt);
        return f->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt).x;
    }

    float cx = x;
    for (int i = 0, glyph = 0; txt[i] != '\0'; ++glyph) {
        // Keep UTF-8 sequences together, or a non-ASCII title would be drawn as
        // a run of replacement boxes.
        const unsigned char lead = static_cast<unsigned char>(txt[i]);
        int want = 1;
        if (lead >= 0xF0)
            want = 4;
        else if (lead >= 0xE0)
            want = 3;
        else if (lead >= 0xC0)
            want = 2;
        char ch[5] = {};
        int got = 0;
        for (; got < want && txt[i + got] != '\0'; ++got)
            ch[got] = txt[i + got];
        if (got == 0)
            break;
        i += got;
        const ImU32 col = hud::GetRainbowColorEx(phase + glyph, rainbowSaturation, timeMs, rainbowType);
        draw->AddText(f, fs, ImVec2(cx, y), col, ch);
        cx += f->CalcTextSizeA(fs, FLT_MAX, 0.0f, ch).x;
    }
    return cx - x;
}

void setText(char* dst, size_t n, const char* src) {
    if (!dst || n == 0)
        return;
    std::snprintf(dst, n, "%s", src ? src : "");
}

Item makeItem(int kind, const char* text, int weight, int colorMode) {
    Item it;
    it.enabled = true;
    it.kind = kind;
    it.weight = weight;
    it.colorMode = colorMode;
    setText(it.text, sizeof(it.text), text);
    return it;
}

// Presets rewrite whole colours at once; spelling out four assignments each time
// buried the intent.
void setColor(float* dst, float r, float g, float b, float a) {
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a;
}

const char* const kKindNames[KindCount] = {"Text", "FPS", "Clock", "Uptime", "Image", "Separator", "Spacer"};
const char* const kPresetNames[] = {"Classic", "Minimal", "Card", "Neon"};

}  // namespace

// ---------------------------------------------------------------------------
// Media status
// ---------------------------------------------------------------------------

bool Watermark::imageLoaded() { return g_image.texture != 0 && g_image.width > 0 && g_image.height > 0; }

bool Watermark::imageAnimated() { return g_image.frameCount > 1; }

void Watermark::requestImageReload() { ++imageReload; }

const char* Watermark::imageStatus() {
    if (imagePath[0] == '\0')
        return "No image selected";
    if (g_image.status[0] != '\0')
        return g_image.status;
    return "Not loaded yet";
}

// ---------------------------------------------------------------------------
// File picker
// ---------------------------------------------------------------------------

void Watermark::browseForImage() {
    bool expected = false;
    if (!g_pickerBusy.compare_exchange_strong(expected, true))
        return;  // a dialog is already up
    {
        std::lock_guard<std::mutex> lock(g_pickerMutex);
        g_pickerResult.assign(imagePath);
        g_pickerReady.store(false);
    }
    try {
        std::thread(pickerWorker).detach();
    } catch (...) {
        g_pickerBusy.store(false);
    }
}

void Watermark::revealImageFolder() {
    // Explorer's /select takes the file, not the folder, and highlights it.
    std::string path(imagePath);
    if (path.empty())
        return;
    try {
        std::thread([path]() {
            const std::wstring args = L"/select,\"" + widen(path) + L"\"";
            ShellExecuteW(nullptr, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }).detach();
    } catch (...) {
    }
}

bool Watermark::pickerBusy() { return g_pickerBusy.load(); }

bool Watermark::pollPicker() {
    if (!g_pickerReady.load())
        return false;
    std::string picked;
    {
        std::lock_guard<std::mutex> lock(g_pickerMutex);
        picked = g_pickerResult;
        g_pickerReady.store(false);
    }
    if (picked.empty())
        return false;
    setText(imagePath, sizeof(imagePath), picked.c_str());
    ++imageReload;
    // Picking a file is a request to see it, so make sure the strip actually has
    // an image slot instead of silently storing a path nothing draws.
    if (!hasKind(KindImage))
        addItem(KindImage);
    return true;
}

// ---------------------------------------------------------------------------
// Item list editing
// ---------------------------------------------------------------------------

const char* Watermark::kindName(int kind) {
    if (kind < 0 || kind >= KindCount)
        return "?";
    return kKindNames[kind];
}

bool Watermark::hasKind(int kind) {
    for (int i = 0; i < itemCount; ++i)
        if (items[i].kind == kind)
            return true;
    return false;
}

bool Watermark::addItem(int kind) {
    if (itemCount >= kMaxItems)
        return false;
    if (kind < 0 || kind >= KindCount)
        kind = KindText;

    Item it;
    it.kind = kind;
    switch (kind) {
        case KindText: setText(it.text, sizeof(it.text), "Text"); break;
        case KindFps: setText(it.text, sizeof(it.text), "FPS"); break;
        case KindUptime: setText(it.text, sizeof(it.text), "UPTIME"); break;
        case KindClock: it.colorMode = ColorSecondary; break;
        default: break;
    }
    items[itemCount++] = it;
    itemsInitialized = true;
    return true;
}

void Watermark::removeItem(int index) {
    if (index < 0 || index >= itemCount)
        return;
    for (int i = index; i + 1 < itemCount; ++i)
        items[i] = items[i + 1];
    --itemCount;
    items[itemCount] = Item();
    itemsInitialized = true;
}

void Watermark::moveItem(int index, int delta) {
    const int target = index + delta;
    if (index < 0 || index >= itemCount || target < 0 || target >= itemCount)
        return;
    const Item tmp = items[index];
    items[index] = items[target];
    items[target] = tmp;
    itemsInitialized = true;
}

// ---------------------------------------------------------------------------
// Resets and presets
// ---------------------------------------------------------------------------

void Watermark::resetItems() {
    itemCount = 0;
    items[itemCount++] = makeItem(KindText, "Cobble", WeightPrimary, ColorRainbow);
    items[itemCount++] = makeItem(KindText, "v" COBBLE_DISPLAY_VERSION, WeightSecondary, ColorSecondary);
    items[itemCount - 1].gap = 9.0f;
    items[itemCount++] = makeItem(KindSeparator, "", WeightPrimary, ColorPrimary);
    items[itemCount - 1].gap = 9.0f;
    items[itemCount++] = makeItem(KindFps, "FPS", WeightPrimary, ColorAccent);
    itemsInitialized = true;
}

void Watermark::resetPosition() {
    // Placement is recomputed from `anchor` + `margin` on the next frame; there
    // are no pixels to clear because none are stored.
    userPlaced = false;
    dragging = false;
}

int Watermark::presetCount() { return static_cast<int>(sizeof(kPresetNames) / sizeof(kPresetNames[0])); }

const char* const* Watermark::presetNames() { return kPresetNames; }

void Watermark::applyPreset(int preset) {
    // Every preset starts from the same baseline so switching between them is
    // deterministic -- otherwise a field only one preset touches would linger.
    scale = 1.0f;
    opacity = 1.0f;
    fontPrimary = 1;
    fontSecondary = 0;
    fontSize = 19.0f;
    secondaryRatio = 0.85f;
    rainbowType = 0;
    rainbowSaturation = 0.7f;
    layout = LayoutHorizontal;
    align = AlignCenter;
    itemGap = 7.0f;
    labelGap = 4.0f;
    setColor(primaryColor, 0.9569f, 0.9608f, 0.9765f, 1.0f);
    setColor(secondaryColor, 0.5490f, 0.5725f, 0.6196f, 1.0f);

    showBackground = true;
    setColor(backgroundColor, 0.0f, 0.0f, 0.0f, 0.53f);
    backgroundGradient = false;
    setColor(backgroundColor2, 0.0784f, 0.0784f, 0.1098f, 0.53f);
    gradientVertical = true;
    rounding = 0.0f;
    paddingX = 10.0f;
    paddingY = 6.0f;
    showBorder = true;
    setColor(borderColor, 1.0f, 1.0f, 1.0f, 0.06f);
    borderWidth = 1.0f;
    showGlow = false;
    glowUseAccent = false;
    setColor(glowColor, 0.0f, 0.0f, 0.0f, 0.45f);
    glowSize = 6.0f;

    showAccentBar = true;
    barSide = BarLeft;
    barColorMode = ColorAccent;
    barWidth = 3.0f;
    barGap = 9.0f;
    barInset = 0.0f;
    barRounding = 0.0f;

    textEffect = EffectNone;
    setColor(effectColor, 0.0f, 0.0f, 0.0f, 0.70f);
    effectSize = 1.0f;

    setColor(separatorColor, 1.0f, 1.0f, 1.0f, 0.18f);
    separatorThickness = 1.0f;
    separatorLength = 0.36f;

    resetItems();

    switch (preset) {
        case 1:  // Minimal -- text only, readable over any scene via a shadow.
            showBackground = false;
            showBorder = false;
            showAccentBar = false;
            textEffect = EffectShadow;
            effectSize = 1.0f;
            itemGap = 6.0f;
            itemCount = 0;
            items[itemCount++] = makeItem(KindText, "Cobble", WeightPrimary, ColorPrimary);
            items[itemCount++] = makeItem(KindFps, "FPS", WeightSecondary, ColorSecondary);
            break;

        case 2:  // Card -- rounded, gradient, soft drop shadow, no accent bar.
            backgroundGradient = true;
            setColor(backgroundColor, 0.0706f, 0.0745f, 0.1020f, 0.86f);
            setColor(backgroundColor2, 0.0275f, 0.0314f, 0.0471f, 0.86f);
            rounding = 7.0f;
            paddingX = 12.0f;
            paddingY = 7.0f;
            showGlow = true;
            setColor(glowColor, 0.0f, 0.0f, 0.0f, 0.38f);
            glowSize = 8.0f;
            showAccentBar = false;
            break;

        case 3:  // Neon -- rainbow name, accent glow, sits on the scene itself.
            showBackground = false;
            showBorder = false;
            showGlow = true;
            glowUseAccent = true;
            glowSize = 10.0f;
            textEffect = EffectOutline;
            setColor(effectColor, 0.0f, 0.0f, 0.0f, 0.85f);
            barSide = BarBottom;
            barWidth = 2.0f;
            barGap = 5.0f;
            barRounding = 1.0f;
            rainbowType = 2;
            rainbowSaturation = 0.85f;
            itemCount = 0;
            items[itemCount++] = makeItem(KindText, "Cobble", WeightPrimary, ColorRainbow);
            items[itemCount - 1].gap = 9.0f;
            items[itemCount++] = makeItem(KindFps, "FPS", WeightPrimary, ColorRainbow);
            break;

        default:  // 0 = Classic, already set up above.
            break;
    }

    itemsInitialized = true;
}

// ---------------------------------------------------------------------------
// Render
//
// Four steps, in order: measure every visible item into a Cell, resolve the two
// sizes that depend on the whole content (separator length and the box), place
// the box, then draw the card and walk the cells. Measuring first is what lets a
// separator be as tall as the tallest text and the card wrap whatever the strip
// happens to contain.
// ---------------------------------------------------------------------------

void Watermark::render() {
    if (!enabled) {
        // Report "not drawn" so the ArrayList's avoidance doesn't dodge a box
        // that is no longer on screen.
        lastW = 0.0f;
        lastH = 0.0f;
        return;
    }

    // A fresh install -- or a config saved before the item list existed -- still
    // draws the default strip instead of an empty card.
    if (!itemsInitialized)
        resetItems();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    // GetById already falls back to an embedded Inter face, so a null here means
    // the atlas itself isn't usable yet.
    ImFont* primaryFont = hud_fonts::GetById(fontPrimary);
    ImFont* secondaryFont = hud_fonts::GetById(fontSecondary);
    if (!primaryFont || primaryFont->FontSize <= 0.0f)
        return;
    if (!secondaryFont || secondaryFont->FontSize <= 0.0f)
        secondaryFont = primaryFont;

    // These ceilings are the real limits, not the settings page's slider ends: the
    // size numbers there accept a typed value, which is how you ask for a badge
    // bigger than the grab can reach. They only have to keep a typo (a font size of
    // 99999) from turning the strip into one screen-filling glyph.
    const float s = (std::clamp)(scale, 0.25f, 8.0f);
    const float sizePrimary = (std::clamp)(fontSize * s, 6.0f, 512.0f);
    const float sizeSecondary =
        (std::clamp)(fontSize * (std::clamp)(secondaryRatio, 0.3f, 2.0f) * s, 6.0f, 512.0f);
    const float op = (std::clamp)(opacity, 0.0f, 1.0f);

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / 60.0f;  // first frame / hitch guard

    // Frame-rate smoothing is safe to do per call here: render() runs once per
    // presented frame, unlike a module's onUpdate.
    const float fpsNow = static_cast<float>(RenderData::gameFps);
    if (g_fpsSmooth <= 0.0f)
        g_fpsSmooth = fpsNow;
    else
        g_fpsSmooth += (fpsNow - g_fpsSmooth) * (std::clamp)(dt * 6.0f, 0.0f, 1.0f);

    ensureImageAsset();
    advanceImage(dt * 1000.0f);

    // Wall clock, matching the ArrayList so both rainbows are in phase.
    const long long timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    // ---- Measure ----------------------------------------------------------

    const bool horizontal = (layout != LayoutVertical);

    Cell cells[kMaxItems];
    int cellCount = 0;
    float contentCross = 0.0f;  // tallest (widest, when vertical) non-separator cell

    for (int i = 0; i < itemCount && cellCount < kMaxItems; ++i) {
        const Item& it = items[i];
        if (!it.enabled)
            continue;

        Cell c;
        c.item = &it;
        c.index = i;
        c.kind = it.kind;
        // A negative per-item gap means "follow the shared spacing slider".
        c.gapAfter = it.gap < 0.0f ? itemGap * s : it.gap * s;

        const bool useSecondary = (it.weight == WeightSecondary);
        ImFont* face = useSecondary ? secondaryFont : primaryFont;
        const float faceSize = useSecondary ? sizeSecondary : sizePrimary;

        switch (it.kind) {
            case KindText: setText(c.value, sizeof(c.value), it.text); break;
            case KindFps: std::snprintf(c.value, sizeof(c.value), "%d", (int)(g_fpsSmooth + 0.5f)); break;
            case KindClock: formatClock(c.value, sizeof(c.value)); break;
            case KindUptime: formatUptime(c.value, sizeof(c.value)); break;
            default: break;
        }

        if (it.kind == KindText || it.kind == KindFps || it.kind == KindClock || it.kind == KindUptime) {
            if (c.value[0] == '\0')
                continue;  // nothing to draw, and an empty cell would still eat a gap
            c.isText = true;
            c.valueFont = face;
            c.valueFS = faceSize;
            // A dynamic item's own text is a unit label drawn next to the value;
            // a plain text item has no label -- its text IS the value. The label
            // always takes the secondary face, so "120 FPS" reads as one figure
            // with a unit rather than two competing words.
            c.label = (it.kind != KindText && it.text[0] != '\0') ? it.text : nullptr;
            c.labelFont = secondaryFont;
            c.labelFS = sizeSecondary;
            c.labelFirst = it.labelBefore;
            c.labelGap = (std::max)(0.0f, labelGap * s);
            c.valueW = c.valueFont->CalcTextSizeA(c.valueFS, FLT_MAX, 0.0f, c.value).x;
            c.labelW = c.label ? c.labelFont->CalcTextSizeA(c.labelFS, FLT_MAX, 0.0f, c.label).x : 0.0f;
            c.valueAscent = ascentPx(c.valueFont, c.valueFS);
            c.labelAscent = c.label ? ascentPx(c.labelFont, c.labelFS) : 0.0f;
            c.w = c.valueW + (c.label ? c.labelGap + c.labelW : 0.0f);
            c.h = (std::max)(c.valueFS, c.label ? c.labelFS : 0.0f);
        } else if (it.kind == KindImage) {
            if (!imageLoaded())
                continue;  // no asset yet: the slot simply collapses
            // Height tracks the primary text size, so an image lines up with the
            // strip by default and imageScale is a relative nudge. The ceiling is far
            // above the slider's 4x because the number can be typed.
            const float h = (std::max)(2.0f, sizePrimary * (std::clamp)(imageScale, 0.1f, 32.0f));
            const float aspect = static_cast<float>(g_image.width) / static_cast<float>(g_image.height);
            c.h = h;
            c.w = (std::max)(2.0f, h * aspect);
        } else if (it.kind == KindSeparator) {
            // Cross-axis length is resolved below, once the content extent is known.
            const float thick = (std::max)(1.0f, separatorThickness * s);
            c.w = horizontal ? thick : 0.0f;
            c.h = horizontal ? 0.0f : thick;
        } else if (it.kind == KindSpacer) {
            const float len = (std::max)(0.0f, it.size * s);
            c.w = horizontal ? len : 0.0f;
            c.h = horizontal ? 0.0f : len;
        }

        if (it.kind != KindSeparator)
            contentCross = (std::max)(contentCross, horizontal ? c.h : c.w);

        cells[cellCount++] = c;
    }

    if (cellCount == 0) {
        lastW = 0.0f;
        lastH = 0.0f;
        return;
    }
    cells[cellCount - 1].gapAfter = 0.0f;  // no trailing gap inside the card

    // A strip of nothing but separators has no text to measure against.
    if (contentCross <= 0.0f)
        contentCross = sizePrimary;

    const float sepCross = (std::max)(1.0f, contentCross * (std::clamp)(separatorLength, 0.05f, 1.0f));
    for (int i = 0; i < cellCount; ++i) {
        if (cells[i].kind != KindSeparator)
            continue;
        if (horizontal)
            cells[i].h = sepCross;
        else
            cells[i].w = sepCross;
    }

    float contentW = 0.0f;
    float contentH = 0.0f;
    for (int i = 0; i < cellCount; ++i) {
        if (horizontal) {
            contentW += cells[i].w + cells[i].gapAfter;
            contentH = (std::max)(contentH, cells[i].h);
        } else {
            contentH += cells[i].h + cells[i].gapAfter;
            contentW = (std::max)(contentW, cells[i].w);
        }
    }

    // ---- Box --------------------------------------------------------------
    //
    // Padding belongs to the card, so a background-less watermark sits exactly
    // where it is placed instead of floating inside invisible margins. The accent
    // bar lives inside the card and reserves its own strip plus a gap.
    const bool hasCard = showBackground || showBorder;
    const float padX = hasCard ? (std::max)(0.0f, paddingX * s) : 0.0f;
    const float padY = hasCard ? (std::max)(0.0f, paddingY * s) : 0.0f;
    const float barThick = showAccentBar ? (std::max)(0.5f, barWidth * s) : 0.0f;
    const float barSpace = showAccentBar ? barThick + (std::max)(0.0f, barGap * s) : 0.0f;
    const bool barVertical = (barSide == BarLeft || barSide == BarRight);

    float boxW = contentW + padX * 2.0f + (barVertical ? barSpace : 0.0f);
    float boxH = contentH + padY * 2.0f + (barVertical ? 0.0f : barSpace);

    // ---- Placement --------------------------------------------------------
    //
    // Fractions of the live screen, never saved pixels: injecting into a game that
    // is already fullscreen, or loading a config written at another resolution,
    // used to leave the badge at coordinates measured against a different screen.
    float screenW = 0.0f;
    float screenH = 0.0f;
    if (!hud::screenSize(screenW, screenH))
        return;  // no usable screen yet -- skip the frame rather than clamp to 0,0

    const bool anchorRight = (anchor == 1 || anchor == 3);
    const bool anchorBottom = (anchor == 2 || anchor == 3);

    float boxX = 0.0f;
    float boxY = 0.0f;
    if (userPlaced) {
        hud::placeToPixels(anchorRight, anchorBottom, relX, relY, boxW, boxH, screenW, screenH, boxX, boxY);
    } else {
        const float inset = (std::max)(0.0f, margin * s);
        boxX = anchorRight ? (screenW - boxW - inset) : inset;
        boxY = anchorBottom ? (screenH - boxH - inset) : inset;
    }

    // Shared grab-and-drag: hover with the menu open to grab, drop to place.
    hud::dragBox(boxX, boxY, boxW, boxH, 2.0f, dragging, dragOffsetX, dragOffsetY);
    if (dragging)
        userPlaced = true;  // an actual drag, unlike auto-placement, is a decision

    boxX = (std::clamp)(boxX, 0.0f, (std::max)(0.0f, screenW - boxW));
    boxY = (std::clamp)(boxY, 0.0f, (std::max)(0.0f, screenH - boxH));

    // Store back what was really drawn, so a clamp or a resize can't desync the
    // saved fraction from the pixels.
    if (userPlaced)
        hud::placeToFractions(anchorRight, anchorBottom, boxX, boxY, boxW, boxH, screenW, screenH, relX, relY);

    lastX = boxX;
    lastY = boxY;
    lastW = boxW;
    lastH = boxH;

    // ---- Card -------------------------------------------------------------

    const ImVec2 p1(boxX, boxY);
    const ImVec2 p2(boxX + boxW, boxY + boxH);
    const float round = (std::clamp)(rounding * s, 0.0f, (std::min)(boxW, boxH) * 0.5f);

    if (showGlow && glowSize > 0.0f) {
        // Concentric rounded rects with a quadratic falloff: cheap, and it reads as
        // a glow with an accent colour or as a drop shadow with a dark one.
        const ImU32 glowBase =
            glowUseAccent ? setAlpha(GetAccentRGB(), glowColor[3]) : colorFromFloats(glowColor);
        const float span = (std::max)(1.0f, glowSize * s);
        const int steps = (std::clamp)(static_cast<int>(span), 1, 14);
        for (int i = steps; i >= 1; --i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float grow = span * t;
            const float fade = (1.0f - t) * (1.0f - t) * 0.75f + 0.06f;
            draw->AddRectFilled(ImVec2(p1.x - grow, p1.y - grow), ImVec2(p2.x + grow, p2.y + grow),
                                mulAlpha(glowBase, fade * op), round + grow);
        }
    }

    if (showBackground) {
        const ImU32 bg = mulAlpha(colorFromFloats(backgroundColor), op);
        if (backgroundGradient)
            fillGradient(draw, p1, p2, bg, mulAlpha(colorFromFloats(backgroundColor2), op), round,
                         gradientVertical);
        else
            draw->AddRectFilled(p1, p2, bg, round);
    }

    if (showBorder && borderWidth > 0.0f)
        draw->AddRect(p1, p2, mulAlpha(colorFromFloats(borderColor), op), round, ImDrawFlags_None,
                      (std::max)(0.5f, borderWidth * s));

    if (showAccentBar && barThick > 0.0f) {
        const ImU32 barCol = mulAlpha(modeColor(barColorMode, barColor, 0, timeMs), op);
        const float inset = (std::clamp)(barInset, 0.0f, 0.45f);
        float bx1, by1, bx2, by2;
        if (barVertical) {
            bx1 = (barSide == BarLeft) ? (boxX + padX) : (boxX + boxW - padX - barThick);
            bx2 = bx1 + barThick;
            const float trim = (boxH - padY * 2.0f) * inset;
            by1 = boxY + padY + trim;
            by2 = boxY + boxH - padY - trim;
        } else {
            by1 = (barSide == BarTop) ? (boxY + padY) : (boxY + boxH - padY - barThick);
            by2 = by1 + barThick;
            const float trim = (boxW - padX * 2.0f) * inset;
            bx1 = boxX + padX + trim;
            bx2 = boxX + boxW - padX - trim;
        }
        if (bx2 > bx1 && by2 > by1)
            draw->AddRectFilled(ImVec2(bx1, by1), ImVec2(bx2, by2), barCol,
                                (std::clamp)(barRounding * s, 0.0f, (std::min)(bx2 - bx1, by2 - by1) * 0.5f));
    }

    // ---- Content ----------------------------------------------------------

    const float contentX = boxX + padX + ((showAccentBar && barSide == BarLeft) ? barSpace : 0.0f);
    const float contentY = boxY + padY + ((showAccentBar && barSide == BarTop) ? barSpace : 0.0f);

    const float effectPx = (textEffect == EffectNone) ? 0.0f : (std::max)(0.0f, effectSize * s);
    const ImU32 effectCol = mulAlpha(colorFromFloats(effectColor), op);
    const ImU32 sepCol = mulAlpha(colorFromFloats(separatorColor), op);

    float cursorX = contentX;
    float cursorY = contentY;

    for (int i = 0; i < cellCount; ++i) {
        const Cell& c = cells[i];

        // Cross-axis alignment: cells shorter than the tallest one float inside
        // the leftover space.
        const float slack = horizontal ? (contentH - c.h) : (contentW - c.w);
        float offset = 0.0f;
        if (align == AlignCenter)
            offset = slack * 0.5f;
        else if (align == AlignEnd)
            offset = slack;
        const float cellX = horizontal ? cursorX : (contentX + offset);
        const float cellY = horizontal ? (contentY + offset) : cursorY;

        if (c.isText) {
            const ImU32 col = mulAlpha(modeColor(c.item->colorMode, c.item->color, c.index, timeMs), op);
            const bool rainbowRun = (c.item->colorMode == ColorRainbow);
            // Value and label share a baseline, so a large number and a small unit
            // sit on one line instead of both hanging from the cell's top edge.
            const float baseAscent = (std::max)(c.valueAscent, c.labelAscent);
            const float valueY = cellY + (baseAscent - c.valueAscent);
            const float labelY = cellY + (baseAscent - c.labelAscent);
            float tx = cellX;
            if (c.label && c.labelFirst) {
                drawRun(draw, c.labelFont, c.labelFS, tx, labelY, c.label, col, rainbowRun, c.index, timeMs,
                        effectPx, effectCol);
                tx += c.labelW + c.labelGap;
                drawRun(draw, c.valueFont, c.valueFS, tx, valueY, c.value, col, rainbowRun, c.index, timeMs,
                        effectPx, effectCol);
            } else {
                drawRun(draw, c.valueFont, c.valueFS, tx, valueY, c.value, col, rainbowRun, c.index, timeMs,
                        effectPx, effectCol);
                if (c.label) {
                    tx += c.valueW + c.labelGap;
                    drawRun(draw, c.labelFont, c.labelFS, tx, labelY, c.label, col, rainbowRun, c.index,
                            timeMs, effectPx, effectCol);
                }
            }
        } else if (c.kind == KindImage) {
            const ImU32 tint =
                mulAlpha(colorFromFloats(imageTint), op * (std::clamp)(imageOpacity, 0.0f, 1.0f));
            // uv (0,0)..(1,1) with no V flip: uv.y = 0 is the first row uploaded,
            // which is the image's top row. See loadImageAsset().
            draw->AddImageRounded((ImTextureID)(intptr_t)g_image.texture, ImVec2(cellX, cellY),
                                  ImVec2(cellX + c.w, cellY + c.h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                  tint,
                                  (std::clamp)(imageRounding * s, 0.0f, (std::min)(c.w, c.h) * 0.5f));
        } else if (c.kind == KindSeparator) {
            draw->AddRectFilled(ImVec2(cellX, cellY), ImVec2(cellX + c.w, cellY + c.h), sepCol);
        }
        // KindSpacer draws nothing; its whole job is the advance below.

        if (horizontal)
            cursorX += c.w + c.gapAfter;
        else
            cursorY += c.h + c.gapAfter;
    }
}
