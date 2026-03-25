#ifndef ESP_OVERLAY_WINDOW_H
#define ESP_OVERLAY_WINDOW_H

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <android/native_window.h>
#include <atomic>
#include "../settings.h"
#include "../utils/logger.h"

/**
 * @file overlay_window.h
 * @brief OpenGL ES 3.1 overlay window management
 * 
 * Creates and manages an EGL context for rendering overlays
 * on top of games using TYPE_APPLICATION_OVERLAY.
 */

namespace ESP {

/**
 * @class OverlayWindow
 * @brief EGL/OpenGL ES overlay window
 */
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    
    // Non-copyable
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    
    /**
     * @brief Initialize OpenGL ES context with native window
     * @param window ANativeWindow from overlay service
     * @param width Window width
     * @param height Window height
     * @return true if initialization successful
     */
    bool initialize(ANativeWindow* window, int width, int height);
    
    /**
     * @brief Shutdown and release OpenGL resources
     */
    void shutdown();
    
    /**
     * @brief Make this context current for rendering
     * @return true if successful
     */
    bool makeCurrent();
    
    /**
     * @brief Swap buffers (present frame)
     * @return true if successful
     */
    bool swapBuffers();
    
    /**
     * @brief Clear the screen with transparent background
     */
    void clear();
    
    /**
     * @brief Update window dimensions
     * @param width New width
     * @param height New height
     */
    void resize(int width, int height);
    
    /**
     * @brief Get window width
     */
    int getWidth() const { return width_; }
    
    /**
     * @brief Get window height
     */
    int getHeight() const { return height_; }
    
    /**
     * @brief Check if window is initialized
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Get underlying ANativeWindow
     */
    ANativeWindow* getNativeWindow() const { return window_; }

private:
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    ANativeWindow* window_;
    
    int width_;
    int height_;
    bool initialized_;
};

} // namespace ESP

#endif // ESP_OVERLAY_WINDOW_H
