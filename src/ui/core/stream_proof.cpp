#include "stream_proof.h"

#include "../../util/logger.hpp"
#include "../hud/notifications.h"

#include <gl/GL.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>

#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_impl_opengl2.h"

namespace {

constexpr DWORD kExcludeFromCapture = 0x11;  // WDA_EXCLUDEFROMCAPTURE
// Older builds accept the flag but treat it as WDA_MONITOR, which records a black box.
constexpr DWORD kExcludeFromCaptureBuild = 19041;
// Without a fresh frame the overlay is hidden, so a skipped or stalled hook never
// leaves a frozen HUD on top of the game.
constexpr ULONGLONG kFrameTimeoutMs = 250;
constexpr wchar_t kWindowClass[] = L"CbxLayer";

constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kPixelPackBuffer = 0x88EB;
constexpr GLenum kPixelUnpackBuffer = 0x88EC;
constexpr GLenum kPixelPackBufferBinding = 0x88ED;
constexpr GLenum kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kStreamRead = 0x88E1;
constexpr GLenum kReadOnly = 0x88B8;
constexpr GLenum kBgra = 0x80E1;

#define STREAM_PROOF_GL_FUNCTIONS(X)                                       \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                           \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                  \
    X(void, BindFramebuffer, (GLenum, GLuint))                             \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum))                            \
    X(void, GenBuffers, (GLsizei, GLuint*))                                \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                       \
    X(void, BindBuffer, (GLenum, GLuint))                                  \
    X(void, BufferData, (GLenum, std::ptrdiff_t, const void*, GLenum))     \
    X(void*, MapBuffer, (GLenum, GLenum))                                  \
    X(GLboolean, UnmapBuffer, (GLenum))                                    \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))

#define STREAM_PROOF_DECLARE(ret, name, args) ret(APIENTRY* gl##name) args = nullptr;
STREAM_PROOF_GL_FUNCTIONS(STREAM_PROOF_DECLARE)
#undef STREAM_PROOF_DECLARE

bool loadGL() {
    // wglGetProcAddress returns 1/2/3/-1 as failure values on some drivers, not just null.
#define STREAM_PROOF_LOAD(ret, name, args)                             \
    {                                                                  \
        const PROC proc = wglGetProcAddress("gl" #name);               \
        const auto raw = reinterpret_cast<std::intptr_t>(proc);        \
        if (raw == 0 || raw == 1 || raw == 2 || raw == 3 || raw == -1) \
            return false;                                              \
        gl##name = reinterpret_cast<decltype(gl##name)>(proc);         \
    }
    STREAM_PROOF_GL_FUNCTIONS(STREAM_PROOF_LOAD)
#undef STREAM_PROOF_LOAD
    return true;
}
#undef STREAM_PROOF_GL_FUNCTIONS

// The frame is drawn into an offscreen target and read back through two pixel
// buffers in turn, so the CPU maps last frame's pixels instead of stalling on
// this one. The overlay therefore trails the game by one frame.
struct GpuTarget {
    HGLRC context = nullptr;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint pbo[2] = {};
    int pboWidth[2] = {};  // 0 = holds no frame
    int pboHeight[2] = {};
    POINT pboOrigin[2] = {};
    int width = 0;
    int height = 0;
    int next = 0;
};

GpuTarget gpu;

// Bottom-up 32bpp DIB: glReadPixels also returns the bottom row first, so the
// readback copies straight in.
struct Surface {
    HBITMAP bitmap = nullptr;
    void* bits = nullptr;
    int width = 0;
    int height = 0;
    POINT origin{};
};

// Triple buffer between the render thread (owns writeSlot) and the window thread
// (owns presentSlot); readySlot changes hands under slotMutex.
Surface surfaces[3];
int writeSlot = 0;
int readySlot = 1;
int presentSlot = 2;
bool readyFresh = false;
std::mutex slotMutex;

std::atomic<bool> wantVisible{false};
std::atomic<ULONGLONG> lastPublish{0};
std::atomic<HWND> trackedGame{nullptr};

HANDLE windowThread = nullptr;
DWORD windowThreadId = 0;
HANDLE wakeEvent = nullptr;
HANDLE readyEvent = nullptr;
HWND layerWindow = nullptr;  // written by the window thread before readyEvent

bool presentLatest(HWND window, HDC screen, HDC memory) {
    {
        std::lock_guard<std::mutex> lock(slotMutex);
        if (!readyFresh)
            return false;
        std::swap(presentSlot, readySlot);
        readyFresh = false;
    }
    const Surface& s = surfaces[presentSlot];
    const HGDIOBJ previous = SelectObject(memory, s.bitmap);
    POINT origin = s.origin;
    SIZE size{s.width, s.height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, screen, &origin, &size, memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, previous);
    return true;
}

bool excludeFromCaptureSupported() {
    using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto rtlGetVersion =
        reinterpret_cast<RtlGetVersion_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{sizeof(version)};
    return rtlGetVersion && rtlGetVersion(&version) == 0 && version.dwBuildNumber >= kExcludeFromCaptureBuild;
}

DWORD WINAPI windowThreadMain(LPVOID) {
    if (!excludeFromCaptureSupported()) {
        log_warn("StreamProof: needs Windows 10 build %lu or newer", kExcludeFromCaptureBuild);
        SetEvent(readyEvent);
        return 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    // The default procedure keeps no code of this DLL on the window's message path.
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const HWND window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass, L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!window || !SetWindowDisplayAffinity(window, kExcludeFromCapture)) {
        log_warn("StreamProof: capture-excluded window unavailable (error %lu)", GetLastError());
        if (window)
            DestroyWindow(window);
        SetEvent(readyEvent);
        return 1;
    }
    layerWindow = window;
    SetEvent(readyEvent);

    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    bool presented = false;
    bool shown = false;
    bool running = true;
    while (running) {
        // The timeout keeps the foreground check running while the game is
        // minimized and no longer swapping buffers.
        MsgWaitForMultipleObjects(1, &wakeEvent, FALSE, 100, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = false;
            else
                DispatchMessageW(&msg);
        }
        if (!running)
            break;

        if (presentLatest(window, screen, memory))
            presented = true;

        // Topmost over everything, so it must vanish the moment the game does.
        // After a hide it waits for a fresh frame rather than reshow stale pixels.
        const bool wanted = wantVisible.load();
        if (!wanted)
            presented = false;
        const HWND game = trackedGame.load();
        const bool fresh = GetTickCount64() - lastPublish.load() < kFrameTimeoutMs;
        const bool visible =
            presented && wanted && fresh && game && GetForegroundWindow() == game && !IsIconic(game);
        if (visible != shown) {
            if (visible)
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            else
                ShowWindow(window, SW_HIDE);
            shown = visible;
        }
    }

    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    DestroyWindow(window);
    return 0;
}

bool startWindow() {
    if (windowThread)
        return true;
    wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    windowThread = CreateThread(nullptr, 0, windowThreadMain, nullptr, 0, &windowThreadId);
    if (windowThread)
        WaitForSingleObject(readyEvent, INFINITE);
    CloseHandle(readyEvent);
    readyEvent = nullptr;
    if (layerWindow)
        return true;

    if (windowThread) {
        WaitForSingleObject(windowThread, INFINITE);
        CloseHandle(windowThread);
        windowThread = nullptr;
    }
    CloseHandle(wakeEvent);
    wakeEvent = nullptr;
    return false;
}

void publish(const void* pixels, int width, int height, POINT origin) {
    Surface& s = surfaces[writeSlot];
    if (s.width != width || s.height != height) {
        if (s.bitmap)
            DeleteObject(s.bitmap);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        s.bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &s.bits, nullptr, 0);
        if (!s.bitmap) {
            s = Surface{};
            return;
        }
        s.width = width;
        s.height = height;
    }
    std::memcpy(s.bits, pixels, (size_t)width * (size_t)height * 4);
    s.origin = origin;

    {
        std::lock_guard<std::mutex> lock(slotMutex);
        std::swap(writeSlot, readySlot);
        readyFresh = true;
    }
    lastPublish = GetTickCount64();
}

// UpdateLayeredWindow composites premultiplied BGRA by its alpha channel. The
// backend's glBlendFunc squares coverage into destination alpha, so this swaps in
// a separate alpha factor. The backend sets its blend state once per
// RenderDrawData, so a callback ahead of the first command covers the frame.
void premultipliedBlend(const ImDrawList*, const ImDrawCmd*) {
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

bool resizeTarget(int width, int height) {
    GLint texture = 0, unpackBuffer = 0, packBuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(kPixelUnpackBufferBinding, &unpackBuffer);
    glGetIntegerv(kPixelPackBufferBinding, &packBuffer);

    glBindBuffer(kPixelUnpackBuffer, 0);
    glBindTexture(GL_TEXTURE_2D, gpu.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, gpu.texture, 0);

    for (int i = 0; i < 2; ++i) {
        glBindBuffer(kPixelPackBuffer, gpu.pbo[i]);
        glBufferData(kPixelPackBuffer, (std::ptrdiff_t)width * height * 4, nullptr, kStreamRead);
        gpu.pboWidth[i] = 0;
    }

    glBindBuffer(kPixelPackBuffer, (GLuint)packBuffer);
    glBindBuffer(kPixelUnpackBuffer, (GLuint)unpackBuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);

    if (glCheckFramebufferStatus(kFramebuffer) != kFramebufferComplete) {
        log_warn("StreamProof: incomplete framebuffer at %dx%d", width, height);
        return false;
    }
    gpu.width = width;
    gpu.height = height;
    return true;
}

void releaseTarget() {
    if (gpu.context && gpu.context == wglGetCurrentContext()) {
        glDeleteFramebuffers(1, &gpu.fbo);
        glDeleteTextures(1, &gpu.texture);
        glDeleteBuffers(2, gpu.pbo);
    }
    gpu = GpuTarget{};
}

bool prepareTarget(int width, int height) {
    const HGLRC context = wglGetCurrentContext();
    if (gpu.context != context) {
        // Names from another context mean nothing here, and that context may be gone.
        gpu = GpuTarget{};
        gpu.context = context;
        glGenFramebuffers(1, &gpu.fbo);
        glGenTextures(1, &gpu.texture);
        glGenBuffers(2, gpu.pbo);
        if (!gpu.fbo || !gpu.texture || !gpu.pbo[0] || !gpu.pbo[1])
            return false;
    }
    glBindFramebuffer(kFramebuffer, gpu.fbo);
    return (gpu.width == width && gpu.height == height) || resizeTarget(width, height);
}

bool drawFrame(ImDrawData* drawData, POINT origin) {
    const int width = (int)drawData->DisplaySize.x;
    const int height = (int)drawData->DisplaySize.y;
    if (width <= 0 || height <= 0)
        return true;

    GLint drawFbo = 0, readFbo = 0, packBuffer = 0;
    glGetIntegerv(kDrawFramebufferBinding, &drawFbo);
    glGetIntegerv(kReadFramebufferBinding, &readFbo);
    glGetIntegerv(kPixelPackBufferBinding, &packBuffer);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

    const bool ready = prepareTarget(width, height);
    if (ready) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (drawData->CmdListsCount > 0) {
            ImDrawCmd blend;
            blend.UserCallback = premultipliedBlend;
            drawData->CmdLists[0]->CmdBuffer.push_front(blend);
        }
        ImGui_ImplOpenGL2_RenderDrawData(drawData);

        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

        const int current = gpu.next;
        glBindBuffer(kPixelPackBuffer, gpu.pbo[current]);
        glReadPixels(0, 0, width, height, kBgra, GL_UNSIGNED_BYTE, nullptr);
        gpu.pboWidth[current] = width;
        gpu.pboHeight[current] = height;
        gpu.pboOrigin[current] = origin;

        const int previous = current ^ 1;
        gpu.next = previous;
        if (gpu.pboWidth[previous]) {
            glBindBuffer(kPixelPackBuffer, gpu.pbo[previous]);
            if (const void* pixels = glMapBuffer(kPixelPackBuffer, kReadOnly)) {
                publish(pixels, gpu.pboWidth[previous], gpu.pboHeight[previous], gpu.pboOrigin[previous]);
                glUnmapBuffer(kPixelPackBuffer);
            }
        }
    }

    glBindBuffer(kPixelPackBuffer, (GLuint)packBuffer);
    glBindFramebuffer(kDrawFramebuffer, (GLuint)drawFbo);
    glBindFramebuffer(kReadFramebuffer, (GLuint)readFbo);
    glPopClientAttrib();
    glPopAttrib();
    if (!ready)
        releaseTarget();
    return ready;
}

void disable(const char* reason) {
    log_warn("StreamProof: %s, drawing in-game", reason);
    StreamProof::enabled = false;
    NotificationManager::addSystem("Stream Proof is not available on this system");
}

}  // namespace

bool StreamProof::Present(ImDrawData* drawData, HWND gameWindow) {
    static const bool glLoaded = loadGL();
    if (!glLoaded) {
        disable("GL 3.0 framebuffer functions missing");
        return false;
    }
    if (!startWindow()) {
        disable("capture-excluded window could not be created");
        return false;
    }

    POINT origin{0, 0};
    ClientToScreen(gameWindow, &origin);
    if (!drawFrame(drawData, origin)) {
        disable("offscreen target could not be created");
        return false;
    }

    trackedGame = gameWindow;
    wantVisible = true;
    SetEvent(wakeEvent);
    return true;
}

void StreamProof::Hide() {
    if (!wantVisible.exchange(false))
        return;
    // Frames still in flight predate the hide and would flash on the next show.
    gpu.pboWidth[0] = gpu.pboWidth[1] = 0;
    {
        std::lock_guard<std::mutex> lock(slotMutex);
        readyFresh = false;
    }
    SetEvent(wakeEvent);
}

void StreamProof::ReleaseGl() {
    if (gpu.context)
        releaseTarget();
}

void StreamProof::Shutdown() {
    if (windowThread) {
        PostThreadMessageW(windowThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(windowThread, INFINITE);
        CloseHandle(windowThread);
        windowThread = nullptr;
        CloseHandle(wakeEvent);
        wakeEvent = nullptr;
        layerWindow = nullptr;
        UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    }
    wantVisible = false;

    for (Surface& s : surfaces) {
        if (s.bitmap)
            DeleteObject(s.bitmap);
        s = Surface{};
    }
    readyFresh = false;
}
