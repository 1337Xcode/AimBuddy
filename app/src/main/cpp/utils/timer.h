#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <chrono>
#include <array>

/**
 * @file Timer.h
 * @brief High-precision timer for FPS counting and performance profiling
 * 
 * Uses std::chrono for microsecond-precision timing. Provides rolling
 * average FPS calculation over the last N frames.
 */

namespace ESP {

/**
 * @class Timer
 * @brief FPS counter with rolling average
 */
class Timer {
public:
    Timer() : frameCount_(0), totalTime_(0.0), lastTime_(std::chrono::high_resolution_clock::now()) {}
    
    /**
     * @brief Mark the start of a new frame
     */
    void tick() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        double deltaTime = std::chrono::duration<double, std::milli>(currentTime - lastTime_).count();
        lastTime_ = currentTime;
        
        // Update rolling average
        frameTimes_[frameCount_ % WINDOW_SIZE] = deltaTime;
        frameCount_++;
        totalTime_ += deltaTime;
    }
    
    /**
     * @brief Get current FPS (rolling average over last WINDOW_SIZE frames)
     * @return FPS as float
     */
    float getFPS() const {
        if (frameCount_ == 0) return 0.0f;
        
        size_t count = (frameCount_ < WINDOW_SIZE) ? frameCount_ : WINDOW_SIZE;
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += frameTimes_[i];
        }
        double avgFrameTime = sum / count;
        return (avgFrameTime > 0.0) ? static_cast<float>(1000.0 / avgFrameTime) : 0.0f;
    }
    
    /**
     * @brief Get average frame time in milliseconds
     * @return Average frame time
     */
    float getAverageFrameTime() const {
        if (frameCount_ == 0) return 0.0f;
        
        size_t count = (frameCount_ < WINDOW_SIZE) ? frameCount_ : WINDOW_SIZE;
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += frameTimes_[i];
        }
        return static_cast<float>(sum / count);
    }
    
    /**
     * @brief Reset timer statistics
     */
    void reset() {
        frameCount_ = 0;
        totalTime_ = 0.0;
        lastTime_ = std::chrono::high_resolution_clock::now();
        frameTimes_.fill(0.0);
    }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    
    static constexpr size_t WINDOW_SIZE = 60;  // Rolling average over 60 frames
    
    std::array<double, WINDOW_SIZE> frameTimes_{};
    size_t frameCount_;
    double totalTime_;
    TimePoint lastTime_;
    
    static TimePoint now() {
        return Clock::now();
    }
};

/**
 * @class ScopedTimer
 * @brief RAII timer for measuring code block execution time
 * 
 * Usage:
 *   {
 *       ScopedTimer timer("Inference");
 *       // ... code to measure ...
 *   } // Logs execution time on destruction
 */
class ScopedTimer {
public:
    explicit ScopedTimer(const char* name) 
        : name_(name), start_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start_).count();
        LOGP("%s took %.2f ms", name_, duration);
    }

private:
    const char* name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

} // namespace ESP

#endif // ESP_TIMER_H
