#ifndef TOUCH_HELPER_H
#define TOUCH_HELPER_H

/**
 * TouchHelper - uinput-based touch simulation
 *
 * Template-matched touch pipeline.
 * Creates a virtual touchscreen via /dev/uinput.
 * Requires root access.
 */
class TouchHelper {
public:
    TouchHelper();
    ~TouchHelper();

    /**
     * Initialize touch system
     * @return true if successful (requires root)
     */
    bool init();

    /**
     * Set screen dimensions for coordinate scaling
     */
    void setScreenSize(int width, int height);

    /**
     * Simulate touch down at screen coordinates
     */
    void touchDown(int slot, float x, float y);

    /**
     * Simulate touch move at screen coordinates
     */
    void touchMove(int slot, float x, float y);

    /**
     * Simulate touch up
     */
    void touchUp(int slot);

    /**
     * Cleanup
     */
    void shutdown();
};

#endif // TOUCH_HELPER_H