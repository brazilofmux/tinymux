#ifndef GANL_TELNET_PROTOCOL_HANDLER_H
#define GANL_TELNET_PROTOCOL_HANDLER_H

#include "protocol_handler.h"
#include <map>
#include <string>
#include <vector>
#include <chrono> // For timeout tracking

namespace ganl {

    // Telnet Options (ensure these values match your defines or an enum)
    enum class TelnetOption : unsigned char {
        BINARY      = 0,
        ECHO        = 1,
        SGA         = 3,
        TTYPE       = 24,
        EOR         = 25,
        NAWS        = 31,
        NEW_ENVIRON = 39,
        CHARSET     = 42,
        STARTTLS    = 46,
        UNKNOWN     = 255
    };

    // Telnet Commands (ensure these are correct)
    enum class TelnetCommand : unsigned char {
        SE = 240, NOP = 241, DM = 242, BRK = 243, IP = 244,
        AO = 245, AYT = 246, EC = 247, EL = 248, GA = 249,
        SB = 250, WILL = 251, WONT = 252, DO = 253, DONT = 254,
        IAC = 255
    };

    class TelnetProtocolHandler : public ProtocolHandler {
    public:
        TelnetProtocolHandler();
        ~TelnetProtocolHandler() override;

        bool createProtocolContext(ConnectionHandle conn) override;
        void destroyProtocolContext(ConnectionHandle conn) override;

        virtual bool canOfferStartTls() const { return true; /* TODO: Make configurable */ }
        void onNegotiationTimeout(ConnectionHandle conn); // Called by Connection

        void startNegotiation(ConnectionHandle conn, IoBuffer& telnet_responses_out) override;
        bool processInput(ConnectionHandle conn, IoBuffer& decrypted_in,
            IoBuffer& app_data_out, IoBuffer& telnet_responses_out,
            bool consumeInput = true) override;
        bool formatOutput(ConnectionHandle conn, IoBuffer& app_data_in,
            IoBuffer& formatted_out, bool consumeInput = true) override;

        NegotiationStatus getNegotiationStatus(ConnectionHandle conn) override;

        bool setEncoding(ConnectionHandle conn, EncodingType encoding) override;
        EncodingType getEncoding(ConnectionHandle conn) override;

        ProtocolState getProtocolState(ConnectionHandle conn) override;
        void updateWidth(ConnectionHandle conn, uint16_t width) override;
        void updateHeight(ConnectionHandle conn, uint16_t height) override;

        std::string getLastProtocolErrorString(ConnectionHandle conn) override;

        bool isStartTlsNegotiated(ConnectionHandle conn);
        bool isReadyForApplication(ConnectionHandle conn);

    protected:
        enum class ParserState { Normal, IAC, Command, Subnegotiation, Subnegotiation_IAC };

        enum class OptionNegotiationState {
            Idle, SentWill, SentWont, SentDo, SentDont,
            ActiveWill, ActiveDo, RejectedWill, RejectedDo,
            ReceivedWill, ReceivedDo
        };

        void handleTelnetStandaloneCommand(ConnectionHandle conn, TelnetCommand cmd, IoBuffer& telnet_responses_out);
        void handleTelnetOptionNegotiation(ConnectionHandle conn, TelnetCommand cmd, TelnetOption opt, IoBuffer& telnet_responses_out);
        void processSubnegotiationData(ConnectionHandle conn, TelnetOption opt, IoBuffer& telnet_responses_out);
        void sendTelnetCommand(IoBuffer& buffer, TelnetCommand cmd, TelnetOption opt);
        void updateNegotiationStatus(ConnectionHandle conn);

        // Get the current state of an option negotiation
        OptionNegotiationState getOptionState(ConnectionHandle conn, TelnetOption opt) const;

    private:
        struct TelnetContext {
            ProtocolState state;
            std::vector<char> inputBuffer;
            bool sawCR{ false };

            ParserState parserState{ ParserState::Normal };
            TelnetCommand lastCmd{ TelnetCommand::NOP };
            TelnetOption lastOpt{ TelnetOption::UNKNOWN };

            std::vector<char> subnegotiationBuffer;

            NegotiationStatus currentNegotiationStatus{ NegotiationStatus::InProgress };
            bool negotiationTimedOut{ false };
            std::chrono::steady_clock::time_point negotiationStartTime;

            std::map<TelnetOption, OptionNegotiationState> optionStates;

            // --- Maps track our initial offers/requests ---
            std::map<TelnetOption, bool> weSentDo;   // Tracks if we sent DO for a specific option
            std::map<TelnetOption, bool> weSentWill; // Tracks if we sent WILL for a specific option
            // --- Individual flags removed ---

            // --- Flags indicating subnegotiation completion ---
            bool nawsDataReceived{ false };
            bool ttypeDataReceived{ false };
            bool newEnvironDataReceived{ false };
            bool charsetDataReceived{ false };
            bool startTlsWillReceived{ false }; // Set when client WILLs STARTTLS

            std::string lastError;

            // Helper to check if an option's state is settled
            bool isStateSettled(TelnetOption opt) const {
                // Corrected lookup: use .count()/.at() for safety or rely on default construction
                auto it = optionStates.find(opt);
                if (it == optionStates.end()) {
                    // If we never started negotiation for it (not in map), it's trivially settled.
                    // Check if we *intended* to negotiate it via weSentDo/weSentWill maps.
                    bool intended = (weSentDo.count(opt) && weSentDo.at(opt)) || (weSentWill.count(opt) && weSentWill.at(opt));
                    return !intended || negotiationTimedOut; // Settled if not intended, or if intended but timed out
                }


                switch (it->second) {
                case OptionNegotiationState::ActiveWill:
                case OptionNegotiationState::ActiveDo:
                case OptionNegotiationState::RejectedWill:
                case OptionNegotiationState::RejectedDo:
                case OptionNegotiationState::ReceivedWill:
                case OptionNegotiationState::ReceivedDo:
                case OptionNegotiationState::SentWont:
                case OptionNegotiationState::SentDont:
                    return true; // Final states are settled
                case OptionNegotiationState::Idle: // Should ideally not be in map if truly Idle, but handle defensively
                case OptionNegotiationState::SentWill:
                case OptionNegotiationState::SentDo:
                    return negotiationTimedOut; // Pending states only settled if timed out
                }
                return false; // Should not happen
            }

            // Returns true if encoding was changed, false otherwise.
            bool setEncoding(EncodingType newEncoding);
        };

        std::map<ConnectionHandle, TelnetContext> contexts_;
        std::chrono::seconds negotiationTimeoutDuration_{ 10 }; // Configurable timeout

        /**
         * Handle Telnet subnegotiation data
         *
         * Called when a complete IAC SB <option> ... IAC SE sequence is detected.
         * Derived classes can override this to handle specific options.
         *
         * @param conn The connection handle
         * @param option The telnet option code (e.g., NAWS, TTYPE)
         * @param subnegotiationData Buffer containing the raw data between SB and SE (excluding IAC escapes)
         * @param telnet_responses_out Buffer for any telnet responses to be sent
         */
        virtual void handleTelnetSubnegotiation(ConnectionHandle conn,
                                               TelnetOption option,
                                               const IoBuffer& subnegotiationData,
                                               IoBuffer& telnet_responses_out)
        {
            // Base implementation is empty - derived classes can override
            // to handle application-specific telnet options
        }
    };

} // namespace ganl

#endif // GANL_TELNET_PROTOCOL_HANDLER_H
