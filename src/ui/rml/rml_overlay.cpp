#include "rml_overlay.h"
#include "menu_document.hpp"

#include "../../util/logger.hpp"
#include "../core/fonts/inter_var.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "RmlUi_Platform_Win32.h"
#include "RmlUi_Renderer_GL2.h"

#include <gl/GL.h>

namespace {

class CobbleSystemInterface : public SystemInterface_Win32 {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)
            log_err("RmlUi: %s", message.c_str());
        else
            log_debug("RmlUi: %s", message.c_str());
        return true;
    }
};

CobbleSystemInterface* g_system = nullptr;
RenderInterface_GL2* g_renderer = nullptr;
TextInputMethodEditor_Win32* g_ime = nullptr;
Rml::Context* g_context = nullptr;
Rml::ElementDocument* g_document = nullptr;
bool g_ready = false;
bool g_rml_alive = false;
int g_width = 0;
int g_height = 0;

void DestroyInterfaces() {
    delete g_ime;
    g_ime = nullptr;
    delete g_renderer;
    g_renderer = nullptr;
    delete g_system;
    g_system = nullptr;
}

}  // namespace

void RmlOverlay::Shutdown() {
    g_document = nullptr;
    g_context = nullptr;
    g_ready = false;
    if (g_rml_alive) {
        Rml::Shutdown();
        g_rml_alive = false;
    }
    DestroyInterfaces();
}

void RmlOverlay::Init(HWND hwnd, int width, int height) {
    Shutdown();

    if (!hwnd || width <= 0 || height <= 0)
        return;

    g_system = new CobbleSystemInterface();
    g_renderer = new RenderInterface_GL2();
    g_ime = new TextInputMethodEditor_Win32();

    g_system->SetWindow(hwnd);
    g_renderer->SetViewport(width, height);

    Rml::SetSystemInterface(g_system);
    Rml::SetRenderInterface(g_renderer);

    if (!Rml::Initialise()) {
        log_err("RmlUi Initialise failed");
        Rml::SetSystemInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        DestroyInterfaces();
        return;
    }
    g_rml_alive = true;

    const Rml::byte* inter = reinterpret_cast<const Rml::byte*>(inter_var_ttf_data);
    if (!Rml::LoadFontFace(Rml::Span<const Rml::byte>(inter, inter_var_ttf_size), "Inter", Rml::Style::FontStyle::Normal,
                           Rml::Style::FontWeight::Auto)) {
        log_err("RmlUi failed to load Inter");
        Shutdown();
        return;
    }

    g_context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (!g_context) {
        log_err("RmlUi CreateContext failed");
        Shutdown();
        return;
    }

    g_document = g_context->LoadDocumentFromMemory(kMenuDocument, "menu.rml");
    if (!g_document) {
        log_err("RmlUi LoadDocumentFromMemory failed");
        Shutdown();
        return;
    }

    g_document->Show();
    g_width = width;
    g_height = height;
    g_ready = true;
    log_ok("RmlUi overlay ready (%dx%d)", width, height);
}

void RmlOverlay::SetDimensions(int width, int height) {
    if (!g_ready || !g_context || !g_renderer || width <= 0 || height <= 0)
        return;
    if (width == g_width && height == g_height)
        return;
    g_width = width;
    g_height = height;
    g_renderer->SetViewport(width, height);
    g_context->SetDimensions(Rml::Vector2i(width, height));
}

void RmlOverlay::Render() {
    if (!g_ready || !g_context || !g_renderer)
        return;

    g_renderer->BeginFrame();
    g_context->Update();
    g_context->Render();
    g_renderer->EndFrame();

    glDisable(GL_STENCIL_TEST);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void RmlOverlay::ProcessEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_ready || !g_context || !g_ime)
        return;
    RmlWin32::WindowProcedure(g_context, *g_ime, hwnd, msg, wParam, lParam);
}
