#include "aimbot_controller.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <unistd.h>

/**
 * Maps tracked target state to bounded touch movement.
 * Applies filtering, lead prediction, and FOV constraints
 * before forwarding touch commands to the input layer.
 */

#define LOG_TAG "AimbotController"
#if defined(NDEBUG)
    #define LOGI(...) ((void)0)
#else
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif

extern UnifiedSettings g_settings;

static constexpr int AIM_SLOT = 9;
static constexpr float MAX_SINGLE_MOVE = 35.0f;
static constexpr float MIN_DISTANCE = 5.0f;
static constexpr float EPSILON = 0.0001f;
static constexpr float AXIS_NO_CROSS_RATIO = 0.85f;

AimbotController::AimbotController(TouchHelper* touch, int screenWidth, int screenHeight)
    : m_touch(touch)
    , m_screenWidth(screenWidth)
    , m_screenHeight(screenHeight)
    , m_crosshairX(screenWidth * 0.5f)
    , m_crosshairY(screenHeight * 0.5f)
    , m_isAiming(false)
    , m_touchX(0.0f)
    , m_touchY(0.0f)
    , m_prevErrX(0.0f)
    , m_prevErrY(0.0f)
    , m_prevMoveX(0.0f)
    , m_prevMoveY(0.0f)
    , m_lastDtSeconds(1.0f / 60.0f)
    , m_recoilCompY(0.0f)
    , m_warmupFramesRemaining(0)
    , m_running(false)
{
}

AimbotController::~AimbotController() {
    stop();
}

void AimbotController::start() {
    if (m_running) return;
    
    m_running = true;
    m_aimThread = std::thread(&AimbotController::aimLoop, this);
    LOGI("Aimbot loop started");
}

void AimbotController::stop() {
    m_running = false;
    if (m_aimThread.joinable()) {
        m_aimThread.join();
    }
    stopAiming();
}

void AimbotController::updateTargets(const ESP::BoundingBox* detections, int count) {
    if (!m_running) return;
    
    // Zero-detection fast-release: immediately stop touch when nothing is detected
    if (count == 0 && m_isAiming) {
        stopAiming();
    }
    
    std::lock_guard<std::mutex> lock(m_trackerMutex);
    UnifiedSettings settingsSnapshot = g_settings;
    settingsSnapshot.validate();
    m_tracker.update(detections, count, settingsSnapshot);
}

void AimbotController::aimLoop() {
    while (m_running) {
        UnifiedSettings settingsSnapshot = g_settings;
        settingsSnapshot.validate();

        if (!settingsSnapshot.aimbotEnabled) {
            if (m_isAiming) stopAiming();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        TrackedTarget bestTarget;
        bool hasTarget = false;
        {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            hasTarget = m_tracker.getBestTargetCopy(settingsSnapshot, m_screenWidth, m_screenHeight, bestTarget);
        }
        
        if (hasTarget) {
            aimAt(bestTarget);
        } else {
            if (m_isAiming) stopAiming();
        }
        
        // Loop rate (separate from inference rate)
        // e.g., 60 FPS aim loop
        int sleepMs = 1000 / settingsSnapshot.aimbotFps;
        if (sleepMs < 1) sleepMs = 1;
        m_lastDtSeconds = static_cast<float>(sleepMs) / 1000.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

void AimbotController::aimAt(const TrackedTarget& target) {
    UnifiedSettings settings = g_settings;
    settings.validate();
    
    if (!m_touch) {
        stopAiming();
        return;
    }
    
    // Use selected filter (fast-path for raw mode to reduce per-frame overhead).
    const int filterType = settings.filterType;
    ESP::Vector2 aimPoint = (filterType == 0)
        ? target.getAimPoint(settings.headPriority, settings.headOffset)
        : target.getFilteredAimPoint(filterType, settings.headPriority, settings.headOffset, settings.emaAlpha);

    // Predictive lead using tracked velocity (bounded to avoid over-throw)
    const float leadClamp = std::max(1.0f, settings.velocityLeadClamp);
    const float leadFactor = std::max(0.0f, settings.velocityLeadFactor);

    const float rawDx = aimPoint.x - m_crosshairX;
    const float rawDy = aimPoint.y - m_crosshairY;
    const float rawDistance = std::sqrt(rawDx * rawDx + rawDy * rawDy);
    const float leadDistanceScale = AimbotMath::clamp((rawDistance - 10.0f) / std::max(80.0f, settings.fovRadius * 0.7f), 0.0f, 1.0f);
    const float leadConfidenceScale = AimbotMath::clamp((target.confidence - 0.40f) / 0.60f, 0.0f, 1.0f);
    float leadScale = leadDistanceScale * leadConfidenceScale;

    // Proportional speed gate: scale lead UP with target speed (faster = more lead)
    const float targetSpeed = std::sqrt(target.velocity.x * target.velocity.x + target.velocity.y * target.velocity.y);
    const float motionSpeedGate = AimbotMath::clamp(targetSpeed / 30.0f, 0.0f, 1.0f);
    leadScale *= motionSpeedGate;

    if (!m_isAiming) {
        leadScale = 0.0f;
    }

    aimPoint.x += AimbotMath::clamp(target.velocity.x * leadFactor * leadScale, -leadClamp, leadClamp);
    aimPoint.y += AimbotMath::clamp(target.velocity.y * leadFactor * leadScale, -leadClamp, leadClamp);
    
    const float dx = aimPoint.x - m_crosshairX;
    const float dy = aimPoint.y - m_crosshairY;
    const float distanceSq = dx * dx + dy * dy;
    
    // FOV hysteresis: easier to enter than exit (prevents flickering at edge)
    const float exitFovMultiplier = 1.2f; // 20% larger to exit
    const float fovThreshold = m_isAiming ? 
        (settings.fovRadius * exitFovMultiplier) : settings.fovRadius;
    
    if (distanceSq > (fovThreshold * fovThreshold)) {
        stopAiming();
        return;
    }
    
    // MIN_DISTANCE hysteresis
    const float exitMinDistance = MIN_DISTANCE * 1.5f; // Larger to hold a stable deadzone near lock
    const float minDistThreshold = m_isAiming ? exitMinDistance : MIN_DISTANCE;
    
    if (distanceSq < (minDistThreshold * minDistThreshold)) {
        // On-target: only apply a very gentle nudge when already locked and
        // the crosshair has drifted noticeably. Tight PD correction here is the
        // primary cause of oscillation, so we keep the gain very low.
        const float closeDistance = std::sqrt(distanceSq);
        if (m_isAiming && closeDistance > 1.5f) {
            float microX = AimbotMath::clamp(dx * 0.06f, -1.2f, 1.2f);
            float microY = AimbotMath::clamp(dy * 0.06f, -1.2f, 1.2f);
            applyMovement(microX, microY, settings);
        }
        return;
    }

    if (!m_isAiming) {
        m_prevErrX = dx;
        m_prevErrY = dy;
    }
    
    // MAX_AIM_DISTANCE hysteresis
    const float exitMaxDistance = settings.maxAimDistance * 1.15f;
    const float maxDistThreshold = m_isAiming ?
        exitMaxDistance : settings.maxAimDistance;
    
    if (distanceSq > (maxDistThreshold * maxDistThreshold)) {
        stopAiming();
        return;
    }

    const float distance = std::sqrt(distanceSq);
    
    float moveX = 0.0f;
    float moveY = 0.0f;
    float modeStrength = 1.0f;
    
    switch (settings.aimMode) {
        case 0:
            calcSmoothAim(dx, dy, distance, settings, moveX, moveY);
            modeStrength = 1.0f;
            break;
        case 1:
            calcSnapAim(dx, dy, settings.aimSpeed, moveX, moveY);
            modeStrength = 0.82f;
            break;
        case 2:
            calcMagneticAim(dx, dy, distance, settings, moveX, moveY);
            modeStrength = 0.74f;
            break;
        default:
            calcSmoothAim(dx, dy, distance, settings, moveX, moveY);
            modeStrength = 1.0f;
            break;
    }

    sanitizeMovement(dx, dy, distance, settings, modeStrength, moveX, moveY);
    
    applyMovement(moveX, moveY, settings);
}

void AimbotController::calcSmoothAim(float dx, float dy, float distance, 
                                   const UnifiedSettings& settings,
                                   float& outX, float& outY) {
    const float speed = settings.aimSpeed;
    const float smooth = settings.smoothness;

    // Less-sensitive response curve: small slider changes should not collapse aim strength.
    const float smoothT = AimbotMath::clamp(1.0f - smooth, 0.0f, 1.0f);
    const float response = smoothT * smoothT;
    float factor = AimbotMath::lerp(speed * 0.12f, speed, response);
    factor = AimbotMath::clamp(factor, 0.02f, 1.0f);
    
    // Distance-based scaling for far targets
    if (distance > 10.0f) {
        float t = AimbotMath::clamp(distance / settings.fovRadius, 0.0f, 1.0f);
        factor *= AimbotMath::smoothstep(0.0f, 1.0f, t);
    }
    
    // Convergence dampening: reduce speed near target to prevent overshoot
    if (settings.enableConvergenceDamping && distance < settings.convergenceRadius) {
        float dampFactor = distance / settings.convergenceRadius;
        // Square for smoother approach
        dampFactor = dampFactor * dampFactor;
        // Minimum 20% speed to still reach target
        dampFactor = std::max(0.2f, dampFactor);
        factor *= dampFactor;
    }
    
    // Proportional term + bounded frame-delta brake to reduce overshoot/oscillation
    float derivativeX = dx - m_prevErrX;
    float derivativeY = dy - m_prevErrY;
    // Clamp derivative to a tighter range to avoid amplifying single-frame jitter.
    // derivativeClamp was distance*0.25+4, which at close range is very small,
    // letting even 1px detector noise produce a large correction signal.
    const float derivativeClamp = AimbotMath::clamp(distance * 0.18f + 5.0f, 5.0f, 20.0f);
    derivativeX = AimbotMath::clamp(derivativeX, -derivativeClamp, derivativeClamp);
    derivativeY = AimbotMath::clamp(derivativeY, -derivativeClamp, derivativeClamp);
    float dGain = settings.pdDerivativeGain * 0.6f;  // Reduce derivative authority
    if (distance < settings.convergenceRadius) {
        const float nearT = AimbotMath::clamp(1.0f - (distance / std::max(settings.convergenceRadius, 1.0f)), 0.0f, 1.0f);
        dGain *= AimbotMath::lerp(1.0f, 1.4f, nearT);  // Reduced from 1.8x to 1.4x
    }

    outX = dx * factor - derivativeX * dGain;
    outY = dy * factor - derivativeY * dGain;

    // Never cross over target in one frame (prevents throw-away oscillation)
    const float axisCapX = std::max(1.0f, std::abs(dx) * AXIS_NO_CROSS_RATIO);
    const float axisCapY = std::max(1.0f, std::abs(dy) * AXIS_NO_CROSS_RATIO);
    outX = AimbotMath::clamp(outX, -axisCapX, axisCapX);
    outY = AimbotMath::clamp(outY, -axisCapY, axisCapY);

    // Close-range safety: never move opposite target direction when near lock
    const float nearLockRadius = std::max(12.0f, settings.convergenceRadius * 1.4f);
    if (distance < nearLockRadius) {
        if (outX * dx < 0.0f) outX = 0.0f;
        if (outY * dy < 0.0f) outY = 0.0f;
    }

    m_prevErrX = dx;
    m_prevErrY = dy;
    
    // Clamp maximum single move to prevent jumps
    const float maxMove = MAX_SINGLE_MOVE * speed;
    const float moveDist = std::sqrt(outX * outX + outY * outY);
    
    if (moveDist > maxMove && moveDist > EPSILON) {
        const float scale = maxMove / moveDist;
        outX *= scale;
        outY *= scale;
    }
}

void AimbotController::calcSnapAim(float dx, float dy, float speed,
                                 float& outX, float& outY) {
    // Snap mode: fast initial acquisition that never overshoots in a single frame.
    // Gain is capped to 0.82 max so the move is always < the remaining distance.
    // The key invariant: |outX| <= |dx| * 0.82  (never cross the target)
    const float snapGain = AimbotMath::clamp(0.30f + speed * 0.52f, 0.20f, 0.82f);
    outX = dx * snapGain;
    outY = dy * snapGain;

    // Axis-cross hard clamp (redundant safety on top of sanitizeMovement)
    if (outX * dx < 0.0f) outX = 0.0f;
    if (outY * dy < 0.0f) outY = 0.0f;
}

void AimbotController::calcMagneticAim(float dx, float dy, float distance,
                                     const UnifiedSettings& settings,
                                     float& outX, float& outY) {
    // Magnetic mode: smooth velocity-proportional pull.
    // Near-lock: very gentle pull (prevents oscillation at deadzone edge).
    // Far: up to 65% of the distance per frame.
    // Gain is always < 1.0 so this mode can never overshoot.
    const float fovNorm = AimbotMath::clamp(distance / std::max(80.0f, settings.fovRadius), 0.0f, 1.0f);
    // Lerp: 0.12 (near) -> 0.62 (far), scaled by speed
    const float pull = AimbotMath::lerp(0.12f, 0.62f, fovNorm);
    const float gain = AimbotMath::clamp(settings.aimSpeed * pull, 0.08f, 0.65f);

    outX = dx * gain;
    outY = dy * gain;

    // Axis-cross hard clamp
    if (outX * dx < 0.0f) outX = 0.0f;
    if (outY * dy < 0.0f) outY = 0.0f;
}

void AimbotController::sanitizeMovement(float dx, float dy, float distance,
                                        const UnifiedSettings& settings,
                                        float modeStrength,
                                        float& inOutX, float& inOutY) {
    if (distance <= EPSILON) {
        inOutX = 0.0f;
        inOutY = 0.0f;
        return;
    }

    // Never move away from the target direction.
    if (inOutX * dx < 0.0f) inOutX = 0.0f;
    if (inOutY * dy < 0.0f) inOutY = 0.0f;

    // Adaptive recoil compensation (mostly active away from lock, very mild near target).
    if (settings.recoilCompensationEnabled && m_isAiming) {
        const float strength = settings.recoilCompensationStrength;
        const float maxComp = settings.recoilCompensationMax;
        const float decay = settings.recoilCompensationDecay;

        const float stableRadius = std::max(16.0f, settings.convergenceRadius * 0.9f);
        const bool stabilized = (distance <= stableRadius);
        const float farNorm = AimbotMath::clamp(
            (distance - stableRadius) / std::max(50.0f, settings.fovRadius * 0.6f),
            0.0f,
            1.0f
        );
        const float nearAttenuation = stabilized ? 0.18f : AimbotMath::lerp(0.35f, 1.0f, farNorm);

        float targetComp = 0.0f;
        if (dy > 0.0f) {
            targetComp = AimbotMath::clamp(dy * strength * nearAttenuation, 0.0f, maxComp * nearAttenuation);
        }

        float effectiveDecay = stabilized ? std::min(0.97f, decay + 0.10f) : decay;
        m_recoilCompY = m_recoilCompY * effectiveDecay + targetComp * (1.0f - effectiveDecay);
        if (stabilized && dy <= 0.0f) {
            m_recoilCompY *= 0.7f;
        }
        m_recoilCompY = AimbotMath::clamp(m_recoilCompY, 0.0f, maxComp);
        inOutY += m_recoilCompY;
    }

    // Re-assert directional safety after recoil compensation.
    if (inOutX * dx < 0.0f) inOutX = 0.0f;
    if (inOutY * dy < 0.0f) inOutY = 0.0f;

    // Axis clamp prevents one-frame crossing/throw-away.
    const float axisT = AimbotMath::clamp(distance / std::max(60.0f, settings.fovRadius), 0.0f, 1.0f);
    const float axisRatio = AimbotMath::lerp(0.68f, AXIS_NO_CROSS_RATIO, axisT);
    const float axisCapX = std::max(0.8f, std::abs(dx) * axisRatio);
    const float axisCapY = std::max(0.8f, std::abs(dy) * axisRatio);
    inOutX = AimbotMath::clamp(inOutX, -axisCapX, axisCapX);
    inOutY = AimbotMath::clamp(inOutY, -axisCapY, axisCapY);

    float maxStep = MAX_SINGLE_MOVE * AimbotMath::clamp(settings.aimSpeed * modeStrength, 0.34f, 1.0f);
    maxStep = std::min(maxStep, distance * 0.92f);
    if (m_isAiming && distance < std::max(100.0f, settings.fovRadius * 0.75f)) {
        maxStep *= 1.12f;
    }

    float moveDist = std::sqrt(inOutX * inOutX + inOutY * inOutY);
    if (moveDist > maxStep && moveDist > EPSILON) {
        const float scale = maxStep / moveDist;
        inOutX *= scale;
        inOutY *= scale;
        moveDist = maxStep;
    }

    // Sticky correction: avoid stalling with tiny deltas while target is still offset.
    const float minStep = (distance > 2.0f) ? 0.52f : 0.0f;
    if (moveDist < minStep && moveDist > EPSILON) {
        const float scale = minStep / moveDist;
        inOutX *= scale;
        inOutY *= scale;
    }
}

void AimbotController::applyMovement(float moveX, float moveY, const UnifiedSettings& settings) {
    const float blend = m_isAiming ? 0.72f : 1.0f;
    moveX = m_prevMoveX * (1.0f - blend) + moveX * blend;
    moveY = m_prevMoveY * (1.0f - blend) + moveY * blend;

    if ((moveX * m_prevMoveX) < 0.0f && std::abs(moveX) < 2.6f) {
        moveX *= 0.5f;
    }
    if ((moveY * m_prevMoveY) < 0.0f && std::abs(moveY) < 2.6f) {
        moveY *= 0.5f;
    }

    m_prevMoveX = moveX;
    m_prevMoveY = moveY;

    // Jitter suppression: dampen very small movements when already locked on target
    if (m_isAiming) {
        const float moveMag = std::sqrt(moveX * moveX + moveY * moveY);
        if (moveMag < 1.5f && moveMag > EPSILON) {
            const float jitterScale = moveMag / 1.5f; // 0..1 ramp
            moveX *= jitterScale * jitterScale; // quadratic suppression
            moveY *= jitterScale * jitterScale;
        }
    }

    const float touchCenterX = settings.touchX;
    const float touchCenterY = settings.touchY;
    const float touchRadius = settings.touchRadius;
    
    if (!m_isAiming) {
        m_touchX = touchCenterX;
        m_touchY = touchCenterY;
        m_touch->touchDown(AIM_SLOT, m_touchX, m_touchY);
        m_isAiming = true;
        m_warmupFramesRemaining = 1;
        usleep(1000);
    }

    if (m_warmupFramesRemaining > 0) {
        moveX *= 0.72f;
        moveY *= 0.72f;
        m_warmupFramesRemaining--;
    }
    
    m_touchX += moveX;
    m_touchY += moveY;
    
    const float distFromCenterX = m_touchX - touchCenterX;
    const float distFromCenterY = m_touchY - touchCenterY;
    const float distFromCenterSq = distFromCenterX * distFromCenterX + 
                                    distFromCenterY * distFromCenterY;
    
    if (distFromCenterSq > touchRadius * touchRadius && distFromCenterSq > EPSILON) {
        const float distFromCenter = std::sqrt(distFromCenterSq);
        const float scale = touchRadius / distFromCenter;
        m_touchX = touchCenterX + distFromCenterX * scale;
        m_touchY = touchCenterY + distFromCenterY * scale;
    }
    
    m_touch->touchMove(AIM_SLOT, m_touchX, m_touchY);
    
    if (settings.aimDelay > 0.0f) {
        usleep(static_cast<useconds_t>(settings.aimDelay * 1000.0f));
    }
}

void AimbotController::stopAiming() {
    if (m_isAiming && m_touch) {
        m_touch->touchUp(AIM_SLOT);
        m_isAiming = false;
        m_touchX = 0.0f;
        m_touchY = 0.0f;
        m_prevErrX = 0.0f;
        m_prevErrY = 0.0f;
        m_prevMoveX = 0.0f;
        m_prevMoveY = 0.0f;
        m_recoilCompY = 0.0f;
        m_warmupFramesRemaining = 0;
    }
}

void AimbotController::setScreenSize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;
    m_crosshairX = width * 0.5f;
    m_crosshairY = height * 0.5f;
}

bool AimbotController::isAiming() const {
    return m_isAiming;
}
