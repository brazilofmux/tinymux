#include "telnet_protocol_handler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <vector> // Ensure vector is included for subnegotiationBuffer

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_TELNET_DEBUG(conn, x) \
    do { std::cerr << "[TelnetPH:" << conn << "] " << x << std::endl; } while (0)
#else
#define GANL_TELNET_DEBUG(conn, x) do {} while (0)
#endif

namespace ganl {

    // Helper to safely check map value (returns false if key not found)
    inline bool checkMapFlag(const std::map<TelnetOption, bool>& map, TelnetOption key) {
        auto it = map.find(key);
        return (it != map.end() && it->second);
    }

    // --- TelnetContext Method Implementation ---

    bool TelnetProtocolHandler::TelnetContext::setEncoding(EncodingType newEncoding) {
        // Optional: Add logging here if desired
        // GANL_TELNET_DEBUG(???, "Context attempting to set encoding to " << static_cast<int>(newEncoding));
        // Need a way to pass ConnectionHandle for logging if needed, or log generically.

        if (state.encoding != newEncoding) {
            state.encoding = newEncoding;
            // Log the change maybe? Requires access to ConnectionHandle or logging context.
            return true; // Encoding changed
        }
        return false; // Encoding was already set to this value
    }

    // --- Constructor / Destructor ---
    TelnetProtocolHandler::TelnetProtocolHandler() {
        std::cerr << "[TelnetPH] Handler Created." << std::endl;
    }

    TelnetProtocolHandler::~TelnetProtocolHandler() {
        std::cerr << "[TelnetPH] Handler Destroyed." << std::endl;
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
        context.state.width = 80;
        context.state.height = 24;
        context.parserState = ParserState::Normal;
        context.currentNegotiationStatus = NegotiationStatus::InProgress;
        context.negotiationTimedOut = false;
        context.negotiationStartTime = std::chrono::steady_clock::now();

        // Maps are default constructed (empty)

        GANL_TELNET_DEBUG(conn, "Context created. Map size: " << contexts_.size());
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
    void TelnetProtocolHandler::updateWidth(ConnectionHandle conn, uint16_t width) { /* ... */ }
    void TelnetProtocolHandler::updateHeight(ConnectionHandle conn, uint16_t height) { /* ... */ }
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


    // --- Data Processing ---
    bool TelnetProtocolHandler::processInput(ConnectionHandle conn, IoBuffer& decrypted_in,
        IoBuffer& app_data_out, IoBuffer& telnet_responses_out, bool consumeInput) {
        auto it = contexts_.find(conn);
        if (it == contexts_.end()) {
            GANL_TELNET_DEBUG(conn, "Error: processInput called with no context.");
            return false;
        }

        bool processedOk = true;
        size_t initialReadable = decrypted_in.readableBytes();
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
                        std::string line(context.inputBuffer.data(), context.inputBuffer.size());
                        app_data_out.append(line.data(), line.size());
                        app_data_out.append("\n", 1); // Normalize to LF
                        context.inputBuffer.clear();
                    }
                }
                else if (uc == '\n') {
                    if (!context.sawCR) { // Avoid double newline for CRLF
                        if (!context.inputBuffer.empty()) {
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
                        std::string line(context.inputBuffer.data(), context.inputBuffer.size());
                        app_data_out.append(line.data(), line.size());
                        app_data_out.append("\n", 1); // Normalize to LF
                        context.inputBuffer.clear();
                    }
                    context.sawCR = false;
                }
                else {
                    // Regular character
                    context.inputBuffer.push_back(static_cast<char>(uc));
                    context.sawCR = false;
                }
                break;

            case ParserState::IAC:
                if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
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
                    context.subnegotiationBuffer.push_back(static_cast<char>(uc));
                }
                break;

            case ParserState::Subnegotiation_IAC:
                if (uc == static_cast<unsigned char>(TelnetCommand::SE)) {
                    processSubnegotiationData(conn, context.lastOpt, telnet_responses_out);
                    context.parserState = ParserState::Normal;
                    // Check if subnegotiation affects overall status
                    if (context.currentNegotiationStatus == NegotiationStatus::InProgress) {
                        updateNegotiationStatus(conn);
                    }
                }
                else if (uc == static_cast<unsigned char>(TelnetCommand::IAC)) {
                    context.subnegotiationBuffer.push_back(static_cast<char>(uc));
                    context.parserState = ParserState::Subnegotiation;
                }
                else {
                    GANL_TELNET_DEBUG(conn, "Sub: Error! IAC followed by invalid byte " << uc << ". Ignoring SB.");
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

        while (current < end) {
            char ch = *current;
            consumed++;
            current++;

            // TODO: Add ANSI/MXP processing based on context.state.supportsANSI etc.

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
        }
        else if (context.negotiationTimedOut) {
            // Timeout occurred before essentials were settled. Force completion.
            GANL_TELNET_DEBUG(conn, "Negotiation timed out before essentials settled. Status -> Completed (forced).");
            context.currentNegotiationStatus = NegotiationStatus::Completed;
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

        GANL_TELNET_DEBUG(conn, "Handling negotiation: CMD=" << static_cast<int>(cmd) << ", OPT=" << static_cast<int>(opt));

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
                case TelnetOption::EOR: context.state.telnetEOR = true; break;
                case TelnetOption::NAWS: break; // Ready for SB NAWS
                case TelnetOption::TTYPE: {
                    GANL_TELNET_DEBUG(conn, "Client WILL TTYPE. Sending SB TTYPE SEND.");
                    char sb_req[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SB),
                                      static_cast<char>(TelnetOption::TTYPE), 1 /* SEND */,
                                      static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SE) };
                    telnet_responses_out.append(sb_req, sizeof(sb_req));
                } break;
                case TelnetOption::NEW_ENVIRON: {
                    // Optionally send SB NEW-ENVIRON SEND IS ... request here
                    GANL_TELNET_DEBUG(conn, "Client WILL NEW-ENVIRON. Optionally send SB NEW-ENVIRON SEND/IS...");
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
                case TelnetOption::SGA: accept = true; context.state.telnetSGA = true; break;
                case TelnetOption::EOR: accept = true; context.state.telnetEOR = true; break;
                    // Add others we are willing to DO if client WILLs
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
                case TelnetOption::EOR: context.state.telnetEOR = false; break;
                case TelnetOption::STARTTLS: context.startTlsWillReceived = false; break;
                    // Reset other states if necessary
                }
            }
            else { // Unsolicited WONT (Confirmation)
                GANL_TELNET_DEBUG(conn, "Client WONT " << static_cast<int>(opt) << " (unsolicited confirmation)");
                newState = OptionNegotiationState::ReceivedWill; // Mark negotiation settled
                switch (opt) { // Ensure state reflects disabled
                case TelnetOption::SGA: context.state.telnetSGA = false; break;
                case TelnetOption::EOR: context.state.telnetEOR = false; break;
                }
            }
            break;

        case TelnetCommand::DO: // Client DO X (Wants *us* to enable X)
            if (weOffered) { // Response to our WILL
                GANL_TELNET_DEBUG(conn, "Client DO " << static_cast<int>(opt) << " (matches our WILL)");
                newState = OptionNegotiationState::ActiveWill;
                switch (opt) { // Ensure state reflects acceptance
                case TelnetOption::SGA: context.state.telnetSGA = true; break;
                case TelnetOption::EOR: context.state.telnetEOR = true; break;
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
                    case TelnetOption::SGA: context.state.telnetSGA = true; break;
                    case TelnetOption::EOR: context.state.telnetEOR = true; break;
                    case TelnetOption::BINARY: context.state.telnetBinary = true; break;
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
                case TelnetOption::SGA: context.state.telnetSGA = false; break;
                case TelnetOption::EOR: context.state.telnetEOR = false; break;
                case TelnetOption::CHARSET: break; // Update charset state if needed
                }
            }
            else { // Unsolicited DONT
                GANL_TELNET_DEBUG(conn, "Client DONT " << static_cast<int>(opt) << " (unsolicited request/confirmation)");
                newState = OptionNegotiationState::ReceivedDo; // Mark negotiation settled
                switch (opt) { // Ensure state reflects disabled
                case TelnetOption::SGA: context.state.telnetSGA = false; break;
                case TelnetOption::EOR: context.state.telnetEOR = false; break;
                case TelnetOption::BINARY: context.state.telnetBinary = false; break;
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
                GANL_TELNET_DEBUG(conn, "Received TTYPE SEND request. Responding with default type.");
                const char* termType = "XTERM-256COLOR"; // Or "ANSI", "VT100", "MUX-DEFAULT"
                char sb_start[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SB), static_cast<char>(TelnetOption::TTYPE), 0 /* IS */ };
                char sb_end[] = { static_cast<char>(TelnetCommand::IAC), static_cast<char>(TelnetCommand::SE) };
                telnet_responses_out.append(sb_start, sizeof(sb_start));
                telnet_responses_out.append(termType, strlen(termType));
                telnet_responses_out.append(sb_end, sizeof(sb_end));
                // We don't mark ttypeDataReceived yet, wait for their *next* response if they send SEND again.
                // Or mark it received now if this is the only type we offer? Let's mark it.
                context.ttypeDataReceived = true; // Assume one round is enough unless we need more specific types
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
                        context.state.supportsANSI = true; // Update ProtocolState
                        // Check for specific color support?
                        // if (upperTerm.find("256") != std::string::npos) { /* more colors */ }
                    }
                    else {
                        GANL_TELNET_DEBUG(conn, "Assuming NO ANSI support based on TTYPE.");
                        context.state.supportsANSI = false;
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
            GANL_TELNET_DEBUG(conn, "Processing NEW-ENVIRON subnegotiation (Basic Parsing)");
            if (buffer.empty()) break;
            if (buffer[0] == 0 /* IS */) {
                // Parse variables sent by client
                GANL_TELNET_DEBUG(conn, "Received NEW-ENVIRON IS...");
                // TODO: Implement detailed parsing of VAR/USERVAR/VALUE sequences
                context.newEnvironDataReceived = true; // Mark received
            }
            else if (buffer[0] == 1 /* SEND */) {
                // Client requests specific variables
                GANL_TELNET_DEBUG(conn, "Received NEW-ENVIRON SEND request...");
                // TODO: Check requested VARs and respond with IAC SB NEW-ENVIRON IS ...
                // For now, just acknowledge receipt of request might be enough
                context.newEnvironDataReceived = true; // Mark request received/processed
            }
            else if (buffer[0] == 2 /* INFO */) {
                // Similar to IS but for unsolicited info
                GANL_TELNET_DEBUG(conn, "Received NEW-ENVIRON INFO...");
                context.newEnvironDataReceived = true;
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
                // ... (handling as before) ...
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
                // ... (handling as before) ...
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
