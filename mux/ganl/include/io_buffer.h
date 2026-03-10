#ifndef GANL_IO_BUFFER_H
#define GANL_IO_BUFFER_H

#include <cstddef>
#include <vector>
#include <string>

namespace ganl {

/**
 * IoBuffer - A buffer for efficient I/O operations
 *
 * This class manages a resizable buffer with separate read and write positions,
 * providing efficient memory management for network operations.
 */
class IoBuffer {
public:
    /**
     * Constructor
     *
     * @param initialCapacity Initial buffer capacity in bytes
     */
    explicit IoBuffer(size_t initialCapacity = 4096);

    /**
     * Destructor
     */
    ~IoBuffer();

    // Delete copy constructor and assignment operator
    IoBuffer(const IoBuffer&) = delete;
    IoBuffer& operator=(const IoBuffer&) = delete;

    // Move constructor and assignment operator
    IoBuffer(IoBuffer&&) noexcept;
    IoBuffer& operator=(IoBuffer&&) noexcept;

    /**
     * Get pointer for writing new data
     *
     * @return Pointer to the buffer position for writing
     */
    char* writePtr();

    /**
     * Get pointer for reading data
     *
     * @return Const pointer to the buffer position for reading
     */
    const char* readPtr() const;

    /**
     * Get number of bytes available for reading
     *
     * @return Number of readable bytes
     */
    size_t readableBytes() const;

    /**
     * Get number of bytes available for writing
     *
     * @return Number of writable bytes (contiguous)
     */
    size_t writableBytes() const;

    /**
     * Update write position after writing data
     *
     * @param bytesWritten Number of bytes written
     */
    void commitWrite(size_t bytesWritten);

    /**
     * Update read position after reading data
     *
     * @param bytesRead Number of bytes read
     */
    void consumeRead(size_t bytesRead);

    /**
     * Ensure buffer has enough space for writing
     *
     * @param required Number of bytes required
     */
    void ensureWritable(size_t required);

    /**
     * Move readable data to the beginning of the buffer
     */
    void compact();

    /**
     * Clear the buffer (reset read and write positions)
     */
    void clear();

    /**
     * Get the total buffer capacity
     *
     * @return Buffer capacity in bytes
     */
    size_t capacity() const;

    /**
     * Check if buffer is empty
     *
     * @return true if no readable data, false otherwise
     */
    bool empty() const;

    /**
     * Append data to the buffer
     *
     * @param data Pointer to data to append
     * @param len Length of data in bytes
     */
    void append(const void* data, size_t len);

    /**
     * Read all available data as a string and advance read position
     *
     * @return String containing all readable data
     */
    std::string consumeReadAllAsString();

    /**
     * Lock buffer for reuse (important for SSL operations)
     *
     * When locked, the buffer content cannot be modified
     *
     * @param lock Whether to lock or unlock the buffer
     */
    void lockForReuse(bool lock);

    /**
     * Check if buffer is locked for reuse
     *
     * @return true if locked, false otherwise
     */
    bool isLockedForReuse() const;

private:
    std::vector<char> buffer_;
    size_t readPos_{0};
    size_t writePos_{0};
    bool locked_{false};
};

namespace utils {
    void dumpIoBufferHex(std::ostream& os, const IoBuffer& buffer,
        size_t maxBytes = 0, size_t bytesPerRow = 16,
        const std::string& prefix = "");
    std::string dumpIoBufferHexString(const IoBuffer& buffer,
        size_t maxBytes = 0, size_t bytesPerRow = 16,
        const std::string& prefix = "");
}

} // namespace ganl

#endif // GANL_IO_BUFFER_H
