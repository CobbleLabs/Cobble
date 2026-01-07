#include "visual_team_resolver.h"
#include "logger.hpp"
#include "http.hpp"
#include "math/worldToScreen.h"
#include "../sdk/player/render_data.h"
#include <Windows.h>
#include <gl/GL.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>

// Global instance
VisualTeamResolver* visualTeamResolver = nullptr;

// Debug toggle
static bool g_visualTeamDebug = true;

// ============================================================
// Main API
// ============================================================

bool VisualTeamResolver::isFriendly(const std::string& uuid) const {
	if (!enabled || uuid.empty()) return false;
	
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = tracks_.find(uuid);
	if (it == tracks_.end()) return false;
	
	return it->second.getResult() == VisualTeamResult::FRIENDLY;
}

float VisualTeamResolver::getConfidence(const std::string& uuid) const {
	if (!enabled || uuid.empty()) return 0.5f;
	
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = tracks_.find(uuid);
	if (it == tracks_.end()) return 0.5f;
	
	return it->second.confidence;
}

VisualTeamResult VisualTeamResolver::getResultWithFallback(const std::string& uuid) const {
	if (!enabled || uuid.empty()) return VisualTeamResult::UNKNOWN;
	
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = tracks_.find(uuid);
	if (it == tracks_.end()) return VisualTeamResult::UNKNOWN;
	
	const VisualTrack& track = it->second;
	VisualTeamResult result = track.getResult();
	
	// If UNKNOWN but had recent valid data, use last known state
	if (result == VisualTeamResult::UNKNOWN && track.consecutiveFailures < fallbackMaxFrames) {
		// Check if we had a stable classification before
		if (track.lastStableConfidence > 0.7f) return VisualTeamResult::FRIENDLY;
		if (track.lastStableConfidence < 0.3f) return VisualTeamResult::ENEMY;
	}
	
	return result;
}

void VisualTeamResolver::updateTracks(const std::vector<PlayerInfo>& players, 
                                       const RenderPos& renderPos, int screenW, int screenH, float partialTicks) {
	if (!enabled) return;
	
	std::lock_guard<std::mutex> lock(mutex_);
	auto now = std::chrono::steady_clock::now();
	
	Matrix mvMatrix = RenderData::modelView;
	Matrix projMatrix = RenderData::projection;
	
	for (const auto& p : players) {
		if (p.uuid.empty()) continue;
		
		VisualTrack& track = tracks_[p.uuid];
		track.uuid = p.uuid;
		track.lastSeen = now;
		
		// Update position and velocity
		track.updatePosition(p.posX, p.posY, p.posZ);
		
		// Calculate speed from velocity
		track.speed = (float)std::sqrt(track.velocityX * track.velocityX + 
		                               track.velocityY * track.velocityY + 
		                               track.velocityZ * track.velocityZ);
		
		// Interpolated position for render sync (using PlayerInfo's lastPos from JNI)
		// This matches the pattern used in ESP/Nametags for frame-accurate positions
		double interpX = p.lastPosX + (p.posX - p.lastPosX) * partialTicks;
		double interpY = p.lastPosY + (p.posY - p.lastPosY) * partialTicks;
		double interpZ = p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks;
		track.worldX = interpX;
		track.worldY = interpY;
		track.worldZ = interpZ;
		
		// Calculate distance (using interpolated position)
		double dx = interpX - renderPos.x;
		double dy = interpY - renderPos.y;
		double dz = interpZ - renderPos.z;
		track.distance = (float)std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Update player name
		if (!p.name.empty()) {
			track.playerName = p.name;
		}
		
		// Store previous screen position for motion compensation
		track.lastScreenX = track.screenX;
		track.lastScreenY = track.screenY;
		
		// Project to screen using interpolated position
		Vector3 headPos(
			(float)(interpX - renderPos.x),
			(float)(interpY + 2.15f - renderPos.y),  // Original height for nametag
			(float)(interpZ - renderPos.z)
		);
		
		Vector2 screenPos;
		track.onScreen = CWorldToScreen::WorldToScreen(
			headPos, mvMatrix, projMatrix, screenW, screenH, screenPos
		);
		
		if (track.onScreen) {
			track.screenX = screenPos.x;
			track.screenY = screenPos.y;
		}
	}
}

void VisualTeamResolver::queueForAnalysis(const std::string& uuid, const PlayerInfo& entity, const RenderPos& renderPos) {
	if (!enabled || uuid.empty()) return;
	
	std::lock_guard<std::mutex> lock(mutex_);
	
	auto it = tracks_.find(uuid);
	if (it == tracks_.end()) return;  // Must have track from updateTracks
	
	VisualTrack& track = it->second;
	track.lastSeen = std::chrono::steady_clock::now();
	
	if (track.sticky) return;
	if (track.distance > maxAnalysisDistance) return;
	if (shouldThrottle(uuid)) return;
	if (!track.onScreen) return;
	
	// Queue using track's already-calculated interpolated screen position
	PendingAnalysis pending;
	pending.uuid = uuid;
	pending.playerName = entity.name;
	pending.headX = track.worldX;
	pending.headY = track.worldY + 2.15;  // Consistent with updateTracks
	pending.headZ = track.worldZ;
	pending.renderX = renderPos.x;
	pending.renderY = renderPos.y;
	pending.renderZ = renderPos.z;
	pending.distance = track.distance;
	
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
	if (!RenderData::updated) return;
	
	auto now = std::chrono::steady_clock::now();
	int processed = 0;
	
	// Sort by distance (prioritize closer targets)
	std::sort(toProcess.begin(), toProcess.end(), [](const PendingAnalysis& a, const PendingAnalysis& b) {
		return a.distance < b.distance;
	});
	
	for (const auto& pending : toProcess) {
		if (processed >= maxAnalysesPerFrame) break;
		
		// Use track's pre-calculated screen position (single source of truth)
		VisualTrack localTrack;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = tracks_.find(pending.uuid);
			if (it == tracks_.end() || !it->second.onScreen) continue;
			localTrack = it->second;
		}
		
		// Motion compensation: predict where the nametag will be this frame
		float deltaX = localTrack.screenX - localTrack.lastScreenX;
		float deltaY = localTrack.screenY - localTrack.lastScreenY;
		float motionFactor = std::clamp(localTrack.speed * 0.12f, 0.0f, 1.0f);
		float compensatedX = localTrack.screenX + deltaX * motionFactor;
		float compensatedY = localTrack.screenY + deltaY * motionFactor;
		
		int centerX = (int)compensatedX;
		int centerY = (int)compensatedY;
		
		float projectedHeight = (displayHeight * 1.8f) / (pending.distance * 2.0f);
		if (projectedHeight < minScreenSize) continue;
		
		int nametagYOffset = std::clamp((int)(projectedHeight * 0.25f), 4, 24);
		int sampleY = centerY - nametagYOffset;
		
		int margin = 30;
		if (centerX < margin || centerX >= displayWidth - margin ||
		    sampleY < margin || sampleY >= displayHeight - margin) {
			continue;
		}
		
		// Dynamic offset based on distance AND speed
		float speedFactor = 1.0f + localTrack.speed * 0.2f;  // Bigger offset when moving fast
		int dynamicOffset = (int)(projectedHeight * 0.15f * speedFactor);
		if (dynamicOffset < 3) dynamicOffset = 3;
		if (dynamicOffset > 20) dynamicOffset = 20;  // Allow larger offset when moving
		
		// Band height: 2 rows when still, 3 rows when moving (dy = -1, 0, +1)
		bool isMoving = localTrack.speed > 2.0f;  // ~walking speed
		int bandHeight = isMoving ? 3 : 2;
		
		float dominantHue = 0.0f;
		bool valid = false;
		VisualRejectReason rejectReason = REJECT_NONE;
		analyzeScreenRegion(centerX, sampleY, displayHeight, dynamicOffset, 
		                    bandHeight, isMoving, dominantHue, valid, rejectReason);
		
		processed++;
		
		// Store debug sample (compensated position for accurate overlay)
		if (debugOverlay) {
			std::lock_guard<std::mutex> lock(mutex_);
			DebugSample ds;
			ds.screenX = centerX;
			ds.screenY = sampleY;
			ds.offset = dynamicOffset;
			ds.bandHeight = bandHeight;
			ds.valid = valid;
			ds.hue = dominantHue;
			ds.timestamp = now;
			debugSamples_.push_back(ds);
			
			while (debugSamples_.size() > 50) {
				debugSamples_.pop_front();
			}
		}
		
		// Update track
		{
			std::lock_guard<std::mutex> lock(mutex_);
			VisualTrack& track = tracks_[pending.uuid];
			track.lastAnalyzed = now;
			track.lastSeen = now;
			
			if (!pending.playerName.empty()) {
				track.playerName = pending.playerName;
			}
			
			// Async Mojang lookup
			if (!track.mojangLookupDone && !track.mojangLookupQueued) {
				track.mojangLookupQueued = true;
				std::string uuidCopy = pending.uuid;
				std::thread([this, uuidCopy]() {
					std::string realName = http::lookupMojangName(uuidCopy);
					if (!realName.empty()) {
						std::lock_guard<std::mutex> lock(mutex_);
						auto it = tracks_.find(uuidCopy);
						if (it != tracks_.end()) {
							it->second.mojangName = realName;
							it->second.mojangLookupDone = true;
							if (g_visualTeamDebug) {
								log_debug("[VisualTeam] Resolved: %s -> %s", 
									uuidCopy.substr(0,8).c_str(), realName.c_str());
							}
						}
					}
				}).detach();
			}
			
			// Update confidence
			track.addSample(dominantHue, valid);
			track.updateSticky();
			track.lastRejectReason = rejectReason;
			
			// Rate-limited debug logging
			if (g_visualTeamDebug) {
				auto logElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					now - track.lastLogTime
				).count();
				
				if (logElapsed >= 500) {
					track.lastLogTime = now;
					
					std::string displayName = !track.mojangName.empty() 
						? track.mojangName 
						: pending.uuid.substr(0, 8);
					
					const char* resultStr = (track.getResult() == VisualTeamResult::FRIENDLY) ? "FRIENDLY" :
					                        (track.getResult() == VisualTeamResult::ENEMY) ? "ENEMY" : "UNKNOWN";
					
					const char* rejectStr = "";
					if (!valid) {
						switch (rejectReason) {
							case REJECT_NO_SAMPLES: rejectStr = " [NO_SAMPLES]"; break;
							case REJECT_LOW_SATURATION: rejectStr = " [LOW_SAT]"; break;
							case REJECT_HIGH_VARIANCE: rejectStr = " [HIGH_VAR]"; break;
							case REJECT_MIXED_COLORS: rejectStr = " [MIXED]"; break;
							default: break;
						}
					}
					
					log_debug("[VisualTeam] %s: %.2f (%s)%s%s",
						displayName.c_str(), track.confidence, resultStr,
						track.sticky ? " [STICKY]" : "", rejectStr);
				}
			}
		}
	}
	
	// Apply cluster inference after all visual analysis
	applyClusterInference();
}

void VisualTeamResolver::applyClusterInference() {
	std::lock_guard<std::mutex> lock(mutex_);
	
	auto now = std::chrono::steady_clock::now();
	
	for (auto& [uuid, track] : tracks_) {
		// Only apply to UNKNOWN players with recent failures
		if (track.getResult() != VisualTeamResult::UNKNOWN) continue;
		if (track.consecutiveFailures < 3) continue;
		
		// Check age
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - track.lastSeen).count();
		if (ageMs > 1000) continue;
		
		// Count nearby friendly/enemy players
		int nearbyFriendly = 0;
		int nearbyEnemy = 0;
		
		for (const auto& [otherUuid, otherTrack] : tracks_) {
			if (otherUuid == uuid) continue;
			if (otherTrack.getResult() == VisualTeamResult::UNKNOWN) continue;
			
			float dist = track.worldDistanceTo(otherTrack);
			if (dist > clusterRadius) continue;
			
			if (otherTrack.getResult() == VisualTeamResult::FRIENDLY) {
				nearbyFriendly++;
			} else {
				nearbyEnemy++;
			}
		}
		
		// Apply cluster inference
		if (nearbyFriendly >= clusterMinPlayers && nearbyEnemy == 0) {
			// Surrounded by friendlies -> probably friendly
			track.confidence += clusterConfidenceBoost;
			if (track.confidence > 1.0f) track.confidence = 1.0f;
		} else if (nearbyEnemy >= clusterMinPlayers && nearbyFriendly == 0) {
			// Surrounded by enemies -> probably enemy
			track.confidence -= clusterConfidenceBoost;
			if (track.confidence < 0.0f) track.confidence = 0.0f;
		}
	}
}

void VisualTeamResolver::tick() {
	std::lock_guard<std::mutex> lock(mutex_);
	auto now = std::chrono::steady_clock::now();
	
	for (auto it = tracks_.begin(); it != tracks_.end(); ) {
		it->second.checkInvalidate(now);
		
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - it->second.lastSeen
		).count();
		
		float effectiveTTL = it->second.sticky ? 30000.0f : cacheTTLMs;
		
		if (ageMs > effectiveTTL) {
			it = tracks_.erase(it);
		} else {
			++it;
		}
	}
	
	// Clean old debug samples
	auto cutoff = now - std::chrono::milliseconds(500);
	while (!debugSamples_.empty() && debugSamples_.front().timestamp < cutoff) {
		debugSamples_.pop_front();
	}
}

void VisualTeamResolver::scanAllPlayers() {
	if (!enabled) return;
	
	auto players = mc.getPlayerList();
	RenderPos renderPos = mc.getRenderPos();
	// Get partialTicks for interpolation (matches ESP/Nametags pattern)
	float partialTicks = mc.getPartialTicks();
	
	// Update tracks first
	updateTracks(players, renderPos, RenderData::screenWidth, RenderData::screenHeight, partialTicks);
	
	for (auto& p : players) {
		if (p.uuid.empty()) continue;
		queueForAnalysis(p.uuid, p, renderPos);
		
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}
}

// ============================================================
// Debug Overlay
// ============================================================

void VisualTeamResolver::renderDebugOverlay(ImDrawList* draw) {
	if (!debugOverlay || !draw) return;
	
	std::lock_guard<std::mutex> lock(mutex_);
	
	// Draw sample points (compensated position - where we actually sample)
	for (const auto& ds : debugSamples_) {
		int x = ds.screenX;
		int y = ds.screenY;
		int off = ds.offset;
		int halfOff = off / 2;
		
		ImU32 centerColor = ds.valid ? IM_COL32(0, 255, 0, 200) : IM_COL32(255, 0, 0, 200);
		ImU32 sampleColor = IM_COL32(0, 200, 255, 180);
		
		// Draw vertical band when moving (bandHeight > 1)
		int startDy = (ds.bandHeight > 1) ? -1 : 0;
		int endDy = (ds.bandHeight > 1) ? 1 : 0;
		
		for (int dy = startDy; dy <= endDy; dy++) {
			int yRow = y + dy;
			// 5 sample points per row
			draw->AddRectFilled(ImVec2((float)(x-off-2), (float)(yRow-2)), ImVec2((float)(x-off+2), (float)(yRow+2)), sampleColor);
			draw->AddRectFilled(ImVec2((float)(x-halfOff-2), (float)(yRow-2)), ImVec2((float)(x-halfOff+2), (float)(yRow+2)), sampleColor);
			draw->AddRectFilled(ImVec2((float)(x-2), (float)(yRow-2)), ImVec2((float)(x+2), (float)(yRow+2)), dy == 0 ? centerColor : sampleColor);
			draw->AddRectFilled(ImVec2((float)(x+halfOff-2), (float)(yRow-2)), ImVec2((float)(x+halfOff+2), (float)(yRow+2)), sampleColor);
			draw->AddRectFilled(ImVec2((float)(x+off-2), (float)(yRow-2)), ImVec2((float)(x+off+2), (float)(yRow+2)), sampleColor);
			
			draw->AddLine(ImVec2((float)(x-off), (float)yRow), ImVec2((float)(x+off), (float)yRow), IM_COL32(255, 255, 0, 100), 1.0f);
		}
	}
	
	// Draw track info for each player
	for (const auto& [uuid, track] : tracks_) {
		if (!track.onScreen) continue;
		
		ImU32 textColor;
		const char* label;
		
		switch (track.getResult()) {
			case VisualTeamResult::FRIENDLY:
				textColor = IM_COL32(0, 255, 0, 255);
				label = "F";
				break;
			case VisualTeamResult::ENEMY:
				textColor = IM_COL32(255, 0, 0, 255);
				label = "E";
				break;
			default:
				textColor = IM_COL32(150, 150, 150, 255);
				label = "?";
				break;
		}
		
		// Draw confidence badge above player
		char buf[32];
		snprintf(buf, sizeof(buf), "[%s %.0f%%]", label, track.confidence * 100.0f);
		draw->AddText(ImVec2(track.screenX - 20.0f, track.screenY - 25.0f), textColor, buf);
	}
}

// ============================================================
// Internal Helpers
// ============================================================

bool VisualTeamResolver::shouldThrottle(const std::string& uuid) const {
	auto it = tracks_.find(uuid);
	if (it == tracks_.end()) return false;
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - it->second.lastAnalyzed
	).count();
	
	return elapsed < analysisIntervalMs;
}

void VisualTeamResolver::rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
	float cmax = r > g ? (r > b ? r : b) : (g > b ? g : b);
	float cmin = r < g ? (r < b ? r : b) : (g < b ? g : b);
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

bool VisualTeamResolver::analyzeScreenRegion(int centerX, int centerY, int displayHeight, 
                                              int dynamicOffset, int bandHeight, bool isMoving,
                                              float& dominantHue, bool& valid,
                                              VisualRejectReason& rejectReason) {
	valid = false;
	dominantHue = 0.0f;
	rejectReason = REJECT_NONE;
	
	// Larger ROI when using vertical band
	int roiSize = dynamicOffset * 2 + 4 + bandHeight * 2;
	int roiHalf = roiSize / 2;
	
	int roiX = centerX - roiHalf;
	int roiY = centerY - roiHalf;
	int glRoiY = displayHeight - roiY - roiSize;
	
	std::vector<GLubyte> buffer(roiSize * roiSize * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(roiX, glRoiY, roiSize, roiSize, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
	
	// Build sample points: horizontal strip with optional vertical band
	std::vector<std::pair<int, int>> samplePoints;
	int halfOff = dynamicOffset / 2;
	
	// Vertical band: sample dy = -1, 0, +1 when moving; only 0 when still
	int startDy = (bandHeight > 1) ? -1 : 0;
	int endDy = (bandHeight > 1) ? 1 : 0;
	
	for (int dy = startDy; dy <= endDy; dy++) {
		samplePoints.push_back({-dynamicOffset, dy});
		samplePoints.push_back({-halfOff, dy});
		samplePoints.push_back({0, dy});
		samplePoints.push_back({halfOff, dy});
		samplePoints.push_back({dynamicOffset, dy});
	}
	
	std::vector<float> validHues;
	int lowSatCount = 0;
	int lowAlphaCount = 0;
	
	// Debug logging
	static int debugCounter = 0;
	bool shouldDebugLog = (++debugCounter % 50 == 1);
	
	for (size_t i = 0; i < samplePoints.size(); i++) {
		const auto& sample = samplePoints[i];
		int bufX = roiHalf + sample.first;
		int bufY = roiHalf + sample.second;
		
		if (bufX < 0 || bufX >= roiSize || bufY < 0 || bufY >= roiSize) continue;
		
		int flippedBufY = roiSize - 1 - bufY;
		int idx = (flippedBufY * roiSize + bufX) * 4;
		
		if (idx < 0 || idx + 3 >= (int)buffer.size()) continue;
		
		float r = buffer[idx] / 255.0f;
		float g = buffer[idx + 1] / 255.0f;
		float b = buffer[idx + 2] / 255.0f;
		float a = buffer[idx + 3] / 255.0f;
		
		if (shouldDebugLog && i == 0) {
			float h, s, v;
			rgbToHsv(r, g, b, h, s, v);
			log_debug("[VT-Pixel] center(%d,%d) band=%d RGBA=%.2f,%.2f,%.2f,%.2f HSV=%.0f,%.2f,%.2f",
				centerX, centerY, bandHeight, r, g, b, a, h, s, v);
		}
		
		if (a < minAlpha) {
			lowAlphaCount++;
			continue;
		}
		
		float h, s, v;
		rgbToHsv(r, g, b, h, s, v);
		
		float satThreshold = (v > 0.8f) ? minSaturation * 0.7f : minSaturation;
		float valueThreshold = minValue;
		
		if (s < satThreshold || v < valueThreshold) {
			lowSatCount++;
			continue;
		}
		
		bool isGreen = (h >= 70.0f && h <= 170.0f);
		bool isRed = (h >= 0.0f && h <= 30.0f) || (h >= 330.0f && h <= 360.0f);
		
		if (isGreen || isRed) {
			validHues.push_back(h);
		}
	}
	
	// Require more than a single hit to avoid noise from particles/overlays
	size_t minSamples = isMoving ? 2 : 3;
	if (validHues.size() < minSamples) {
		rejectReason = (lowSatCount >= 3) ? REJECT_LOW_SATURATION : REJECT_NO_SAMPLES;
		return false;
	}
	
	// Variance rejection
	std::vector<float> normalizedHues;
	for (float h : validHues) {
		if (h > 300.0f) h -= 360.0f;
		normalizedHues.push_back(h);
	}
	
	float mean = std::accumulate(normalizedHues.begin(), normalizedHues.end(), 0.0f) / normalizedHues.size();
	float variance = 0.0f;
	for (float h : normalizedHues) {
		variance += (h - mean) * (h - mean);
	}
	variance /= normalizedHues.size();
	float stddev = std::sqrt(variance);
	
	if (stddev > maxHueVariance) {
		rejectReason = REJECT_HIGH_VARIANCE;
		return false;
	}
	
	// Count colors
	int greenCount = 0, redCount = 0;
	float greenSum = 0, redSum = 0;
	
	for (float h : validHues) {
		if (h >= 70.0f && h <= 170.0f) {
			greenCount++;
			greenSum += h;
		} else {
			redCount++;
			redSum += (h > 300.0f) ? (h - 360.0f) : h;
		}
	}
	
	// Anti-mix ratio
	int total = greenCount + redCount;
	if (total >= 3) {
		int dominant = greenCount > redCount ? greenCount : redCount;
		float ratio = (float)dominant / total;
		if (ratio < 0.75f) {
			rejectReason = REJECT_MIXED_COLORS;
			return false;
		}
	}
	
	if (greenCount > redCount && greenCount >= 2) {
		dominantHue = greenSum / greenCount;
		valid = true;
	} else if (redCount > greenCount && redCount >= 2) {
		dominantHue = redSum / redCount;
		if (dominantHue < 0) dominantHue += 360.0f;
		valid = true;
	}
	
	return true;
}
