#ifndef AIMBOT_CONTROLLER_H
#define AIMBOT_CONTROLLER_H

#include "../settings.h"
#include "../utils/aimbot_types.h"
#include "../input/touch_helper.h"
#include "target_tracker.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include "../detector/bounding_box.h"

class AimbotController {
public:
    AimbotController(TouchHelper* touch, int screenWidth, int screenHeight);
    ~AimbotController();

    void updateTargets(const ESP::BoundingBox* detections, int count);
    void aimAt(const TrackedTarget& target);
    void stopAiming();
    void setScreenSize(int width, int height);
    bool isAiming() const;
    void start();
    void stop();

private:
    TouchHelper* m_touch;
    int m_screenWidth;
    int m_screenHeight;
    
    TargetTracker m_tracker;
    
    float m_crosshairX;
    float m_crosshairY;
    
    bool m_isAiming;
    float m_touchX;
    float m_touchY;
    float m_prevErrX;
    float m_prevErrY;
    float m_prevMoveX;
    float m_prevMoveY;
    float m_lastDtSeconds;
    float m_recoilCompY;
    int m_warmupFramesRemaining;
    
    std::atomic<bool> m_running;
    std::thread m_aimThread;
    std::mutex m_trackerMutex;
    
    void aimLoop();
    
    void calcSmoothAim(float dx, float dy, float distance, 
                      const UnifiedSettings& settings,
                      float& outX, float& outY);
                      
    void calcSnapAim(float dx, float dy, float speed,
                    float& outX, float& outY);
                    
    void calcMagneticAim(float dx, float dy, float distance,
                        const UnifiedSettings& settings,
                        float& outX, float& outY);

    void sanitizeMovement(float dx, float dy, float distance,
                          const UnifiedSettings& settings,
                          float modeStrength,
                          float& inOutX, float& inOutY);
                        
    void applyMovement(float moveX, float moveY, const UnifiedSettings& settings);
};

#endif