#include "telnet_protocol_handler.h"
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <map>
#include <vector> // Ensure vector is included for subnegotiationBuffer

// Define a macro for debug logging (disabled — stdout/stderr not valid on Windows detached process)
#define GANL_TELNET_DEBUG(conn, x) do {} while (0)

namespace ganl {

    namespace {

        constexpr unsigned char kNewEnvironIs = 0;
        constexpr unsigned char kNewEnvironSend = 1;
        constexpr unsigned char kNewEnvironInfo = 2;
        constexpr unsigned char kNewEnvironVar = 0;
        constexpr unsigned char kNewEnvironValue = 1;
        constexpr unsigned char kNewEnvironEsc = 2;
        constexpr unsigned char kNewEnvironUserVar = 3;
        constexpr uint16_t kMaxNewEnvironDimension = 1000;

        struct NewEnvironEntry {
            std::string name;
            std::string value;
            bool isUserVar{false};
        };

        std::string uppercaseCopy(const std::string& input) {
            std::string upper = input;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            return upper;
        }

        bool parseUint16InRange(const std::string& value, uint16_t& out) {
            if (value.empty()) {
                return false;
            }

            unsigned long parsed = 0;
            for (unsigned char ch : value) {
                if (!std::isdigit(ch)) {
                    return false;
                }
                parsed = (parsed * 10) + static_cast<unsigned long>(ch - '0');
                if (parsed > kMaxNewEnvironDimension) {
                    return false;
                }
            }

            if (parsed == 0 || parsed > kMaxNewEnvironDimension) {
                return false;
            }

            out = static_cast<uint16_t>(parsed);
            return true;
        }

        std::vector<NewEnvironEntry> parseNewEnvironEntries(const std::vector<char>& buffer, size_t startIndex) {
            std::vector<NewEnvironEntry> entries;
            NewEnvironEntry current;
            bool haveCurrent = false;
            bool readingValue = false;

            auto flushCurrent = [&]() {
                if (haveCurrent && !current.name.empty()) {
                    entries.push_back(current);
                }
                current = {};
                haveCurrent = false;
                readingValue = false;
            };

            for (size_t i = startIndex; i < buffer.size(); ++i) {
                unsigned char ch = static_cast<unsigned char>(buffer[i]);
                switch (ch) {
                case kNewEnvironVar:
                case kNewEnvironUserVar:
                    flushCurrent();
                    haveCurrent = true;
                    current.isUserVar = (ch == kNewEnvironUserVar);
                    break;

                case kNewEnvironValue:
                    if (haveCurrent) {
                        readingValue = true;
                    }
                    break;

                case kNewEnvironEsc:
                    if (i + 1 < buffer.size() && haveCurrent) {
                        unsigned char escaped = static_cast<unsigned char>(buffer[++i]);
                        std::string& target = readingValue ? current.value : current.name;
                        target.push_back(static_cast<char>(escaped));
                    }
                    break;

                default:
                    if (haveCurrent) {
                        std::string& target = readingValue ? current.value : current.name;
                        target.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }

            flushCurrent();
            return entries;
        }

        void appendNewEnvironEscaped(IoBuffer& out, const std::string& value) {
            for (unsigned char ch : value) {
                if (ch == kNewEnvironVar || ch == kNewEnvironValue ||
                    ch == kNewEnvironEsc || ch == kNewEnvironUserVar)
                {
                    const char esc = static_cast<char>(kNewEnvironEsc);
                    out.append(&esc, 1);
                }
                const char byte = static_cast<char>(ch);
                out.append(&byte, 1);
            }
        }

        void appendNewEnvironEntry(IoBuffer& out, const NewEnvironEntry& entry) {
            const char kind = static_cast<char>(entry.isUserVar ? kNewEnvironUserVar : kNewEnvironVar);
            out.append(&kind, 1);
            appendNewEnvironEscaped(out, entry.name);
            if (!entry.value.empty()) {
                const char valueMarker = static_cast<char>(kNewEnvironValue);
                out.append(&valueMarker, 1);
                appendNewEnvironEscaped(out, entry.value);
            }
        }

    } // namespace

    // Helper to safely check map value (returns false if key not found)
    inline bool checkMapFlag(const std::map<TelnetOption, bool>& map, TelnetOption key) {
        auto it = map.find(key);
        return (it != map.end() && it->second);
    }

    // --- TelnetContext Method Implementation ---

    bool TelnetProtocolHandler::TelnetContext::setEncoding(EncodingType newEncoding) {
        if (state.encoding != newEncoding) {
            state.encoding = newEncoding;
            markStateChange(ProtocolStateChangeEncoding);
            return true;
        }
        return false;
    }

    void TelnetProtocolHandler::TelnetContext::setTelnetBinary(bool enabled) {
        if (state.telnetBinary != enabled) {
            state.telnetBinary = enabled;
            markStateChange(ProtocolStateChangeBinary);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setTelnetEcho(bool enabled) {
        if (state.telnetEcho != enabled) {
            state.telnetEcho = enabled;
            markStateChange(ProtocolStateChangeEcho);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setTelnetSGA(bool enabled) {
        if (state.telnetSGA != enabled) {
            state.telnetSGA = enabled;
            markStateChange(ProtocolStateChangeSGA);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setTelnetEOR(bool enabled) {
        if (state.telnetEOR != enabled) {
            state.telnetEOR = enabled;
            markStateChange(ProtocolStateChangeEOR);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setSupportsANSI(bool enabled) {
        if (state.supportsANSI != enabled) {
            state.supportsANSI = enabled;
            markStateChange(ProtocolStateChangeAnsi);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setSupportsMXP(bool enabled) {
        if (state.supportsMXP != enabled) {
            state.supportsMXP = enabled;
            markStateChange(ProtocolStateChangeMxp);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setWidth(uint16_t width) {
        if (state.width != width) {
            state.width = width;
            markStateChange(ProtocolStateChangeWidth);
        }
    }

    void TelnetProtocolHandler::TelnetContext::setHeight(uint16_t height) {
        if (state.height != height) {
            state.height = height;
            markStateChange(ProtocolStateChangeHeight);
        }
    }

    // --- Constructor / Destructor ---
    TelnetProtocolHandler::TelnetProtocolHandler(bool offerStartTls)
        : offerStartTls_(offerStartTls) {
    }

    TelnetProtocolHandler::~TelnetProtocolHandler() {
    }

    // --- Context Management ---
    bool TelnetProtocolHandler::createProtocolContext(ConnectionHandle conn) {
        GANL_TELNET_DEBUG(conn, "Creating protocol context.");
        auto [it, success] = contexts_.try_emplace(conn);
        if (!success) {
            GANL_TELNET_DEBUG(conn, "Error: Context already exists.");
            return false;
        }

        TelnetContext& context = it->second;
        context.state = {}; // Default initialize ProtocolState (including telnetEOR{false})
        context.state.encoding = EncodingType::Ascii;
        context.state.width = kDefaultTerminalWidth;
        context.state.height = kDefaultTerminalHeight;
        context.parserState = ParserState::Normal;
        context.currentNegotiationStatus = NegotiationStatus::InProgress;
        context.negotiationTimedOut = false;
        context.negotiationStartTime = std::chrono::steady_clock::now();

        // Maps are default constructed (empty)

        GANL_TELNET_DEBUG(conn, "Context created. Map size: " << contexts_.size());
        return true;
    }

    bool TelnetProtocolHandler::createClientProtocolContext(ConnectionHandle conn,
                                                              uint16_t clientWidth,
                                                              uint16_t clientHeight,
                                                              const std::string& clientTtype) {
        GANL_TELNET_DEBUG(conn, "Creating CLIENT protocol context.");
        auto [it, success] = contexts_.try_emplace(conn);
        if (!success) {
            GANL_TELNET_DEBUG(conn, "Error: Context already exists.");
            return false;
        }

        TelnetContext& context = it->second;
        context.mode = NegotiationMode::Client;
        context.clientWidth = clientWidth;
        context.clientHeight = clientHeight;
        context.clientTtype = clientTtype;
        context.state = {};
        context.state.encoding = EncodingType::Ascii;
        context.state.width = clientWidth;
        context.state.height = clientHeight;
        context.parserState = ParserState::Normal;
        context.currentNegotiationStatus = NegotiationStatus::InProgress;
        context.negotiationTimedOut = false;
        context.negotiationStartTime = std::chrono::steady_clock::now();

        GANL_TELNET_DEBUG(conn, "Client context created (" << clientWidth << "x" << clientHeight
            << ", ttype=" << clientTtype << ").");
        return true;
    }

    void TelnetProtocolHandler::destroyProtocolContext(ConnectionHandle conn) {
        GANL_TELNET_DEBUG(conn, "Destroying protocol context.");
        size_t erased = contexts_.erase(conn);
        if (erased == 0) {
            GANL_TELNET_DEBUG(conn, "Warning: Attempted to destroy non-existent context.");
        }
        else {
            GANL_TELNET_DEBUG(conn, "Context destroyed successfully.");
        }
    }

    // --- State Accessors / Mutators ---

    bool TelnetProtocolHandler::setEncoding(ConnectionHandle conn, EncodingType encoding) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Error: setEncoding failed, no context.");
            return false;
        }
        // Call the context's method
        bool changed = it->second.setEncoding(encoding);
        if (changed) {
            GANL_TELNET_DEBUG(conn, "Set encoding to " << static_cast<int>(encoding));
        }
        else {
            GANL_TELNET_DEBUG(conn, "Encoding already set to " << static_cast<int>(encoding));
        }
        return true; // Return true indicating the operation was attempted (success doesn't necessarily mean changed)
        // Or return 'changed' if the caller needs to know if it actually changed? Let's stick with true for success.
    }

    // --- State Accessors / Mutators ---

    EncodingType TelnetProtocolHandler::getEncoding(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            return EncodingType::Ascii; // Default
        }
        return it->second.state.encoding; // Directly access state for getter
    }

    ProtocolState TelnetProtocolHandler::getProtocolState(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            return {}; // Return default state
        }
        return it->second.state;
    }
    void TelnetProtocolHandler::updateWidth(ConnectionHandle conn, uint16_t width) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Warning: updateWidth called with no context.");
            return;
        }
        it->second.setWidth(width);
    }

    void TelnetProtocolHandler::updateHeight(ConnectionHandle conn, uint16_t height) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Warning: updateHeight called with no context.");
            return;
        }
        it->second.setHeight(height);
    }
    std::string TelnetProtocolHandler::getLastProtocolErrorString(ConnectionHandle conn) { /* ... */ return ""; }

    bool TelnetProtocolHandler::isStartTlsNegotiated(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) return false;
        // True ONLY if client sent WILL STARTTLS (flag set) and negotiation settled
        return it->second.startTlsWillReceived && it->second.isStateSettled(TelnetOption::STARTTLS);
    }

    bool TelnetProtocolHandler::isReadyForApplication(ConnectionHandle conn) {
        NegotiationStatus status = getNegotiationStatus(conn);
        // Ready if Telnet negotiation is complete.
        // Connection class MUST ALSO check if STARTTLS is pending separately.
        return status == NegotiationStatus::Completed;
    }

    bool TelnetProtocolHandler::consumeStateChanges(ConnectionHandle conn,
                                                    ProtocolState& outState,
                                                    ProtocolStateChangeFlags& outFlags)
    {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            outFlags = ProtocolStateChangeNone;
            return false;
        }

        TelnetContext& context = it->second;
        if (context.pendingStateChanges == ProtocolStateChangeNone) {
            outFlags = ProtocolStateChangeNone;
            return false;
        }

        outState = context.state;
        outFlags = context.pendingStateChanges;
        context.pendingStateChanges = ProtocolStateChangeNone;
        return true;
    }

    // --- Negotiation ---

    void TelnetProtocolHandler::startNegotiation(ConnectionHandle conn, IoBuffer& telnet_responses_out) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Error: Cannot start negotiation, no context.");
            return;
        }
        TelnetContext& context = it->second;

        context.currentNegotiationStatus = NegotiationStatus::InProgress;
        context.negotiationTimedOut = false;
        context.negotiationStartTime = std::chrono::steady_clock::now();
        context.optionStates.clear(); // Clear previous states if re-negotiating
        context.weSentDo.clear();     // Clear previous intentions
        context.weSentWill.clear();   // Clear previous intentions

        // In Client mode, we don't initiate negotiation — we wait for
        // the server to send its WILL/DO offers and respond to them.
        if (context.mode == NegotiationMode::Client) {
            GANL_TELNET_DEBUG(conn, "Client mode: waiting for server negotiation offers.");
            return;
        }

        GANL_TELNET_DEBUG(conn, "Sending initial Telnet negotiation offers (MUX style).");

        // --- Server Offers (WILL) ---
        sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, TelnetOption::EOR);
        context.optionStates[TelnetOption::EOR] = OptionNegotiationState::SentWill;
        context.weSentWill[TelnetOption::EOR] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: WILL EOR");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, TelnetOption::CHARSET);
        context.optionStates[TelnetOption::CHARSET] = OptionNegotiationState::SentWill;
        context.weSentWill[TelnetOption::CHARSET] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: WILL CHARSET");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, TelnetOption::SGA);
        context.optionStates[TelnetOption::SGA] = OptionNegotiationState::SentWill;
        context.weSentWill[TelnetOption::SGA] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: WILL SGA");

        // --- Server Requests (DO) ---
        sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::EOR);
        // Overwrite state for EOR to SentDo as it was the last command for this option
        context.optionStates[TelnetOption::EOR] = OptionNegotiationState::SentDo;
        context.weSentDo[TelnetOption::EOR] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: DO EOR");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::TTYPE);
        context.optionStates[TelnetOption::TTYPE] = OptionNegotiationState::SentDo;
        context.weSentDo[TelnetOption::TTYPE] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: DO TTYPE");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::NAWS);
        context.optionStates[TelnetOption::NAWS] = OptionNegotiationState::SentDo;
        context.weSentDo[TelnetOption::NAWS] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: DO NAWS");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::NEW_ENVIRON);
        context.optionStates[TelnetOption::NEW_ENVIRON] = OptionNegotiationState::SentDo;
        context.weSentDo[TelnetOption::NEW_ENVIRON] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: DO NEW-ENVIRON");

        // Offer GMCP to clients
        sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, TelnetOption::GMCP);
        context.optionStates[TelnetOption::GMCP] = OptionNegotiationState::SentWill;
        context.weSentWill[TelnetOption::GMCP] = true;
        GANL_TELNET_DEBUG(conn, "Sent: WILL GMCP");

        sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::CHARSET);
        // Overwrite state for CHARSET to SentDo
        context.optionStates[TelnetOption::CHARSET] = OptionNegotiationState::SentDo;
        context.weSentDo[TelnetOption::CHARSET] = true; // Populate map
        GANL_TELNET_DEBUG(conn, "Sent: DO CHARSET");

        if (canOfferStartTls()) {
            sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, TelnetOption::STARTTLS);
            context.optionStates[TelnetOption::STARTTLS] = OptionNegotiationState::SentDo;
            context.weSentDo[TelnetOption::STARTTLS] = true; // Populate map
            GANL_TELNET_DEBUG(conn, "Sent: DO STARTTLS");
        }
        else {
            GANL_TELNET_DEBUG(conn, "Skipping DO STARTTLS (TLS not available/configured).");
            // No need to set false in map, absence means false
        }

        GANL_TELNET_DEBUG(conn, "Initial negotiation sent. Status -> InProgress.");
    }

    // --- Timeout Handling ---
    void TelnetProtocolHandler::onNegotiationTimeout(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end() || it->second.negotiationTimedOut) {
            return;
        }
        TelnetContext& context = it->second;

        GANL_TELNET_DEBUG(conn, "Negotiation timeout reached after " << negotiationTimeoutDuration_.count() << "s.");
        context.negotiationTimedOut = true;

        updateNegotiationStatus(conn); // Update status based on timeout

        // Force completion if still in progress
        if (context.currentNegotiationStatus == NegotiationStatus::InProgress) {
            GANL_TELNET_DEBUG(conn, "Forcing negotiation status to Completed due to timeout.");
            context.currentNegotiationStatus = NegotiationStatus::Completed;
            for (auto const& [opt, state] : context.optionStates) {
                if (!context.isStateSettled(opt)) { // Use the improved helper
                    GANL_TELNET_DEBUG(conn, "Warning: Option " << static_cast<int>(opt) << " did not settle before timeout. State: " << static_cast<int>(state));
                }
            }
            // Also check options we intended but never got a response for (not in optionStates map)
            for (const auto& [opt, sent] : context.weSentDo) {
                if (sent && !context.optionStates.count(opt) && !context.isStateSettled(opt)) {
                    GANL_TELNET_DEBUG(conn, "Warning: Sent DO " << static_cast<int>(opt) << " but received no response before timeout.");
                }
            }
            for (const auto& [opt, sent] : context.weSentWill) {
                if (sent && !context.optionStates.count(opt) && !context.isStateSettled(opt)) {
                    GANL_TELNET_DEBUG(conn, "Warning: Sent WILL " << static_cast<int>(opt) << " but received no response before timeout.");
                }
            }
        }
    }

#undef min
    // --- Data Processing ---
    bool TelnetProtocolHandler::processInput(ConnectionHandle conn, IoBuffer& decrypted_in,
        IoBuffer& app_data_out, IoBuffer& telnet_responses_out, bool consumeInput) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Error: processInput called with no context.");
            return false;
        }

        // DEBUG: Show first bytes of input
        size_t initialReadable = decrypted_in.readableBytes();
        if (initialReadable > 0) {
            const char* ptr = decrypted_in.readPtr();
            std::string hexdump;
            for (size_t i = 0; i < std::min(initialReadable, size_t(80)); i++) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02x ", (unsigned char)ptr[i]);
                hexdump += buf;
            }
            GANL_TELNET_DEBUG(conn, "processInput called with " << initialReadable << " bytes. First 80 hex: [" << hexdump << "]");
        }

        bool processedOk = true;
        size_t totalConsumed = 0;

        while (totalConsumed < initialReadable) {
            const char* current = decrypted_in.readPtr();
            if (!current) break; // Should not happen if readableBytes > 0
            unsigned char uc = static_cast<unsigned char>(*current);
            size_t bytesConsumedThisLoop = 1;

            // Re-get context reference inside loop in case map reallocates (unlikely but safe)
            auto current_it = contexts_.find(conn);
            if (current_it == contexts_.end()) return false; // Context disappeared mid-processing?
            TelnetContext& context = current_it->second;

            // DEBUG: Show parser state and byte for first 20 bytes and nulls
            if (totalConsumed < 20 || uc == 0) {
                GANL_TELNET_DEBUG(conn, "Byte[" << totalConsumed << "]: 0x" << std::hex << (int)uc << std::dec
                    << " state=" << static_cast<int>(context.parserState)
                    << " neg_status=" << static_cast<int>(context.currentNegotiationStatus));
            }

            switch (context.parserState) {
            case ParserState::Normal:
                if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
                    context.parserState = ParserState::IAC;
                }
                else if (uc == '\r') {
                    context.sawCR = true; // Handle CR/LF/CRLF/CRNUL logic
                    // Simple: Treat CR as potential end-of-line trigger if buffer has content
                    // More robust: Wait for subsequent LF or NUL, or treat as NL if neither follows
                    if (!context.inputBuffer.empty()) {
                        // DEBUG: Show what we're about to output
                        std::string hexdump;
                        for (size_t i = 0; i < std::min(context.inputBuffer.size(), size_t(32)); i++) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "%02x ", (unsigned char)context.inputBuffer[i]);
                            hexdump += buf;
                        }
                        GANL_TELNET_DEBUG(conn, "Outputting line from inputBuffer (CR): size=" << context.inputBuffer.size() << " hex=[" << hexdump << "]");

                        std::string line(context.inputBuffer.data(), context.inputBuffer.size());
                        app_data_out.append(line.data(), line.size());
                        app_data_out.append("\n", 1); // Normalize to LF
                        context.inputBuffer.clear();
                    }
                }
                else if (uc == '\n') {
                    if (!context.sawCR) { // Avoid double newline for CRLF
                        if (!context.inputBuffer.empty()) {
                            // DEBUG: Show what we're about to output
                            std::string hexdump;
                            for (size_t i = 0; i < std::min(context.inputBuffer.size(), size_t(32)); i++) {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "%02x ", (unsigned char)context.inputBuffer[i]);
                                hexdump += buf;
                            }
                            GANL_TELNET_DEBUG(conn, "Outputting line from inputBuffer (LF): size=" << context.inputBuffer.size() << " hex=[" << hexdump << "]");

                            std::string line(context.inputBuffer.data(), context.inputBuffer.size());
                            app_data_out.append(line.data(), line.size());
                            app_data_out.append("\n", 1); // Normalize to LF
                            context.inputBuffer.clear();
                        }
                        else {
                            // Received bare LF, maybe treat as command/enter?
                            app_data_out.append("\n", 1);
                        }
                    }
                    context.sawCR = false;
                }
                else if (uc == '\0' && context.sawCR) {
                    // Handle CR NUL as newline
                    if (!context.inputBuffer.empty()) {
                        // DEBUG: Show what we're about to output
                        std::string hexdump;
                        for (size_t i = 0; i < std::min(context.inputBuffer.size(), size_t(32)); i++) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "%02x ", (unsigned char)context.inputBuffer[i]);
                            hexdump += buf;
                        }
                        GANL_TELNET_DEBUG(conn, "Outputting line from inputBuffer (CR-NUL): size=" << context.inputBuffer.size() << " hex=[" << hexdump << "]");

                        std::string line(context.inputBuffer.data(), context.inputBuffer.size());
                        app_data_out.append(line.data(), line.size());
                        app_data_out.append("\n", 1); // Normalize to LF
                        context.inputBuffer.clear();
                    }
                    context.sawCR = false;
                }
                else {
                    // Regular character
                    // DEBUG: Show what character we're adding (especially nulls)
                    if (uc == 0 || uc == 0xe2 || context.inputBuffer.size() < 10) {
                        GANL_TELNET_DEBUG(conn, "Adding byte to inputBuffer: 0x" << std::hex << (int)uc << std::dec
                            << " ('" << (uc >= 32 && uc < 127 ? (char)uc : '?') << "')"
                            << " buffer_size=" << context.inputBuffer.size()
                            << " neg_status=" << static_cast<int>(context.currentNegotiationStatus));
                    }
                    if (context.inputBuffer.size() >= kMaxInputBufferBytes) {
                        GANL_TELNET_DEBUG(conn, "Error: inputBuffer exceeded limit of " << kMaxInputBufferBytes << " bytes.");
                        context.lastError = "Telnet input buffer exceeded limit";
                        processedOk = false;
                        break;
                    }
                    context.inputBuffer.push_back(static_cast<char>(uc));
                    context.sawCR = false;
                }
                break;

            case ParserState::IAC:
                if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
                    if (context.inputBuffer.size() >= kMaxInputBufferBytes) {
                        GANL_TELNET_DEBUG(conn, "Error: inputBuffer exceeded limit of " << kMaxInputBufferBytes << " bytes while escaping IAC.");
                        context.lastError = "Telnet input buffer exceeded limit";
                        processedOk = false;
                        break;
                    }
                    context.inputBuffer.push_back(static_cast<char>(uc));
                    context.parserState = ParserState::Normal;
                }
                else if (uc >= static_cast<unsigned char>(TelnetCommand::SB) && uc <= static_cast<unsigned char>(TelnetCommand::DONT)) {
                    context.lastCmd = static_cast<TelnetCommand>(uc);
                    context.parserState = ParserState::Command;
                }
                else {
                    // Standalone command
                    handleTelnetStandaloneCommand(conn, static_cast<TelnetCommand>(uc), telnet_responses_out);
                    context.parserState = ParserState::Normal;
                }
                break;

            case ParserState::Command: {
                context.lastOpt = static_cast<TelnetOption>(uc);
                GANL_TELNET_DEBUG(conn, "Got OPT " << static_cast<int>(context.lastOpt) << " for CMD " << static_cast<int>(context.lastCmd));
                bool negotiationAffected = false;
                if (context.lastCmd == TelnetCommand::SB) {
                    context.parserState = ParserState::Subnegotiation;
                    context.subnegotiationBuffer.clear();
                }
                else {
                    handleTelnetOptionNegotiation(conn, context.lastCmd, context.lastOpt, telnet_responses_out);
                    context.parserState = ParserState::Normal;
                    negotiationAffected = true; // WILL/WONT/DO/DONT affects negotiation
                }
                if (negotiationAffected && context.currentNegotiationStatus == NegotiationStatus::InProgress) {
                    updateNegotiationStatus(conn);
                }
            } break;

            case ParserState::Subnegotiation:
                if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
                    context.parserState = ParserState::Subnegotiation_IAC;
                }
                else {
                    if (context.subnegotiationBuffer.size() >= kMaxSubnegotiationBufferBytes) {
                        GANL_TELNET_DEBUG(conn, "Error: subnegotiationBuffer exceeded limit of " << kMaxSubnegotiationBufferBytes << " bytes.");
                        context.lastError = "Telnet subnegotiation buffer exceeded limit";
                        processedOk = false;
                        break;
                    }
                    context.subnegotiationBuffer.push_back(static_cast<char>(uc));
                }
                break;

            case ParserState::Subnegotiation_IAC:
                if (uc == static_cast<unsigned char>(TelnetCommand::SE)) {
                    processSubnegotiationData(conn, context.lastOpt, telnet_responses_out);
                    context.subnegotiationBuffer.clear(); // Clear buffer after processing
                    context.parserState = ParserState::Normal;
                    // Check if subnegotiation affects overall status
                    if (context.currentNegotiationStatus == NegotiationStatus::InProgress) {
                        updateNegotiationStatus(conn);
                    }
                }
                else if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
                    if (context.subnegotiationBuffer.size() >= kMaxSubnegotiationBufferBytes) {
                        GANL_TELNET_DEBUG(conn, "Error: subnegotiationBuffer exceeded limit of " << kMaxSubnegotiationBufferBytes << " bytes while escaping IAC.");
                        context.lastError = "Telnet subnegotiation buffer exceeded limit";
                        processedOk = false;
                        break;
                    }
                    context.subnegotiationBuffer.push_back(static_cast<char>(uc));
                    context.parserState = ParserState::Subnegotiation;
                }
                else {
                    GANL_TELNET_DEBUG(conn, "Sub: Error! IAC followed by invalid byte " << uc << ". Ignoring SB.");
                    context.subnegotiationBuffer.clear(); // Clear buffer on error
                    context.parserState = ParserState::Normal;
                    context.lastError = "Invalid byte after IAC during subnegotiation";
                    processedOk = false; // Indicate an error occurred
                }
                break;
            } // end switch

            if (consumeInput) {
                decrypted_in.consumeRead(bytesConsumedThisLoop);
            }
            totalConsumed += bytesConsumedThisLoop;

        } // end while

        GANL_TELNET_DEBUG(conn, "Processed input loop. Consumed=" << totalConsumed << ", Error=" << !processedOk << ", Data actually consumed=" << consumeInput);
        return processedOk;
    }


    bool TelnetProtocolHandler::formatOutput(ConnectionHandle conn, IoBuffer& app_data_in,
        IoBuffer& formatted_out, bool consumeInput) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) return false;
        TelnetContext& context = it->second; // Context needed for EOR, ANSI etc.

        const char* current = app_data_in.readPtr();
        const char* const end = current + app_data_in.readableBytes();
        size_t consumed = 0;

        auto shouldStripMxpTag = [&](const char* start, const char* finish) -> const char* {
            if (context.state.supportsMXP || start >= finish || *start != '<') {
                return nullptr;
            }

            const char* next = start + 1;
            if (next >= finish) {
                return nullptr;
            }

            unsigned char marker = static_cast<unsigned char>(*next);
            if (!(std::isalpha(marker) || marker == '/' || marker == '!')) {
                return nullptr;
            }

            const char* scan = next;
            size_t span = 0;
            while (scan < finish && span < 256) {
                if (*scan == '>') {
                    return scan + 1;
                }
                if (*scan == '\r' || *scan == '\n') {
                    return nullptr;
                }
                ++scan;
                ++span;
            }
            return nullptr;
        };

        auto skipAnsiSequence = [&](const char* start, const char* finish) -> const char* {
            if (context.state.supportsANSI || start >= finish ||
                *start != static_cast<char>(0x1b) || start + 1 >= finish)
            {
                return nullptr;
            }

            const char leader = *(start + 1);
            if (leader == '[') {
                const char* scan = start + 2;
                while (scan < finish) {
                    unsigned char uch = static_cast<unsigned char>(*scan);
                    if (uch >= 0x40 && uch <= 0x7e) {
                        return scan + 1;
                    }
                    ++scan;
                }
                return finish;
            }

            if (leader == ']') {
                const char* scan = start + 2;
                while (scan < finish) {
                    if (*scan == '\a') {
                        return scan + 1;
                    }
                    if (*scan == static_cast<char>(0x1b) &&
                        scan + 1 < finish && *(scan + 1) == '\\')
                    {
                        return scan + 2;
                    }
                    ++scan;
                }
                return finish;
            }

            return nullptr;
        };

        while (current < end) {
            if (const char* afterAnsi = skipAnsiSequence(current, end)) {
                consumed += static_cast<size_t>(afterAnsi - current);
                current = afterAnsi;
                continue;
            }

            if (const char* afterTag = shouldStripMxpTag(current, end)) {
                consumed += static_cast<size_t>(afterTag - current);
                current = afterTag;
                continue;
            }

            char ch = *current;
            consumed++;
            current++;

            if (ch == '\n') {
                formatted_out.append("\r\n", 2); // Standard Telnet line ending
                // If EOR is active, application needs to signal prompts explicitly.
                // If app sent a special sequence (e.g., "\n\x01") to mean "prompt",
                // we could detect that here and send IAC EOR.
                // Simple approach: Assume app sends CRLF for normal lines.
                // if (context.state.telnetEOR) { /* Check for prompt signal */ }
            }
            else if (ch == static_cast<char>(TelnetCommand::IAC)) {
                char iac_pair[2] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::IAC) };
                formatted_out.append(iac_pair, 2);
            }
            else {
                formatted_out.append(&ch, 1);
            }
        }

        if (consumeInput) {
            app_data_in.consumeRead(consumed);
        }
        return true;
    }

    // --- Negotiation Status ---

    NegotiationStatus TelnetProtocolHandler::getNegotiationStatus(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            return NegotiationStatus::Failed;
        }
        // Re-check status if timer might be involved and it's still in progress
        TelnetContext& context = it->second;
        if (context.currentNegotiationStatus == NegotiationStatus::InProgress && !context.negotiationTimedOut) {
            auto now = std::chrono::steady_clock::now();
            if (now >= context.negotiationStartTime + negotiationTimeoutDuration_) {
                // Timer *should* have fired, but check defensively
                onNegotiationTimeout(conn); // This will update the status
            }
        }
        return context.currentNegotiationStatus;
    }

    void TelnetProtocolHandler::updateNegotiationStatus(ConnectionHandle conn) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) return;
        TelnetContext& context = it->second;

        if (context.currentNegotiationStatus == NegotiationStatus::Completed ||
            context.currentNegotiationStatus == NegotiationStatus::Failed) {
            return; // Don't re-evaluate final states
        }

        // --- Define Completion Criteria ---
        // Essentials: SGA, EOR (us+him), STARTTLS (if offered) must be settled.
        // Desirables: NAWS, TTYPE data should be received if requested (unless timed out).

        bool sga_settled = context.isStateSettled(TelnetOption::SGA);
        bool eor_settled = context.isStateSettled(TelnetOption::EOR); // Combined check is sufficient with map approach
        bool starttls_settled = context.isStateSettled(TelnetOption::STARTTLS);

        bool naws_ready = !checkMapFlag(context.weSentDo, TelnetOption::NAWS) || context.nawsDataReceived || context.negotiationTimedOut;
        bool ttype_ready = !checkMapFlag(context.weSentDo, TelnetOption::TTYPE) || context.ttypeDataReceived || context.negotiationTimedOut;
        // Add checks for CHARSET, NEW_ENVIRON readiness if needed
        bool charset_ready = !checkMapFlag(context.weSentDo, TelnetOption::CHARSET) || context.charsetDataReceived || context.negotiationTimedOut;
        bool environ_ready = !checkMapFlag(context.weSentDo, TelnetOption::NEW_ENVIRON) || context.newEnvironDataReceived || context.negotiationTimedOut;


        bool essentials_settled = sga_settled && eor_settled && starttls_settled;
        bool desirables_ready = naws_ready && ttype_ready && charset_ready && environ_ready; // Or however strict you need to be

        GANL_TELNET_DEBUG(conn, "Updating negotiation status: "
            << "TimedOut=" << context.negotiationTimedOut
            << " EssentialsSettled=" << essentials_settled
            << " (SGA=" << sga_settled << ", EOR=" << eor_settled << ", STARTTLS=" << starttls_settled << ")"
            << " DesirablesReady=" << desirables_ready
            << " (NAWS=" << naws_ready << ", TTYPE=" << ttype_ready << ", CHARSET=" << charset_ready << ", ENV=" << environ_ready << ")");


        // --- Determine overall status ---
        if (essentials_settled) { // Maybe add && desirables_ready if they are critical?
            if (context.startTlsWillReceived) {
                // Telnet negotiation part is done, but need TLS handshake via Connection/SecureTransport
                context.currentNegotiationStatus = NegotiationStatus::Completed;
                GANL_TELNET_DEBUG(conn, "Telnet negotiation settled. Status -> Completed (STARTTLS pending).");
            }
            else {
                // No STARTTLS involved or rejected, truly complete.
                context.currentNegotiationStatus = NegotiationStatus::Completed;
                GANL_TELNET_DEBUG(conn, "Telnet negotiation settled. Status -> Completed.");
            }

            // Clear any bytes accumulated during negotiation that shouldn't be treated as commands
            if (!context.inputBuffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Clearing " << context.inputBuffer.size() << " bytes from inputBuffer after negotiation.");
                context.inputBuffer.clear();
            }
            if (!context.subnegotiationBuffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Clearing " << context.subnegotiationBuffer.size() << " bytes from subnegotiationBuffer after negotiation.");
                context.subnegotiationBuffer.clear();
            }
            context.sawCR = false; // Reset line ending state
        }
        else if (context.negotiationTimedOut) {
            // Timeout occurred before essentials were settled. Force completion.
            GANL_TELNET_DEBUG(conn, "Negotiation timed out before essentials settled. Status -> Completed (forced).");
            context.currentNegotiationStatus = NegotiationStatus::Completed;

            // Clear any bytes accumulated during negotiation
            if (!context.inputBuffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Clearing " << context.inputBuffer.size() << " bytes from inputBuffer after timeout.");
                context.inputBuffer.clear();
            }
            if (!context.subnegotiationBuffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Clearing " << context.subnegotiationBuffer.size() << " bytes from subnegotiationBuffer after timeout.");
                context.subnegotiationBuffer.clear();
            }
            context.sawCR = false;
        }
        else {
            // Still waiting for essential responses, not timed out yet.
            context.currentNegotiationStatus = NegotiationStatus::InProgress;
        }
    }

    // --- Private Helper Methods ---

    void TelnetProtocolHandler::handleTelnetStandaloneCommand(ConnectionHandle conn, TelnetCommand cmd, IoBuffer& telnet_responses_out) {
        // Handle NOP, AYT, etc.
        GANL_TELNET_DEBUG(conn, "Processing standalone command: " << static_cast<int>(cmd));
        // Example AYT response:
        if (cmd == TelnetCommand::AYT) {
            const char* response = "[Yes]\r\n"; // Simple AYT response
            // This should ideally go through formatOutput, but for simple AYT:
            telnet_responses_out.append(response, strlen(response));
        }
    }

    void TelnetProtocolHandler::handleTelnetOptionNegotiation(ConnectionHandle conn, TelnetCommand cmd, TelnetOption opt, IoBuffer& telnet_responses_out) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) return;
        TelnetContext& context = it->second;

        GANL_TELNET_DEBUG(conn, "Handling negotiation: CMD=" << static_cast<int>(cmd) << ", OPT=" << static_cast<int>(opt)
            << " mode=" << (context.mode == NegotiationMode::Client ? "Client" : "Server"));

        // --- Client-mode negotiation ---
        // In Client mode, we are connecting TO a server.  The server
        // sends WILL/DO offers and we respond.  This is the reverse of
        // the Server-mode logic below.
        if (context.mode == NegotiationMode::Client) {
            switch (cmd) {
            case TelnetCommand::WILL: // Server WILL X — do we want it?
            {
                bool accept = false;
                switch (opt) {
                case TelnetOption::EOR:     accept = true; context.setTelnetEOR(true); break;
                case TelnetOption::ECHO:    accept = true; context.setTelnetEcho(true); break;
                case TelnetOption::SGA:     accept = true; context.setTelnetSGA(true); break;
                case TelnetOption::CHARSET: accept = true; break;
                case TelnetOption::GMCP:    accept = true; break;
                case TelnetOption::MSSP:    accept = true; break;
                default: accept = false; break;
                }
                if (accept) {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, opt);
                    context.optionStates[opt] = OptionNegotiationState::ActiveDo;
                } else {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::DONT, opt);
                    context.optionStates[opt] = OptionNegotiationState::RejectedDo;
                }
            } break;

            case TelnetCommand::WONT: // Server WONT X
                context.optionStates[opt] = OptionNegotiationState::RejectedDo;
                switch (opt) {
                case TelnetOption::EOR:  context.setTelnetEOR(false); break;
                case TelnetOption::ECHO: context.setTelnetEcho(false); break;
                case TelnetOption::SGA:  context.setTelnetSGA(false); break;
                default: break;
                }
                break;

            case TelnetCommand::DO: // Server DO X — wants us to enable X
            {
                bool weWill = false;
                switch (opt) {
                case TelnetOption::NAWS:
                    weWill = true;
                    // Send WILL NAWS, then immediately send our terminal size
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    {
                        // SB NAWS <width-hi> <width-lo> <height-hi> <height-lo> SE
                        char sb[] = {
                            static_cast<char>(TelnetCommand::IAC),
                            static_cast<char>(TelnetCommand::SB),
                            static_cast<char>(TelnetOption::NAWS),
                            static_cast<char>((context.clientWidth >> 8) & 0xFF),
                            static_cast<char>(context.clientWidth & 0xFF),
                            static_cast<char>((context.clientHeight >> 8) & 0xFF),
                            static_cast<char>(context.clientHeight & 0xFF),
                            static_cast<char>(TelnetCommand::IAC),
                            static_cast<char>(TelnetCommand::SE)
                        };
                        telnet_responses_out.append(sb, sizeof(sb));
                    }
                    context.optionStates[opt] = OptionNegotiationState::ActiveWill;
                    break;

                case TelnetOption::TTYPE:
                    weWill = true;
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    // The server will follow up with SB TTYPE SEND, handled
                    // in processSubnegotiationData.
                    context.optionStates[opt] = OptionNegotiationState::ActiveWill;
                    break;

                case TelnetOption::SGA:
                    weWill = true;
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    context.setTelnetSGA(true);
                    context.optionStates[opt] = OptionNegotiationState::ActiveWill;
                    break;

                case TelnetOption::EOR:
                    weWill = true;
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    context.setTelnetEOR(true);
                    context.optionStates[opt] = OptionNegotiationState::ActiveWill;
                    break;

                case TelnetOption::NEW_ENVIRON:
                case TelnetOption::CHARSET:
                    weWill = true;
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    context.optionStates[opt] = OptionNegotiationState::ActiveWill;
                    break;

                default:
                    weWill = false;
                    break;
                }
                if (!weWill) {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WONT, opt);
                    context.optionStates[opt] = OptionNegotiationState::RejectedWill;
                }
            } break;

            case TelnetCommand::DONT: // Server DONT X
                context.optionStates[opt] = OptionNegotiationState::RejectedWill;
                sendTelnetCommand(telnet_responses_out, TelnetCommand::WONT, opt);
                break;

            default: break;
            }
            return; // Client mode handled — skip Server mode logic below
        }

        // --- Server-mode negotiation (existing logic) ---

        OptionNegotiationState oldState = context.optionStates.count(opt) ? context.optionStates.at(opt) : OptionNegotiationState::Idle;
        OptionNegotiationState newState = oldState;

        // Use helper 'checkMapFlag' for weSentDo/weSentWill lookups
        bool weRequested = checkMapFlag(context.weSentDo, opt);
        bool weOffered = checkMapFlag(context.weSentWill, opt);

        switch (cmd) {
        case TelnetCommand::WILL: // Client WILL X
            if (weRequested) { // Response to our DO
                GANL_TELNET_DEBUG(conn, "Client WILL " << static_cast<int>(opt) << " (matches our DO)");
                newState = OptionNegotiationState::ActiveDo;
                // Update ProtocolState, handle side effects (like sending SB TTYPE SEND)
                switch (opt) {
                case TelnetOption::EOR: context.setTelnetEOR(true); break;
                case TelnetOption::NAWS: break; // Ready for SB NAWS
                case TelnetOption::TTYPE: {
                    GANL_TELNET_DEBUG(conn, "Client WILL TTYPE. Sending SB TTYPE SEND.");
                    char sb_req[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SB),
                                      static_cast<char>(TelnetOption::TTYPE), 1 /* SEND */,
                                      static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SE) };
                    telnet_responses_out.append(sb_req, sizeof(sb_req));
                } break;
                case TelnetOption::NEW_ENVIRON: {
                    GANL_TELNET_DEBUG(conn, "Client WILL NEW-ENVIRON. Sending SB NEW-ENVIRON SEND request.");
                    const char sb_req[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SB),
                        static_cast<char>(TelnetOption::NEW_ENVIRON),
                        static_cast<char>(kNewEnvironSend),
                        static_cast<char>(kNewEnvironVar), 'T', 'E', 'R', 'M',
                        static_cast<char>(kNewEnvironVar), 'C', 'O', 'L', 'U', 'M', 'N', 'S',
                        static_cast<char>(kNewEnvironVar), 'L', 'I', 'N', 'E', 'S',
                        static_cast<char>(kNewEnvironUserVar), 'C', 'O', 'L', 'O', 'R', 'T', 'E', 'R', 'M',
                        static_cast<char>(kNewEnvironUserVar), 'M', 'X', 'P',
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SE)
                    };
                    telnet_responses_out.append(sb_req, sizeof(sb_req));
                } break;
                case TelnetOption::CHARSET: {
                    // Optionally send SB CHARSET REQUEST here
                    GANL_TELNET_DEBUG(conn, "Client WILL CHARSET. Optionally send SB CHARSET REQUEST...");
                } break;
                case TelnetOption::STARTTLS:
                    GANL_TELNET_DEBUG(conn, "Client WILL STARTTLS received!");
                    context.startTlsWillReceived = true; // Mark for Connection
                    // NO response here, Connection handles TLS start
                    break;
                default: break; // Handle other options like SGA, ECHO if needed
                }
            }
            else { // Unsolicited WILL
                GANL_TELNET_DEBUG(conn, "Client WILL " << static_cast<int>(opt) << " (unsolicited)");
                bool accept = false;
                switch (opt) { // Decide if we accept unsolicited WILLs
                case TelnetOption::SGA: accept = true; context.setTelnetSGA(true); break;
                case TelnetOption::EOR: accept = true; context.setTelnetEOR(true); break;
                case TelnetOption::GMCP: accept = true; break;
                default: accept = false; break;
                }
                if (accept) {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::DO, opt);
                    newState = OptionNegotiationState::ReceivedWill;
                }
                else {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::DONT, opt);
                    newState = OptionNegotiationState::ReceivedWill; // Still marks negotiation happened
                }
            }
            break;

        case TelnetCommand::WONT: // Client WONT X
            if (weRequested) { // Response to our DO
                GANL_TELNET_DEBUG(conn, "Client WONT " << static_cast<int>(opt) << " (rejects our DO)");
                newState = OptionNegotiationState::RejectedDo;
                switch (opt) { // Ensure state reflects rejection
                case TelnetOption::EOR: context.setTelnetEOR(false); break;
                case TelnetOption::STARTTLS: context.startTlsWillReceived = false; break;
                    // Reset other states if necessary
                }
            }
            else { // Unsolicited WONT (Confirmation)
                GANL_TELNET_DEBUG(conn, "Client WONT " << static_cast<int>(opt) << " (unsolicited confirmation)");
                newState = OptionNegotiationState::ReceivedWill; // Mark negotiation settled
                switch (opt) { // Ensure state reflects disabled
                case TelnetOption::SGA: context.setTelnetSGA(false); break;
                case TelnetOption::EOR: context.setTelnetEOR(false); break;
                }
            }
            break;

        case TelnetCommand::DO: // Client DO X (Wants *us* to enable X)
            if (weOffered) { // Response to our WILL
                GANL_TELNET_DEBUG(conn, "Client DO " << static_cast<int>(opt) << " (matches our WILL)");
                newState = OptionNegotiationState::ActiveWill;
                switch (opt) { // Ensure state reflects acceptance
                case TelnetOption::SGA: context.setTelnetSGA(true); break;
                case TelnetOption::EOR: context.setTelnetEOR(true); break;
                case TelnetOption::CHARSET: break; // Ready for SB CHARSET IS/etc.
                }
            }
            else { // Unsolicited DO
                GANL_TELNET_DEBUG(conn, "Client DO " << static_cast<int>(opt) << " (unsolicited request)");
                bool weWill = false;
                switch (opt) { // Decide if we WILL do this if asked
                case TelnetOption::SGA: weWill = true; break;
                case TelnetOption::EOR: weWill = true; break;
                case TelnetOption::BINARY: weWill = true; break; // If supported
                case TelnetOption::CHARSET: weWill = true; break; // If supported
                default: weWill = false; break;
                }
                if (weWill) {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WILL, opt);
                    newState = OptionNegotiationState::ReceivedDo;
                    switch (opt) { // Update state
                    case TelnetOption::SGA: context.setTelnetSGA(true); break;
                    case TelnetOption::EOR: context.setTelnetEOR(true); break;
                    case TelnetOption::BINARY: context.setTelnetBinary(true); break;
                    }
                }
                else {
                    sendTelnetCommand(telnet_responses_out, TelnetCommand::WONT, opt);
                    newState = OptionNegotiationState::ReceivedDo;
                }
            }
            break;

        case TelnetCommand::DONT: // Client DONT X (Wants *us* to disable X)
            if (weOffered) { // Response to our WILL
                GANL_TELNET_DEBUG(conn, "Client DONT " << static_cast<int>(opt) << " (rejects our WILL)");
                newState = OptionNegotiationState::RejectedWill;
                switch (opt) { // Ensure state reflects rejection
                case TelnetOption::SGA: context.setTelnetSGA(false); break;
                case TelnetOption::EOR: context.setTelnetEOR(false); break;
                case TelnetOption::CHARSET: break; // Update charset state if needed
                }
            }
            else { // Unsolicited DONT
                GANL_TELNET_DEBUG(conn, "Client DONT " << static_cast<int>(opt) << " (unsolicited request/confirmation)");
                newState = OptionNegotiationState::ReceivedDo; // Mark negotiation settled
                switch (opt) { // Ensure state reflects disabled
                case TelnetOption::SGA: context.setTelnetSGA(false); break;
                case TelnetOption::EOR: context.setTelnetEOR(false); break;
                case TelnetOption::BINARY: context.setTelnetBinary(false); break;
                }
                // Acknowledge DONT with WONT
                sendTelnetCommand(telnet_responses_out, TelnetCommand::WONT, opt);
            }
            break;
        default: break;
        }

        // Update the state map if changed
        if (newState != oldState) {
            GANL_TELNET_DEBUG(conn, "Option " << static_cast<int>(opt) << " state change: " << static_cast<int>(oldState) << " -> " << static_cast<int>(newState));
            context.optionStates[opt] = newState;
        }
    }


    void TelnetProtocolHandler::processSubnegotiationData(ConnectionHandle conn, TelnetOption opt, IoBuffer& telnet_responses_out) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) return;
        TelnetContext& context = it->second;
        const auto& buffer = context.subnegotiationBuffer; // Use const ref

        GANL_TELNET_DEBUG(conn, "Processing subnegotiation data for OPT " << static_cast<int>(opt) << ". Data size: " << buffer.size());

        // Create a temporary IoBuffer with the subnegotiation data for passing to derived classes
        IoBuffer subnegotiationData;
        if (!buffer.empty()) {
            subnegotiationData.append(buffer.data(), buffer.size());
        }

        // Call the virtual method to allow derived classes to handle the data
        handleTelnetSubnegotiation(conn, opt, subnegotiationData, telnet_responses_out);

        // Continue with built-in handling of standard options

        switch (opt) {
        case TelnetOption::NAWS:
            // IAC SB NAWS <16-bit width> <16-bit height> IAC SE
            // Payload is 4 bytes.
            if (buffer.size() == 4) {
                // Network byte order (Big Endian)
                uint16_t width = (static_cast<uint16_t>(static_cast<unsigned char>(buffer[0])) << 8) |
                    static_cast<uint16_t>(static_cast<unsigned char>(buffer[1]));
                uint16_t height = (static_cast<uint16_t>(static_cast<unsigned char>(buffer[2])) << 8) |
                    static_cast<uint16_t>(static_cast<unsigned char>(buffer[3]));

                // Handle 0xFF escaping if necessary (rare for NAWS, but possible)
                // If width/height bytes could be 255 (IAC), they'd be sent as IAC IAC.
                // This simple parsing doesn't handle that. A more robust parser would
                // scan the 4 bytes for IAC IAC sequences. Assuming standard clients.

                if (width == 0 || width > kMaxTerminalDimension ||
                    height == 0 || height > kMaxTerminalDimension) {
                    GANL_TELNET_DEBUG(conn, "Warning: Received out-of-range NAWS: Width=" << width
                        << ", Height=" << height << ". Resetting to defaults.");
                    width = kDefaultTerminalWidth;
                    height = kDefaultTerminalHeight;
                }

                GANL_TELNET_DEBUG(conn, "Received NAWS: Width=" << width << ", Height=" << height);
                updateWidth(conn, width);  // Use the public update methods
                updateHeight(conn, height);
                context.nawsDataReceived = true; // Mark data received
            }
            else {
                GANL_TELNET_DEBUG(conn, "Warning: Received NAWS subnegotiation with incorrect data length: " << buffer.size() << " (expected 4)");
            }
            break;

        case TelnetOption::TTYPE:
            // IAC SB TTYPE IS <string> IAC SE  (IS = 0)
            // IAC SB TTYPE SEND IAC SE         (SEND = 1)
            if (buffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Warning: Received empty TTYPE subnegotiation.");
                break;
            }
            if (buffer[0] == 1 /* SEND */) {
                // Respond with our terminal type.  In Client mode, use
                // the configured clientTtype; in Server mode (where this
                // fires if we ever act as a relay), use a default.
                std::string termType = context.clientTtype.empty()
                    ? "XTERM-256COLOR" : context.clientTtype;
                if (termType.size() > kMaxTerminalTypeBytes) {
                    GANL_TELNET_DEBUG(conn, "Warning: Truncating TTYPE response from " << termType.size()
                        << " to " << kMaxTerminalTypeBytes << " bytes.");
                    termType.resize(kMaxTerminalTypeBytes);
                }
                GANL_TELNET_DEBUG(conn, "Received TTYPE SEND. Responding with '" << termType << "'.");
                char sb_start[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SB), static_cast<char>(TelnetOption::TTYPE), 0 /* IS */ };
                char sb_end[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SE) };
                telnet_responses_out.append(sb_start, sizeof(sb_start));
                telnet_responses_out.append(termType.c_str(), termType.size());
                telnet_responses_out.append(sb_end, sizeof(sb_end));
                context.ttypeDataReceived = true;
            }
            else if (buffer[0] == 0 /* IS */) {
                if (buffer.size() > 1) {
                    std::string term(buffer.begin() + 1, buffer.end());
                    GANL_TELNET_DEBUG(conn, "Received TTYPE IS: '" << term << "'");
                    // Basic ANSI/Color check
                    std::string upperTerm = term;
                    std::transform(upperTerm.begin(), upperTerm.end(), upperTerm.begin(), ::toupper);
                    if (upperTerm.find("ANSI") != std::string::npos ||
                        upperTerm.find("XTERM") != std::string::npos ||
                        upperTerm.find("VT100") != std::string::npos ||
                        upperTerm.find("LINUX") != std::string::npos ||
                        upperTerm.find("COLOR") != std::string::npos)
                    {
                        GANL_TELNET_DEBUG(conn, "Assuming ANSI support based on TTYPE.");
                        context.setSupportsANSI(true);
                        // Check for specific color support?
                        // if (upperTerm.find("256") != std::string::npos) { /* more colors */ }
                    }
                    else {
                        GANL_TELNET_DEBUG(conn, "Assuming NO ANSI support based on TTYPE.");
                        context.setSupportsANSI(false);
                    }
                    context.ttypeDataReceived = true; // Mark data received
                    // Optional: Send SB TTYPE SEND again to request next type if client supports multiple?
                }
                else {
                    GANL_TELNET_DEBUG(conn, "Warning: Received TTYPE IS with no type string.");
                }
            }
            else {
                GANL_TELNET_DEBUG(conn, "Warning: Received TTYPE subnegotiation with unknown command: " << static_cast<int>(buffer[0]));
            }
            break;

        case TelnetOption::NEW_ENVIRON: // RFC 1572
            // IAC SB NEW-ENVIRON IS <VAR val...> <USERVAR val...> IAC SE
            // IAC SB NEW-ENVIRON SEND <VAR / USERVAR ...> IAC SE
            GANL_TELNET_DEBUG(conn, "Processing NEW-ENVIRON subnegotiation");
            if (buffer.empty()) break;
            if (buffer[0] == kNewEnvironIs || buffer[0] == kNewEnvironInfo) {
                GANL_TELNET_DEBUG(conn, "Received NEW-ENVIRON " << (buffer[0] == kNewEnvironIs ? "IS" : "INFO") << ".");
                const auto entries = parseNewEnvironEntries(buffer, 1);
                for (const auto& entry : entries) {
                    const std::string upperName = uppercaseCopy(entry.name);
                    const std::string upperValue = uppercaseCopy(entry.value);

                    if (upperName == "TERM") {
                        if (upperValue.find("ANSI") != std::string::npos ||
                            upperValue.find("XTERM") != std::string::npos ||
                            upperValue.find("VT100") != std::string::npos ||
                            upperValue.find("LINUX") != std::string::npos ||
                            upperValue.find("COLOR") != std::string::npos)
                        {
                            context.setSupportsANSI(true);
                        }
                    }
                    else if (upperName == "COLORTERM") {
                        context.setSupportsANSI(!entry.value.empty() &&
                            upperValue != "0" && upperValue != "FALSE" && upperValue != "OFF" && upperValue != "NO");
                    }
                    else if (upperName == "MXP") {
                        context.setSupportsMXP(!entry.value.empty() &&
                            upperValue != "0" && upperValue != "FALSE" && upperValue != "OFF" && upperValue != "NO");
                    }
                    else if (upperName == "COLUMNS") {
                        uint16_t width = 0;
                        if (parseUint16InRange(entry.value, width)) {
                            updateWidth(conn, width);
                        }
                    }
                    else if (upperName == "LINES") {
                        uint16_t height = 0;
                        if (parseUint16InRange(entry.value, height)) {
                            updateHeight(conn, height);
                        }
                    }
                }
                context.newEnvironDataReceived = true; // Mark received
            }
            else if (buffer[0] == kNewEnvironSend) {
                GANL_TELNET_DEBUG(conn, "Received NEW-ENVIRON SEND request...");
                const auto requestedEntries = parseNewEnvironEntries(buffer, 1);
                std::vector<NewEnvironEntry> responseEntries;

                auto addResponse = [&](const std::string& name, const std::string& value, bool isUserVar) {
                    if (!value.empty()) {
                        responseEntries.push_back(NewEnvironEntry{name, value, isUserVar});
                    }
                };

                const std::string termValue = context.clientTtype.empty() ? "Hydra" : context.clientTtype;
                const std::string columnsValue = std::to_string(
                    context.mode == NegotiationMode::Client ? context.clientWidth : context.state.width);
                const std::string linesValue = std::to_string(
                    context.mode == NegotiationMode::Client ? context.clientHeight : context.state.height);
                const std::string colorTermValue = context.state.supportsANSI ? "truecolor" : "";
                const std::string mxpValue = context.state.supportsMXP ? "1" : "0";

                auto addRequested = [&](const std::string& requestedName, bool requestedUserVar) {
                    const std::string upperName = uppercaseCopy(requestedName);
                    if (upperName == "TERM") {
                        addResponse("TERM", termValue, false);
                    }
                    else if (upperName == "COLUMNS") {
                        addResponse("COLUMNS", columnsValue, false);
                    }
                    else if (upperName == "LINES") {
                        addResponse("LINES", linesValue, false);
                    }
                    else if (upperName == "COLORTERM") {
                        addResponse("COLORTERM", colorTermValue, requestedUserVar);
                    }
                    else if (upperName == "MXP") {
                        addResponse("MXP", mxpValue, true);
                    }
                };

                if (requestedEntries.empty()) {
                    addResponse("TERM", termValue, false);
                    addResponse("COLUMNS", columnsValue, false);
                    addResponse("LINES", linesValue, false);
                    if (!colorTermValue.empty()) {
                        addResponse("COLORTERM", colorTermValue, true);
                    }
                    addResponse("MXP", mxpValue, true);
                }
                else {
                    for (const auto& requested : requestedEntries) {
                        addRequested(requested.name, requested.isUserVar);
                    }
                }

                if (!responseEntries.empty()) {
                    const char sb_start[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SB),
                        static_cast<char>(TelnetOption::NEW_ENVIRON),
                        static_cast<char>(kNewEnvironIs)
                    };
                    const char sb_end[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SE)
                    };
                    telnet_responses_out.append(sb_start, sizeof(sb_start));
                    for (const auto& entry : responseEntries) {
                        appendNewEnvironEntry(telnet_responses_out, entry);
                    }
                    telnet_responses_out.append(sb_end, sizeof(sb_end));
                }
                context.newEnvironDataReceived = true; // Mark request received/processed
            }
            break;

        case TelnetOption::CHARSET: // RFC 2066
            // Handles client request: IAC SB CHARSET REQUEST <separators> <charsets...> IAC SE
            // Responds with: IAC SB CHARSET ACCEPTED <charset> IAC SE or IAC SB CHARSET REJECTED IAC SE
            GANL_TELNET_DEBUG(conn, "Processing CHARSET subnegotiation");
            if (buffer.empty()) {
                GANL_TELNET_DEBUG(conn, "Received empty CHARSET subnegotiation buffer.");
                break;
            }

            if (buffer[0] == 1 /* REQUEST */) {
                GANL_TELNET_DEBUG(conn, "Received CHARSET REQUEST.");

                // RFC 2066: If we have an outstanding CHARSET REQUEST of our
                // own, ignore the client's REQUEST and wait for the client to
                // respond to ours.
                //
                if (context.charsetRequestPending) {
                    GANL_TELNET_DEBUG(conn, "Ignoring client CHARSET REQUEST (our REQUEST pending).");
                    break;
                }

                // --- Simple Policy: Always try to accept UTF-8 if offered ---
                // A more complex policy would parse buffer[1] (separators) and
                // the list of charsets starting from buffer[2], checking against
                // a list of supported charsets on the server.

                // For now, let's assume we always support and prefer UTF-8
                // TODO: Add parsing of client's requested list if needed.
                bool weSupportUtf8 = true; // Assume server supports UTF-8

                if (weSupportUtf8) {
                    const char* acceptedCharset = "UTF-8";
                    GANL_TELNET_DEBUG(conn, "Responding CHARSET ACCEPTED " << acceptedCharset);

                    // Construct the response: IAC SB CHARSET ACCEPTED <charset> IAC SE
                    char sb_start[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SB),
                        static_cast<char>(TelnetOption::CHARSET),
                        2 /* ACCEPTED */
                    };
                    char sb_end[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SE)
                    };

                    // --- Fill in the append calls ---
                    telnet_responses_out.append(sb_start, sizeof(sb_start));
                    telnet_responses_out.append(acceptedCharset, strlen(acceptedCharset));
                    telnet_responses_out.append(sb_end, sizeof(sb_end));
                    // --- End of filled-in part ---

                    // Update context encoding using its own method
                    context.setEncoding(EncodingType::Utf8);
                    context.charsetDataReceived = true; // Mark settled

                }
                else {
                    // We don't support any requested charset (or just rejecting)
                    GANL_TELNET_DEBUG(conn, "Responding CHARSET REJECTED");

                    // Construct the response: IAC SB CHARSET REJECTED IAC SE
                    char sb_reject[] = {
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SB),
                        static_cast<char>(TelnetOption::CHARSET),
                        3, /* REJECTED */
                        static_cast<char>(TelnetCommand::IAC),
                        static_cast<char>(TelnetCommand::SE)
                    };
                    telnet_responses_out.append(sb_reject, sizeof(sb_reject));

                    context.setEncoding(EncodingType::Ascii); // Fallback
                    context.charsetDataReceived = true; // Mark settled (negatively)
                }

            }
            else if (buffer[0] == 2 /* ACCEPTED */) {
                // Client accepted a charset we offered (if we sent REQUEST)
                //
                context.charsetRequestPending = false;
                std::string accepted(buffer.begin() + 1, buffer.end());
                GANL_TELNET_DEBUG(conn, "Received CHARSET ACCEPTED: " << accepted);
                EncodingType newEncoding = EncodingType::Ascii;
                if (accepted == "UTF-8") newEncoding = EncodingType::Utf8;
                else if (accepted == "ISO-8859-1") newEncoding = EncodingType::Latin1;
                context.setEncoding(newEncoding);
                context.charsetDataReceived = true;

            }
            else if (buffer[0] == 3 /* REJECTED */) {
                // Client rejected a charset we offered (if we sent REQUEST)
                //
                context.charsetRequestPending = false;
                GANL_TELNET_DEBUG(conn, "Received CHARSET REJECTED.");
                context.setEncoding(EncodingType::Ascii);
                context.charsetDataReceived = true;

            }
            else {
                // Handle other CHARSET subcommands if needed (TTABLE etc.)
                GANL_TELNET_DEBUG(conn, "Received unhandled CHARSET subcommand: " << static_cast<int>(buffer[0]));
            }
            break; // End CHARSET case

        default:
            GANL_TELNET_DEBUG(conn, "Ignoring subnegotiation data for unhandled OPT " << static_cast<int>(opt));
            break;
        }

        // Clear buffer only after processing is complete
        context.subnegotiationBuffer.clear();
    }

    void TelnetProtocolHandler::sendTelnetCommand(IoBuffer& buffer, TelnetCommand cmd, TelnetOption opt) {
        char data[3];
        data[0] = static_cast<char>(TelnetCommand::IAC);
        data[1] = static_cast<char>(cmd);
        data[2] = static_cast<char>(opt);
        buffer.append(data, 3);
    }

} // namespace ganl
