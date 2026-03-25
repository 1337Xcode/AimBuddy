#ifndef TARGET_TRACKER_H
#define TARGET_TRACKER_H

#include "../utils/aimbot_types.h"
#include "../settings.h"
#include "../detector/bounding_box.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

class TargetTracker {
public:
    TargetTracker();
    
    void update(const ESP::BoundingBox* detections, int count, const UnifiedSettings& settings);
    const TargetArray& getTargets() const;
    bool getBestTargetCopy(const UnifiedSettings& settings, float screenWidth, float screenHeight, TrackedTarget& outTarget);
    void reset();

private:
    TargetArray m_targets;
    int32_t m_nextId;
    int32_t m_lockedTargetId;
    int16_t m_lockFrameCount;
    int16_t m_lockedMissFrames;
    int16_t m_switchCooldownFrames;
    int64_t m_lastUpdateNs;
    
    void predictTargets(float dt);
    void updateVelocity(TrackedTarget& track, const ESP::BoundingBox& detection, float dt, float smoothing);
};

#endif // TARGET_TRACKER_H
