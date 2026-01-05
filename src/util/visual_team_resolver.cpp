#include "visual_team_resolver.h"
#include "logger.hpp"
#include "math/worldToScreen.h"
#include "../sdk/player/render_data.h"
#include <Windows.h>
#include <gl/GL.h>
#include <algorithm>
#include <cmath>

// Global instance
VisualTeamResolver* visualTeamResolver = nullptr;

// Debug toggle - DISABLED for production (causes FPS lag from console spam)
static bool g_visualTeamDebug = false;

// ============================================================
// Main API
// ============================================================

bool VisualTeamResolver::isFriendly(const std::string& uuid) const {
	if (!enabled || uuid.empty()) return false;
	
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = cache_.find(uuid);
	if (it == cache_.end()) return false;
	
	// Use consensus (majority vote), not single-frame result
	bool friendly = it->second.consensus == VisualTeamResult::FRIENDLY;
	
	if (g_visualTeamDebug && friendly) {
		log_debug("[VisualTeam] isFriendly(%s) = TRUE (consensus, %zu samples)",
			uuid.substr(0, 8).c_str(), it->second.history.size());
	}
	
	return friendly;
}

void VisualTeamResolver::queueForAnalysis(const std::string& uuid, const PlayerInfo& entity, const RenderPos& renderPos) {
	if (!enabled || uuid.empty()) return;
	
	// Calculate distance for early-return
	double dx = entity.posX - renderPos.x;
	double dy = entity.posY - renderPos.y;
	double dz = entity.posZ - renderPos.z;
	float distance = (float)std::sqrt(dx*dx + dy*dy + dz*dz);
	
	// Early-return: don't analyze if too far
	if (distance > maxAnalysisDistance) {
		return;
	}
	
	std::lock_guard<std::mutex> lock(mutex_);
	
	// Sticky players skip GPU analysis entirely - just update lastSeen
	auto it = cache_.find(uuid);
	if (it != cache_.end()) {
		it->second.lastSeen = std::chrono::steady_clock::now();
		if (it->second.sticky) {
			return;  // No GPU needed - result is locked in
		}
	}
	
	// Check throttle
	if (shouldThrottle(uuid)) {
		return;
	}
	
	// Nametag position: above head
	// Player height 1.8, nametag renders ~0.7 above head (raised for better accuracy)
	double headY = entity.posY + 1.8 + 0.7;
	
	PendingAnalysis pending;
	pending.uuid = uuid;
	pending.headX = entity.posX;
	pending.headY = headY;
	pending.headZ = entity.posZ;
	pending.renderX = renderPos.x;
	pending.renderY = renderPos.y;
	pending.renderZ = renderPos.z;
	pending.distance = distance;
	
	pendingQueue_.push_back(pending);
}

void VisualTeamResolver::processRenderThread(int displayWidth, int displayHeight) {
	if (!enabled) return;
	if (displayWidth <= 0 || displayHeight <= 0) return;
	
	std::vector<PendingAnalysis> toProcess;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		toProcess = std::move(pendingQueue_);
		pendingQueue_.clear();
	}
	
	if (toProcess.empty()) return;
	
	// Check RenderData
	if (!RenderData::updated) {
		return;
	}
	
	Matrix mvMatrix = RenderData::modelView;
	Matrix projMatrix = RenderData::projection;
	
	auto now = std::chrono::steady_clock::now();
	
	// Budget: max players to process per frame to prevent GPU stalls
	constexpr int kMaxPerFrame = 4;
	int processed = 0;
	
	for (const auto& pending : toProcess) {
		if (processed >= kMaxPerFrame) break;  // Budget exhausted
		// Camera-relative coordinates
		Vector3 headCamRel(
			(float)(pending.headX - pending.renderX),
			(float)(pending.headY - pending.renderY),
			(float)(pending.headZ - pending.renderZ)
		);
		Vector2 screenPos;
		
		bool onScreen = CWorldToScreen::WorldToScreen(
			headCamRel, mvMatrix, projMatrix,
			displayWidth, displayHeight, screenPos
		);
		
		if (!onScreen) continue;
		
		int centerX = (int)screenPos.x;
		int centerY = (int)screenPos.y;
		
		// Early-return: near screen edge
		int margin = 50;
		if (centerX < margin || centerX >= displayWidth - margin ||
		    centerY < margin || centerY >= displayHeight - margin) {
			continue;
		}
		
		// Early-return: check projected size (rough estimate based on distance)
		float projectedHeight = (displayHeight * 1.8f) / (pending.distance * 2.0f);
		if (projectedHeight < minScreenSize) {
			continue;
		}
		
		// Bounds check for ROI
		if (centerX < roiWidth / 2 || centerX >= displayWidth - roiWidth / 2) continue;
		if (centerY < roiHeight / 2 || centerY >= displayHeight - roiHeight / 2) continue;
		
		// Analyze
		VisualTeamResult result = analyzeScreenRegion(centerX, centerY, displayHeight);
		processed++;  // Count towards frame budget
		
		// Update cache with temporal history
		{
			std::lock_guard<std::mutex> lock(mutex_);
			VisualTeamState& state = cache_[pending.uuid];
			state.addSample(result);
			state.lastAnalyzed = now;
			state.lastSeen = now;
			
			if (g_visualTeamDebug) {
				const char* resultStr = (result == VisualTeamResult::FRIENDLY) ? "FRIENDLY" :
				                        (result == VisualTeamResult::ENEMY) ? "ENEMY" : "UNKNOWN";
				const char* consensusStr = (state.consensus == VisualTeamResult::FRIENDLY) ? "FRIENDLY" :
				                           (state.consensus == VisualTeamResult::ENEMY) ? "ENEMY" : "UNKNOWN";
				log_debug("[VisualTeam] %s: frame=%s consensus=%s (%zu samples)",
					pending.uuid.substr(0, 8).c_str(), resultStr, consensusStr, state.history.size());
			}
		}
	}
}

void VisualTeamResolver::tick() {
	std::lock_guard<std::mutex> lock(mutex_);
	
	auto now = std::chrono::steady_clock::now();
	
	for (auto it = cache_.begin(); it != cache_.end(); ) {
		// Check sticky invalidation
		it->second.checkInvalidate(now);
		
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - it->second.lastSeen
		).count();
		
		// Sticky players stay longer (30s), non-sticky use normal TTL
		float effectiveTTL = it->second.sticky ? 30000.0f : cacheTTLMs;
		
		if (ageMs > effectiveTTL) {
			it = cache_.erase(it);
		} else {
			++it;
		}
	}
}

void VisualTeamResolver::scanAllPlayers() {
	if (!enabled) return;
	
	// Get all players from the game
	auto players = mc.getPlayerList();
	RenderPos renderPos = mc.getRenderPos();
	
	for (auto& p : players) {
		if (p.uuid.empty()) continue;
		
		// Queue each player for analysis (throttling is handled internally)
		queueForAnalysis(p.uuid, p, renderPos);
		
		// Cleanup JNI reference
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}
}

// ============================================================
// Internal Helpers
// ============================================================

bool VisualTeamResolver::shouldThrottle(const std::string& uuid) const {
	auto it = cache_.find(uuid);
	if (it == cache_.end()) return false;
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - it->second.lastAnalyzed
	).count();
	
	return elapsed < analysisIntervalMs;
}

bool VisualTeamResolver::readPixelRGB(int x, int y, int displayHeight, float& r, float& g, float& b) {
	int glY = displayHeight - y - 1;
	
	GLubyte pixel[3] = {0, 0, 0};
	glReadPixels(x, glY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	
	r = pixel[0] / 255.0f;
	g = pixel[1] / 255.0f;
	b = pixel[2] / 255.0f;
	
	return true;
}

void VisualTeamResolver::rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
	float cmax = std::max({r, g, b});
	float cmin = std::min({r, g, b});
	float diff = cmax - cmin;
	
	v = cmax;
	s = (cmax == 0.0f) ? 0.0f : diff / cmax;
	
	if (diff == 0.0f) {
		h = 0.0f;
	} else if (cmax == r) {
		h = 60.0f * std::fmod((g - b) / diff, 6.0f);
	} else if (cmax == g) {
		h = 60.0f * ((b - r) / diff + 2.0f);
	} else {
		h = 60.0f * ((r - g) / diff + 4.0f);
	}
	
	if (h < 0.0f) h += 360.0f;
}

int VisualTeamResolver::classifyHsv(float h, float s, float v) {
	// Ignore low saturation or value (grays, shadows)
	if (s < minSaturation || v < minValue) {
		return 0;
	}
	
	// Green: H in [80, 160] - wider range for Minecraft greens
	if (h >= 80.0f && h <= 160.0f) {
		return 1; // FRIENDLY
	}
	
	// Red: H in [0, 25] or [335, 360] - wider range
	if ((h >= 0.0f && h <= 25.0f) || (h >= 335.0f && h <= 360.0f)) {
		return 2; // ENEMY
	}
	
	return 0;
}

VisualTeamResult VisualTeamResolver::analyzeScreenRegion(int centerX, int centerY, int displayHeight) {
	int startX = centerX - roiWidth / 2;
	int startY = centerY - roiHeight / 2;
	
	// Flip Y for OpenGL (origin at bottom-left)
	int glStartY = displayHeight - startY - roiHeight;
	
	// === PERFORMANCE FIX: Single glReadPixels for entire ROI ===
	// Instead of 32 separate calls, read once and sample from CPU buffer
	std::vector<GLubyte> buffer(roiWidth * roiHeight * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(startX, glStartY, roiWidth, roiHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());
	
	float stepX = (float)roiWidth / (float)sampleCols;
	float stepY = (float)roiHeight / (float)sampleRows;
	
	int greenCount = 0;
	int redCount = 0;
	int unknownCount = 0;
	
	for (int row = 0; row < sampleRows; row++) {
		for (int col = 0; col < sampleCols; col++) {
			// Calculate sample position within the ROI buffer
			int localX = (int)(col * stepX + stepX / 2);
			int localY = (int)(row * stepY + stepY / 2);
			
			// OpenGL reads bottom-to-top, so flip Y within buffer
			int flippedLocalY = roiHeight - 1 - localY;
			
			// Buffer index: row-major, 3 bytes per pixel
			int bufferIdx = (flippedLocalY * roiWidth + localX) * 3;
			
			if (bufferIdx < 0 || bufferIdx + 2 >= (int)buffer.size()) {
				unknownCount++;
				continue;
			}
			
			float r = buffer[bufferIdx] / 255.0f;
			float g = buffer[bufferIdx + 1] / 255.0f;
			float b = buffer[bufferIdx + 2] / 255.0f;
			
			float h, s, v;
			rgbToHsv(r, g, b, h, s, v);
			
			int classification = classifyHsv(h, s, v);
			if (classification == 1) greenCount++;
			else if (classification == 2) redCount++;
			else unknownCount++;
		}
	}
	
	// Relaxed logic: if we have ANY green or red, try to decide
	int validSamples = greenCount + redCount;
	
	// If no colored pixels at all, UNKNOWN
	if (validSamples == 0) {
		return VisualTeamResult::UNKNOWN;
	}
	
	// Simple ratio comparison - whichever is higher wins
	// But need at least 2 of that color to be confident
	if (greenCount >= 2 && greenCount > redCount) {
		return VisualTeamResult::FRIENDLY;
	}
	
	if (redCount >= 2 && redCount > greenCount) {
		return VisualTeamResult::ENEMY;
	}
	
	return VisualTeamResult::UNKNOWN;
}
