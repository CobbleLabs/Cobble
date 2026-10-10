#pragma once

#include <Windows.h>

struct ImDrawData;

// Draws the ImGui frame on a click-through window laid over the game and excluded
// from screen capture, instead of into the game's own frame. OBS, NVIDIA capture
// and Discord then record the game without any of the overlay.
namespace StreamProof {
inline bool enabled = false;

// Render thread only. Returns false when the excluded window can't be used, after
// turning the mode off; the caller then draws the frame in-game.
bool Present(ImDrawData* drawData, HWND gameWindow);
void Hide();
// Render thread, game context current: frees the offscreen target before unload.
void ReleaseGl();
void Shutdown();
}  // namespace StreamProof
