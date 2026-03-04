/*! \file telnet.cpp
 * \brief Telnet protocol handling.
 *
 * NVT character classification, state machine tables, option negotiation,
 * and the main input parser (process_input_helper).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

/*! \brief Table to quickly classify characters recieved from the wire with
 * their Telnet meaning.
 *
 * The use of this table reduces the size of the state table.
 *
 * Class  0 - Any byte.    Class  5 - BRK  (0xF3)  Class 10 - WONT (0xFC)
 * Class  1 - BS   (0x08)  Class  5 - IP   (0xF4)  Class 11 - DO   (0xFD)
 * Class  2 - LF   (0x0A)  Class  5 - AO   (0xF5)  Class 12 - DONT (0xFE)
 * Class  3 - CR   (0x0D)  Class  6 - AYT  (0xF6)  Class 13 - IAC  (0xFF)
 * Class  1 - DEL  (0x7F)  Class  7 - EC   (0xF7)
 * Class  5 - EOR  (0xEF)  Class  5 - EL   (0xF8)
 * Class  4 - SE   (0xF0)  Class  5 - GA   (0xF9)
 * Class  5 - NOP  (0xF1)  Class  8 - SB   (0xFA)
 * Class  5 - DM   (0xF2)  Class  9 - WILL (0xFB)
 */

static const unsigned char nvt_input_xlat_table[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  2,  0,  0,  3,  0,  0,  // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  // 7

    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  5,  // E
    4,  5,  5,  5,  5,  5,  6,  7,  5,  5,  8,  9, 10, 11, 12, 13   // F
};

/*! \brief Table to map current telnet parsing state state and input to
 * specific actions and state changes.
 *
 * Action  0 - Nothing.
 * Action  1 - Accept CHR(X) (and transition to Normal state).
 * Action  2 - Erase Character.
 * Action  3 - Accept Line.
 * Action  4 - Transition to the Normal state.
 * Action  5 - Transition to Have_IAC state.
 * Action  6 - Transition to the Have_IAC_WILL state.
 * Action  7 - Transition to the Have_IAC_DONT state.
 * Action  8 - Transition to the Have_IAC_DO state.
 * Action  9 - Transition to the Have_IAC_WONT state.
 * Action 10 - Transition to the Have_IAC_SB state.
 * Action 11 - Transition to the Have_IAC_SB_IAC state.
 * Action 12 - Respond to IAC AYT and return to the Normal state.
 * Action 13 - Respond to IAC WILL X
 * Action 14 - Respond to IAC DONT X
 * Action 15 - Respond to IAC DO X
 * Action 16 - Respond to IAC WONT X
 * Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
 * Action 18 - Accept Completed Sub-option and transition to Normal state.
 */

static const int nvt_input_action_table[8][14] =
{
//    Any   BS   LF   CR   SE  NOP  AYT   EC   SB WILL DONT   DO WONT  IAC
    {   1,   2,   3,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   5  }, // Normal
    {   4,   4,   4,   4,   4,   4,  12,   2,  10,   6,   7,   8,   9,   1  }, // Have_IAC
    {  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,   4  }, // Have_IAC_WILL
    {  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,   4  }, // Have_IAC_DONT
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   4  }, // Have_IAC_DO
    {  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,   4  }, // Have_IAC_WONT
    {  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  11  }, // Have_IAC_SB
    {   0,   0,   0,   0,  18,   0,   0,   0,   0,   0,   0,   0,   0,  17  }, // Have_IAC_SB_IAC
};

/*! \brief Transmit a Telnet SB sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param chRequest Telnet SB command.
 */
static void send_sb(DESC *d, unsigned char chOption, unsigned char chRequest)
{
    UTF8 aSB[6] = { NVT_IAC, NVT_SB, 0, 0, NVT_IAC, NVT_SE };
    aSB[2] = chOption;
    aSB[3] = chRequest;
    queue_write_LEN(d, aSB, sizeof(aSB));
}

/*! \brief Transmit a Telnet SB sequence for the given option with the given payload
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param chRequest Telnet SB command.
 * \param pPayload  Pointer to the payload.
 * \param nPayload  Length of the payload.
 */
static void send_sb
(
    DESC *d,
    const unsigned char chOption,
    const unsigned char chRequest,
    const unsigned char *pPayload,
    const size_t nPayload
)
{
    const auto nMaximum = 6 + 2*nPayload;

    unsigned char buffer[100];
    auto pSB = buffer;
    if (sizeof(buffer) < nMaximum)
    {
        pSB = static_cast<unsigned char *>(MEMALLOC(nMaximum));
        if (nullptr == pSB)
        {
            return;
        }
    }

    pSB[0] = NVT_IAC;
    pSB[1] = NVT_SB;
    pSB[2] = chOption;
    pSB[3] = chRequest;

    auto p = &pSB[4];

    for (size_t loop = 0; loop < nPayload; loop++)
    {
        if (NVT_IAC == pPayload[loop])
        {
            *(p++) = NVT_IAC;
        }
        *(p++) = pPayload[loop];
    }
    *(p++) = NVT_IAC;
    *(p++) = NVT_SE;

    const size_t length = p - pSB;
    queue_write_LEN(d, pSB, length);

    if (pSB != buffer)
    {
        MEMFREE(pSB);
    }
}

/*! \brief Transmit a Telnet WILL sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_will(DESC *d, unsigned char chOption)
{
    UTF8 aWill[3] = { NVT_IAC, NVT_WILL, 0 };
    aWill[2] = chOption;
    queue_write_LEN(d, aWill, sizeof(aWill));
}

/*! \brief Transmit a Telnet DONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_dont(DESC *d, unsigned char chOption)
{
    UTF8 aDont[3] = { NVT_IAC, NVT_DONT, 0 };
    aDont[2] = chOption;
    queue_write_LEN(d, aDont, sizeof(aDont));
}

/*! \brief Transmit a Telnet DO sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_do(DESC *d, unsigned char chOption)
{
    UTF8 aDo[3]   = { NVT_IAC, NVT_DO,   0 };
    aDo[2] = chOption;
    queue_write_LEN(d, aDo, sizeof(aDo));
}

/*! \brief Transmit a Telnet WONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_wont(DESC *d, unsigned char chOption)
{
    unsigned char aWont[3] = { NVT_IAC, NVT_WONT, 0 };
    aWont[2] = chOption;
    queue_write_LEN(d, aWont, sizeof(aWont));
}

/*! \brief Return the other side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int him_state(const DESC *d, const unsigned char chOption)
{
    return d->nvt_him_state[chOption];
}

/*! \brief Return our side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int us_state(const DESC *d, const unsigned char chOption)
{
    return d->nvt_us_state[chOption];
}

void send_charset_request(DESC *d, bool fDefacto = false)
{
    if (  OPTION_YES == d->nvt_us_state[static_cast<unsigned char>(TELNET_CHARSET)]
       || (  fDefacto
          && OPTION_YES == d->nvt_him_state[static_cast<unsigned char>(TELNET_CHARSET)]))
    {
        const unsigned char aCharsets[] = ";UTF-8;ISO-8859-1;ISO-8859-2;US-ASCII;CP437";
        send_sb(d, TELNET_CHARSET, TELNETSB_REQUEST, aCharsets, sizeof(aCharsets)-1);
    }
}

void defacto_charset_check(DESC *d)
{
    if (  nullptr != d->ttype
       && OPTION_NO == d->nvt_us_state[static_cast<unsigned char>(TELNET_CHARSET)]
       && OPTION_YES == d->nvt_him_state[static_cast<unsigned char>(TELNET_CHARSET)]
       && mux_stricmp(d->ttype, T("mushclient")) == 0)
    {
        send_charset_request(d, true);
    }
}

/*! \brief Change the other side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option
 * \param iHimState One of the six option negotiation states.
 */
static void set_him_state(DESC *d, unsigned char chOption, int iHimState)
{
    d->nvt_him_state[chOption] = iHimState;

    if (OPTION_YES == iHimState)
    {
        if (TELNET_TTYPE == chOption)
        {
            send_sb(d, chOption, TELNETSB_SEND);
        }
        else if (TELNET_ENV == chOption)
        {
            // Request environment variables.
            //
            const unsigned char aEnvReq[2] = { TELNETSB_VAR, TELNETSB_USERVAR };
            send_sb(d, chOption, TELNETSB_SEND, aEnvReq, 2);
        }
        else if (TELNET_BINARY == chOption)
        {
            enable_us(d, TELNET_BINARY);
        }
        else if (TELNET_CHARSET == chOption)
        {
            defacto_charset_check(d);
        }
    }
    else if (OPTION_NO == iHimState)
    {
        if (TELNET_BINARY == chOption)
        {
            disable_us(d, TELNET_BINARY);
        }
    }
}

/*! \brief Change our side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param iUsState  One of the six option negotiation states.
 */
static void set_us_state(DESC *d, unsigned char chOption, int iUsState)
{
    d->nvt_us_state[chOption] = iUsState;

    if (OPTION_YES == iUsState)
    {
        if (TELNET_EOR == chOption)
        {
            enable_us(d, TELNET_SGA);
        }
        else if (TELNET_CHARSET == chOption)
        {
            send_charset_request(d);
        }
    }
    else if (OPTION_NO == iUsState)
    {
        if (TELNET_EOR == chOption)
        {
            disable_us(d, TELNET_SGA);
        }
        else if (TELNET_CHARSET == chOption)
        {
            defacto_charset_check(d);
        }
    }
}

/*! \brief Determine whether we want a particular option on his side of the
 * link to be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

static bool desired_him_option(DESC *d, unsigned char chOption)
{
    UNUSED_PARAMETER(d);

    if (  TELNET_NAWS    == chOption
       || TELNET_EOR     == chOption
       || TELNET_SGA     == chOption
       || TELNET_ENV     == chOption
       || TELNET_BINARY  == chOption
       || TELNET_CHARSET == chOption)
    {
        return true;
    }
    return false;
}

/*! \brief Determine whether we want a particular option on our side of the
 * link to be enabled.
 *
 * It doesn't make sense for NAWS to be enabled on the server side, and we
 * only negotiate SGA on our side if we have already successfully negotiated
 * the EOR option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

static bool desired_us_option(DESC *d, unsigned char chOption)
{
    return TELNET_EOR == chOption || TELNET_BINARY == chOption || TELNET_CHARSET == chOption || (TELNET_SGA == chOption
        && OPTION_YES == us_state(d, TELNET_EOR));
}

/*! \brief Start the process of negotiating the enablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void enable_him(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_NO:
        set_him_state(d, chOption, OPTION_WANTYES_EMPTY);
        send_do(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        set_him_state(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        set_him_state(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void disable_him(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_YES:
        set_him_state(d, chOption, OPTION_WANTNO_EMPTY);
        send_dont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        set_him_state(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        set_him_state(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Start the process of negotiating the enablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void enable_us(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_NO:
        set_us_state(d, chOption, OPTION_WANTYES_EMPTY);
        send_will(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        set_us_state(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        set_us_state(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void disable_us(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_YES:
        set_us_state(d, chOption, OPTION_WANTNO_EMPTY);
        send_wont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        set_us_state(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        set_us_state(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Begin initial telnet negotiations on a socket.
 *
 * The two sides of the connection may not agree on the following set of
 * options, and keep in mind that the successful negotiation of a particular
 * option may cause the negotiation of another option.
 *
 * Without this function, we are only react to client requests.
 *
 * \param d        Player connection on which the input arrived.
 */

/*! \brief Parse raw data from network connection into command lines and
 * Telnet indications.
 *
 * Once input has been received from a particular socket, it is given to this
 * function for initial parsing. While most clients do line editing on their
 * side, a raw telnet client is still capable of sending backspace (BS) and
 * Delete (DEL) to the server, so we perform basic editing on our side.
 *
 * TinyMUX only allows printable characters through, imposes a maximum line
 * length, and breaks lines at CRLF.
 *
 * \param d        Player connection on which the input arrived.
 * \param pBytes   Point to received bytes.
 * \param nBytes   Number of received bytes in above buffer.
 */
void process_input_helper(DESC *d, char *pBytes, int nBytes)
{
    char szUTF8[] = "UTF-8";
    char szISO8859_1[] = "ISO-8859-1";
    char szISO8859_2[] = "ISO-8859-2";
    char szCp437[] = "CP437";
    char szUSASCII[] = "US-ASCII";
    constexpr size_t nUTF8 = sizeof(szUTF8) - 1;
    constexpr size_t nISO8859_1 = sizeof(szISO8859_1) - 1;
    constexpr size_t nISO8859_2 = sizeof(szISO8859_2) - 1;
    constexpr size_t nCp437 = sizeof(szCp437) - 1;
    constexpr size_t nUSASCII = sizeof(szUSASCII) - 1;

    if (!d->raw_input_buf)
    {
        d->raw_input_buf = alloc_lbuf("process_input.raw");
        d->raw_input_at = d->raw_input_buf;
    }

    size_t nInputBytes = 0;
    size_t nLostBytes  = 0;

    auto p    = d->raw_input_at;
    auto pend = d->raw_input_buf + (LBUF_SIZE - 1);

    auto q    = d->aOption + d->nOption;
    const auto qend = d->aOption + SBUF_SIZE - 1;

    auto n = nBytes;
    while (n--)
    {
        const auto ch = static_cast<unsigned char>(*pBytes);
        const auto iAction = nvt_input_action_table[d->raw_input_state][nvt_input_xlat_table[ch]];
        switch (iAction)
        {
        case 1:
            // Action 1 - Accept CHR(X).
            //
            if (CHARSET_UTF8 == d->encoding)
            {
                // Execute UTF-8 state machine.
                //
                auto iColumn = cl_print_itt[static_cast<unsigned char>(ch)];
                auto iOffset = cl_print_sot[d->raw_codepoint_state];
                for (;;)
                {
                    int y = static_cast<char>(cl_print_sbt[iOffset]);
                    if (0 < y)
                    {
                        // RUN phrase.
                        //
                        if (iColumn < y)
                        {
                            d->raw_codepoint_state = cl_print_sbt[iOffset+1];
                            break;
                        }
                        else
                        {
                            iColumn = static_cast<unsigned char>(iColumn - y);
                            iOffset += 2;
                        }
                    }
                    else
                    {
                        // COPY phrase.
                        //
                        y = -y;
                        if (iColumn < y)
                        {
                            d->raw_codepoint_state = cl_print_sbt[iOffset+iColumn+1];
                            break;
                        }
                        else
                        {
                            iColumn = static_cast<unsigned char>(iColumn - y);
                            iOffset = static_cast<unsigned short>(iOffset + y + 1);
                        }
                    }
                }

                if (  1 == d->raw_codepoint_state - CL_PRINT_ACCEPTING_STATES_START
                   && p < pend)
                {
                    // Save the byte and reset the state machine.  This is
                    // the most frequently-occuring case.
                    //
                    *p++ = ch;
                    nInputBytes += d->raw_codepoint_length + 1;
                    d->raw_codepoint_length = 0;
                    d->raw_codepoint_state = CL_PRINT_START_STATE;
                }
                else if (  d->raw_codepoint_state < CL_PRINT_ACCEPTING_STATES_START
                        && p < pend)
                {
                    // Save the byte and we're done for now.
                    //
                    *p++ = ch;
                    d->raw_codepoint_length++;
                }
                else
                {
                    // The code point is not printable or there isn't enough room.
                    // Back out any bytes in this code point.
                    //
                    if (pend <= p)
                    {
                        nLostBytes += d->raw_codepoint_length + 1;
                    }

                    p -= d->raw_codepoint_length;
                    if (p < d->raw_input_buf)
                    {
                        p = d->raw_input_buf;
                    }
                    d->raw_codepoint_length = 0;
                    d->raw_codepoint_state = CL_PRINT_START_STATE;
                }
            }
            else if (CHARSET_LATIN1 == d->encoding)
            {
                // CHARSET_LATIN1
                //
                if (mux_isprint_latin1(ch))
                {
                    // Convert this latin1 character to the internal UTF-8 form.
                    //
                    auto pUTF = latin1_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_LATIN2 == d->encoding)
            {
                // CHARSET_LATIN2
                //
                if (mux_isprint_latin2(ch))
                {
                    // Convert this latin2 character to the internal UTF-8 form.
                    //
                    auto pUTF = latin2_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_CP437 == d->encoding)
            {
                // CHARSET_CP437
                //
                if (mux_isprint_cp437(ch))
                {
                    // Convert this cp437 character to the internal UTF-8 form.
                    //
                    auto pUTF = cp437_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_ASCII == d->encoding)
            {
                // CHARSET_ASCII
                //
                if (mux_isprint_ascii(ch))
                {
                    if (p < pend)
                    {
                        *p++ = ch;
                        nInputBytes++;
                    }
                    else
                    {
                        nLostBytes++;
                    }
                }
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 0:
            // Action 0 - Nothing.
            //
            break;

        case 2:
            // Action 2 - Erase Character.
            //
            if (  CHARSET_UTF8 == d->encoding
               && 0 < d->raw_codepoint_length)
            {
                p -= d->raw_codepoint_length;
                if (p < d->raw_input_buf)
                {
                    p = d->raw_input_buf;
                }
                d->raw_codepoint_length = 0;
                d->raw_codepoint_state = CL_PRINT_START_STATE;
            }

            if (NVT_DEL == ch)
            {
                queue_string(d, T("\b \b"));
            }
            else
            {
                queue_string(d, T(" \b"));
            }

            // Rewind until we pass the first byte of a UTF-8 sequence.
            //
            while (d->raw_input_buf < p)
            {
                nInputBytes--;
                p--;
                if (utf8_FirstByte[static_cast<UTF8>(*p)] < UTF8_CONTINUE)
                {
                    break;
                }
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 3:
            // Action  3 - Accept Line.
            //
            if (  CHARSET_UTF8 == d->encoding
               && 0 < d->raw_codepoint_length)
            {
                p -= d->raw_codepoint_length;
                if (p < d->raw_input_buf)
                {
                    p = d->raw_input_buf;
                }
                d->raw_codepoint_length = 0;
                d->raw_codepoint_state = CL_PRINT_START_STATE;
            }

            *p = '\0';
            if (d->raw_input_buf < p)
            {
                save_command(d, d->raw_input_buf, p - d->raw_input_buf);
                p = d->raw_input_at = d->raw_input_buf;
            }
            break;

        case 4:
            // Action 4 - Transition to the Normal state.
            //
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 5:
            // Action  5 - Transition to Have_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC;
            break;

        case 6:
            // Action 6 - Transition to the Have_IAC_WILL state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WILL;
            break;

        case 7:
            // Action  7 - Transition to the Have_IAC_DONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DONT;
            break;

        case 8:
            // Action  8 - Transition to the Have_IAC_DO state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DO;
            break;

        case 9:
            // Action  9 - Transition to the Have_IAC_WONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WONT;
            break;

        case 10:
            // Action 10 - Transition to the Have_IAC_SB state.
            //
            q = d->aOption;
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            break;

        case 11:
            // Action 11 - Transition to the Have_IAC_SB_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB_IAC;
            break;

        case 12:
            // Action 12 - Respond to IAC AYT and return to the Normal state.
            //
            queue_string(d, T("\r\n[Yes]\r\n"));
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 13:
            // Action 13 - Respond to IAC WILL X
            //
            switch (him_state(d, ch))
            {
            case OPTION_NO:
                if (desired_him_option(d, ch))
                {
                    set_him_state(d, ch, OPTION_YES);
                    send_do(d, ch);
                }
                else
                {
                    send_dont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                set_him_state(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                set_him_state(d, ch, OPTION_WANTNO_EMPTY);
                send_dont(d, ch);
                break;

            default:
                set_him_state(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 14:
            // Action 14 - Respond to IAC DONT X
            //
            switch (us_state(d, ch))
            {
            case OPTION_YES:
                set_us_state(d, ch, OPTION_NO);
                send_wont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                set_us_state(d, ch, OPTION_WANTYES_EMPTY);
                send_will(d, ch);
                break;

            default:
                set_us_state(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 15:
            // Action 15 - Respond to IAC DO X
            //
            switch (us_state(d, ch))
            {
            case OPTION_NO:
                if (desired_us_option(d, ch))
                {
                    set_us_state(d, ch, OPTION_YES);
                    send_will(d, ch);
                }
                else
                {
                    send_wont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                set_us_state(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                set_us_state(d, ch, OPTION_WANTNO_EMPTY);
                send_wont(d, ch);
                break;

            default:
                set_us_state(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 16:
            // Action 16 - Respond to IAC WONT X
            //
            switch (him_state(d, ch))
            {
            case OPTION_NO:
                break;

            case OPTION_YES:
                set_him_state(d, ch, OPTION_NO);
                send_dont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                set_him_state(d, ch, OPTION_WANTYES_EMPTY);
                send_do(d, ch);
                break;

            default:
                set_him_state(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 17:
            // Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            if (  d->aOption <= q
               && q < qend)
            {
                *q++ = ch;
            }
            break;

        case 18:
            // Action 18 - Accept Completed Sub-option and transition to Normal state.
            //
            if (  d->aOption < q
               && q < qend)
            {
                const size_t m = q - d->aOption;
                switch (d->aOption[0])
                {
                case TELNET_NAWS:
                    if (5 == m)
                    {
                        d->width  = (d->aOption[1] << 8 ) | d->aOption[2];
                        d->height = (d->aOption[3] << 8 ) | d->aOption[4];
                    }
                    break;

                case TELNET_TTYPE:
                    if (  2 <= m
                       && TELNETSB_IS == d->aOption[1])
                    {
                        // Skip past the TTYPE and TELQUAL_IS bytes validating
                        // that terminal type information is an NVT ASCII
                        // string.
                        //
                        const auto nTermType = m-2;
                        const auto pTermType = &d->aOption[2];

                        auto fASCII = true;
                        for (size_t i = 0; i < nTermType; i++)
                        {
                            if (!mux_isprint_ascii(pTermType[i]))
                            {
                                fASCII = false;
                                break;
                            }
                        }

                        if (fASCII)
                        {
                            if (nullptr != d->ttype)
                            {
                                MEMFREE(d->ttype);
                                d->ttype = nullptr;
                            }
                            d->ttype = static_cast<UTF8 *>(MEMALLOC(nTermType+1));
                            memcpy(d->ttype, pTermType, nTermType);
                            d->ttype[nTermType] = '\0';

                            defacto_charset_check(d);
                        }
                    }
                    break;

                case TELNET_ENV:
                case TELNET_OLDENV:
                    if (  2 <= m
                       && (  TELNETSB_IS == d->aOption[1]
                          || TELNETSB_INFO == d->aOption[1]))
                    {
                        auto envPtr = &d->aOption[2];
                        while (envPtr < &d->aOption[m])
                        {
                            auto ch2 = *envPtr++;
                            if (  TELNETSB_USERVAR == ch2
                               || TELNETSB_VAR     == ch2)
                            {
                                const auto pVarnameStart = envPtr;
                                unsigned char *pVarnameEnd = nullptr;
                                unsigned char *pVarvalStart = nullptr;
                                unsigned char *pVarvalEnd = nullptr;

                                while (envPtr < &d->aOption[m])
                                {
                                    ch2 = *envPtr++;
                                    if (TELNETSB_VALUE == ch2)
                                    {
                                        pVarnameEnd = envPtr - 1;
                                        pVarvalStart = envPtr;

                                        while (envPtr < &d->aOption[m])
                                        {
                                            ch2 = *envPtr++;
                                            if (  TELNETSB_USERVAR == ch2
                                               || TELNETSB_VAR == ch2)
                                            {
                                                pVarvalEnd = envPtr - 1;
                                                break;
                                            }
                                        }

                                        if (envPtr == &d->aOption[m])
                                        {
                                            pVarvalEnd = envPtr;
                                        }
                                        break;
                                    }
                                }

                                if (  envPtr == &d->aOption[m]
                                   && nullptr == pVarnameEnd)
                                {
                                    pVarnameEnd = envPtr;
                                }

                                size_t nVarname = 0;
                                size_t nVarval = 0;

                                if (  nullptr != pVarnameStart
                                   && nullptr != pVarnameEnd)
                                {
                                    nVarname = pVarnameEnd - pVarnameStart;
                                }

                                if (  nullptr != pVarvalStart
                                   && nullptr != pVarvalEnd)
                                {
                                    nVarval = pVarvalEnd - pVarvalStart;
                                }

                                UTF8 varname[1024];
                                UTF8 varval[1024];
                                if (  nullptr != pVarvalStart
                                   && 0 < nVarname
                                   && nVarname < sizeof(varname) - 1
                                   && 0 < nVarval
                                   && nVarval < sizeof(varval) - 1)
                                {
                                    memcpy(varname, pVarnameStart, nVarname);
                                    varname[nVarname] = '\0';
                                    memcpy(varval, pVarvalStart, nVarval);
                                    varval[nVarval] = '\0';

                                    // This is a horrible, horrible nasty hack
                                    // to try and detect UTF8.  We do not even
                                    // try to figure out the other encodings
                                    // this way, and just default to Latin1 if
                                    // we can't get a UTF8 locale.
                                    //
                                    if (  mux_stricmp(varname, T("LC_CTYPE")) == 0
                                       || mux_stricmp(varname, T("LC_ALL")) == 0
                                       || mux_stricmp(varname, T("LANG")) == 0)
                                    {
                                        auto pEncoding = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(varval), '.'));
                                        if (nullptr != pEncoding)
                                        {
                                            pEncoding++;
                                        }
                                        else
                                        {
                                            pEncoding = &varval[0];
                                        }

                                        if (  mux_stricmp(pEncoding, T("utf-8")) == 0
                                           && CHARSET_UTF8 != d->encoding)
                                        {
                                            // Since we are changing to the
                                            // UTF-8 character set, the
                                            // printable state machine needs
                                            // to be initialized.
                                            //
                                            d->encoding = CHARSET_UTF8;
                                            d->negotiated_encoding = CHARSET_UTF8;
                                            d->raw_codepoint_state = CL_PRINT_START_STATE;

                                            enable_us(d, TELNET_BINARY);
                                            enable_him(d, TELNET_BINARY);
                                        }
                                    }
                                    else if (mux_stricmp(varname, T("USER")) == 0)
                                    {
                                        memcpy(d->username, varval, nVarval + 1);
                                    }

                                    // We can also get 'DISPLAY' here if we were
                                    // feeling masochistic, and actually use
                                    // Xterm functionality.
                                }
                            }
                        }
                    }
                    break;

                case TELNET_CHARSET:
                    if (2 <= m)
                    {
                        if (TELNETSB_ACCEPT == d->aOption[1])
                        {
                            const auto pCharset = &d->aOption[2];

                            if (  nUTF8 == m - 2
                               && memcmp(reinterpret_cast<char *>(pCharset), szUTF8, nUTF8) == 0)
                            {
                                if (CHARSET_UTF8 != d->encoding)
                                {
                                    // Since we are changing to the UTF-8
                                    // character set, the printable state machine
                                    // needs to be initialized.
                                    //
                                    d->encoding = CHARSET_UTF8;
                                    d->negotiated_encoding = CHARSET_UTF8;
                                    d->raw_codepoint_state = CL_PRINT_START_STATE;

                                    enable_us(d, TELNET_BINARY);
                                    enable_him(d, TELNET_BINARY);
                                }
                            }
                            else if (  nISO8859_1 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szISO8859_1, nISO8859_1) == 0)
                            {
                                d->encoding = CHARSET_LATIN1;
                                d->negotiated_encoding = CHARSET_LATIN1;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nISO8859_2 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szISO8859_2, nISO8859_2) == 0)
                            {
                                d->encoding = CHARSET_LATIN2;
                                d->negotiated_encoding = CHARSET_LATIN2;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nCp437 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szCp437, nCp437) == 0)
                            {
                                d->encoding = CHARSET_CP437;
                                d->negotiated_encoding = CHARSET_CP437;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nUSASCII == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szUSASCII, nUSASCII) == 0)
                            {
                                d->encoding = CHARSET_ASCII;
                                d->negotiated_encoding = CHARSET_ASCII;

                                disable_us(d, TELNET_BINARY);
                                disable_him(d, TELNET_BINARY);
                            }
                        }
                        else if (TELNETSB_REJECT == d->aOption[1])
                        {
                            // The client has replied that it doesn't even support
                            // Latin1/ISO-8859-1 accented characters.  Thus, we
                            // should probably record this to strip out any
                            // accents.
                            //
                            d->encoding = CHARSET_ASCII;
                            d->negotiated_encoding = CHARSET_ASCII;

                            disable_us(d, TELNET_BINARY);
                            disable_him(d, TELNET_BINARY);
                        }
                        else if (TELNETSB_REQUEST == d->aOption[1])
                        {
                            auto fRequestAcknowledged = false;
                            auto reqPtr = &d->aOption[2];
                            if (reqPtr < &d->aOption[m])
                            {
                                // NVT_IAC is not permitted as a separator.
                                // '[' might be the beginning of "[TTABLE]"
                                // <version>, but we don't support parsing
                                // and ignoring that.
                                //
                                auto chSep = *reqPtr++;
                                if (  NVT_IAC != chSep
                                   && '[' != chSep)
                                {
                                    auto pTermStart = reqPtr;

                                    while (reqPtr < &d->aOption[m])
                                    {
                                        auto ch3 = *reqPtr++;
                                        if (  chSep == ch3
                                           || reqPtr == &d->aOption[m])
                                        {
                                            const size_t nTerm = reqPtr - pTermStart - 1;

                                            // Process [pTermStart, pTermStart+nTermEnd)
                                            // We let the client determine priority by its order of the list.
                                            //
                                            if (  nUTF8 == nTerm
                                               && memcmp(reinterpret_cast<char *>(pTermStart), szUTF8, nUTF8) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                if (CHARSET_UTF8 != d->encoding)
                                                {
                                                    // Since we are changing to the UTF-8
                                                    // character set, the printable state machine
                                                    // needs to be initialized.
                                                    //
                                                    d->encoding = CHARSET_UTF8;
                                                    d->negotiated_encoding = CHARSET_UTF8;
                                                    d->raw_codepoint_state = CL_PRINT_START_STATE;

                                                    enable_us(d, TELNET_BINARY);
                                                    enable_him(d, TELNET_BINARY);
                                                }
                                                break;
                                            }
                                            else if (  nISO8859_1 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szISO8859_1, nISO8859_1) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_LATIN1;
                                                d->negotiated_encoding = CHARSET_LATIN1;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nISO8859_2 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szISO8859_2, nISO8859_2) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_LATIN2;
                                                d->negotiated_encoding = CHARSET_LATIN2;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nCp437 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szCp437, nCp437) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_CP437;
                                                d->negotiated_encoding = CHARSET_CP437;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nUSASCII== nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szUSASCII, nUSASCII) == 0)
                                            {
                                                fRequestAcknowledged = true;
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                d->encoding = CHARSET_ASCII;
                                                d->negotiated_encoding = CHARSET_ASCII;

                                                disable_us(d, TELNET_BINARY);
                                                disable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            pTermStart = reqPtr;
                                        }
                                    }
                                }
                            }

                            if (!fRequestAcknowledged)
                            {
                                send_sb(d, TELNET_CHARSET, TELNETSB_REJECT, nullptr, 0);
                            }
                        }
                    }
                }
            }
            q = d->aOption;
            d->raw_input_state = NVT_IS_NORMAL;
            break;
        }
        pBytes++;
    }

    if (  d->raw_input_buf < p
       && p <= pend)
    {
        d->raw_input_at = p;
    }
    else
    {
        free_lbuf(d->raw_input_buf);
        d->raw_input_buf = nullptr;
        d->raw_input_at = nullptr;
    }

    if (  d->aOption <= q
       && q < qend)
    {
        d->nOption = q - d->aOption;
    }
    else
    {
        d->nOption = 0;
    }
    d->input_tot  += nBytes;
    d->input_size += nInputBytes;
    d->input_lost += nLostBytes;
}
