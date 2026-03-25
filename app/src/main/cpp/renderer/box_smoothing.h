#ifndef ESP_BOX_SMOOTHING_H
#define ESP_BOX_SMOOTHING_H

#include "../detector/bounding_box.h"
#include <array>
#include <cmath>

/**
 * @file BoxSmoothing.h
 * @brief Temporal smoothing for ESP boxes to reduce jitter
 * 
 * Implements exponential moving average (EMA) for box positions
 * with ID tracking to maintain smooth transitions between frames.
 */

namespace ESP {

/**
 * @struct SmoothedBox
 * @brief Tracked box with smoothed position
 */
struct SmoothedBox {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float confidence{0.0f};
    int classId{0};
    int age{0};             ///< Frames since last update
    bool active{false};     ///< Is this slot in use?
    
    /**
     * @brief Update with new detection using EMA smoothing
     * @param newBox New detection
     * @param alpha Smoothing factor (0=no update, 1=instant)
     */
    void updateSmooth(const BoundingBox& newBox, float alpha) {
        const float oldCenterX = x + width * 0.5f;
        const float oldCenterY = y + height * 0.5f;
        const float newCenterX = newBox.x + newBox.width * 0.5f;
        const float newCenterY = newBox.y + newBox.height * 0.5f;

        const float jumpX = newCenterX - oldCenterX;
        const float jumpY = newCenterY - oldCenterY;
        const float jumpDist = std::sqrt(jumpX * jumpX + jumpY * jumpY);
        const float jumpRef = std::max(12.0f, std::max(width, height) * 0.45f);
        const float adaptiveBoost = std::min(0.32f, jumpDist / jumpRef * 0.14f);
        const float blend = std::max(alpha, std::min(0.88f, alpha + adaptiveBoost));

        width = width * (1.0f - blend) + newBox.width * blend;
        height = height * (1.0f - blend) + newBox.height * blend;

        const float blendedCenterX = oldCenterX * (1.0f - blend) + newCenterX * blend;
        const float blendedCenterY = oldCenterY * (1.0f - blend) + newCenterY * blend;
        x = blendedCenterX - width * 0.5f;
        y = blendedCenterY - height * 0.5f;

        const float instVx = blendedCenterX - oldCenterX;
        const float instVy = blendedCenterY - oldCenterY;
        vx = vx * 0.72f + instVx * 0.28f;
        vy = vy * 0.72f + instVy * 0.28f;

        confidence = confidence * (1.0f - blend) + newBox.confidence * blend;
        classId = newBox.classId;
        age = 0;
        active = true;
    }
    
    /**
     * @brief Initialize from detection
     */
    void init(const BoundingBox& box) {
        x = box.x;
        y = box.y;
        width = box.width;
        height = box.height;
        vx = 0.0f;
        vy = 0.0f;
        confidence = box.confidence;
        classId = box.classId;
        age = 0;
        active = true;
    }
    
    /**
     * @brief Get as BoundingBox
     */
    BoundingBox toBox() const {
        return BoundingBox(x, y, width, height, confidence, classId);
    }
};

/**
 * @class BoxSmoother
 * @brief Manages smoothed box tracking across frames
 */
class BoxSmoother {
public:
    static constexpr int MAX_TRACKED = 32;
    static constexpr float DEFAULT_ALPHA = 0.4f;      ///< Smoothing factor (lower = smoother)
    static constexpr float IOU_THRESHOLD = 0.20f;     ///< Matching threshold
    static constexpr int MAX_AGE = 2;                 ///< Max frames before dropping stale tracks (was 6, reduced to avoid ghost boxes)
    
    BoxSmoother() = default;
    
    /**
     * @brief Update smoothed boxes with new detections
     * @param detections New detection results
     * @param smoothedOut Output smoothed boxes
     * @param smoothedCount Output count
     * @param alpha Smoothing factor (lower = smoother, higher = more responsive)
     */
    void update(const std::array<BoundingBox, Config::MAX_DETECTIONS>& detections,
                int detectionCount,
                std::array<BoundingBox, Config::MAX_DETECTIONS>& smoothedOut,
                int& smoothedCount,
                float alpha = DEFAULT_ALPHA) {
        
        // Mark all tracked boxes as unmatched initially
        std::array<bool, MAX_TRACKED> matched{};
        std::array<bool, Config::MAX_DETECTIONS> detMatched{};
        
        // Match detections to existing tracked boxes using IoU
        for (int t = 0; t < MAX_TRACKED; ++t) {
            if (!tracked_[t].active) continue;
            
            float bestScore = -1.0f;
            int bestIdx = -1;
            
            BoundingBox trackedBox = tracked_[t].toBox();
            float trackedCenterX = trackedBox.x + trackedBox.width * 0.5f + tracked_[t].vx;
            float trackedCenterY = trackedBox.y + trackedBox.height * 0.5f + tracked_[t].vy;
            
            for (int d = 0; d < detectionCount; ++d) {
                if (detMatched[d]) continue;
                if (detections[d].classId != tracked_[t].classId) continue;

                float detCenterX = detections[d].x + detections[d].width * 0.5f;
                float detCenterY = detections[d].y + detections[d].height * 0.5f;
                float deltaX = detCenterX - trackedCenterX;
                float deltaY = detCenterY - trackedCenterY;
                float centerDistSq = deltaX * deltaX + deltaY * deltaY;

                const float maxCenterJump = std::max(32.0f, std::max(trackedBox.width, trackedBox.height) * 1.35f + 18.0f * static_cast<float>(tracked_[t].age + 1));
                if (centerDistSq > maxCenterJump * maxCenterJump) continue;
                
                float iou = trackedBox.iou(detections[d]);
                const float centerScore = 1.0f - std::min(1.0f, std::sqrt(centerDistSq) / std::max(1.0f, maxCenterJump));
                const bool nearCenter = centerDistSq <= (maxCenterJump * maxCenterJump * 0.25f);
                if (iou < IOU_THRESHOLD && !nearCenter) {
                    continue;
                }

                float score = iou * 0.60f + centerScore * 0.40f;
                if (score > bestScore) {
                    bestScore = score;
                    bestIdx = d;
                }
            }
            
            if (bestIdx >= 0) {
                // Update existing track
                tracked_[t].updateSmooth(detections[bestIdx], alpha);
                matched[t] = true;
                detMatched[bestIdx] = true;
            }
        }
        
        // Age unmatched tracks and decay their confidence quickly
        for (int t = 0; t < MAX_TRACKED; ++t) {
            if (!tracked_[t].active) continue;
            
            if (!matched[t]) {
                tracked_[t].age++;
                // Apply velocity prediction for short coasting
                tracked_[t].x += tracked_[t].vx;
                tracked_[t].y += tracked_[t].vy;
                // Kill velocity immediately so box doesn't drift far
                tracked_[t].vx = 0.0f;
                tracked_[t].vy = 0.0f;
                // Aggressively decay confidence so renderer filters it below threshold
                tracked_[t].confidence *= 0.40f;
                if (tracked_[t].age > MAX_AGE || tracked_[t].confidence < 0.05f) {
                    tracked_[t].active = false; // Drop this ghost
                }
            }
        }
        
        // Create new tracks for unmatched detections
        for (int d = 0; d < detectionCount; ++d) {
            if (detMatched[d]) continue;
            
            // Find empty slot
            for (int t = 0; t < MAX_TRACKED; ++t) {
                if (!tracked_[t].active) {
                    tracked_[t].init(detections[d]);
                    break;
                }
            }
        }
        
        // Output all active tracked boxes
        smoothedCount = 0;
        for (int t = 0; t < MAX_TRACKED && smoothedCount < Config::MAX_DETECTIONS; ++t) {
            if (tracked_[t].active) {
                smoothedOut[smoothedCount++] = tracked_[t].toBox();
            }
        }
    }
    
    /**
     * @brief Clear all tracked boxes
     */
    void clear() {
        for (auto& box : tracked_) {
            box.active = false;
        }
    }
    
private:
    std::array<SmoothedBox, MAX_TRACKED> tracked_;
};

} // namespace ESP

#endif // ESP_BOX_SMOOTHING_H
