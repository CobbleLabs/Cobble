#pragma once

#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <chrono>
#include "../sdk/core/minecraft.h"
#include "math/geometry.h"
#include "../../infra/ext/imgui/imgui.h"

// Visual Team Resolver v3 - With Tracking & Persistence
// ============================================================
// Features:
// - Pixel-based team detection (horizontal strip sampling)
// - Position tracking with velocity prediction
// - Fallback to last known state when visual fails
// - Cluster inference (nearby players influence)
// - Nearest-neighbor association
// ============================================================

enum class VisualTeamResult {
	UNKNOWN = 0,   // Could not determine -> treat as ENEMY
	FRIENDLY = 1,  // High confidence green
	ENEMY = 2      // Red or uncertain
};

enum VisualRejectReason {
	REJECT_NONE = 0,
	REJECT_NO_SAMPLES,
	REJECT_LOW_SATURATION,
	REJECT_HIGH_VARIANCE,
	REJECT_MIXED_COLORS,
	REJECT_OCCLUDED
};

// ============================================================
// Visual Track - Persistent tracking data per player
// ============================================================
struct VisualTrack {
	// Identity
	std::string uuid;
	std::string playerName;
	std::string mojangName;
	bool mojangLookupDone = false;
	bool mojangLookupQueued = false;
	
	// Team classification
	float confidence = 0.5f;  // 0.0 = enemy, 1.0 = friendly
	VisualRejectReason lastRejectReason = REJECT_NONE;
	int consecutiveFailures = 0;
	int consecutiveSuccesses = 0;
	
	// Position tracking
	double worldX = 0, worldY = 0, worldZ = 0;
	double lastWorldX = 0, lastWorldY = 0, lastWorldZ = 0;
	double velocityX = 0, velocityY = 0, velocityZ = 0;
	float screenX = 0, screenY = 0;
	float lastScreenX = 0, lastScreenY = 0;  // For motion compensation
	bool onScreen = false;
	float distance = 0;
	float speed = 0.0f;  // Cached speed for adaptive parameters
	
	// Timing
	std::chrono::steady_clock::time_point lastSeen;
	std::chrono::steady_clock::time_point lastAnalyzed;
	std::chrono::steady_clock::time_point lastLogTime;
	
	// Sticky state
	bool sticky = false;
	std::chrono::steady_clock::time_point stickySince;
	float lastStableConfidence = 0.5f;
	
	// Get current result based on confidence
	VisualTeamResult getResult() const {
		if (confidence > 0.7f) return VisualTeamResult::FRIENDLY;
		if (confidence < 0.3f) return VisualTeamResult::ENEMY;
		return VisualTeamResult::UNKNOWN;
	}
	
	// Update confidence with new sample
	void addSample(float hue, bool valid) {
		if (!valid) {
			consecutiveFailures++;
			consecutiveSuccesses = 0;
			// Lean to enemy faster when vision repeatedly fails
			float decayTarget = 0.25f;
			float decayRate = (consecutiveFailures >= 3) ? 0.22f : 0.12f;
			confidence = confidence * (1.0f - decayRate) + decayTarget * decayRate;
			if (confidence < 0.0f) confidence = 0.0f;
			return;
		}
		
		consecutiveSuccesses++;
		consecutiveFailures = 0;
		
		float target = 0.5f;
		if (hue >= 80.0f && hue <= 160.0f) {
			target = 1.0f;  // Green = friendly
		} else if ((hue >= 0.0f && hue <= 30.0f) || (hue >= 330.0f && hue <= 360.0f)) {
			target = 0.0f;  // Red = enemy
		}
		
		// Faster learning when consistent
		float learningRate = consecutiveSuccesses >= 3 ? 0.3f : 0.2f;
		confidence = confidence * (1.0f - learningRate) + target * learningRate;
		if (confidence < 0.0f) confidence = 0.0f;
		if (confidence > 1.0f) confidence = 1.0f;
	}
	
	// Update sticky state
	void updateSticky() {
		auto now = std::chrono::steady_clock::now();
		
		if (std::abs(confidence - lastStableConfidence) > 0.20f) {
			lastStableConfidence = confidence;
			stickySince = now;
			sticky = false;
			return;
		}
		
		auto stableMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - stickySince).count();
		if (stableMs >= 1500 && (confidence > 0.8f || confidence < 0.2f)) {
			sticky = true;
		}
	}
	
	// Predict position based on velocity
	void predictPosition(double deltaTime) {
		worldX += velocityX * deltaTime;
		worldY += velocityY * deltaTime;
		worldZ += velocityZ * deltaTime;
	}
	
	// Update position and calculate velocity
	void updatePosition(double x, double y, double z) {
		auto now = std::chrono::steady_clock::now();
		auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSeen).count() / 1000.0;
		
		if (dt > 0.0 && dt < 1.0) {  // Reasonable delta
			velocityX = (x - worldX) / dt;
			velocityY = (y - worldY) / dt;
			velocityZ = (z - worldZ) / dt;
		}
		
		lastWorldX = worldX;
		lastWorldY = worldY;
		lastWorldZ = worldZ;
		worldX = x;
		worldY = y;
		worldZ = z;
	}
	
	// Check if track should be invalidated
	void checkInvalidate(std::chrono::steady_clock::time_point now) {
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSeen).count();
		if (ageMs > 30000) {
			sticky = false;
			confidence = 0.5f;
		}
	}
	
	// Distance to another track (screen space)
	float screenDistanceTo(const VisualTrack& other) const {
		if (!onScreen || !other.onScreen) return 9999.0f;
		float dx = screenX - other.screenX;
		float dy = screenY - other.screenY;
		return std::sqrt(dx*dx + dy*dy);
	}
	
	// Distance in world space
	float worldDistanceTo(const VisualTrack& other) const {
		double dx = worldX - other.worldX;
		double dy = worldY - other.worldY;
		double dz = worldZ - other.worldZ;
		return (float)std::sqrt(dx*dx + dy*dy + dz*dz);
	}
};

class VisualTeamResolver {
public:
	VisualTeamResolver() = default;
	
	// ============================================================
	// Main API
	// ============================================================
	
	// Check if entity should be skipped (FRIENDLY with high confidence)
	bool isFriendly(const std::string& uuid) const;
	
	// Get confidence score (0.0 = enemy, 1.0 = friendly)
	float getConfidence(const std::string& uuid) const;
	
	// Get result with fallback to tracking
	VisualTeamResult getResultWithFallback(const std::string& uuid) const;
	
	// Queue entity for analysis
	void queueForAnalysis(const std::string& uuid, const PlayerInfo& entity, const RenderPos& renderPos);
	
	// Update all tracks with new player positions (call every frame)
	// partialTicks: interpolation factor from render (0.0-1.0)
	void updateTracks(const std::vector<PlayerInfo>& players, const RenderPos& renderPos, int screenW, int screenH, float partialTicks);
	
	// Process queued entities (call from render thread)
	void processRenderThread(int displayWidth, int displayHeight);
	
	// Apply cluster inference (call after visual analysis)
	void applyClusterInference();
	
	// Clean stale entries
	void tick();
	
	// Background scan
	void scanAllPlayers();
	
	// Debug overlay
	void renderDebugOverlay(ImDrawList* draw);
	
	// ============================================================
	// Settings
	// ============================================================
	
	bool enabled = false;
	bool debugOverlay = true;
	float cacheTTLMs = 15000.0f;
	float analysisIntervalMs = 100.0f;
	
	// HSV thresholds
	float minSaturation = 0.16f;
	float minValue = 0.20f;
	float minAlpha = 0.80f;
	
	// Variance rejection
	float maxHueVariance = 35.0f;
	
	// Distance/quality filters
	float maxAnalysisDistance = 15.0f;
	float minScreenSize = 10.0f;
	
	// Rate limiting
	int maxAnalysesPerFrame = 3;
	
	// Cluster inference
	float clusterRadius = 2.0f;        // World units
	int clusterMinPlayers = 2;         // Min neighbors for inference
	float clusterConfidenceBoost = 0.1f;
	
	// Tracking fallback
	int fallbackMaxFrames = 10;        // Max frames to use old data
	
private:
	mutable std::mutex mutex_;
	
	// Tracks: UUID -> VisualTrack
	std::map<std::string, VisualTrack> tracks_;
	
	// Pending analysis queue
	struct PendingAnalysis {
		std::string uuid;
		std::string playerName;
		double headX, headY, headZ;
		double renderX, renderY, renderZ;
		float distance;
	};
	std::vector<PendingAnalysis> pendingQueue_;
	
	// Debug samples for overlay
	struct DebugSample {
		int screenX, screenY;
		int offset;
		int bandHeight;  // 2=still, 3=moving
		bool valid;
		float hue;
		std::chrono::steady_clock::time_point timestamp;
	};
	std::deque<DebugSample> debugSamples_;
	
	// ============================================================
	// Internal Helpers
	// ============================================================
	
	void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
	bool analyzeScreenRegion(int centerX, int centerY, int displayHeight, int dynamicOffset,
	                         int bandHeight, bool isMoving,
	                         float& dominantHue, bool& valid, VisualRejectReason& rejectReason);
	bool shouldThrottle(const std::string& uuid) const;
};

// Global instance
extern VisualTeamResolver* visualTeamResolver;
