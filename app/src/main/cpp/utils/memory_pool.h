/**
 * memory_pool.h - Pre-allocated memory pool for zero-allocation runtime
 */

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include "../detector/bounding_box.h"
#include "../settings.h"

namespace ESP {

/**
 * Fixed-size array that acts like a vector but never allocates
 * on the heap. Uses stack/static memory.
 */
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
    
    inline T* data() { return m_data; }
    inline const T* data() const { return m_data; }
    
    // Iterator support
    inline T* begin() { return m_data; }
    inline T* end() { return m_data + m_size; }
    inline const T* begin() const { return m_data; }
    inline const T* end() const { return m_data + m_size; }
    
    // Remove element at index (swap with last for O(1))
    inline void removeAt(int index) {
        if (index < 0 || index >= m_size) return;
        if (index < m_size - 1) {
            m_data[index] = m_data[m_size - 1]; // Move last element to hole
        }
        m_size--;
    }
    
    // Fast sort
    template<typename Compare>
    void sort(Compare comp) {
        std::sort(begin(), end(), comp);
    }
    
private:
    T m_data[Capacity];
    int m_size;
};

// Common types
using DetectionArray = FixedArray<BoundingBox, Config::MAX_DETECTIONS>;

/**
 * Ring buffer for frame timing history
 */
template<typename T, int Size>
class RingBuffer {
public:
    RingBuffer() : m_head(0), m_count(0) {
        memset(m_data, 0, sizeof(m_data));
    }
    
    inline void push(T value) {
        m_data[m_head] = value;
        m_head = (m_head + 1) % Size;
        if (m_count < Size) m_count++;
    }
    
    inline T average() const {
        if (m_count == 0) return T(0);
        T sum = T(0);
        for (int i = 0; i < m_count; i++) {
            sum += m_data[i];
        }
        return sum / m_count;
    }
    
    inline int count() const { return m_count; }
    
private:
    T m_data[Size];
    int m_head;
    int m_count;
};

} // namespace ESP

#endif // MEMORY_POOL_H
