#include "target_tracker.h"
#include <chrono>

/**
 * Associates detections into stable target identities.
 * Maintains lock state and switching hysteresis,
 * then selects the best target candidate for aiming.
 */

TargetTracker::TargetTracker()
    : m_nextId(1)
    , m_lockedTargetId(-1)
    , m_lockFrameCount(0)
    , m_lockedMissFrames(0)
    , m_switchCooldownFrames(0)
    , m_lastUpdateNs(0) {
}

void TargetTracker::reset() {
    m_targets.clear();
    m_nextId = 1;
    m_lockedTargetId = -1;
    m_lockFrameCount = 0;
    m_lockedMissFrames = 0;
    m_switchCooldownFrames = 0;
    m_lastUpdateNs = 0;
}

const TargetArray& TargetTracker::getTargets() const {
    return m_targets;
}

void TargetTracker::updateVelocity(TrackedTarget& track, const ESP::BoundingBox& detection, float dt, float smoothing) {
    if (dt <= 0.0001f) {
        return;
    }

    ESP::Vector2 centerOld = track.box.center();
    ESP::Vector2 centerNew = detection.center();
    ESP::Vector2 newVelocity = (centerNew - centerOld) * (1.0f / dt);

    // Clamp velocity spikes caused by single-frame detector jitter
    const float maxVelocity = std::max(60.0f, detection.height * 1.5f);
    newVelocity.x = AimbotMath::clamp(newVelocity.x, -maxVelocity, maxVelocity);
    newVelocity.y = AimbotMath::clamp(newVelocity.y, -maxVelocity, maxVelocity);
    
    // EMA filter: velocity = velocity * alpha + newVelocity * (1 - alpha)
    track.velocity = track.velocity * smoothing + newVelocity * (1.0f - smoothing);
}

void TargetTracker::update(const ESP::BoundingBox* detections, int count, const UnifiedSettings& settings) {
    // Keep tracker focused on enemy class only (reduces identity churn and false lock candidates)
    for (int t = m_targets.size() - 1; t >= 0; --t) {
        if (m_targets[t].box.classId != Config::ENEMY_CLASS_ID) {
            if (m_targets[t].id == m_lockedTargetId) {
                m_lockedTargetId = -1;
                m_lockFrameCount = 0;
                m_lockedMissFrames = 0;
                m_switchCooldownFrames = 0;
            }
            m_targets.removeAt(t);
        }
    }

    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    float dt = 1.0f / 60.0f;
    if (m_lastUpdateNs > 0 && nowNs > m_lastUpdateNs) {
        dt = static_cast<float>(nowNs - m_lastUpdateNs) / 1'000'000'000.0f;
        dt = AimbotMath::clamp(dt, 1.0f / 120.0f, 1.0f / 20.0f);
    }
    m_lastUpdateNs = nowNs;

    // 1. Predict existing targets
    predictTargets(dt);
    
    const int numDets = count;
    const int numTracks = m_targets.size();
    
    constexpr int MAX_LOCAL_DETS = 128;
    bool detMatched[MAX_LOCAL_DETS] = {false};
    bool trkMatched[MAX_TRACKED_TARGETS] = {false};
    
    int safeCount = std::min(numDets, MAX_LOCAL_DETS);
    
    // 2. Matching Cascade by Age (DeepSORT approach)
    // Match younger tracks first to prevent occlusions from stealing stable tracks
    if (numTracks > 0 && safeCount > 0) {
        // Find max age
        int maxAge = 0;
        for (int t = 0; t < numTracks; t++) {
            if (m_targets[t].age > maxAge) maxAge = m_targets[t].age;
        }
        
        // Match tracks in order of increasing age (younger first)
        for (int currentAge = 0; currentAge <= maxAge; currentAge++) {
            for (int t = 0; t < numTracks; t++) {
                if (trkMatched[t]) continue;
                TrackedTarget& track = m_targets[t];
                
                // Only process tracks of current age
                if (track.age != currentAge) continue;
                
                float bestScore = -1.0f;
                int bestDetIdx = -1;

                const ESP::Vector2 trackCenter = track.box.center();
                const float trackArea = std::max(1.0f, track.box.width * track.box.height);
                const float baseCenterJump = std::max(45.0f, std::max(track.box.width, track.box.height) * 1.2f);
                const float maxCenterJump = baseCenterJump + static_cast<float>(track.lost) * 12.0f;
                const bool isLockedTrack = (track.id == m_lockedTargetId);
                const float minIou = std::max(0.12f, settings.iouThreshold * (isLockedTrack ? 0.70f : 1.0f));
                
                for (int d = 0; d < safeCount; d++) {
                    if (detMatched[d]) continue;

                    const ESP::BoundingBox& det = detections[d];
                    if (det.classId != track.box.classId) continue;

                    const ESP::Vector2 detCenter = det.center();
                    const float centerDx = detCenter.x - trackCenter.x;
                    const float centerDy = detCenter.y - trackCenter.y;
                    const float centerDistSq = centerDx * centerDx + centerDy * centerDy;
                    if (centerDistSq > maxCenterJump * maxCenterJump) continue;

                    const float iou = track.box.iou(det);
                    if (iou < minIou) continue;

                    const float detArea = std::max(1.0f, det.width * det.height);
                    const float areaRatio = std::max(trackArea, detArea) / std::min(trackArea, detArea);
                    if (areaRatio > 4.5f) continue;

                    const float centerScore = 1.0f - AimbotMath::clamp(std::sqrt(centerDistSq) / std::max(1.0f, maxCenterJump), 0.0f, 1.0f);
                    const float areaScore = 1.0f - AimbotMath::clamp((areaRatio - 1.0f) / 2.0f, 0.0f, 1.0f);
                    float score = iou * 0.70f + centerScore * 0.22f + areaScore * 0.08f;
                    if (isLockedTrack) {
                        score += 0.06f;
                    }

                    if (score > bestScore) {
                        bestScore = score;
                        bestDetIdx = d;
                    }
                }
                
                // Link if IOU threshold met
                if (bestDetIdx != -1) {
                    const ESP::BoundingBox& match = detections[bestDetIdx];

                    const ESP::Vector2 oldCenter = track.box.center();
                    const ESP::Vector2 newCenter = match.center();
                    const float centerJump = ESP::Vector2::Distance(oldCenter, newCenter);
                    const float oldArea = std::max(1.0f, track.box.width * track.box.height);
                    const float newArea = std::max(1.0f, match.width * match.height);
                    const float areaRatio = std::max(oldArea, newArea) / std::min(oldArea, newArea);
                    const float resetJumpThreshold = std::max(80.0f, std::max(track.box.width, track.box.height) * 1.6f);
                    const bool resetFilterState = (centerJump > resetJumpThreshold) || (areaRatio > 2.3f);

                    if (resetFilterState) {
                        track.velocity = ESP::Vector2::Zero();
                        track.ema_x = newCenter.x;
                        track.ema_y = newCenter.y;
                        track.ema_initialized = true;
                        track.kalman_x = newCenter.x;
                        track.kalman_y = newCenter.y;
                        track.kalman_p_x = 12.0f;
                        track.kalman_p_y = 12.0f;
                        track.kalman_initialized = true;
                    }
                    
                    // Update velocity with EMA smoothing
                    updateVelocity(track, match, dt, settings.velocitySmoothing);
                    
                    track.box = match;
                    track.confidence = match.confidence;
                    
                    // Update aim position filters based on selected filter type
                    ESP::Vector2 measured = match.center();
                    
                    if (settings.filterType == 1) {
                        // EMA filter (lightweight, fast, good balance)
                        track.updateEMA(measured.x, measured.y, settings.emaAlpha);
                    } else if (settings.filterType == 2) {
                        // Kalman filter (more sophisticated, heavier)
                        track.updateKalman(measured.x, measured.y, 
                                         settings.kalmanProcessNoise, 
                                         settings.kalmanMeasurementNoise);
                    }
                    // filterType == 0: No filtering
                    
                    track.age++;
                    track.lost = 0;
                    track.consecutiveMatches++;
                    
                    // Tentative track confirmation (DeepSORT: 3 consecutive matches)
                    if (track.state == TrackState::TENTATIVE && track.consecutiveMatches >= 3) {
                        track.state = TrackState::CONFIRMED;
                    }
                    
                    detMatched[bestDetIdx] = true;
                    trkMatched[t] = true;
                }
            }
        }
    }
    
    // 3. Handle unmatched tracks
    for (int t = numTracks - 1; t >= 0; t--) {
        if (!trkMatched[t]) {
            m_targets[t].lost++;
            m_targets[t].consecutiveMatches = 0;
            
            // Decay velocity (reduce confidence in prediction)
            m_targets[t].velocity = m_targets[t].velocity * 0.8f;
            
            // Remove tentative tracks immediately if lost
            if (m_targets[t].state == TrackState::TENTATIVE && m_targets[t].lost > 1) {
                m_targets.removeAt(t);
                continue;
            }
            
            // Remove confirmed tracks after maxLostFrames
            if (m_targets[t].lost > settings.maxLostFrames) {
                m_targets.removeAt(t);
            }
        }
    }
    
    // 4. Create new tracks for unmatched detections (start as TENTATIVE)
    for (int d = 0; d < safeCount; d++) {
        if (!detMatched[d]) {
            if (detections[d].classId != Config::ENEMY_CLASS_ID) {
                continue;
            }
            if (m_targets.full()) break;
            
            TrackedTarget newTrack;
            newTrack.id = m_nextId++;
            newTrack.box = detections[d];
            newTrack.confidence = detections[d].confidence;
            newTrack.velocity = ESP::Vector2::Zero();
            newTrack.age = 0;
            newTrack.lost = 0;
            newTrack.consecutiveMatches = 1;
            newTrack.state = TrackState::TENTATIVE;
            
            m_targets.push(newTrack);
        }
    }
}

void TargetTracker::predictTargets(float dt) {
    for (int i = 0; i < m_targets.size(); i++) {
        TrackedTarget& t = m_targets[i];
        if (t.lost > 0) {
            // Apply velocity prediction for lost targets
            ESP::Vector2 pred = t.predictPosition(dt);
            // Update box center (keep size)
            float halfW = t.box.width * 0.5f;
            float halfH = t.box.height * 0.5f;
            t.box.x = pred.x - halfW;
            t.box.y = pred.y - halfH;
        }
    }
}

bool TargetTracker::getBestTargetCopy(const UnifiedSettings& settings, float screenWidth, float screenHeight, TrackedTarget& outTarget) {
    if (m_switchCooldownFrames > 0) {
        m_switchCooldownFrames--;
    }

    if (m_targets.empty()) {
        if (m_lockedTargetId >= 0) {
            m_lockedMissFrames++;
            if (m_lockedMissFrames > settings.maxLockMissFrames) {
                m_lockedTargetId = -1;
                m_lockFrameCount = 0;
                m_lockedMissFrames = 0;
                m_switchCooldownFrames = 0;
            }
        }
        return false;
    }
    
    ESP::Vector2 crosshair(screenWidth * 0.5f, screenHeight * 0.5f);
    const float aimFovRadius = (settings.aimFovRadius > 0.0f) ? settings.aimFovRadius : settings.fovRadius;
    const int maxFreshLostFrames = std::max(1, std::min(settings.maxLockMissFrames, 3));
    const TrackedTarget* best = nullptr;
    const TrackedTarget* lockedTarget = nullptr;
    float bestScore = 1e9f;
    float lockedScore = 1e9f;
    
    // Find locked target if exists
    if (m_lockedTargetId >= 0) {
        for (int i = 0; i < m_targets.size(); i++) {
            TrackedTarget& t = m_targets[i];
            if (t.id == m_lockedTargetId && t.state == TrackState::CONFIRMED && t.box.classId == Config::ENEMY_CLASS_ID) {
                if (t.lost > maxFreshLostFrames) {
                    break;
                }
                ESP::Vector2 aimPos = t.getAimPoint(settings.headPriority, settings.headOffset);
                float distToCrosshair = ESP::Vector2::Distance(aimPos, crosshair);
                t.distanceToCrosshair = distToCrosshair;
                
                if (distToCrosshair <= aimFovRadius) {
                    lockedTarget = &t;
                    m_lockedMissFrames = 0;
                    
                    // Calculate score based on priority
                    if (settings.targetPriority == 0) {
                        lockedScore = distToCrosshair;
                    } else if (settings.targetPriority == 1) {
                        lockedScore = -t.box.height;
                    } else if (settings.targetPriority == 2) {
                        lockedScore = -t.confidence;
                    }
                    break;
                }
            }
        }
    }
    
    // Find best candidate from all confirmed tracks
    for (int i = 0; i < m_targets.size(); i++) {
        TrackedTarget& t = m_targets[i];
        
        // Only target CONFIRMED tracks (not tentative)
        if (t.state != TrackState::CONFIRMED) continue;
        if (t.box.classId != Config::ENEMY_CLASS_ID) continue;
        if (t.lost > maxFreshLostFrames) continue;
        
        ESP::Vector2 aimPos = t.getAimPoint(settings.headPriority, settings.headOffset);
        float distToCrosshair = ESP::Vector2::Distance(aimPos, crosshair);
        t.distanceToCrosshair = distToCrosshair;
        
        if (distToCrosshair > aimFovRadius) continue;
        
        float score = distToCrosshair;
        if (settings.targetPriority == 1) {
            score = -t.box.height;
        } else if (settings.targetPriority == 2) {
            score = -t.confidence;
        }
        
        if (score < bestScore) {
            bestScore = score;
            best = &t;
        }
    }
    
    // Target lock-in with hysteresis (DeepSORT approach)
    // Make it harder to switch away from locked target
    if (lockedTarget != nullptr && best != nullptr) {
        // Apply switch threshold: new target must be significantly better
        float switchThreshold = settings.targetSwitchThreshold; // Default 1.3 (30% better)
        const bool cooldownReady = (m_switchCooldownFrames <= 0);
        const bool lockMatured = (m_lockFrameCount >= 4);
        bool canSwitch = cooldownReady && lockMatured;
        
        if (settings.targetPriority == 0) {
            // For distance: new target must be closer by threshold factor
            if (canSwitch && bestScore < lockedScore / switchThreshold) {
                m_lockedTargetId = best->id;
                m_lockFrameCount = 0;
                m_switchCooldownFrames = static_cast<int16_t>(settings.targetSwitchDelayFrames);
                outTarget = *best;
                return true;
            }
        } else {
            // For height/confidence (negative scores): new must be larger by factor
            if (canSwitch && bestScore < lockedScore * switchThreshold) {
                m_lockedTargetId = best->id;
                m_lockFrameCount = 0;
                m_switchCooldownFrames = static_cast<int16_t>(settings.targetSwitchDelayFrames);
                outTarget = *best;
                return true;
            }
        }
        
        // Stay locked
        m_lockFrameCount++;
        outTarget = *lockedTarget;
        return true;
    }
    
    // No locked target or locked target lost
    if (best != nullptr) {
        m_lockedTargetId = best->id;
        m_lockFrameCount = 0;
        m_lockedMissFrames = 0;
        m_switchCooldownFrames = static_cast<int16_t>(settings.targetSwitchDelayFrames);
        outTarget = *best;
        return true;
    }
    
    if (m_lockedTargetId >= 0) {
        m_lockedMissFrames++;
        if (m_lockedMissFrames > settings.maxLockMissFrames) {
            m_lockedTargetId = -1;
            m_lockFrameCount = 0;
            m_lockedMissFrames = 0;
            m_switchCooldownFrames = 0;
        }
    }
    return false;
}
