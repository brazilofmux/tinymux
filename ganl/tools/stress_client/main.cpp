#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <fstream>
#include <iomanip>
#include <queue> // Still used, but differently
#include <map>   // For correlation
#include <condition_variable>
#include <algorithm>
#include <csignal>
#include <functional>
#include <sstream>
#include <system_error> // For std::error_code
#include <optional>

// Platform-specific includes (Focus on POSIX)
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <cerrno> // Use <cerrno> for errno

typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#define SOCKET_ERROR_VAL (-1)

// --- GlobalStats, ClientConfig, Message structs remain the same ---
// Global statistics
struct GlobalStats {
    std::atomic<int> activeConnections{0};
    std::atomic<int> totalConnections{0};
    std::atomic<int> connectionFailures{0};
    std::atomic<int> messagesReceived{0}; // Now accurately correlated
    std::atomic<int> messagesSent{0};
    std::atomic<int> bytesReceived{0};
    std::atomic<int> bytesSent{0};
    std::atomic<int> errors{0}; // General send/recv/validation errors
    std::atomic<int> telnetCommandsIgnored{0};
    std::atomic<int> asyncMessagesIgnored{0};
    std::atomic<int> unmatchedResponses{0};

    // Timing statistics (in microseconds)
    std::atomic<uint64_t> totalResponseTime{0}; // For calculating average latency

    // High-resolution wall clock for test duration
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    void reset() {
        activeConnections = 0;
        totalConnections = 0;
        connectionFailures = 0;
        messagesReceived = 0;
        messagesSent = 0;
        bytesReceived = 0;
        bytesSent = 0;
        errors = 0;
        telnetCommandsIgnored = 0;
        asyncMessagesIgnored = 0;
        unmatchedResponses = 0;
        totalResponseTime = 0;
        startTime = std::chrono::steady_clock::now();
    }

    double getAverageLatencyMs() const {
        long long receivedCount = messagesReceived.load();
        if (receivedCount == 0) return 0.0;
        // Use floating-point division
        return (static_cast<double>(totalResponseTime.load()) / static_cast<double>(receivedCount)) / 1000.0;
    }


    double getTestDurationSec() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = now - startTime;
        return std::chrono::duration<double>(duration).count();
    }
};

// Client configuration
struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 4000;
    bool useTls = false; // Placeholder
    std::string tlsCertFile; // Placeholder
    std::string messagePattern = "random"; // "random", "fixed", "incremental", "file"
    std::string messageFile;
    std::string fixedMessage = "Test message from stress client";
    int messageMinLength = 10;
    int messageMaxLength = 100;
    int delayMinMs = 100;
    int delayMaxMs = 1000;
    int connectTimeoutMs = 5000;
    int responseTimeoutMs = 10000;
    bool validateResponse = true;   // Verify server echoes the exact message
    int reconnectAttemptsMax = 3;   // Maximum reconnection attempts on failure
    int reconnectDelayMs = 1000;    // Delay between reconnection attempts
};

// Message with metadata for tracking correlation
struct Message {
    std::string data; // Includes CRLF
    std::string payload; // Data part only, for validation
    std::string id; // Unique ID, e.g., "Client1-Msg5"
    std::chrono::time_point<std::chrono::steady_clock> sendTime;

    Message(const std::string& messageData, const std::string& messageId)
        : data(messageData + "\r\n"), id(messageId)
    {
        // Extract payload (assuming format "ID: Payload")
        size_t colonPos = messageData.find(':');
        if (colonPos != std::string::npos && colonPos + 2 < messageData.length()) {
            payload = messageData.substr(colonPos + 2); // Skip ": "
        } else {
            payload = messageData; // Fallback if format is unexpected
        }
        sendTime = std::chrono::steady_clock::now();
    }
};


// --- SocketUtils Class (POSIX focus) ---
class SocketUtils {
public:
    static bool initialize() { return true; } // No-op for POSIX
    static void cleanup() {} // No-op for POSIX

    static void closeSocket(socket_t sock) {
        if (sock != INVALID_SOCKET_VALUE) {
            close(sock);
        }
    }

    static bool setNonBlocking(socket_t sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) return false;
        return (fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1);
    }

    static bool wouldBlock() {
        return (errno == EAGAIN || errno == EWOULDBLOCK);
    }

    static int getLastError() {
        return errno;
    }

    static std::string getErrorString(int errorCode) {
        return strerror(errorCode);
    }

    // Uses poll to check readiness
    static bool pollSocket(socket_t sock, short events, int timeoutMs) {
        if (sock == INVALID_SOCKET_VALUE) return false;
        struct pollfd fds;
        fds.fd = sock;
        fds.events = events;
        fds.revents = 0;

        int result = poll(&fds, 1, timeoutMs);

        if (result < 0) { // Error
            // Optionally log error: std::cerr << "poll error: " << strerror(errno) << std::endl;
            return false;
        }
        if (result == 0) { // Timeout
            return false;
        }
        // Check for the specific events OR error conditions
        return (fds.revents & (events | POLLERR | POLLHUP | POLLNVAL));
    }

    static bool canRead(socket_t sock, int timeoutMs = 0) {
        return pollSocket(sock, POLLIN, timeoutMs);
    }

    static bool canWrite(socket_t sock, int timeoutMs = 0) {
        return pollSocket(sock, POLLOUT, timeoutMs);
    }
};

// Global variables
GlobalStats gStats;
std::atomic<bool> shutdownRequested(false);
std::mutex printMutex; // Protects std::cout/cerr

// Forward declaration
void signalHandler(int signal);

// --- printStats function remains largely the same ---
//     (maybe add telnet/async/unmatched counts)
void printStats(bool final = false) {
    std::lock_guard<std::mutex> lock(printMutex);

    double duration = gStats.getTestDurationSec();
    double avgLatency = gStats.getAverageLatencyMs();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "================== Stress Client Stats ==================" << std::endl;
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << "Active connections: " << gStats.activeConnections
              << " / Total: " << gStats.totalConnections
              << " / Failed: " << gStats.connectionFailures << std::endl;
    std::cout << "Messages sent: " << gStats.messagesSent
              << " / correlated: " << gStats.messagesReceived << std::endl;
    std::cout << "Errors: Send/Recv/Validation=" << gStats.errors
              << " / UnmatchedResp=" << gStats.unmatchedResponses << std::endl;
    std::cout << "Ignored: Telnet=" << gStats.telnetCommandsIgnored
              << " / AsyncMsgs=" << gStats.asyncMessagesIgnored << std::endl;

    if (duration > 0 && gStats.messagesSent > 0) {
        std::cout << "Messages/sec (sent): " << static_cast<double>(gStats.messagesSent) / duration << std::endl;
    }
    if (duration > 0 && gStats.messagesReceived > 0) {
        std::cout << "Messages/sec (recv): " << static_cast<double>(gStats.messagesReceived) / duration << std::endl;
    }


    std::cout << "Bytes sent: " << gStats.bytesSent
              << " / received: " << gStats.bytesReceived << std::endl;

    if (duration > 0) {
        double kbps_sent = (static_cast<double>(gStats.bytesSent) * 8.0 / 1000.0) / duration;
        double kbps_recv = (static_cast<double>(gStats.bytesReceived) * 8.0 / 1000.0) / duration;
        std::cout << "Throughput: " << kbps_sent << " kbps (send) / "
                  << kbps_recv << " kbps (receive)" << std::endl;
    }

    std::cout << "Average response time: " << avgLatency << " ms" << std::endl;

    if (final) {
        std::cout << "=================== Test Complete =====================" << std::endl;
    } else {
        std::cout << "=======================================================" << std::endl;
    }
}


// --- generateMessage function remains the same ---
std::string generateMessage(const ClientConfig& config, int clientId, int messageId) {
    // Base message with client and message IDs for tracking
    std::string baseMsg = "Client" + std::to_string(clientId) + "-Msg" + std::to_string(messageId); // No colon here

    std::string payload;
    if (config.messagePattern == "fixed") {
        payload = config.fixedMessage;
    }
    else if (config.messagePattern == "incremental") {
        // Create a message with incrementing sequence
        for (int i = 0; i < config.messageMinLength; i++) {
            payload += static_cast<char>('A' + (i % 26));
        }
    }
    else if (config.messagePattern == "file" && !config.messageFile.empty()) {
        // Not implemented in this version - would read messages from file
        payload = config.fixedMessage;
    }
    else { // "random" (default)
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> msgLength(config.messageMinLength, config.messageMaxLength);
        std::uniform_int_distribution<> charIdx(0, 61); // a-z, A-Z, 0-9

        const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        int length = msgLength(gen);

        payload.reserve(length);
        for (int i = 0; i < length; i++) {
            payload += charset[charIdx(gen)];
        }
    }

    return baseMsg + ": " + payload; // Add colon separator
}

// --- Telnet constants ---
namespace Telnet {
    const unsigned char IAC  = 255; // Interpret As Command
    const unsigned char DONT = 254;
    const unsigned char DO   = 253;
    const unsigned char WONT = 252;
    const unsigned char WILL = 251;
    const unsigned char SB   = 250; // Subnegotiation Begin
    const unsigned char SE   = 240; // Subnegotiation End
    // ... add others if needed (NOP, DM, BRK, IP, AO, AYT, EC, EL, GA)
}

// --- Input Processor Helper ---
// Parses the received data buffer, handles Telnet, filters async, correlates responses.
void processReceivedData(
    int clientId,
    const ClientConfig& config,
    std::string& receivedData,
    std::map<std::string, Message>& pendingMessages)
{
    bool processedSomething = true;
    while (processedSomething && !receivedData.empty() && !shutdownRequested) {
        processedSomething = false; // Assume nothing processed in this pass

        // 1. Handle Telnet IAC sequences
        size_t iacPos = receivedData.find(static_cast<char>(Telnet::IAC));
        if (iacPos == 0) { // IAC at the beginning of the buffer
            if (receivedData.length() < 2) return; // Need at least IAC + command

            unsigned char command = static_cast<unsigned char>(receivedData[1]);
            size_t consumedLength = 0;

            switch (command) {
                case Telnet::WILL:
                case Telnet::WONT:
                case Telnet::DO:
                case Telnet::DONT:
                    if (receivedData.length() < 3) return; // Need IAC + command + option
                    consumedLength = 3;
                    // Optional: Log the command and option
                    // { std::lock_guard<std::mutex> lock(printMutex);
                    //   std::cout << "Client " << clientId << " RCV TELNET: IAC " << (int)command << " " << (int)(unsigned char)receivedData[2] << std::endl; }
                    gStats.telnetCommandsIgnored++;
                    break;

                case Telnet::SB: // Subnegotiation
                    {
                        size_t iacSePos = receivedData.find({static_cast<char>(Telnet::IAC), static_cast<char>(Telnet::SE)});
                        if (iacSePos == std::string::npos) return; // Need complete SB...SE sequence
                        consumedLength = iacSePos + 2;
                        // Optional: Log subnegotiation content
                        gStats.telnetCommandsIgnored++;
                    }
                    break;

                case Telnet::IAC: // Escaped IAC (255 255) -> treat as single 255 data byte
                    // This case is tricky. If we treat it as data, it might interfere
                    // with line parsing. For a stress client, maybe just skip it?
                    // Or replace "IAC IAC" with "IAC" in the buffer?
                    // Simplest for now: Skip the escaped IAC if ignoring Telnet data.
                    if (receivedData.length() >= 2 && static_cast<unsigned char>(receivedData[1]) == Telnet::IAC) {
                         consumedLength = 1; // Consume only the first IAC, leaving the second as data (or just consume both?) Let's consume both to ignore it.
                         consumedLength = 2;
                         gStats.telnetCommandsIgnored++; // Count it
                    } else {
                         return; // Incomplete sequence
                    }
                    break;

                // Handle other single-byte IAC commands if necessary (NOP, etc.)
                case Telnet::SE: // SE should only appear after SB
                     // Log error or just ignore? Ignore for robustness.
                     consumedLength = 2;
                     gStats.telnetCommandsIgnored++;
                     break;

                default: // Simple command (NOP, AYT, etc.) or potentially invalid
                    consumedLength = 2;
                    gStats.telnetCommandsIgnored++;
                    break;
            }

            if (consumedLength > 0 && receivedData.length() >= consumedLength) {
                 receivedData.erase(0, consumedLength);
                 processedSomething = true;
                 continue; // Restart processing loop from the beginning of the modified buffer
            } else {
                 return; // Need more data for the Telnet sequence
            }
        } // End Telnet Handling (if iacPos == 0)

        // If IAC wasn't at pos 0, continue to other processing

        // 2. Filter known Async Server Messages (Example: lines starting with "***")
        if (receivedData.rfind("***", 0) == 0) { // Check prefix
            size_t lineEnd = receivedData.find("\r\n");
            if (lineEnd != std::string::npos) {
                // Optional: Log ignored message
                // { std::lock_guard<std::mutex> lock(printMutex);
                //   std::cout << "Client " << clientId << " IGN ASYNC: " << receivedData.substr(0, lineEnd) << std::endl; }
                receivedData.erase(0, lineEnd + 2);
                gStats.asyncMessagesIgnored++;
                processedSomething = true;
                continue; // Restart processing
            } else {
                 // Partial async message line? Wait for more data.
                 // If buffer gets huge, might need a limit.
                 if (receivedData.length() > 8192) { // Example limit
                      std::lock_guard<std::mutex> lock(printMutex);
                      std::cerr << "Client " << clientId << " WARN: Large partial async message detected, clearing buffer." << std::endl;
                      receivedData.clear();
                      gStats.errors++;
                 }
                 return; // Need more data
            }
        } // End Async Filtering

        // 3. Look for potential Response Lines (\r\n terminated)
        size_t lineEnd = receivedData.find("\r\n");
        if (lineEnd != std::string::npos) {
            std::string line = receivedData.substr(0, lineEnd);
            receivedData.erase(0, lineEnd + 2); // Consume the line + CRLF
            processedSomething = true;

            // Attempt to correlate and validate
            // Expected format in 'line': OptionalPrefix ClientX-MsgY: Payload
            size_t idStart = line.find("Client" + std::to_string(clientId) + "-Msg");
            if (idStart == std::string::npos) {
                 // Didn't find the expected client/msg marker for *this* client
                 // Could be echo from another client, or other server noise
                 // Optional: Log unmatched line
                 // { std::lock_guard<std::mutex> lock(printMutex);
                 //   std::cout << "Client " << clientId << " RCV UNMATCHED: " << line << std::endl; }
                 gStats.unmatchedResponses++;
                 continue; // Process next potential part of buffer
            }

            size_t colonPos = line.find(':', idStart);
            if (colonPos == std::string::npos) {
                 // Found marker but no colon? Malformed response?
                 gStats.unmatchedResponses++;
                 continue;
            }

            std::string receivedId = line.substr(idStart, colonPos - idStart);
            std::string receivedPayload;
            if (colonPos + 2 < line.length()) { // Skip ": "
                 receivedPayload = line.substr(colonPos + 2);
            }

            // Trim trailing '\r' from received payload if present
            if (!receivedPayload.empty() && receivedPayload.back() == '\r') {
                receivedPayload.pop_back();
            }

            // Look up in pending map
            auto it = pendingMessages.find(receivedId);
            if (it != pendingMessages.end()) {
                const Message& sentMsg = it->second;

                bool validated = false;
                if (config.validateResponse) {
                    if (receivedPayload == sentMsg.payload) {
                        validated = true;
                    } else {
                         // Validation failed
                         gStats.errors++;
                         { std::lock_guard<std::mutex> lock(printMutex);
                           std::cerr << "Client " << clientId << ": Payload mismatch for " << receivedId
                                     << ".\n  Expected: '" << sentMsg.payload
                                     << "'\n  Got:      '" << receivedPayload << "'" << std::endl;
                         }
                    }
                } else {
                     validated = true; // Validation disabled
                }

                if (validated) {
                     auto now = std::chrono::steady_clock::now();
                     auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - sentMsg.sendTime).count();
                     if (latency < 0) latency = 0; // Clock skew sanity check
                     gStats.totalResponseTime += latency;
                     gStats.messagesReceived++;
                }

                // Remove from map whether validated or not (we processed this ID)
                pendingMessages.erase(it);

            } else {
                 // ID found in line, but not in our pending map (duplicate response? late response?)
                 gStats.unmatchedResponses++;
                 { std::lock_guard<std::mutex> lock(printMutex);
                   std::cerr << "Client " << clientId << ": Received response for ID " << receivedId
                             << " which was not pending." << std::endl;
                 }
            }

            continue; // Restart processing loop for potentially more data
        } // End Response Line Parsing

        // If we reached here, no complete Telnet, Async, or Response line was found
        // Check for buffer getting too large without progress
        if (!processedSomething && receivedData.length() > 8192) {
             std::lock_guard<std::mutex> lock(printMutex);
             std::cerr << "Client " << clientId << " WARN: Receive buffer > 8KB without processing line/command, clearing." << std::endl;
             receivedData.clear();
             gStats.errors++;
        }

        break; // Exit processing loop, wait for more recv data
    } // end while(processedSomething)
}


// --- REWRITTEN clientThread Function ---
void clientThread(const ClientConfig& config, int clientId, int messageCount) {
    // Random generator for delays
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> msgDelay(config.delayMinMs, config.delayMaxMs);

    // Tracking sent messages for response correlation
    std::map<std::string, Message> pendingMessages;
    std::string receivedData; // Persistent buffer for received bytes

    int messagesSentCount = 0; // Separate from messageId used in message content
    int nextMessageId = 1;
    int reconnectAttempts = 0;
    bool connected = false;
    socket_t sock = INVALID_SOCKET_VALUE;

    auto connectToServer = [&]() -> bool {
        if (sock != INVALID_SOCKET_VALUE) { SocketUtils::closeSocket(sock); } // Ensure clean state

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET_VALUE) {
             { std::lock_guard<std::mutex> lock(printMutex);
               std::cerr << "Client " << clientId << ": Failed create socket: " << SocketUtils::getErrorString(SocketUtils::getLastError()) << std::endl; }
             return false;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) <= 0) {
             { std::lock_guard<std::mutex> lock(printMutex);
               std::cerr << "Client " << clientId << ": Invalid address: " << config.host << std::endl; }
             SocketUtils::closeSocket(sock); sock = INVALID_SOCKET_VALUE; return false;
        }

        if (!SocketUtils::setNonBlocking(sock)) {
             { std::lock_guard<std::mutex> lock(printMutex);
               std::cerr << "Client " << clientId << ": Failed set non-blocking" << std::endl; }
             SocketUtils::closeSocket(sock); sock = INVALID_SOCKET_VALUE; return false;
        }

        int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        if (result == SOCKET_ERROR_VAL && errno != EINPROGRESS) {
            { std::lock_guard<std::mutex> lock(printMutex);
              std::cerr << "Client " << clientId << ": Connect failed immediately: " << SocketUtils::getErrorString(errno) << std::endl; }
            SocketUtils::closeSocket(sock); sock = INVALID_SOCKET_VALUE; return false;
        }

        if (result == 0) { // Connected immediately (rare but possible)
             gStats.activeConnections++; gStats.totalConnections++; return true;
        }

        // Wait for connection using poll (equivalent to canWrite)
        if (!SocketUtils::pollSocket(sock, POLLOUT, config.connectTimeoutMs)) {
            { std::lock_guard<std::mutex> lock(printMutex);
              std::cerr << "Client " << clientId << ": Connection timed out (" << SocketUtils::getLastError() << ")" << std::endl; }
            SocketUtils::closeSocket(sock); sock = INVALID_SOCKET_VALUE; return false;
        }

        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            { std::lock_guard<std::mutex> lock(printMutex);
              std::cerr << "Client " << clientId << ": Connect failed: " << SocketUtils::getErrorString(error) << std::endl; }
            SocketUtils::closeSocket(sock); sock = INVALID_SOCKET_VALUE; return false;
        }

        gStats.activeConnections++; gStats.totalConnections++;
        return true;
    };

    auto cleanup = [&]() {
        if (sock != INVALID_SOCKET_VALUE) {
            SocketUtils::closeSocket(sock);
            sock = INVALID_SOCKET_VALUE;
            if (connected) {
                gStats.activeConnections--;
                connected = false;
            }
        }
    };

    // --- Main Client Loop ---
    while (messagesSentCount < messageCount && !shutdownRequested) {

        // 1. Connect or Reconnect if necessary
        if (!connected) {
             if (reconnectAttempts >= config.reconnectAttemptsMax) break; // Give up
             reconnectAttempts++;
             std::this_thread::sleep_for(std::chrono::milliseconds(config.reconnectDelayMs));
             connected = connectToServer();
             if (!connected) { gStats.connectionFailures++; continue; }
             reconnectAttempts = 0; // Reset on success
        }

        // 2. Try to Send a Message
        if (connected && messagesSentCount < messageCount) {
             std::string messageBase = generateMessage(config, clientId, nextMessageId);
             std::string messageId = messageBase.substr(0, messageBase.find(':'));
             Message msg(messageBase, messageId);

             const char* ptr = msg.data.c_str();
             size_t remaining = msg.data.length();
             bool sendErrorOccurred = false;

             while (remaining > 0 && !shutdownRequested) {
                 ssize_t sent = send(sock, ptr, remaining, MSG_NOSIGNAL); // MSG_NOSIGNAL to prevent SIGPIPE

                 if (sent > 0) {
                     ptr += sent;
                     remaining -= sent;
                     gStats.bytesSent += sent;
                 } else if (sent == SOCKET_ERROR_VAL) {
                     if (SocketUtils::wouldBlock()) {
                          // Wait briefly until writable
                          if (!SocketUtils::pollSocket(sock, POLLOUT, 100)) { // Wait 100ms
                               if (shutdownRequested) break;
                               // Check connection state after wait? Maybe redundant.
                               continue; // Still blocked or timeout, retry poll/send
                          }
                          // Socket became writable, loop back to send
                     } else {
                          // Real send error
                          { std::lock_guard<std::mutex> lock(printMutex);
                            std::cerr << "Client " << clientId << ": Send error: " << SocketUtils::getErrorString(errno) << std::endl; }
                          gStats.errors++; cleanup(); sendErrorOccurred = true; break;
                     }
                 } else if (sent == 0) { // Should not happen for blocking TCP? Treat as error.
                     { std::lock_guard<std::mutex> lock(printMutex);
                       std::cerr << "Client " << clientId << ": Send returned 0." << std::endl; }
                     gStats.errors++; cleanup(); sendErrorOccurred = true; break;
                 }
             } // end while(remaining > 0) for send

             if (sendErrorOccurred) continue; // Try reconnecting in next outer loop iteration
             if (shutdownRequested) break;    // Exit outer loop

             // If send completed successfully
             if (remaining == 0) {
                  pendingMessages.emplace(msg.id, std::move(msg));
                  gStats.messagesSent++;
                  messagesSentCount++;
                  nextMessageId++;
             } else {
                 // Shutdown happened mid-send
                 { std::lock_guard<std::mutex> lock(printMutex);
                   std::cerr << "Client " << clientId << ": Shutdown mid-send for " << messageId << std::endl; }
             }
        } // End Send block

        // 3. Try to Receive and Process Data
        if (connected) {
            // Use poll for non-blocking check
            if (SocketUtils::pollSocket(sock, POLLIN, 0)) { // 0 timeout = check immediately
                 char buffer[4096];
                 ssize_t bytes = recv(sock, buffer, sizeof(buffer), 0);

                 if (bytes > 0) {
                     receivedData.append(buffer, bytes);
                     gStats.bytesReceived += bytes;
                     // Call the processor function
                     processReceivedData(clientId, config, receivedData, pendingMessages);
                 } else if (bytes == 0) {
                      // Connection closed by peer
                      { std::lock_guard<std::mutex> lock(printMutex);
                        std::cerr << "Client " << clientId << ": Connection closed by server." << std::endl; }
                      cleanup(); // Sets connected = false
                 } else if (bytes == SOCKET_ERROR_VAL) {
                      if (!SocketUtils::wouldBlock()) {
                          // Real receive error
                          { std::lock_guard<std::mutex> lock(printMutex);
                            std::cerr << "Client " << clientId << ": Recv error: " << SocketUtils::getErrorString(errno) << std::endl; }
                          gStats.errors++; cleanup();
                      }
                      // else: Would block (shouldn't happen after poll success, but handle defensively)
                 }
            }
            // else: poll indicated no data or error (error logged by pollSocket implicitly)
        } // End Receive block

        // 4. Optional Delay
        if (connected && config.delayMaxMs > 0) {
             std::this_thread::sleep_for(std::chrono::milliseconds(msgDelay(gen)));
        } else if (!connected) {
            // Add a small delay even if disconnected to prevent busy-looping on reconnect attempts
             std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } // --- End of main while loop ---

    // --- Drain remaining responses ---
    if (connected && !pendingMessages.empty() && !shutdownRequested) {
        { std::lock_guard<std::mutex> lock(printMutex);
          std::cout << "Client " << clientId << ": Sent all messages. Waiting "
                    << config.responseTimeoutMs << "ms for remaining "
                    << pendingMessages.size() << " responses..." << std::endl;
        }
        auto drainEndTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.responseTimeoutMs);

        while (!pendingMessages.empty() && connected && !shutdownRequested && std::chrono::steady_clock::now() < drainEndTime) {
             // Use poll with a timeout
             if (SocketUtils::pollSocket(sock, POLLIN, 100)) { // Check for 100ms
                 char buffer[4096];
                 ssize_t bytes = recv(sock, buffer, sizeof(buffer), 0);
                 if (bytes > 0) {
                     receivedData.append(buffer, bytes);
                     gStats.bytesReceived += bytes;
                     processReceivedData(clientId, config, receivedData, pendingMessages);
                 } else if (bytes == 0) {
                      { std::lock_guard<std::mutex> lock(printMutex);
                        std::cerr << "Client " << clientId << ": Connection closed by server during drain." << std::endl; }
                      connected = false; break;
                 } else if (bytes == SOCKET_ERROR_VAL) {
                      if (!SocketUtils::wouldBlock()) {
                          { std::lock_guard<std::mutex> lock(printMutex);
                            std::cerr << "Client " << clientId << ": Recv error during drain: " << SocketUtils::getErrorString(errno) << std::endl; }
                          gStats.errors++; connected = false; break;
                      }
                      // else: Would block (ignore, let loop timeout or receive data)
                 }
            }
            // If poll timed out or returned error, loop continues until drainEndTime
            // Add a tiny sleep to prevent pure busy-wait if poll keeps timing out with no data
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!pendingMessages.empty() && !shutdownRequested) {
             { std::lock_guard<std::mutex> lock(printMutex);
               std::cerr << "Client " << clientId << ": Timed out waiting for "
                         << pendingMessages.size() << " responses." << std::endl; }
             gStats.errors += pendingMessages.size(); // Count timed-out messages as errors
        } else if (!pendingMessages.empty() && shutdownRequested) {
             { std::lock_guard<std::mutex> lock(printMutex);
               std::cerr << "Client " << clientId << ": Shutdown requested with "
                         << pendingMessages.size() << " responses still pending." << std::endl; }
        }
    } // End drain block

    cleanup(); // Final cleanup
     { std::lock_guard<std::mutex> lock(printMutex);
       std::cout << "Client " << clientId << ": Thread finished." << std::endl; }
}


// --- signalHandler, printUsage, main functions remain largely the same ---
//     (Adjust main to use the POSIX-focused SocketUtils)

// Signal handler implementation
void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down..." << std::endl;
    shutdownRequested = true;
}

// Print usage information
void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --host HOST            Connect to HOST (default: 127.0.0.1)" << std::endl;
    std::cout << "  --port PORT            Connect to port PORT (default: 4000)" << std::endl;
    std::cout << "  --clients N            Start N clients (default: 10)" << std::endl;
    std::cout << "  --messages N           Send N messages per client (default: 100)" << std::endl;
    // std::cout << "  --tls                  Use TLS for connections (Not Implemented)" << std::endl;
    // std::cout << "  --cert FILE            TLS certificate file (Not Implemented)" << std::endl;
    std::cout << "  --pattern TYPE         Message pattern: random, fixed, incremental, file (default: random)" << std::endl;
    std::cout << "  --message TEXT         Fixed message text when using --pattern=fixed" << std::endl;
    std::cout << "  --message-file FILE    Message file when using --pattern=file" << std::endl;
    std::cout << "  --min-length N         Minimum message length for random pattern (default: 10)" << std::endl;
    std::cout << "  --max-length N         Maximum message length for random pattern (default: 100)" << std::endl;
    std::cout << "  --min-delay N          Minimum delay between messages in ms (default: 100)" << std::endl;
    std::cout << "  --max-delay N          Maximum delay between messages in ms (default: 1000)" << std::endl;
    std::cout << "  --ramp-up N            Ramp-up time in seconds (gradually add clients, default: 0)" << std::endl;
    std::cout << "  --duration N           Test duration in seconds (default: until all messages sent)" << std::endl;
    std::cout << "  --no-validate          Don't validate server response payloads" << std::endl;
    std::cout << "  --reconnect N          Reconnect attempts on failure (default: 3)" << std::endl;
    std::cout << "  --report-interval N    Stats report interval in seconds (default: 5)" << std::endl;
    std::cout << "  --help                 Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    // Ignore SIGPIPE globally for this process, handle errors inline
    signal(SIGPIPE, SIG_IGN);


    // Default configuration
    ClientConfig config;
    int numClients = 10;
    int messageCount = 100;
    int rampUpTime = 0;
    int testDuration = 0;
    int reportInterval = 5;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--clients" && i + 1 < argc) {
            numClients = std::stoi(argv[++i]);
            if (numClients <= 0) numClients = 1;
        }
        else if (arg == "--messages" && i + 1 < argc) {
            messageCount = std::stoi(argv[++i]);
             if (messageCount < 0) messageCount = 0;
        }
        // else if (arg == "--tls") {
        //     config.useTls = true; // Ignored for now
        // }
        // else if (arg == "--cert" && i + 1 < argc) {
        //     config.tlsCertFile = argv[++i]; // Ignored for now
        // }
        else if (arg == "--pattern" && i + 1 < argc) {
            config.messagePattern = argv[++i];
        }
        else if (arg == "--message" && i + 1 < argc) {
            config.fixedMessage = argv[++i];
        }
        else if (arg == "--message-file" && i + 1 < argc) {
            config.messageFile = argv[++i];
        }
        else if (arg == "--min-length" && i + 1 < argc) {
            config.messageMinLength = std::stoi(argv[++i]);
        }
        else if (arg == "--max-length" && i + 1 < argc) {
            config.messageMaxLength = std::stoi(argv[++i]);
        }
        else if (arg == "--min-delay" && i + 1 < argc) {
            config.delayMinMs = std::stoi(argv[++i]);
        }
        else if (arg == "--max-delay" && i + 1 < argc) {
            config.delayMaxMs = std::stoi(argv[++i]);
        }
        else if (arg == "--ramp-up" && i + 1 < argc) {
            rampUpTime = std::stoi(argv[++i]);
        }
        else if (arg == "--duration" && i + 1 < argc) {
            testDuration = std::stoi(argv[++i]);
        }
        else if (arg == "--no-validate") {
            config.validateResponse = false;
        }
        else if (arg == "--reconnect" && i + 1 < argc) {
            config.reconnectAttemptsMax = std::stoi(argv[++i]);
        }
        else if (arg == "--report-interval" && i + 1 < argc) {
            reportInterval = std::stoi(argv[++i]);
            if (reportInterval <= 0) reportInterval = 1;
        }
        else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Initialize socket library (no-op for POSIX)
    if (!SocketUtils::initialize()) {
        std::cerr << "Failed to initialize socket library" << std::endl;
        return 1;
    }

    // Validate configuration
    if (config.messageMinLength <= 0) config.messageMinLength = 1;
    if (config.messageMaxLength < config.messageMinLength) {
        config.messageMaxLength = config.messageMinLength;
    }

    if (config.delayMinMs < 0) config.delayMinMs = 0;
    if (config.delayMaxMs < config.delayMinMs) {
        config.delayMaxMs = config.delayMinMs;
    }


    // Print test configuration
    std::cout << "=================== Stress Test Configuration ===================" << std::endl;
    std::cout << "Target: " << config.host << ":" << config.port << std::endl;
    std::cout << "Clients: " << numClients << ", Messages per client: " << messageCount << std::endl;
    std::cout << "Message pattern: " << config.messagePattern
              << " (Length: " << config.messageMinLength << "-" << config.messageMaxLength << ")" << std::endl;
    std::cout << "Delay between messages: " << config.delayMinMs << "-" << config.delayMaxMs << " ms" << std::endl;
    std::cout << "TLS: Disabled (Not Implemented)" << std::endl;
    std::cout << "Ramp-up time: " << rampUpTime << " seconds" << std::endl;
    std::cout << "Test duration: " << (testDuration > 0 ? std::to_string(testDuration) + " seconds" : "Until completion") << std::endl;
    std::cout << "Response validation: " << (config.validateResponse ? "Enabled" : "Disabled") << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Starting stress test... (Press Ctrl+C to stop)" << std::endl;

    // Reset statistics
    gStats.reset();

    // Start client threads
    std::vector<std::thread> threads;
    threads.reserve(numClients);

    // Calculate client start interval for ramp-up
    double clientStartIntervalMs = 0;
    if (rampUpTime > 0 && numClients > 1) {
        clientStartIntervalMs = static_cast<double>(rampUpTime * 1000) / (numClients - 1);
    }

    for (int i = 0; i < numClients; i++) {
        if (shutdownRequested) break;

        threads.emplace_back(clientThread, config, i + 1, messageCount);

        // Wait between client starts if ramp-up is enabled
        if (clientStartIntervalMs > 0 && i < numClients - 1) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(clientStartIntervalMs));
        }
    }

    // Set test end time if duration is specified
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> endTime;
    if (testDuration > 0) {
        endTime = gStats.startTime + std::chrono::seconds(testDuration);
    }

    // Main monitoring loop
    while (!shutdownRequested) {
        // Use condition variable for timed wait instead of sleep
        // (Allows quicker exit on shutdown signal)
        // However, simpler sleep is fine for this tool.
        std::this_thread::sleep_for(std::chrono::seconds(reportInterval));

        printStats();

        // Check completion conditions
        bool all_messages_sent = (gStats.messagesSent >= (long long)numClients * messageCount);
        bool all_threads_implicitly_done = (gStats.activeConnections == 0 && threads.size() == numClients); // Heuristic

        // If duration is set, check it
        if (endTime && std::chrono::steady_clock::now() >= *endTime) {
            std::cout << "\nTest duration reached. Requesting shutdown..." << std::endl;
            shutdownRequested = true;
            break; // Exit monitoring loop
        }

        // If duration isn't set, finish when all messages sent *and* connections inactive
        if (!endTime && all_messages_sent && all_threads_implicitly_done) {
             std::cout << "\nAll clients appear finished. Exiting monitoring loop." << std::endl;
             // Don't set shutdownRequested, let threads finish draining naturally if they haven't
             break;
        }

        // Handle case where threads might exit prematurely
         if (!endTime && gStats.activeConnections == 0 && gStats.totalConnections == numClients && !all_messages_sent) {
              std::cout << "\nAll clients disconnected prematurely. Exiting monitoring loop." << std::endl;
              shutdownRequested = true; // Force stop
              break;
         }

    }

    // Signal remaining threads if shutdown was triggered externally or by duration
    if(shutdownRequested) {
        std::cout << "\nWaiting for client threads to terminate..." << std::endl;
    } else {
         std::cout << "\nWaiting for final response drain and thread cleanup..." << std::endl;
    }


    // Wait for threads to finish
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "\nAll client threads joined." << std::endl;

    // Print final statistics
    printStats(true);

    // Cleanup socket library (no-op for POSIX)
    SocketUtils::cleanup();

    return 0;
}
