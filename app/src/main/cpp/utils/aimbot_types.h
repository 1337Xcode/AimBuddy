/**
 * aimbot_types.h - Unified settings and types for complete ESP+Aimbot system
 * All settings saved to single binary file for persistence
 */

#ifndef AIMBOT_TYPES_H
#define AIMBOT_TYPES_H

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include "../settings.h"
#include "../detector/bounding_box.h"
#include "../utils/vector2.h"

struct UnifiedSettings {
    uint32_t magic = 0xE5BA1005;  // Magic number for validation
    
    bool aimbotEnabled = false;
    bool espEnabled = true;
    
    uint32_t aimbotFps = 60;
    int32_t aimMode = 0;
    float aimSpeed = 0.45f;
    float smoothness = 0.78f;
    
    int32_t targetPriority = 0;
    bool headPriority = true;
    float headOffset = 0.2f;
    float fovRadius = Config::DEFAULT_DETECTION_FOV_RADIUS;
    float aimFovRadius = Config::DEFAULT_AIM_FOV_RADIUS;
    float maxAimDistance = 400.0f;
    
    float touchX = 540.0f;
    float touchY = 150.0f;
    float touchRadius = 250.0f;
    float aimDelay = 3.5f;
    
    int32_t maxLostFrames = 30;
    float iouThreshold = 0.3f;
    float velocitySmoothing = 0.7f;
    float targetSwitchThreshold = 1.3f;
    int32_t targetSwitchDelayFrames = 6;
    int32_t maxLockMissFrames = 5;
    
    // Aim Stabilization System
    int32_t filterType = 1;  // 0=None, 1=EMA, 2=Kalman
    float emaAlpha = 0.4f;  // EMA smoothing factor (lower = smoother)
    bool enableConvergenceDamping = true;  // Reduce overshoot near target
    float convergenceRadius = 30.0f;  // Distance for full dampening
    float pdDerivativeGain = 0.045f;  // Derivative brake (damping) for smooth mode
    float velocityLeadFactor = 0.14f; // Scales tracked velocity when leading targets
    float velocityLeadClamp = 10.0f;  // Max lead in pixels per axis
    bool recoilCompensationEnabled = false;
    float recoilCompensationStrength = 0.18f; // Scales adaptive recoil correction
    float recoilCompensationMax = 12.0f;      // Max additional Y correction in px/frame
    float recoilCompensationDecay = 0.84f;    // Integrator decay (lower = faster response)
    
    bool enableKalmanFilter = false;  // Deprecated: use filterType instead
    float kalmanProcessNoise = 1.0f;
    float kalmanMeasurementNoise = 4.0f;
    
    float boxColorR = 1.0f;
    float boxColorG = 0.0f;
    float boxColorB = 0.0f;
    int32_t boxThickness = 2;
    float confidenceThreshold = 0.5f;
    bool showFPS = false;
    bool showDetectionCount = false;
    bool showLabels = true;
    bool drawLine = true;
    bool drawDot = true;
    bool enableSmoothing = true;
    float smoothingFactor = 0.30f;
    
    bool showTouchZone = true;
    float touchZoneAlpha = 0.3f;
    
    int32_t screenWidth = 1080;
    int32_t screenHeight = 2400;
    
    // Save all settings to file
    bool save(const char* path = "/data/local/tmp/settings.bin") const {
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        
        bool success = (fwrite(this, sizeof(UnifiedSettings), 1, f) == 1);
        fclose(f);
        return success;
    }
    
    // Load all settings from file
    bool load(const char* path = "/data/local/tmp/settings.bin") {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        
        UnifiedSettings temp;
        if (fread(&temp, sizeof(UnifiedSettings), 1, f) == 1) {
            // Verify magic number
            if (temp.magic == 0xE5BA1005) {
                *this = temp;
                fclose(f);
                return true;
            }
        }
        fclose(f);
        return false;
    }
    
    // Reset to defaults
    void reset() {
        UnifiedSettings defaults;
        defaults.screenWidth = this->screenWidth;  // Preserve runtime values
        defaults.screenHeight = this->screenHeight;
        *this = defaults;
    }
    
    // Validate and clamp all values
    void validate() {
        aimbotFps = (aimbotFps < 30) ? 30 : (aimbotFps > 120) ? 120 : aimbotFps;
        aimMode = (aimMode < 0) ? 0 : (aimMode > 2) ? 2 : aimMode;
        filterType = (filterType < 0) ? 0 : (filterType > 2) ? 2 : filterType;
        aimSpeed = (aimSpeed < 0.1f) ? 0.1f : (aimSpeed > 1.0f) ? 1.0f : aimSpeed;
        smoothness = (smoothness < 0.0f) ? 0.0f : (smoothness > 1.0f) ? 1.0f : smoothness;
        fovRadius = (fovRadius < 50.0f) ? 50.0f : (fovRadius > 600.0f) ? 600.0f : fovRadius;
        aimFovRadius = (aimFovRadius < 50.0f) ? 50.0f : (aimFovRadius > 600.0f) ? 600.0f : aimFovRadius;
        if (aimFovRadius > fovRadius) {
            aimFovRadius = fovRadius;
        }
        confidenceThreshold = (confidenceThreshold < 0.1f) ? 0.1f : (confidenceThreshold > 0.95f) ? 0.95f : confidenceThreshold;
        maxAimDistance = (maxAimDistance < 100.0f) ? 100.0f : (maxAimDistance > 1000.0f) ? 1000.0f : maxAimDistance;
        touchRadius = (touchRadius < 50.0f) ? 50.0f : (touchRadius > 500.0f) ? 500.0f : touchRadius;
        aimDelay = (aimDelay < 0.0f) ? 0.0f : (aimDelay > 50.0f) ? 50.0f : aimDelay;
        boxThickness = (boxThickness < 1) ? 1 : (boxThickness > 10) ? 10 : boxThickness;
        boxColorR = (boxColorR < 0.0f) ? 0.0f : (boxColorR > 1.0f) ? 1.0f : boxColorR;
        boxColorG = (boxColorG < 0.0f) ? 0.0f : (boxColorG > 1.0f) ? 1.0f : boxColorG;
        boxColorB = (boxColorB < 0.0f) ? 0.0f : (boxColorB > 1.0f) ? 1.0f : boxColorB;
        smoothingFactor = (smoothingFactor < 0.1f) ? 0.1f : (smoothingFactor > 1.0f) ? 1.0f : smoothingFactor;
        touchZoneAlpha = (touchZoneAlpha < 0.1f) ? 0.1f : (touchZoneAlpha > 1.0f) ? 1.0f : touchZoneAlpha;
        headOffset = (headOffset < 0.0f) ? 0.0f : (headOffset > 1.0f) ? 1.0f : headOffset;
        targetSwitchDelayFrames = (targetSwitchDelayFrames < 0) ? 0 : (targetSwitchDelayFrames > 30) ? 30 : targetSwitchDelayFrames;
        maxLockMissFrames = (maxLockMissFrames < 1) ? 1 : (maxLockMissFrames > 30) ? 30 : maxLockMissFrames;
        pdDerivativeGain = (pdDerivativeGain < 0.0f) ? 0.0f : (pdDerivativeGain > 0.35f) ? 0.35f : pdDerivativeGain;
        velocityLeadFactor = (velocityLeadFactor < 0.0f) ? 0.0f : (velocityLeadFactor > 0.8f) ? 0.8f : velocityLeadFactor;
        velocityLeadClamp = (velocityLeadClamp < 1.0f) ? 1.0f : (velocityLeadClamp > 40.0f) ? 40.0f : velocityLeadClamp;
        recoilCompensationStrength = (recoilCompensationStrength < 0.0f) ? 0.0f : (recoilCompensationStrength > 1.5f) ? 1.5f : recoilCompensationStrength;
        recoilCompensationMax = (recoilCompensationMax < 2.0f) ? 2.0f : (recoilCompensationMax > 60.0f) ? 60.0f : recoilCompensationMax;
        recoilCompensationDecay = (recoilCompensationDecay < 0.50f) ? 0.50f : (recoilCompensationDecay > 0.98f) ? 0.98f : recoilCompensationDecay;
        emaAlpha = (emaAlpha < 0.08f) ? 0.08f : (emaAlpha > 0.90f) ? 0.90f : emaAlpha;
        kalmanProcessNoise = (kalmanProcessNoise < 0.01f) ? 0.01f : (kalmanProcessNoise > 20.0f) ? 20.0f : kalmanProcessNoise;
        kalmanMeasurementNoise = (kalmanMeasurementNoise < 0.5f) ? 0.5f : (kalmanMeasurementNoise > 40.0f) ? 40.0f : kalmanMeasurementNoise;
        
        // Clamp touch position to screen bounds
        if (screenWidth > 0 && screenHeight > 0) {
            touchX = (touchX < 0.0f) ? 0.0f : (touchX > screenWidth) ? screenWidth : touchX;
            touchY = (touchY < 0.0f) ? 0.0f : (touchY > screenHeight) ? screenHeight : touchY;
        }
    }
    
    void setDefaultTouchPosition(int scrnWidth, int scrnHeight) {
        screenWidth = scrnWidth;
        screenHeight = scrnHeight;
        touchX = scrnWidth * 0.5f;
        touchY = 150.0f;
        touchRadius = 250.0f;
    }
};

static_assert(sizeof(UnifiedSettings) < 512, "Settings struct too large");

namespace AimbotMath {
    template<typename T>
    inline T clamp(T value, T min, T max) {
        return std::max(min, std::min(max, value));
    }
    
    inline float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
    
    inline float smoothstep(float edge0, float edge1, float x) {
        float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

enum class TrackState : uint8_t {
    TENTATIVE = 0,
    CONFIRMED = 1,
    DELETED = 2
};

struct TrackedTarget {
    int32_t id = -1;
    int16_t age = 0;
    int16_t lost = 0;
    int16_t consecutiveMatches = 0;
    TrackState state = TrackState::TENTATIVE;
    
    ESP::BoundingBox box;
    float confidence = 0.0f;
    float distanceToCrosshair = 0.0f;
    
    ESP::Vector2 velocity;
    
    // EMA filter state (lightweight alternative to Kalman)
    float ema_x = 0.0f;
    float ema_y = 0.0f;
    bool ema_initialized = false;
    
    // Lightweight Kalman filter state (1D filter per axis)
    float kalman_x = 0.0f;
    float kalman_y = 0.0f;
    float kalman_p_x = 100.0f;  // Initial uncertainty
    float kalman_p_y = 100.0f;
    bool kalman_initialized = false;
    
    // EMA (Exponential Moving Average) filter update
    inline void updateEMA(float measured_x, float measured_y, float alpha) {
        if (!ema_initialized) {
            ema_x = measured_x;
            ema_y = measured_y;
            ema_initialized = true;
            return;
        }
        
        // EMA: smoothed = smoothed * (1 - alpha) + measured * alpha
        // Lower alpha = more smoothing, higher lag
        ema_x = ema_x * (1.0f - alpha) + measured_x * alpha;
        ema_y = ema_y * (1.0f - alpha) + measured_y * alpha;
    }
    
    // Simple 1D Kalman filter update (lightweight)
    inline void updateKalman(float measured_x, float measured_y, float Q, float R) {
        const float processNoise = std::max(0.01f, std::min(Q, 20.0f));
        const float measurementNoise = std::max(0.5f, std::min(R, 40.0f));

        if (!kalman_initialized) {
            kalman_x = measured_x;
            kalman_y = measured_y;
            kalman_p_x = 10.0f;
            kalman_p_y = 10.0f;
            kalman_initialized = true;
            return;
        }

        if (!std::isfinite(kalman_x) || !std::isfinite(kalman_y) || !std::isfinite(kalman_p_x) || !std::isfinite(kalman_p_y)) {
            kalman_x = measured_x;
            kalman_y = measured_y;
            kalman_p_x = 10.0f;
            kalman_p_y = 10.0f;
            kalman_initialized = true;
            return;
        }

        const float jumpX = measured_x - kalman_x;
        const float jumpY = measured_y - kalman_y;
        if (const float jumpDistSq = jumpX * jumpX + jumpY * jumpY;
            jumpDistSq > (180.0f * 180.0f)) {
            kalman_x = measured_x;
            kalman_y = measured_y;
            kalman_p_x = 12.0f;
            kalman_p_y = 12.0f;
            return;
        }
        
        // Predict step (position only, no velocity model to keep it simple)
        kalman_p_x += processNoise;
        kalman_p_y += processNoise;
        
        // Update step (Kalman gain + correction)
        float K_x = kalman_p_x / (kalman_p_x + measurementNoise);
        float K_y = kalman_p_y / (kalman_p_y + measurementNoise);
        
        kalman_x += K_x * (measured_x - kalman_x);
        kalman_y += K_y * (measured_y - kalman_y);
        
        kalman_p_x *= (1.0f - K_x);
        kalman_p_y *= (1.0f - K_y);
    }
    
    inline ESP::Vector2 getAimPoint(bool headPriority, float headOffset) const {
        const float centerX = box.centerX();
        const float centerY = box.centerY();
        if (headPriority) {
            const float clampedOffset = std::max(0.0f, std::min(headOffset, 1.0f));
            const float headY = box.y + box.height * clampedOffset;
            return ESP::Vector2(centerX, headY);
        }
        return ESP::Vector2(centerX, centerY);
    }
    
    inline ESP::Vector2 getFilteredAimPoint(int filterType, bool headPriority, float headOffset, [[maybe_unused]] float emaAlpha) const {
        // Get raw aim point first
        ESP::Vector2 raw = getAimPoint(headPriority, headOffset);
        
        if (filterType == 0) {
            // No filter
            return raw;
        } else if (filterType == 1) {
            // EMA filter (lightweight and responsive)
            if (!ema_initialized) {
                // First frame, no filtering yet
                return raw;
            }
            
            if (headPriority) {
                const float clampedOffset = std::max(0.0f, std::min(headOffset, 1.0f));
                const float rawHeadY = box.y + box.height * clampedOffset;
                const float centerY = box.centerY();
                const float yOffset = rawHeadY - centerY;
                const float maxDriftX = std::max(6.0f, box.width * 0.35f);
                const float maxDriftY = std::max(8.0f, box.height * 0.35f);
                const float filteredX = std::max(raw.x - maxDriftX, std::min(raw.x + maxDriftX, ema_x));
                const float filteredBaseY = std::max((raw.y - yOffset) - maxDriftY, std::min((raw.y - yOffset) + maxDriftY, ema_y));
                return ESP::Vector2(filteredX, filteredBaseY + yOffset);
            }
            const float maxDriftX = std::max(6.0f, box.width * 0.35f);
            const float maxDriftY = std::max(8.0f, box.height * 0.35f);
            const float filteredX = std::max(raw.x - maxDriftX, std::min(raw.x + maxDriftX, ema_x));
            const float filteredY = std::max(raw.y - maxDriftY, std::min(raw.y + maxDriftY, ema_y));
            return ESP::Vector2(filteredX, filteredY);
        } else if (filterType == 2) {
            // Kalman filter (more sophisticated)
            if (!kalman_initialized) {
                return raw;
            }
            
            if (headPriority) {
                const float clampedOffset = std::max(0.0f, std::min(headOffset, 1.0f));
                const float rawHeadY = box.y + box.height * clampedOffset;
                const float centerY = box.centerY();
                const float yOffset = rawHeadY - centerY;
                const float maxDriftX = std::max(8.0f, box.width * 0.45f);
                const float maxDriftY = std::max(10.0f, box.height * 0.45f);
                const float filteredX = std::max(raw.x - maxDriftX, std::min(raw.x + maxDriftX, kalman_x));
                const float filteredBaseY = std::max((raw.y - yOffset) - maxDriftY, std::min((raw.y - yOffset) + maxDriftY, kalman_y));
                return ESP::Vector2(filteredX, filteredBaseY + yOffset);
            }
            const float maxDriftX = std::max(8.0f, box.width * 0.45f);
            const float maxDriftY = std::max(10.0f, box.height * 0.45f);
            const float filteredX = std::max(raw.x - maxDriftX, std::min(raw.x + maxDriftX, kalman_x));
            const float filteredY = std::max(raw.y - maxDriftY, std::min(raw.y + maxDriftY, kalman_y));
            return ESP::Vector2(filteredX, filteredY);
        }
        
        return raw;
    }
    
    inline ESP::Vector2 predictPosition(float dt = 1.0f) const {
        return ESP::Vector2(box.x, box.y) + velocity * dt;
    }
    
    inline float iou(const TrackedTarget& other) const {
        float x1 = std::max(box.x - box.width * 0.5f, other.box.x - other.box.width * 0.5f);
        float y1 = std::max(box.y - box.height * 0.5f, other.box.y - other.box.height * 0.5f);
        float x2 = std::min(box.x + box.width * 0.5f, other.box.x + other.box.width * 0.5f);
        float y2 = std::min(box.y + box.height * 0.5f, other.box.y + other.box.height * 0.5f);
        
        float interW = std::max(0.0f, x2 - x1);
        float interH = std::max(0.0f, y2 - y1);
        float intersection = interW * interH;
        
        float area1 = box.width * box.height;
        float area2 = other.box.width * other.box.height;
        float unionArea = area1 + area2 - intersection;
        
        return (unionArea > 0.0f) ? (intersection / unionArea) : 0.0f;
    }
};

template<typename T, int Capacity>
class FixedArray {
public:
    FixedArray() : m_size(0) {}
    
    inline void clear() { m_size = 0; }
    inline bool push(const T& item) {
        if (m_size >= Capacity) return false;
        m_data[m_size++] = item;
        return true;
    }
    
    inline T& operator[](int index) { return m_data[index]; }
    inline const T& operator[](int index) const { return m_data[index]; }
    inline int size() const { return m_size; }
    inline int capacity() const { return Capacity; }
    inline bool empty() const { return m_size == 0; }
    inline bool full() const { return m_size >= Capacity; }
    
    inline void removeAt(int index) {
        if (index < 0 || index >= m_size) return;
        if (index < m_size - 1) {
            m_data[index] = m_data[m_size - 1];
        }
        m_size--;
    }
    
private:
    T m_data[Capacity];
    int m_size;
};

static constexpr int MAX_TRACKED_TARGETS = 50;
using TargetArray = FixedArray<TrackedTarget, MAX_TRACKED_TARGETS>;

#endif
