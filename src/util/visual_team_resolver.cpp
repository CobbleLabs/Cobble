#include "visual_team_resolver.h"
#include "friends_manager.h"
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
		
		// Interpolated position for render sync
		double interpX = p.lastPosX + (p.posX - p.lastPosX) * partialTicks;
		double interpY = p.lastPosY + (p.posY - p.lastPosY) * partialTicks;
		double interpZ = p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks;
		track.worldX = interpX;
		track.worldY = interpY;
		track.worldZ = interpZ;
		
		// Calculate distance
		double dx = interpX - renderPos.x;
		double dy = interpY - renderPos.y;
		double dz = interpZ - renderPos.z;
		track.distance = (float)std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Update player name
		if (!p.name.empty()) {
			track.playerName = p.name;
		}
		
		// Store previous screen position
		track.lastScreenX = track.screenX;
		track.lastScreenY = track.screenY;
		
		// Project to screen using interpolated position
		Vector3 headPos(
			(float)(interpX - renderPos.x),
			(float)(interpY + 2.15f - renderPos.y),
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
	if (it == tracks_.end()) return;
	
	VisualTrack& track = it->second;
	auto now = std::chrono::steady_clock::now();
	track.lastSeen = now;
	
	// Allow sticky players to be revalidated every stickyRecheckMs
	auto sinceLastAnalyze = std::chrono::duration_cast<std::chrono::milliseconds>(now - track.lastAnalyzed).count();
	if (track.sticky && sinceLastAnalyze < stickyRecheckMs) return;
	if (track.distance > maxAnalysisDistance) return;
	if (shouldThrottle(uuid)) return;
	if (!track.onScreen) return;
	
	// Queue using track's already-calculated interpolated screen position
	PendingAnalysis pending;
	pending.uuid = uuid;
	pending.playerName = entity.name;
	pending.headX = track.worldX;
	pending.headY = track.worldY + 2.15;
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
		
		// Use track's pre-calculated screen position
		VisualTrack localTrack;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = tracks_.find(pending.uuid);
			if (it == tracks_.end() || !it->second.onScreen) continue;
			localTrack = it->second;
		}
		
		int centerX = (int)localTrack.screenX;
		int centerY = (int)localTrack.screenY;
		
		float projectedHeight = (displayHeight * 1.8f) / (pending.distance * 2.0f);
		if (projectedHeight < minScreenSize) continue;
		
		int margin = 30;
		if (centerX < margin || centerX >= displayWidth - margin ||
		    centerY < margin || centerY >= displayHeight - margin) {
			continue;
		}
		
		// Dynamic offset based on distance AND speed (wider ROI when moving)
		float speedBonus = std::clamp(localTrack.speed * 2.0f, 0.0f, 10.0f);
		int dynamicOffset = (int)(projectedHeight * 0.15f) + (int)speedBonus;
		if (dynamicOffset < 5) dynamicOffset = 5;
		if (dynamicOffset > 25) dynamicOffset = 25;
		
		float dominantHue = 0.0f;
		bool valid = false;
		VisualRejectReason rejectReason = REJECT_NONE;
		int bestRowOffset = 0;
		
		// New robust sampling with vertical band scan
		analyzeScreenRegion(centerX, centerY, displayHeight, dynamicOffset, 
		                    dominantHue, valid, rejectReason, bestRowOffset);
		
		processed++;
		
		// Store debug sample
		if (debugOverlay) {
			std::lock_guard<std::mutex> lock(mutex_);
			DebugSample ds;
			ds.screenX = centerX;
			ds.screenY = centerY + bestRowOffset;  // Show where we actually found the nametag
			ds.offset = dynamicOffset;
			ds.bandHeight = 3;  // Always 3 rows now
			ds.bestRowOffset = bestRowOffset;
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
			
			// Update confidence with rejection reason for smart decay
			track.addSample(dominantHue, valid, rejectReason);
			track.updateSticky();
			track.lastRejectReason = rejectReason;
			
			// ============================================================
			// Auto-friend: register high-confidence teammates
			// Trigger on: sticky + 85% OR any time confidence > 95%
			// ============================================================
			if (friendsManager) {
				bool shouldAutoFriend = !track.autoFriend && (
					(track.sticky && track.confidence > 0.85f) ||  // Sticky + high confidence
					(track.confidence > 0.95f)                      // Very high confidence (faster trigger)
				);
				
				if (shouldAutoFriend) {
					friendsManager->addVisualFriend(pending.uuid);
					track.autoFriend = true;
					log_debug("[VisualTeam] Auto-friended %s (confidence=%.2f)", 
						track.playerName.empty() ? pending.uuid.substr(0, 8).c_str() : track.playerName.c_str(),
						track.confidence);
				}
				
				// Auto-unfriend: ONLY remove when we have EVIDENCE (consecutive enemy samples)
				// No confidence decay unfriend - visual friend = innocent until proven guilty
				// CRITICAL: Only count valid samples (saw actual red), not decay from LOW_SAT/OCCLUDED
				if (track.autoFriend) {
					if (valid && track.getResult() == VisualTeamResult::ENEMY) {
						// We SAW red color - this is real evidence
						track.consecutiveEnemySamples++;
						if (track.consecutiveEnemySamples >= 3) {
							friendsManager->removeVisualFriend(pending.uuid);
							track.autoFriend = false;
							track.consecutiveEnemySamples = 0;
							log_debug("[VisualTeam] Auto-unfriended %s (3 VALID enemy samples)", 
								track.playerName.empty() ? pending.uuid.substr(0, 8).c_str() : track.playerName.c_str());
						}
					} else if (valid) {
						// Valid sample but not enemy - reset counter
						track.consecutiveEnemySamples = 0;
					}
					// Invalid samples (LOW_SAT, OCCLUDED, etc.) don't affect the counter either way
				}
			}
			
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
							case REJECT_OCCLUDED: rejectStr = " [OCCLUDED]"; break;
							default: break;
						}
					}
					
					log_debug("[VisualTeam] %s: %.2f (%s) row=%+d%s%s",
						displayName.c_str(), track.confidence, resultStr,
						bestRowOffset, track.sticky ? " [STICKY]" : "", rejectStr);
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
			track.confidence += clusterConfidenceBoost;
			if (track.confidence > 1.0f) track.confidence = 1.0f;
		} else if (nearbyEnemy >= clusterMinPlayers && nearbyFriendly == 0) {
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
		
		if (it->second.sticky) {
			auto stickyAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.stickySince).count();
			if (stickyAgeMs > stickyTimeoutMs) {
				it->second.sticky = false;
				it->second.stickyContradictions = 0;
			}
		}
		
		float effectiveTTL = it->second.sticky ? 30000.0f : cacheTTLMs;
		
		if (ageMs > effectiveTTL) {
			it = tracks_.erase(it);
		} else {
			++it;
		}
	}
	
	// Clean old debug samples (very short TTL to avoid stale overlay when camera moves)
	auto cutoff = now - std::chrono::milliseconds(50);
	while (!debugSamples_.empty() && debugSamples_.front().timestamp < cutoff) {
		debugSamples_.pop_front();
	}
}

void VisualTeamResolver::resetAllTracks() {
	std::lock_guard<std::mutex> lock(mutex_);
	tracks_.clear();
	pendingQueue_.clear();
	debugSamples_.clear();
	
	// Clear visual friends on world change
	if (friendsManager) {
		friendsManager->clearVisualFriends();
	}
	
	log_debug("[VisualTeam] All tracks reset (world change detected)");
}

// Static to track world reference (GlobalRef)
static jobject s_lastWorldRef = nullptr;

void VisualTeamResolver::scanAllPlayers() {
	if (!enabled) return;
	
	// Detect world change using IsSameObject (not pointer comparison - local refs change every call!)
	jobject currentWorld = mc.getWorld();
	bool worldChanged = false;
	
	if (s_lastWorldRef == nullptr && currentWorld != nullptr) {
		worldChanged = true;
	} else if (s_lastWorldRef != nullptr && currentWorld == nullptr) {
		worldChanged = true;
	} else if (s_lastWorldRef != nullptr && currentWorld != nullptr) {
		if (!Java::Env->IsSameObject(s_lastWorldRef, currentWorld)) {
			worldChanged = true;
		}
	}
	
	if (worldChanged) {
		resetAllTracks();
		
		if (s_lastWorldRef) {
			Java::Env->DeleteGlobalRef(s_lastWorldRef);
			s_lastWorldRef = nullptr;
		}
		
		if (currentWorld) {
			s_lastWorldRef = Java::Env->NewGlobalRef(currentWorld);
		}
	}
	
	if (currentWorld) {
		Java::Env->DeleteLocalRef(currentWorld);
	}
	
	auto players = mc.getPlayerList();
	RenderPos renderPos = mc.getRenderPos();
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
	
	// Draw sample points
	for (const auto& ds : debugSamples_) {
		int x = ds.screenX;
		int y = ds.screenY;
		int off = ds.offset;
		
		ImU32 centerColor = ds.valid ? IM_COL32(0, 255, 0, 200) : IM_COL32(255, 0, 0, 200);
		ImU32 sampleColor = IM_COL32(0, 200, 255, 180);
		ImU32 bestRowColor = IM_COL32(255, 255, 0, 200);
		
		// Draw 9 sample points per row, 3 rows
		for (int dy = -1; dy <= 1; dy++) {
			int yRow = y + dy;
			bool isBestRow = (dy == 0);  // centerY is already adjusted to best row
			ImU32 rowColor = isBestRow ? bestRowColor : sampleColor;
			
			// 9 horizontal sample points
			for (int i = -4; i <= 4; i++) {
				int xPos = x + (off * i / 4);
				draw->AddRectFilled(
					ImVec2((float)(xPos-1), (float)(yRow-1)), 
					ImVec2((float)(xPos+1), (float)(yRow+1)), 
					(i == 0 && isBestRow) ? centerColor : rowColor
				);
			}
			
			draw->AddLine(ImVec2((float)(x-off), (float)yRow), ImVec2((float)(x+off), (float)yRow), 
			              IM_COL32(255, 255, 0, isBestRow ? 150 : 60), 1.0f);
		}
	}
	
	// Draw 2D ESP boxes for each player
	auto now = std::chrono::steady_clock::now();
	for (const auto& [uuid, track] : tracks_) {
		if (!track.onScreen) continue;
		
		// Skip stale tracks (not seen in last 1 second)
		auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - track.lastSeen).count();
		if (ageMs > 1000) continue;
		
		ImU32 boxColor;
		const char* label;
		
		// Check if this player is a visual friend (more reliable than just getResult)
		bool isVisualFriend = track.autoFriend || track.getResult() == VisualTeamResult::FRIENDLY;
		
		if (isVisualFriend) {
			boxColor = IM_COL32(0, 255, 0, 200);  // Green for friends
			label = "F";
		} else if (track.getResult() == VisualTeamResult::ENEMY) {
			boxColor = IM_COL32(255, 0, 0, 200);  // Red for enemies
			label = "E";
		} else {
			boxColor = IM_COL32(150, 150, 150, 150);  // Gray for unknown
			label = "?";
		}
		
		// Calculate box size based on distance (approximate 2D projection)
		float boxHeight = 1800.0f / (track.distance + 1.0f);  // ~1.8 blocks player height
		float boxWidth = boxHeight * 0.5f;  // ~0.6 blocks wide
		
		// Clamp box size
		if (boxHeight > 300.0f) boxHeight = 300.0f;
		if (boxHeight < 20.0f) boxHeight = 20.0f;
		boxWidth = boxHeight * 0.5f;
		
		// Box corners (centered on screen position, which is at head level)
		float left = track.screenX - boxWidth * 0.5f;
		float right = track.screenX + boxWidth * 0.5f;
		float top = track.screenY - boxHeight * 0.15f;  // Head is near top
		float bottom = track.screenY + boxHeight * 0.85f;  // Feet extend down
		
		// Draw box outline (no fill)
		draw->AddRect(
			ImVec2(left, top), 
			ImVec2(right, bottom), 
			boxColor, 
			0.0f,  // rounding
			0,     // flags
			2.0f   // thickness
		);
		
		// Draw confidence badge above box
		char buf[32];
		snprintf(buf, sizeof(buf), "[%s %.0f%%]", label, track.confidence * 100.0f);
		draw->AddText(ImVec2(left, top - 15.0f), boxColor, buf);
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

SampleClassification VisualTeamResolver::classifySample(float r, float g, float b, float a, float& hue) {
	hue = 0.0f;
	
	// Check transparency first
	if (a < minAlpha) {
		return SAMPLE_TRANSPARENT;
	}
	
	float h, s, v;
	rgbToHsv(r, g, b, h, s, v);
	hue = h;
	
	// Near-green relaxation: allow darker/less saturated greens (for &2 dark green)
	float distToGreen = fabsf(h - 120.0f);
	if (distToGreen > 180.0f) distToGreen = 360.0f - distToGreen;
	bool nearGreen = distToGreen < 40.0f;
	
	// Relax thresholds for near-green colors (shadow/outline still filtered)
	float satThreshold = nearGreen ? minSaturation * 0.4f : minSaturation;
	float valThreshold = nearGreen ? minValue * 0.6f : minValue;
	
	if (s < satThreshold || v < valThreshold) {
		return SAMPLE_LOW_SAT;
	}
	
	// Any saturated color is valid for classification
	// Green = friendly, anything else = enemy (handled in addSample)
	return SAMPLE_COLOR_VALID;
}

bool VisualTeamResolver::sampleBlock3x3(const std::vector<unsigned char>& buffer, int roiSize, int bufX, int bufY,
                                         float& avgR, float& avgG, float& avgB, float& avgA) {
	avgR = avgG = avgB = avgA = 0.0f;
	int count = 0;
	
	for (int dx = -1; dx <= 1; dx++) {
		for (int dy = -1; dy <= 1; dy++) {
			int x = bufX + dx;
			int y = bufY + dy;
			
			if (x < 0 || x >= roiSize || y < 0 || y >= roiSize) continue;
			
			int flippedY = roiSize - 1 - y;
			int idx = (flippedY * roiSize + x) * 4;
			
			if (idx < 0 || idx + 3 >= (int)buffer.size()) continue;
			
			avgR += buffer[idx] / 255.0f;
			avgG += buffer[idx + 1] / 255.0f;
			avgB += buffer[idx + 2] / 255.0f;
			avgA += buffer[idx + 3] / 255.0f;
			count++;
		}
	}
	
	if (count == 0) return false;
	
	avgR /= count;
	avgG /= count;
	avgB /= count;
	avgA /= count;
	return true;
}

bool VisualTeamResolver::analyzeScreenRegion(int centerX, int centerY, int displayHeight,
                                              int dynamicOffset,
                                              float& dominantHue, bool& valid,
                                              VisualRejectReason& rejectReason,
                                              int& bestRowOffset) {
	valid = false;
	dominantHue = 0.0f;
	rejectReason = REJECT_NONE;
	bestRowOffset = 0;
	
	// ============================================================
	// VERTICAL BAND SCAN: Find the best Y offset (-12 to +2)
	// ============================================================
	
	// ROI large enough for vertical scan
	int roiWidth = dynamicOffset * 2 + 10;
	int roiHeight = 20;  // -12 to +2 = 15 rows, plus margin
	int roiSize = std::max(roiWidth, roiHeight);
	int roiHalf = roiSize / 2;
	
	int roiX = centerX - roiHalf;
	int roiY = centerY - 12 - 2;  // Start above the expected nametag position
	int glRoiY = displayHeight - roiY - roiSize;
	
	// Clamp ROI to screen bounds
	if (roiX < 0) roiX = 0;
	if (glRoiY < 0) glRoiY = 0;
	
	std::vector<unsigned char> buffer(roiSize * roiSize * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(roiX, glRoiY, roiSize, roiSize, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
	
	// ============================================================
	// SCAN EACH ROW (dy = -12 to +2) and count valid samples
	// ============================================================
	
	struct RowResult {
		int dy;
		int validCount;
		int greenCount;
		int redCount;
		int transparentCount;
		int lowSatCount;
		float greenSum;
		float redSum;
	};
	
	std::vector<RowResult> rowResults;
	
	// 9 horizontal sample points per row
	std::vector<int> xOffsets;
	for (int i = -4; i <= 4; i++) {
		xOffsets.push_back(dynamicOffset * i / 4);
	}
	
	for (int dy = -12; dy <= 2; dy++) {
		RowResult row = { dy, 0, 0, 0, 0, 0, 0.0f, 0.0f };
		
		// Always 3 vertical sub-rows per scan row
		for (int subDy = -1; subDy <= 1; subDy++) {
			for (int xOff : xOffsets) {
				int bufX = roiHalf + xOff;
				int bufY = 12 + dy + subDy;  // 12 is our centerY offset in ROI
				
				float avgR, avgG, avgB, avgA;
				if (!sampleBlock3x3(buffer, roiSize, bufX, bufY, avgR, avgG, avgB, avgA)) {
					continue;
				}
				
				float hue;
				SampleClassification cls = classifySample(avgR, avgG, avgB, avgA, hue);
				
				switch (cls) {
					case SAMPLE_COLOR_VALID: {
						row.validCount++;
						// Angular distance classification
						float distToGreen = fabsf(hue - 120.0f);
						if (distToGreen > 180.0f) distToGreen = 360.0f - distToGreen;
						if (distToGreen < 50.0f) {
							row.greenCount++;
							row.greenSum += hue;
						} else {
							row.redCount++;
							row.redSum += (hue > 300.0f) ? (hue - 360.0f) : hue;
						}
						break;
					}
					case SAMPLE_TRANSPARENT:
						row.transparentCount++;
						break;
					case SAMPLE_LOW_SAT:
						row.lowSatCount++;
						break;
					case SAMPLE_MIXED:
						break;
				}
			}
		}
		
		rowResults.push_back(row);
	}
	
	// ============================================================
	// FIND BEST ROW (most valid samples)
	// ============================================================
	
	int bestIdx = -1;
	int bestValidCount = 0;
	
	for (size_t i = 0; i < rowResults.size(); i++) {
		if (rowResults[i].validCount > bestValidCount) {
			bestValidCount = rowResults[i].validCount;
			bestIdx = (int)i;
		}
	}
	
	if (bestIdx < 0 || bestValidCount < 3) {
		// Check if mostly transparent (occluded)
		int totalTransparent = 0;
		int totalSamples = 0;
		for (const auto& r : rowResults) {
			totalTransparent += r.transparentCount;
			totalSamples += r.validCount + r.transparentCount + r.lowSatCount;
		}
		
		if (totalSamples > 0 && (float)totalTransparent / totalSamples > 0.7f) {
			rejectReason = REJECT_OCCLUDED;
		} else {
			int totalLowSat = 0;
			for (const auto& r : rowResults) totalLowSat += r.lowSatCount;
			rejectReason = (totalLowSat >= 10) ? REJECT_LOW_SATURATION : REJECT_NO_SAMPLES;
		}
		return false;
	}
	
	const RowResult& best = rowResults[bestIdx];
	bestRowOffset = best.dy;
	
	// ============================================================
	// DETERMINE DOMINANT COLOR
	// ============================================================
	
	if (best.greenCount >= best.redCount && best.greenCount >= 2) {
		// Majority green = friendly
		dominantHue = best.greenSum / best.greenCount;
		valid = true;
	} else if (best.redCount >= 1) {
		// Any non-green = enemy (redCount is actually "nonGreenCount")
		dominantHue = best.redSum / best.redCount;
		if (dominantHue < 0) dominantHue += 360.0f;
		valid = true;
	}
	
	return true;
}
