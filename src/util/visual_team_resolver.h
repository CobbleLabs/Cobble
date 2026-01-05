#pragma once

#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <chrono>
#include "../sdk/core/minecraft.h"
#include "math/geometry.h"

// Visual Team Resolver - Pixel-based team detection with temporal smoothing
// =========================================================================
// Analyzes actual rendered nametag colors on screen to detect teammates.
// Used as FALLBACK when scoreboard/§color detection returns UNKNOWN.
// 
// Design:
// - Runs on render thread (requires OpenGL context)
// - Uses TEMPORAL SMOOTHING: majority vote over last N samples
// - Rate-limited + cached with hysteresis
// - UNKNOWN never counts as friendly (fail-safe)
// =========================================================================

enum class VisualTeamResult {
	UNKNOWN = 0,   // Could not determine (ignore - not friendly)
	FRIENDLY = 1,  // Green nametag detected
	ENEMY = 2      // Red nametag detected
};

// State per entity with temporal history
struct VisualTeamState {
	// Ring buffer for temporal smoothing (last N results)
	std::deque<VisualTeamResult> history;
	static constexpr size_t HISTORY_SIZE = 8;
	
	// Current consensus (majority vote result)
	VisualTeamResult consensus = VisualTeamResult::UNKNOWN;
	
	// Sticky classification - once stable, skip all GPU analysis
	bool sticky = false;
	std::chrono::steady_clock::time_point stickySince;
	std::chrono::steady_clock::time_point consensusStableSince;
	VisualTeamResult lastStableConsensus = VisualTeamResult::UNKNOWN;
	
	// Timing
	std::chrono::steady_clock::time_point lastAnalyzed;
	std::chrono::steady_clock::time_point lastSeen;
	
	// Add a sample and update consensus
	void addSample(VisualTeamResult result) {
		history.push_back(result);
		while (history.size() > HISTORY_SIZE) {
			history.pop_front();
		}
		updateConsensus();
	}
	
	void updateConsensus() {
		int friendly = 0, enemy = 0;
		for (auto r : history) {
			if (r == VisualTeamResult::FRIENDLY) friendly++;
			else if (r == VisualTeamResult::ENEMY) enemy++;
			// UNKNOWN doesn't count
		}
		
		// Need at least 3 valid samples to decide
		int validSamples = friendly + enemy;
		if (validSamples < 3) {
			consensus = VisualTeamResult::UNKNOWN;
			return;
		}
		
		// Majority vote with 60% threshold for hysteresis
		float friendlyRatio = (float)friendly / (float)validSamples;
		VisualTeamResult newConsensus = consensus;
		
		if (friendlyRatio >= 0.6f) {
			newConsensus = VisualTeamResult::FRIENDLY;
		} else if (friendlyRatio <= 0.4f) {
			newConsensus = VisualTeamResult::ENEMY;
		}
		
		// Track consensus stability for sticky
		auto now = std::chrono::steady_clock::now();
		if (newConsensus != lastStableConsensus) {
			lastStableConsensus = newConsensus;
			consensusStableSince = now;
		}
		
		// Check if we should become sticky (80% dominant, stable for 3+ seconds, full history)
		if (!sticky && newConsensus != VisualTeamResult::UNKNOWN && 
		    history.size() == HISTORY_SIZE) {
			float dominantRatio = (friendlyRatio >= 0.5f) ? friendlyRatio : (1.0f - friendlyRatio);
			auto stableMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - consensusStableSince).count();
			
			if (dominantRatio >= 0.8f && stableMs >= 3000) {
				sticky = true;
				stickySince = now;
			}
		}
		
		consensus = newConsensus;
	}
	
	// Invalidate sticky if player hasn't been seen for too long
	void checkInvalidate(std::chrono::steady_clock::time_point now) {
		if (!sticky) return;
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSeen).count();
		if (ageMs > 30000) {  // 30 seconds without seeing = invalidate
			sticky = false;
			history.clear();
			consensus = VisualTeamResult::UNKNOWN;
		}
	}
};

class VisualTeamResolver {
public:
	VisualTeamResolver() = default;
	
	// ============================================================
	// Main API
	// ============================================================
	
	// Check if entity should be skipped (FRIENDLY consensus)
	// Returns false for UNKNOWN or ENEMY (safe default)
	bool isFriendly(const std::string& uuid) const;
	
	// Check if we have enough samples to make a decision
	// If false and visual team is enabled, AimAssist should skip this player
	bool hasClassification(const std::string& uuid) const;
	
	// Queue entity for analysis (called from AimAssist during scan)
	// renderPos should be from mc.getRenderPos() on the main thread
	void queueForAnalysis(const std::string& uuid, const PlayerInfo& entity, const RenderPos& renderPos);
	
	// Process queued entities (call from render thread)
	void processRenderThread(int displayWidth, int displayHeight);
	
	// Clear stale cache entries (call periodically)
	void tick();
	
	// Background scan: queue all visible players for analysis
	// Call from main loop to pre-cache team detection
	void scanAllPlayers();
	
	// ============================================================
	// Settings
	// ============================================================
	
	bool enabled = false;                   // Master toggle
	float cacheTTLMs = 10000.0f;            // Cache lifetime (10 seconds - prevents reset while visible)
	float analysisIntervalMs = 80.0f;       // Per-entity cooldown
	
	// ROI settings (taller to catch nametag better)
	int roiWidth = 32;    // Slightly wider
	int roiHeight = 16;   // Taller - nametag has some height
	int sampleCols = 8;   // 8x4 = 32 samples
	int sampleRows = 4;
	
	// Classification thresholds (HSV) - more lenient
	float minSaturation = 0.20f;  // Lower to catch faded colors
	float minValue = 0.35f;       // Lower to catch darker text
	
	// Distance/quality filters
	float maxAnalysisDistance = 20.0f;  // Don't analyze beyond this
	float minScreenSize = 8.0f;         // Skip if projected too small
	
private:
	mutable std::mutex mutex_;
	
	// Cache: UUID -> VisualTeamState (with history)
	std::map<std::string, VisualTeamState> cache_;
	
	// Pending analysis queue
	struct PendingAnalysis {
		std::string uuid;
		double headX, headY, headZ;
		double renderX, renderY, renderZ;
		float distance;  // For early-return
	};
	std::vector<PendingAnalysis> pendingQueue_;
	
	// ============================================================
	// Internal Helpers
	// ============================================================
	
	bool readPixelRGB(int x, int y, int displayHeight, float& r, float& g, float& b);
	void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
	int classifyHsv(float h, float s, float v);
	VisualTeamResult analyzeScreenRegion(int centerX, int centerY, int displayHeight);
	bool shouldThrottle(const std::string& uuid) const;
};

// Global instance
extern VisualTeamResolver* visualTeamResolver;
