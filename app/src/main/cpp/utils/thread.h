#ifndef ESP_THREAD_H
#define ESP_THREAD_H

#include <pthread.h>
#include <sched.h>
#include <functional>
#include <memory>
#include "logger.h"

/**
 * @file Thread.h
 * @brief RAII pthread wrapper with CPU affinity support
 * 
 * Provides a clean interface for creating threads with specific CPU
 * affinity for performance optimization on big.LITTLE architectures.
 */

namespace ESP {

/**
 * @class Thread
 * @brief RAII wrapper for pthread with CPU affinity
 */
class Thread {
public:
    using ThreadFunc = std::function<void()>;
    
    /**
     * @brief Construct thread (does not start immediately)
     * @param func Function to execute in thread
     * @param name Thread name for debugging
     */
    Thread(ThreadFunc func, const char* name = "ESPThread")
        : func_(std::move(func))
        , name_(name)
        , thread_(0)
        , running_(false) {}
    
    /**
     * @brief Destructor - joins thread if running
     */
    ~Thread() {
        if (running_) {
            join();
        }
    }
    
    // Non-copyable, non-movable
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    
    /**
     * @brief Start thread execution
     * @param cpuAffinity CPU core to bind to (-1 for no affinity)
     * @return true if started successfully
     */
    bool start(int cpuAffinity = -1) {
        if (running_) {
            LOGW("Thread %s already running", name_);
            return false;
        }
        
        // Store affinity for application in threadEntry
        cpuAffinity_ = cpuAffinity;
        
        int result = pthread_create(&thread_, nullptr, threadEntry, this);
        
        if (result != 0) {
            LOGE("Failed to create thread %s: error %d", name_, result);
            return false;
        }
        
        running_ = true;
        LOGI("Thread %s started successfully", name_);
        return true;
    }
    
    /**
     * @brief Join thread (wait for completion)
     */
    void join() {
        if (running_ && thread_ != 0) {
            pthread_join(thread_, nullptr);
            running_ = false;
            LOGD("Thread %s joined", name_);
        }
    }
    
    /**
     * @brief Check if thread is running
     * @return true if running
     */
    bool isRunning() const {
        return running_;
    }

private:
    static void* threadEntry(void* arg) {
        Thread* thread = static_cast<Thread*>(arg);
        
        // Set thread name
        pthread_setname_np(pthread_self(), thread->name_);
        
        // Set CPU affinity if requested
        if (thread->cpuAffinity_ >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(thread->cpuAffinity_, &cpuset);
            
            // Use sched_setaffinity (standard syscall) instead of pthread_attr_setaffinity_np
            // 0 as pid means current thread/process
            // This is the portable way on modern Android
            if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
                LOGW("Thread %s: Failed to set CPU affinity", thread->name_);
            } else {
                LOGD("Thread %s: CPU affinity set to core %d", thread->name_, thread->cpuAffinity_);
            }
        }
        
        LOGD("Thread %s executing", thread->name_);
        thread->func_();
        LOGD("Thread %s completed", thread->name_);
        
        return nullptr;
    }
    
    ThreadFunc func_;
    const char* name_;
    pthread_t thread_;
    bool running_;
    int cpuAffinity_ = -1;  // Added member to store requested affinity
};

} // namespace ESP

#endif // ESP_THREAD_H
