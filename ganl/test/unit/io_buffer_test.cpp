#include <gtest/gtest.h>
#include "ganl/io_buffer.h"
#include <string>
#include <vector>
#include <cstring> // For std::memcpy, std::memcmp
#include <stdexcept> // For std::runtime_error, std::out_of_range

// Test Fixture (optional, but good practice if common setup is needed)
class IoBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup for tests, if needed
    }

    void TearDown() override {
        // Common teardown for tests, if needed
    }
};

// Test Construction and Initial State
TEST_F(IoBufferTest, Construction) {
    ganl::IoBuffer buf_default;
    EXPECT_EQ(buf_default.readableBytes(), 0);
    EXPECT_EQ(buf_default.writableBytes(), 4096); // Default initial capacity
    EXPECT_EQ(buf_default.capacity(), 4096);
    EXPECT_TRUE(buf_default.empty());
    EXPECT_FALSE(buf_default.isLockedForReuse());
    EXPECT_NE(buf_default.readPtr(), nullptr);
    EXPECT_NE(buf_default.writePtr(), nullptr);
    EXPECT_EQ(buf_default.readPtr(), buf_default.writePtr());

    ganl::IoBuffer buf_custom(1024);
    EXPECT_EQ(buf_custom.readableBytes(), 0);
    EXPECT_EQ(buf_custom.writableBytes(), 1024);
    EXPECT_EQ(buf_custom.capacity(), 1024);
    EXPECT_TRUE(buf_custom.empty());
}

// Test Basic Write and Read Operations
TEST_F(IoBufferTest, BasicWriteRead) {
    ganl::IoBuffer buf(100); // Small capacity for testing limits
    const char* data1 = "Hello";
    size_t len1 = std::strlen(data1);

    // Write data using writePtr and commitWrite
    std::memcpy(buf.writePtr(), data1, len1);
    buf.commitWrite(len1);

    EXPECT_EQ(buf.readableBytes(), len1);
    EXPECT_EQ(buf.writableBytes(), 100 - len1);
    EXPECT_FALSE(buf.empty());

    // Read data using readPtr and consumeRead
    EXPECT_EQ(std::memcmp(buf.readPtr(), data1, len1), 0);
    buf.consumeRead(len1);

    EXPECT_EQ(buf.readableBytes(), 0);
    EXPECT_EQ(buf.writableBytes(), 100); // Positions should reset
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.readPtr(), buf.writePtr()); // Check reset

    // Write more data
    const char* data2 = " World";
    size_t len2 = std::strlen(data2);
    std::memcpy(buf.writePtr(), data2, len2);
    buf.commitWrite(len2);

    EXPECT_EQ(buf.readableBytes(), len2);
    EXPECT_EQ(std::memcmp(buf.readPtr(), data2, len2), 0);
}

// Test Append Operation
TEST_F(IoBufferTest, Append) {
    ganl::IoBuffer buf(20);
    std::string s1 = "TestData";
    std::string s2 = "MoreData";
    std::string combined = s1 + s2;

    buf.append(s1.c_str(), s1.length());
    EXPECT_EQ(buf.readableBytes(), s1.length());
    EXPECT_EQ(std::memcmp(buf.readPtr(), s1.c_str(), s1.length()), 0);

    buf.append(s2.c_str(), s2.length());
    EXPECT_EQ(buf.readableBytes(), combined.length());
    EXPECT_EQ(std::memcmp(buf.readPtr(), combined.c_str(), combined.length()), 0);

    // Test appending zero bytes
    buf.append("", 0);
    EXPECT_EQ(buf.readableBytes(), combined.length());
}

// Test ConsumeReadAllAsString
TEST_F(IoBufferTest, ConsumeReadAllAsString) {
    ganl::IoBuffer buf;
    std::string test_data = "This is a test string.";
    buf.append(test_data.c_str(), test_data.length());

    EXPECT_EQ(buf.readableBytes(), test_data.length());
    std::string consumed = buf.consumeReadAllAsString();

    EXPECT_EQ(consumed, test_data);
    EXPECT_EQ(buf.readableBytes(), 0);
    EXPECT_TRUE(buf.empty());
}

// Test Buffer Compaction
TEST_F(IoBufferTest, Compact) {
    ganl::IoBuffer buf(20);
    std::string s1 = "ConsumeThis"; // 11 bytes
    std::string s2 = "KeepThis";    // 8 bytes
    std::string combined = s1 + s2; // 19 bytes

    buf.append(combined.c_str(), combined.length());
    EXPECT_EQ(buf.readableBytes(), combined.length());
    EXPECT_EQ(buf.writableBytes(), 20 - combined.length());

    // Consume part of the data
    buf.consumeRead(s1.length());
    EXPECT_EQ(buf.readableBytes(), s2.length());
    // EXPECT_NE(buf.readPtr(), buf.writePtr() - s2.length()); // <-- REMOVE THIS LINE

    // Compact
    buf.compact();
    EXPECT_EQ(buf.readableBytes(), s2.length());
    // Now data should be at the start
    EXPECT_EQ(std::memcmp(buf.readPtr(), s2.c_str(), s2.length()), 0);
    // Check positions after compact
    EXPECT_NE(buf.writePtr(), buf.readPtr()); // writePos should be > readPos (which is 0)
    EXPECT_GT(buf.writableBytes(), 20 - combined.length()); // Should have more writable space (specifically 20 - 8 = 12)
    EXPECT_EQ(buf.writePtr(), buf.readPtr() + s2.length()); // writePos should be exactly s2.length()

    // Test compacting an empty buffer (after full consume)
    buf.consumeRead(s2.length());
    EXPECT_TRUE(buf.empty());
    buf.compact(); // Should just reset positions
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.readPtr(), buf.writePtr());
    EXPECT_EQ(buf.writableBytes(), 20);

    // Test compacting when readPos is already 0
    buf.clear();
    buf.append(s1.c_str(), s1.length());
    const char* initial_read_ptr = buf.readPtr();
    // Get internal read position before compacting (for verification)
    // Note: Accessing internals like this is generally discouraged in production code,
    // but acceptable in unit tests for detailed verification if necessary.
    // We'll rely on the public interface checks which should be sufficient.
    // size_t readPosBefore = /* internal access needed */;
    // EXPECT_EQ(readPosBefore, 0);

    buf.compact(); // Should be a no-op in terms of data movement
    EXPECT_EQ(buf.readableBytes(), s1.length());
    EXPECT_EQ(buf.readPtr(), initial_read_ptr); // Pointer shouldn't change if readPos was 0
    EXPECT_EQ(std::memcmp(buf.readPtr(), s1.c_str(), s1.length()), 0);
    // size_t readPosAfter = /* internal access needed */;
    // EXPECT_EQ(readPosAfter, 0); // readPos should still be 0
}

// Test ensureWritable causing compaction
TEST_F(IoBufferTest, EnsureWritableCompaction) {
    ganl::IoBuffer buf(20);
    std::string s1 = "0123456789"; // 10 bytes
    std::string s2 = "ABCDEFGHIJ"; // 10 bytes
    std::string s3 = "KLM";        // 3 bytes

    buf.append(s1.c_str(), s1.length());
    buf.append(s2.c_str(), s2.length()); // Buffer is now full (20 bytes readable)
    EXPECT_EQ(buf.readableBytes(), 20);
    EXPECT_EQ(buf.writableBytes(), 0);

    // Consume first 10 bytes
    buf.consumeRead(s1.length());
    EXPECT_EQ(buf.readableBytes(), 10);
    EXPECT_EQ(buf.writableBytes(), 0); // Still 0 writable at the end
    EXPECT_EQ(std::memcmp(buf.readPtr(), s2.c_str(), s2.length()), 0);

    // Need to write 3 bytes, should trigger compaction
    buf.ensureWritable(3);
    EXPECT_GE(buf.writableBytes(), 3); // Should have at least 3 bytes now
    EXPECT_EQ(buf.capacity(), 20);    // Should not have resized
    // Check that remaining data (s2) is still there and now at the beginning
    EXPECT_EQ(buf.readableBytes(), s2.length());
    EXPECT_EQ(std::memcmp(buf.readPtr(), s2.c_str(), s2.length()), 0);

    // Now append s3
    buf.append(s3.c_str(), s3.length());
    EXPECT_EQ(buf.readableBytes(), s2.length() + s3.length()); // 13 bytes
    EXPECT_EQ(std::memcmp(buf.readPtr() + s2.length(), s3.c_str(), s3.length()), 0);
}

// Test ensureWritable causing resize
TEST_F(IoBufferTest, EnsureWritableResize) {
    ganl::IoBuffer buf(10);
    std::string s1 = "0123456789"; // 10 bytes

    buf.append(s1.c_str(), s1.length()); // Fill the buffer
    EXPECT_EQ(buf.readableBytes(), 10);
    EXPECT_EQ(buf.writableBytes(), 0);
    EXPECT_EQ(buf.capacity(), 10);

    // Need 5 more bytes, no space to compact, must resize
    buf.ensureWritable(5);
    EXPECT_GE(buf.writableBytes(), 5);
    EXPECT_GT(buf.capacity(), 10); // Capacity must have increased
    EXPECT_EQ(buf.readableBytes(), 10); // Existing data still there
    EXPECT_EQ(std::memcmp(buf.readPtr(), s1.c_str(), s1.length()), 0);

    // Ensure it can resize again if needed
    size_t current_cap = buf.capacity();
    buf.ensureWritable(current_cap); // Ask for more space than currently writable
    EXPECT_GE(buf.writableBytes(), current_cap);
    EXPECT_GT(buf.capacity(), current_cap);
}

// Test ensureWritable causing resize after compaction attempt
TEST_F(IoBufferTest, EnsureWritableCompactThenResize) {
    ganl::IoBuffer buf(20);
    std::string s1 = "0123456789"; // 10 bytes
    std::string s2 = "ABCDEFGHIJ"; // 10 bytes

    buf.append(s1.c_str(), s1.length());
    buf.append(s2.c_str(), s2.length()); // Buffer full

    // Consume 5 bytes
    buf.consumeRead(5);
    EXPECT_EQ(buf.readableBytes(), 15);
    EXPECT_EQ(buf.writableBytes(), 0);

    // Need 10 bytes. Compacting frees 5 bytes (readPos), but that's not enough.
    // It should compact *and then* resize.
    buf.ensureWritable(10);
    EXPECT_GE(buf.writableBytes(), 10);
    EXPECT_GT(buf.capacity(), 20); // Must have resized
    EXPECT_EQ(buf.readableBytes(), 15); // Check data is preserved
    // Check remaining part of s1 and all of s2 are present at the start
    EXPECT_EQ(std::memcmp(buf.readPtr(), s1.c_str() + 5, 5), 0);
    EXPECT_EQ(std::memcmp(buf.readPtr() + 5, s2.c_str(), s2.length()), 0);
}


// Test Clear Operation
TEST_F(IoBufferTest, Clear) {
    ganl::IoBuffer buf;
    buf.append("Some data", 9);
    EXPECT_FALSE(buf.empty());

    buf.clear();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.readableBytes(), 0);
    EXPECT_EQ(buf.writableBytes(), buf.capacity());
    EXPECT_EQ(buf.readPtr(), buf.writePtr());
}

// Test Locking Behavior
TEST_F(IoBufferTest, Locking) {
    ganl::IoBuffer buf;
    buf.append("Lock Me", 7);

    buf.lockForReuse(true);
    EXPECT_TRUE(buf.isLockedForReuse());

    // Operations that modify state should throw
    EXPECT_THROW(buf.commitWrite(1), std::runtime_error);
    EXPECT_THROW(buf.ensureWritable(10), std::runtime_error);
    EXPECT_THROW(buf.compact(), std::runtime_error);
    EXPECT_THROW(buf.clear(), std::runtime_error);
    EXPECT_THROW(buf.append("x", 1), std::runtime_error);

    // Test move construction preserves lock state
    ganl::IoBuffer buf2(std::move(buf));
    EXPECT_TRUE(buf2.isLockedForReuse()); // State should be transferred
    EXPECT_EQ(buf2.readableBytes(), 7);
    // Source buffer `buf` should be reset and unlocked after move
    EXPECT_FALSE(buf.isLockedForReuse()); // buf is now unlocked
    EXPECT_TRUE(buf.empty());


    // Test moving back via assignment preserves lock state
    buf = std::move(buf2); // Move assignment
    EXPECT_TRUE(buf.isLockedForReuse()); // buf is locked again
    EXPECT_FALSE(buf2.isLockedForReuse());
    EXPECT_TRUE(buf2.empty());


    // Read operations should still work
    EXPECT_EQ(buf.readableBytes(), 7);
    EXPECT_NE(buf.readPtr(), nullptr);
    // writePtr() itself *also* throws if locked, according to implementation behavior
    EXPECT_THROW(buf.writePtr(), std::runtime_error); // <-- FIX 1: Expect writePtr to throw

    // Check consumeRead behavior when locked
    // Based on current implementation, consumeRead does NOT throw when locked.
    // If this changes, the test should change.
    EXPECT_NO_THROW(buf.consumeRead(1));
    EXPECT_EQ(buf.readableBytes(), 6); // Verify consumeRead worked


    // Reset buffer state for subsequent checks (Unlock *before* modifying)
    buf.lockForReuse(false); // <-- FIX 2: Unlock BEFORE clear
    EXPECT_FALSE(buf.isLockedForReuse());
    buf.clear();             // <-- FIX 2: Clear AFTER unlock
    EXPECT_TRUE(buf.empty());
    buf.append("Lock Me Again", 13); // Add some data back
    buf.lockForReuse(true);          // Lock it again
    EXPECT_TRUE(buf.isLockedForReuse());
    EXPECT_EQ(buf.readableBytes(), 13);

    // Final unlock
    buf.lockForReuse(false);
    EXPECT_FALSE(buf.isLockedForReuse());

    // Operations should work again after final unlock
    EXPECT_NO_THROW(buf.append("!", 1));
    EXPECT_EQ(buf.readableBytes(), 14);
    EXPECT_NO_THROW(buf.consumeRead(14));
    EXPECT_TRUE(buf.empty());
    EXPECT_NO_THROW(buf.ensureWritable(100));
    EXPECT_NO_THROW(buf.clear());
}

// Test Move Semantics
TEST_F(IoBufferTest, MoveSemantics) {
    ganl::IoBuffer buf1(100);
    std::string data = "Move test data";
    buf1.append(data.c_str(), data.length());
    buf1.consumeRead(5); // Change readPos

    size_t readable = buf1.readableBytes();
    size_t writable = buf1.writableBytes();
    size_t capacity = buf1.capacity();
    const char* readPtr = buf1.readPtr();

    // Move Construct
    ganl::IoBuffer buf2(std::move(buf1));

    // Check buf2 state
    EXPECT_EQ(buf2.readableBytes(), readable);
    EXPECT_EQ(buf2.writableBytes(), writable);
    EXPECT_EQ(buf2.capacity(), capacity);
    EXPECT_EQ(std::memcmp(buf2.readPtr(), readPtr, readable), 0);

    // Check buf1 state (should be empty and valid)
    EXPECT_TRUE(buf1.empty()); // Implementation specific, but usually moved-from is empty
    EXPECT_EQ(buf1.readableBytes(), 0);
    // Capacity might remain or be 0, depends on std::vector move
    // EXPECT_EQ(buf1.capacity(), 0); // Don't rely on capacity being zero

    // Move Assign
    ganl::IoBuffer buf3(50);
    buf3.append("Initial buf3 data", 17);
    buf3 = std::move(buf2);

    // Check buf3 state
    EXPECT_EQ(buf3.readableBytes(), readable);
    EXPECT_EQ(buf3.writableBytes(), writable);
    EXPECT_EQ(buf3.capacity(), capacity);
    EXPECT_EQ(std::memcmp(buf3.readPtr(), readPtr, readable), 0);

    // Check buf2 state (should be empty and valid)
    EXPECT_TRUE(buf2.empty());
    EXPECT_EQ(buf2.readableBytes(), 0);
}

// Test Error Handling
TEST_F(IoBufferTest, ErrorHandling) {
    ganl::IoBuffer buf(10);

    // Consume more than readable
    EXPECT_THROW(buf.consumeRead(1), std::out_of_range);

    // Commit more than writable
    EXPECT_THROW(buf.commitWrite(11), std::out_of_range);

    // Write exactly to capacity, then try to commit more
    buf.commitWrite(10);
    EXPECT_EQ(buf.writableBytes(), 0);
    EXPECT_THROW(buf.commitWrite(1), std::out_of_range);

    // Consume some, then try to consume too much
    buf.consumeRead(5);
    EXPECT_EQ(buf.readableBytes(), 5);
    EXPECT_THROW(buf.consumeRead(6), std::out_of_range);
}

// Test Edge Case: Buffer exactly full
TEST_F(IoBufferTest, FullBuffer) {
    ganl::IoBuffer buf(10);
    const char* data = "0123456789"; // Exactly 10 bytes

    buf.append(data, 10);
    EXPECT_EQ(buf.readableBytes(), 10);
    EXPECT_EQ(buf.writableBytes(), 0);
    EXPECT_EQ(buf.capacity(), 10);
    EXPECT_EQ(std::memcmp(buf.readPtr(), data, 10), 0);

    // EnsureWritable(0) should do nothing
    buf.ensureWritable(0);
    EXPECT_EQ(buf.writableBytes(), 0);
    EXPECT_EQ(buf.capacity(), 10);

    // ensureWritable(1) should resize
    buf.ensureWritable(1);
    EXPECT_GE(buf.writableBytes(), 1);
    EXPECT_GT(buf.capacity(), 10);
    EXPECT_EQ(buf.readableBytes(), 10); // Check data still there
    EXPECT_EQ(std::memcmp(buf.readPtr(), data, 10), 0);
}

// Test dumping functions (basic check for no crash)
TEST_F(IoBufferTest, DumpFunctions) {
    ganl::IoBuffer buf;
    // Input: H  e  l  l  o \x01 \x02 \xFF  W  o  r  l  d  (13 bytes total, mistake in previous analysis)
    // Let's stick to the original test string for consistency:
    // "Hello\x01\x02\xFFWorld" = 12 bytes.
    // H e l l o \x01 = 6 bytes
    // \x02 \xFF W o r l d = 6 bytes
    std::string original_data = "Hello\x01\x02\xFFWorld";
    buf.append(original_data.c_str(), original_data.length()); // Append 12 bytes
    buf.consumeRead(6); // Consume "Hello\x01", leaving "\x02\xFFWorld" (6 bytes) readable

    std::string dumpStr;
    EXPECT_NO_THROW(dumpStr = ganl::utils::dumpIoBufferHexString(buf, 0)); // Dump all 6 readable bytes
    EXPECT_FALSE(dumpStr.empty());

    // Expected Hex for \x02 \xFF W o r l : "02 ff 57 6f 72 6c "
    std::string expected_hex = "02 ff 57 6f 72 6c "; // <-- FIX: Correct expected hex sequence
    EXPECT_NE(dumpStr.find(expected_hex), std::string::npos)
        << "Expected Hex not found. Dump was:\n" << dumpStr;

    // Expected ASCII for \x02 \xFF W o r l : "..Worl"
    std::string expected_ascii = "..Worl"; // <-- FIX: Correct expected ASCII sequence
    // Check within the ASCII part, usually after " | "
    size_t separator_pos = dumpStr.find(" | ");
    EXPECT_NE(separator_pos, std::string::npos) << "Separator ' | ' not found. Dump was:\n" << dumpStr;
    if (separator_pos != std::string::npos) {
        EXPECT_NE(dumpStr.find(expected_ascii, separator_pos), std::string::npos)
            << "Expected ASCII not found after separator. Dump was:\n" << dumpStr;
    }


    std::string dumpStrLimited;
     // Dump only 3 bytes: \x02 \xFF W
     EXPECT_NO_THROW(dumpStrLimited = ganl::utils::dumpIoBufferHexString(buf, 3));
     EXPECT_FALSE(dumpStrLimited.empty());
     // Expected Hex for \x02 \xFF W : "02 ff 57 "
     std::string expected_hex_lim = "02 ff 57 "; // <-- FIX: Correct limited hex
     EXPECT_NE(dumpStrLimited.find(expected_hex_lim), std::string::npos)
        << "Expected limited Hex not found. Dump was:\n" << dumpStrLimited;
     // Expected ASCII for \x02 \xFF W : "..W"
     std::string expected_ascii_lim = "..W"; // <-- FIX: Correct limited ASCII
     size_t separator_pos_lim = dumpStrLimited.find(" | ");
     EXPECT_NE(separator_pos_lim, std::string::npos) << "Separator ' | ' not found in limited dump. Dump was:\n" << dumpStrLimited;
     if (separator_pos_lim != std::string::npos) {
        // Be careful: the ASCII part might be shorter than the expected string if bytesPerRow > dumpLength
        // Let's just check if the expected ASCII starts the ASCII section
        EXPECT_EQ(dumpStrLimited.substr(separator_pos_lim + 3, expected_ascii_lim.length()), expected_ascii_lim)
            << "Expected limited ASCII not found starting after separator. Dump was:\n" << dumpStrLimited;
     }
     EXPECT_NE(dumpStrLimited.find("more bytes not shown"), std::string::npos); // Check truncation message

     std::ostringstream oss;
     EXPECT_NO_THROW(ganl::utils::dumpIoBufferHex(oss, buf, 0));
     EXPECT_FALSE(oss.str().empty());

     // Test dump on empty buffer
     buf.clear();
     EXPECT_NO_THROW(dumpStr = ganl::utils::dumpIoBufferHexString(buf));
     EXPECT_NE(dumpStr.find("<Buffer Empty>"), std::string::npos) // <-- FIX: Check for empty message
        << "Expected empty message not found. Dump was:\n" << dumpStr;
}
