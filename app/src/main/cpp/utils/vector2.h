#ifndef ESP_VECTOR2_H
#define ESP_VECTOR2_H

#include <cmath>
#include <algorithm>

namespace ESP {

/**
 * @struct Vector2
 * @brief Lightweight 2D vector for coordinate math and FOV calculations
 * 
 * Provides fast distance, magnitude, clamping, and interpolation operations
 * optimized for ESP box coordinate calculations.
 */
struct Vector2 {
    float x;
    float y;

    // Constructors
    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}

    // Static helpers
    static Vector2 Zero() { return Vector2(0.0f, 0.0f); }
    static Vector2 One() { return Vector2(1.0f, 1.0f); }

    /**
     * @brief Calculate distance between two points
     * @param a First point
     * @param b Second point
     * @return Distance in pixels
     */
    static inline float Distance(const Vector2& a, const Vector2& b) {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        return sqrtf(dx * dx + dy * dy);
    }

    /**
     * @brief Calculate squared distance (faster, avoids sqrt)
     * @param a First point
     * @param b Second point
     * @return Squared distance
     */
    static inline float DistanceSqr(const Vector2& a, const Vector2& b) {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        return dx * dx + dy * dy;
    }

    /**
     * @brief Get magnitude (length) of vector
     * @param v Input vector
     * @return Length
     */
    static inline float Magnitude(const Vector2& v) {
        return sqrtf(v.x * v.x + v.y * v.y);
    }

    /**
     * @brief Clamp vector magnitude to max length
     * @param v Input vector
     * @param maxLength Maximum allowed length
     * @return Clamped vector
     */
    static inline Vector2 ClampMagnitude(const Vector2& v, float maxLength) {
        float mag = Magnitude(v);
        if (mag > maxLength && mag > 0.0f) {
            float scale = maxLength / mag;
            return Vector2(v.x * scale, v.y * scale);
        }
        return v;
    }

    /**
     * @brief Linear interpolation between two vectors
     * @param a Start vector
     * @param b End vector
     * @param t Interpolation factor [0-1]
     * @return Interpolated vector
     */
    static inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t) {
        t = std::max(0.0f, std::min(1.0f, t));
        return Vector2(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t
        );
    }

    /**
     * @brief Move towards target by max distance delta
     * @param current Current position
     * @param target Target position
     * @param maxDistanceDelta Maximum movement distance
     * @return New position
     */
    static inline Vector2 MoveTowards(const Vector2& current, const Vector2& target, float maxDistanceDelta) {
        Vector2 delta(target.x - current.x, target.y - current.y);
        float dist = Magnitude(delta);
        
        if (dist <= maxDistanceDelta || dist < 0.001f) {
            return target;
        }
        
        float scale = maxDistanceDelta / dist;
        return Vector2(
            current.x + delta.x * scale,
            current.y + delta.y * scale
        );
    }

    /**
     * @brief Clamp vector components to min/max bounds
     * @param v Input vector
     * @param min Minimum bounds
     * @param max Maximum bounds
     * @return Clamped vector
     */
    static inline Vector2 Clamp(const Vector2& v, const Vector2& min, const Vector2& max) {
        return Vector2(
            std::max(min.x, std::min(v.x, max.x)),
            std::max(min.y, std::min(v.y, max.y))
        );
    }

    // Operators
    Vector2 operator+(const Vector2& other) const {
        return Vector2(x + other.x, y + other.y);
    }

    Vector2 operator-(const Vector2& other) const {
        return Vector2(x - other.x, y - other.y);
    }

    Vector2 operator*(float scalar) const {
        return Vector2(x * scalar, y * scalar);
    }

    Vector2 operator/(float scalar) const {
        return Vector2(x / scalar, y / scalar);
    }

    Vector2& operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    // Utility
    float lengthSqr() const {
        return x * x + y * y;
    }

    float length() const {
        return sqrtf(lengthSqr());
    }

    Vector2 normalized() const {
        float len = length();
        if (len > 0.0f) {
            return Vector2(x / len, y / len);
        }
        return Vector2::Zero();
    }
};

} // namespace ESP

#endif // ESP_VECTOR2_H
