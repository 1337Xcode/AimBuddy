#ifndef ESP_BOUNDING_BOX_H
#define ESP_BOUNDING_BOX_H

#include "../utils/vector2.h"
#include <algorithm>

/**
 * @file bounding_box.h
 * @brief Detection result data structure
 * 
 * Represents a single object detection with bounding box coordinates,
 * confidence score, and class ID. Coordinates are in screen space.
 */

namespace ESP {

/**
 * @struct BoundingBox
 * @brief Detection bounding box in screen coordinates
 */
struct BoundingBox {
    float x;           ///< Top-left X coordinate (screen space)
    float y;           ///< Top-left Y coordinate (screen space)
    float width;       ///< Box width in pixels
    float height;      ///< Box height in pixels
    float confidence;  ///< Detection confidence (0.0 - 1.0)
    int classId;       ///< Class ID (0 for enemy in single-class model)
    
    /**
     * @brief Default constructor
     */
    BoundingBox() 
        : x(0.0f), y(0.0f), width(0.0f), height(0.0f)
        , confidence(0.0f), classId(0) {}
    
    /**
     * @brief Parameterized constructor
     */
    BoundingBox(float x_, float y_, float w_, float h_, float conf_, int cls_)
        : x(x_), y(y_), width(w_), height(h_)
        , confidence(conf_), classId(cls_) {}
    
    /**
     * @brief Get center as Vector2
     */
    Vector2 center() const {
        return Vector2(x + width * 0.5f, y + height * 0.5f);
    }
    
    /**
     * @brief Get center X coordinate
     */
    float centerX() const {
        return x + width * 0.5f;
    }
    
    /**
     * @brief Get center Y coordinate
     */
    float centerY() const {
        return y + height * 0.5f;
    }
    
    /**
     * @brief Check if center is within FOV radius from screen center
     * @param screenCenter Screen center point
     * @param fovRadius FOV radius in pixels
     * @return true if within FOV
     */
    bool isWithinFOV(const Vector2& screenCenter, float fovRadius) const {
        return Vector2::DistanceSqr(center(), screenCenter) <= (fovRadius * fovRadius);
    }
    
    /**
     * @brief Get distance from box center to a point
     * @param point Target point
     * @return Distance in pixels
     */
    float distanceToPoint(const Vector2& point) const {
        return Vector2::Distance(center(), point);
    }
    
    /**
     * @brief Calculate IoU (Intersection over Union) with another box
     * @param other The other bounding box
     * @return IoU value (0.0 - 1.0)
     */
    float iou(const BoundingBox& other) const {
        float x1 = std::max(x, other.x);
        float y1 = std::max(y, other.y);
        float x2 = std::min(x + width, other.x + other.width);
        float y2 = std::min(y + height, other.y + other.height);
        
        if (x2 <= x1 || y2 <= y1) {
            return 0.0f;
        }
        
        float intersection = (x2 - x1) * (y2 - y1);
        float area1 = width * height;
        float area2 = other.width * other.height;
        float unionArea = area1 + area2 - intersection;
        
        return (unionArea > 0.0f) ? (intersection / unionArea) : 0.0f;
    }
};

} // namespace ESP

#endif // ESP_BOUNDING_BOX_H
