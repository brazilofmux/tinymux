#include "io_buffer.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <iostream> // Added for std::cerr
#include <iomanip>  // Added for std::hex/std::setw if needed later, good practice
#include <sstream>

// Define a macro for debug logging to easily disable it later
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_BUFFER_DEBUG(x) do { std::cerr << "[IoBuffer Debug][this=" << this << "] " << x << std::endl; } while (0)
#else
#define GANL_BUFFER_DEBUG(x) do {} while (0)
#endif

namespace ganl {

IoBuffer::IoBuffer(size_t initialCapacity)
    : buffer_(initialCapacity), readPos_(0), writePos_(0), locked_(false) {
    GANL_BUFFER_DEBUG("Constructed. Initial capacity: " << initialCapacity
        << ", State: readPos=" << readPos_ << ", writePos=" << writePos_
        << ", cap=" << capacity() << ", locked=" << locked_);
}

IoBuffer::~IoBuffer() {
    GANL_BUFFER_DEBUG("Destroyed. Final State: readPos=" << readPos_ << ", writePos=" << writePos_
        << ", readable=" << readableBytes() << ", cap=" << capacity() << ", locked=" << locked_);
}

IoBuffer::IoBuffer(IoBuffer&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      readPos_(other.readPos_),
      writePos_(other.writePos_),
      locked_(other.locked_) {
    GANL_BUFFER_DEBUG("Move Constructed from other=" << &other
        << ". New State: readPos=" << readPos_ << ", writePos=" << writePos_
        << ", cap=" << capacity() << ", locked=" << locked_);
    other.readPos_ = 0;
    other.writePos_ = 0;
    other.locked_ = false; // Ensure source is clean
    GANL_BUFFER_DEBUG("Source other=" << &other << " state reset.");
}

IoBuffer& IoBuffer::operator=(IoBuffer&& other) noexcept {
    GANL_BUFFER_DEBUG("Move Assigning from other=" << &other << ".");
    if (this != &other) {
        buffer_ = std::move(other.buffer_);
        readPos_ = other.readPos_;
        writePos_ = other.writePos_;
        locked_ = other.locked_;
        GANL_BUFFER_DEBUG("State moved. New State: readPos=" << readPos_ << ", writePos=" << writePos_
            << ", cap=" << capacity() << ", locked=" << locked_);

        other.readPos_ = 0;
        other.writePos_ = 0;
        other.locked_ = false; // Ensure source is clean
        GANL_BUFFER_DEBUG("Source other=" << &other << " state reset.");
    } else {
        GANL_BUFFER_DEBUG("Move assignment to self ignored.");
    }
    return *this;
}

char* IoBuffer::writePtr() {
    if (locked_) {
        GANL_BUFFER_DEBUG("Attempted writePtr() on locked buffer!");
        throw std::runtime_error("Cannot write to a locked buffer");
    }
    // GANL_BUFFER_DEBUG("Providing writePtr() at offset " << writePos_); // Maybe too noisy
    return buffer_.data() + writePos_;
}

const char* IoBuffer::readPtr() const {
    // GANL_BUFFER_DEBUG("Providing readPtr() at offset " << readPos_); // Maybe too noisy
    return buffer_.data() + readPos_;
}

size_t IoBuffer::readableBytes() const {
    return writePos_ - readPos_;
}

size_t IoBuffer::writableBytes() const {
    return buffer_.size() - writePos_;
}

void IoBuffer::commitWrite(size_t bytesWritten) {
    GANL_BUFFER_DEBUG("Attempting commitWrite(" << bytesWritten << "). Current state: writePos=" << writePos_
        << ", writable=" << writableBytes() << ", locked=" << locked_);
    if (locked_) {
        GANL_BUFFER_DEBUG("ERROR: Cannot commit write to a locked buffer!");
        throw std::runtime_error("Cannot commit write to a locked buffer");
    }

    if (bytesWritten > writableBytes()) {
        GANL_BUFFER_DEBUG("ERROR: commitWrite(" << bytesWritten << ") exceeds writable bytes (" << writableBytes() << ")!");
        throw std::out_of_range("Cannot commit more bytes than writable capacity");
    }

    writePos_ += bytesWritten;
    GANL_BUFFER_DEBUG("commitWrite successful. New state: writePos=" << writePos_
        << ", readable=" << readableBytes());
}

void IoBuffer::consumeRead(size_t bytesRead) {
    GANL_BUFFER_DEBUG("Attempting consumeRead(" << bytesRead << "). Current state: readPos=" << readPos_
        << ", readable=" << readableBytes());
    if (bytesRead > readableBytes()) {
         GANL_BUFFER_DEBUG("ERROR: consumeRead(" << bytesRead << ") exceeds readable bytes (" << readableBytes() << ")!");
        throw std::out_of_range("Cannot consume more bytes than readable");
    }

    readPos_ += bytesRead;

    // If we've read everything, reset positions to start
    if (readPos_ == writePos_) {
        GANL_BUFFER_DEBUG("Buffer emptied after consumeRead. Resetting positions: readPos=0, writePos=0");
        readPos_ = 0;
        writePos_ = 0;
    } else {
         GANL_BUFFER_DEBUG("consumeRead successful. New state: readPos=" << readPos_
            << ", readable=" << readableBytes());
    }
}

void IoBuffer::ensureWritable(size_t required) {
     GANL_BUFFER_DEBUG("ensureWritable(" << required << ") called. Current state: writable=" << writableBytes()
        << ", readPos=" << readPos_ << ", writePos=" << writePos_ << ", cap=" << capacity() << ", locked=" << locked_);
    if (locked_) {
        GANL_BUFFER_DEBUG("ERROR: Cannot resize/compact a locked buffer!");
        throw std::runtime_error("Cannot resize a locked buffer");
    }

    if (writableBytes() >= required) {
        GANL_BUFFER_DEBUG("Already have enough writable space (" << writableBytes() << "). No action needed.");
        return;  // Already have enough space
    }

    // Check if compaction + existing capacity is enough, *without* actually resizing yet
    // Available space = space before readPos + space after writePos
    size_t availableTotal = readPos_ + writableBytes();
    if (availableTotal < required) {
        // Need to resize the buffer
        size_t newCapacity = std::max(buffer_.size() * 3 / 2, writePos_ + required);
        GANL_BUFFER_DEBUG("Not enough space even after potential compact (" << availableTotal << " < " << required
            << "). Resizing buffer from " << capacity() << " to " << newCapacity);
        buffer_.resize(newCapacity); // Resize vector, preserving existing content up to writePos_
        // After resize, writableBytes() is updated implicitly by buffer_.size() changing
         GANL_BUFFER_DEBUG("Resize complete. New state: cap=" << capacity() << ", writable=" << writableBytes());
         // No need to compact *after* resizing in this case, as all free space is now contiguous at the end.
         return; // We are done
    }

     // Compaction *might* be enough. Only compact if readPos > 0
    if (readPos_ > 0) {
        GANL_BUFFER_DEBUG("Attempting compact() to make space.");
        compact(); // compact() logs its own details
        if (writableBytes() >= required) {
             GANL_BUFFER_DEBUG("Compaction successful. Have enough space now (" << writableBytes() << ").");
            return;  // Compaction created enough space
        } else {
            // This case should ideally not be hit if the 'availableTotal' check above was correct,
            // but handle defensively. It implies we need to resize *after* compacting.
            size_t newCapacity = std::max(buffer_.size() * 3 / 2, writePos_ + required);
            GANL_BUFFER_DEBUG("Compaction wasn't enough (" << writableBytes() << " < " << required
                << "). Resizing buffer from " << capacity() << " to " << newCapacity << " after compact.");
             buffer_.resize(newCapacity);
             GANL_BUFFER_DEBUG("Resize post-compact complete. New state: cap=" << capacity() << ", writable=" << writableBytes());
        }
    } else {
         // readPos_ == 0 but writableBytes < required. This means we must resize.
         size_t newCapacity = std::max(buffer_.size() * 3 / 2, writePos_ + required);
         GANL_BUFFER_DEBUG("Buffer already compact (readPos=0), but need more space (" << writableBytes() << " < " << required
                << "). Resizing buffer from " << capacity() << " to " << newCapacity);
        buffer_.resize(newCapacity);
        GANL_BUFFER_DEBUG("Resize complete. New state: cap=" << capacity() << ", writable=" << writableBytes());
    }


}

void IoBuffer::compact() {
    GANL_BUFFER_DEBUG("compact() called. Current state: readPos=" << readPos_
        << ", writePos=" << writePos_ << ", locked=" << locked_);
    if (locked_) {
         GANL_BUFFER_DEBUG("ERROR: Cannot compact a locked buffer!");
        throw std::runtime_error("Cannot compact a locked buffer");
    }

    if (readPos_ == 0) {
        GANL_BUFFER_DEBUG("Buffer already compact (readPos=0). No action needed.");
        return;  // Already compact
    }

    size_t readable = readableBytes();
    if (readable == 0) {
        // Reset both positions to start - more efficient than memmove(0 bytes)
        GANL_BUFFER_DEBUG("Buffer is empty (readable=0). Resetting positions: readPos=0, writePos=0");
        readPos_ = 0;
        writePos_ = 0;
        return;
    }

    // Move data to the front
    GANL_BUFFER_DEBUG("Moving " << readable << " bytes from offset " << readPos_ << " to offset 0.");
    std::memmove(buffer_.data(), buffer_.data() + readPos_, readable);
    readPos_ = 0;
    writePos_ = readable;
    GANL_BUFFER_DEBUG("Compaction complete. New state: readPos=" << readPos_
        << ", writePos=" << writePos_ << ", readable=" << readableBytes()
        << ", writable=" << writableBytes());
}

void IoBuffer::clear() {
     GANL_BUFFER_DEBUG("clear() called. Current state: readPos=" << readPos_
        << ", writePos=" << writePos_ << ", locked=" << locked_);
    if (locked_) {
         GANL_BUFFER_DEBUG("ERROR: Cannot clear a locked buffer!");
        throw std::runtime_error("Cannot clear a locked buffer");
    }
    readPos_ = 0;
    writePos_ = 0;
     GANL_BUFFER_DEBUG("Buffer cleared. New state: readPos=" << readPos_ << ", writePos=" << writePos_);
}

size_t IoBuffer::capacity() const {
    return buffer_.size();
}

bool IoBuffer::empty() const {
    return readableBytes() == 0;
}

void IoBuffer::append(const void* data, size_t len) {
     GANL_BUFFER_DEBUG("append() called with len=" << len << ". Locked=" << locked_);
    if (locked_) {
         GANL_BUFFER_DEBUG("ERROR: Cannot append to a locked buffer!");
        throw std::runtime_error("Cannot append to a locked buffer");
    }
    if (len == 0) {
        GANL_BUFFER_DEBUG("append() called with len=0. No action needed.");
        return;
    }

    ensureWritable(len); // ensureWritable logs its actions
    GANL_BUFFER_DEBUG("Appending " << len << " bytes at offset " << writePos_);
    std::memcpy(writePtr(), data, len);
    // commitWrite handles updating writePos_ and logging
    commitWrite(len);
}

std::string IoBuffer::consumeReadAllAsString() {
    size_t readable = readableBytes();
    GANL_BUFFER_DEBUG("consumeReadAllAsString() called. Readable bytes: " << readable);
    if (readable == 0) {
        return "";
    }

    std::string result(readPtr(), readable);
    // consumeRead handles updating readPos_ and logging
    consumeRead(readable);
    return result;
}

void IoBuffer::lockForReuse(bool lock) {
    GANL_BUFFER_DEBUG("lockForReuse(" << (lock ? "true" : "false") << ") called. Current locked state: " << locked_);
    locked_ = lock;
     GANL_BUFFER_DEBUG("New locked state: " << locked_);
}

bool IoBuffer::isLockedForReuse() const {
    // No need to log here, it's just a query
    return locked_;
}

namespace utils {
    /**
     * @brief Dumps the readable contents of an IoBuffer in hex and ASCII format.
     *
     * @param os The output stream (e.g., std::cerr).
     * @param buffer The IoBuffer to dump.
     * @param maxBytes Maximum number of bytes to dump (0 for all).
     * @param bytesPerRow Number of bytes per row in the output.
     * @param prefix Optional prefix string for each line.
     */
    void dumpIoBufferHex(std::ostream& os, const IoBuffer& buffer,
        size_t maxBytes, size_t bytesPerRow,
        const std::string& prefix) {

        const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.readPtr());
        size_t length = buffer.readableBytes();
        size_t dumpLength = (maxBytes > 0 && maxBytes < length) ? maxBytes : length;

        if (dumpLength == 0) {
            os << prefix << "<Buffer Empty>" << std::endl;
            return;
        }

        os << prefix << "Dumping " << dumpLength << " of " << length << " readable bytes:" << std::endl;

        for (size_t i = 0; i < dumpLength; i += bytesPerRow) {
            os << prefix;
            // Offset
            os << std::hex << std::setw(4) << std::setfill('0') << i << " - ";

            // Hex bytes
            for (size_t j = 0; j < bytesPerRow; ++j) {
                if (i + j < dumpLength) {
                    os << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]) << " ";
                }
                else {
                    os << "   "; // Padding
                }
                if (j == (bytesPerRow / 2) - 1) { // Add extra space in the middle
                    os << " ";
                }
            }
            os << " | ";

            // ASCII representation
            for (size_t j = 0; j < bytesPerRow; ++j) {
                if (i + j < dumpLength) {
                    unsigned char c = data[i + j];
                    os << (std::isprint(static_cast<int>(c)) ? static_cast<char>(c) : '.');
                }
                else {
                    os << " "; // Padding
                }
            }
            os << std::endl;
        }
        if (dumpLength < length) {
            os << prefix << "... (" << (length - dumpLength) << " more bytes not shown)" << std::endl;
        }
    }

    // Overload to return as string (useful for logging frameworks)
    std::string dumpIoBufferHexString(const IoBuffer& buffer,
        size_t maxBytes, size_t bytesPerRow,
        const std::string& prefix) {
        std::stringstream ss;
        dumpIoBufferHex(ss, buffer, maxBytes, bytesPerRow, prefix);
        return ss.str();
    }
}

} // namespace ganl
