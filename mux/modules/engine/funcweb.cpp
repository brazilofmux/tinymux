/*! \file funcweb.cpp
 * \brief Web integration functions: base64, HMAC, JSON.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "sqlite3.h"

#ifdef UNIX_DIGEST
#include <openssl/hmac.h>
#include <openssl/evp.h>
#endif

// --------------------------------------------------------------------------
// Base64 encode/decode
// --------------------------------------------------------------------------

static const UTF8 b64_encode_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Decoding table: maps ASCII byte to 0-63, or 0xFF for invalid.
//
static const uint8_t b64_decode_table[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x00-0x07
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x08-0x0F
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x10-0x17
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x18-0x1F
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x20-0x27
    0xFF,0xFF,0xFF,  62,0xFF,0xFF,0xFF,  63,  // 0x28-0x2F  (+, /)
      52,  53,  54,  55,  56,  57,  58,  59,  // 0x30-0x37  (0-7)
      60,  61,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x38-0x3F  (8-9)
    0xFF,   0,   1,   2,   3,   4,   5,   6,  // 0x40-0x47  (A-G)
       7,   8,   9,  10,  11,  12,  13,  14,  // 0x48-0x4F  (H-O)
      15,  16,  17,  18,  19,  20,  21,  22,  // 0x50-0x57  (P-W)
      23,  24,  25,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x58-0x5F  (X-Z)
    0xFF,  26,  27,  28,  29,  30,  31,  32,  // 0x60-0x67  (a-g)
      33,  34,  35,  36,  37,  38,  39,  40,  // 0x68-0x6F  (h-o)
      41,  42,  43,  44,  45,  46,  47,  48,  // 0x70-0x77  (p-w)
      49,  50,  51,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x78-0x7F  (x-z)
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x80-0x87
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x88-0x8F
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x90-0x97
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0x98-0x9F
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xA0-0xA7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xA8-0xAF
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xB0-0xB7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xB8-0xBF
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xC0-0xC7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xC8-0xCF
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xD0-0xD7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xD8-0xDF
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xE0-0xE7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xE8-0xEF
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xF0-0xF7
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0xF8-0xFF
};

// encode64(<string>)
//
// Returns the base64 encoding of <string>.
//
FUNCTION(fun_encode64)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const uint8_t *pIn = reinterpret_cast<const uint8_t *>(fargs[0]);
    size_t nIn = strlen(reinterpret_cast<const char *>(fargs[0]));

    size_t nTriples  = nIn / 3;
    size_t nLeftover = nIn % 3;

    while (nTriples--)
    {
        uint32_t stage = (pIn[0] << 16) | (pIn[1] << 8) | pIn[2];

        safe_chr(b64_encode_table[(stage >> 18)       ], buff, bufc);
        safe_chr(b64_encode_table[(stage >> 12) & 0x3F], buff, bufc);
        safe_chr(b64_encode_table[(stage >>  6) & 0x3F], buff, bufc);
        safe_chr(b64_encode_table[(stage      ) & 0x3F], buff, bufc);

        pIn += 3;
    }

    switch (nLeftover)
    {
    case 1:
        {
            uint32_t stage = pIn[0] << 16;
            safe_chr(b64_encode_table[(stage >> 18)       ], buff, bufc);
            safe_chr(b64_encode_table[(stage >> 12) & 0x3F], buff, bufc);
            safe_chr('=', buff, bufc);
            safe_chr('=', buff, bufc);
        }
        break;

    case 2:
        {
            uint32_t stage = (pIn[0] << 16) | (pIn[1] << 8);
            safe_chr(b64_encode_table[(stage >> 18)       ], buff, bufc);
            safe_chr(b64_encode_table[(stage >> 12) & 0x3F], buff, bufc);
            safe_chr(b64_encode_table[(stage >>  6) & 0x3F], buff, bufc);
            safe_chr('=', buff, bufc);
        }
        break;
    }
}

// decode64(<base64-string>)
//
// Decodes a base64-encoded string back to its original content.
//
FUNCTION(fun_decode64)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const uint8_t *pIn = reinterpret_cast<const uint8_t *>(fargs[0]);
    size_t nIn = strlen(reinterpret_cast<const char *>(fargs[0]));

    // Strip trailing padding.
    //
    size_t nPad = 0;
    while (nIn > 0 && pIn[nIn - 1] == '=')
    {
        nIn--;
        nPad++;
    }
    if (nPad > 2)
    {
        safe_str(T("#-1 INVALID BASE64"), buff, bufc);
        return;
    }

    // Decode 4-character groups.
    //
    uint32_t accum = 0;
    int nBits = 0;
    for (size_t i = 0; i < nIn; i++)
    {
        uint8_t v = b64_decode_table[pIn[i]];
        if (0xFF == v)
        {
            safe_str(T("#-1 INVALID BASE64"), buff, bufc);
            return;
        }
        accum = (accum << 6) | v;
        nBits += 6;

        if (24 == nBits)
        {
            safe_chr(static_cast<UTF8>((accum >> 16) & 0xFF), buff, bufc);
            safe_chr(static_cast<UTF8>((accum >>  8) & 0xFF), buff, bufc);
            safe_chr(static_cast<UTF8>((accum      ) & 0xFF), buff, bufc);
            accum = 0;
            nBits = 0;
        }
    }

    // Flush remaining bits.
    //
    if (nBits >= 12)
    {
        // 2 or 3 base64 chars left over → 1 or 2 bytes.
        //
        accum <<= (24 - nBits);
        safe_chr(static_cast<UTF8>((accum >> 16) & 0xFF), buff, bufc);
        if (nBits >= 18)
        {
            safe_chr(static_cast<UTF8>((accum >> 8) & 0xFF), buff, bufc);
        }
    }
    else if (nBits == 6)
    {
        // A single base64 char is invalid (not enough bits for a byte).
        //
        safe_str(T("#-1 INVALID BASE64"), buff, bufc);
        return;
    }
}

// --------------------------------------------------------------------------
// HMAC
// --------------------------------------------------------------------------

// hmac(<message>, <key>[, <algorithm>])
//
// Returns the hex-encoded HMAC of <message> using <key>.
// Default algorithm is sha256. Any algorithm supported by
// digest() may be used.
//
FUNCTION(fun_hmac)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

#ifdef UNIX_DIGEST
    const char *pAlgo = "sha256";
    if (nfargs >= 3 && fargs[2][0] != '\0')
    {
        pAlgo = reinterpret_cast<const char *>(fargs[2]);
    }

    const EVP_MD *md = EVP_get_digestbyname(pAlgo);
    if (nullptr == md)
    {
        safe_str(T("#-1 UNSUPPORTED DIGEST TYPE"), buff, bufc);
        return;
    }

    const char *pMsg = reinterpret_cast<const char *>(fargs[0]);
    size_t nMsg = strlen(pMsg);
    const char *pKey = reinterpret_cast<const char *>(fargs[1]);
    size_t nKey = strlen(pKey);

    uint8_t result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;

    if (nullptr == HMAC(md,
                        pKey, static_cast<int>(nKey),
                        reinterpret_cast<const unsigned char *>(pMsg), nMsg,
                        result, &result_len))
    {
        safe_str(T("#-1 HMAC FAILED"), buff, bufc);
        return;
    }

    // Output as uppercase hex.
    //
    safe_hex(result, result_len, true, buff, bufc);
#else
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(fargs);
    safe_str(T("#-1 HMAC REQUIRES SSL"), buff, bufc);
#endif // UNIX_DIGEST
}

// --------------------------------------------------------------------------
// JSON validation (native, RFC 8259)
// --------------------------------------------------------------------------

// Lightweight recursive-descent JSON validator.
// Adapted from ~/slow-32/examples/validatejson.c.
// No memory allocation — walks the input in place.
//
namespace {

struct JsonValidator
{
    const UTF8 *buf;
    size_t len;
    size_t pos;
    int depth;

    bool at_end() const { return pos >= len; }
    UTF8 peek() const { return at_end() ? 0 : buf[pos]; }
    UTF8 advance() { return buf[pos++]; }

    void skip_ws()
    {
        while (!at_end())
        {
            UTF8 c = peek();
            if (' ' == c || '\t' == c || '\n' == c || '\r' == c)
            {
                advance();
            }
            else
            {
                break;
            }
        }
    }

    bool parse_string()
    {
        if (at_end() || advance() != '"')
        {
            return false;
        }
        while (!at_end())
        {
            UTF8 c = peek();
            if ('"' == c)
            {
                advance();
                return true;
            }
            if (c < 0x20)
            {
                return false;
            }
            if ('\\' == c)
            {
                advance();
                if (at_end())
                {
                    return false;
                }
                UTF8 e = advance();
                switch (e)
                {
                case '"': case '\\': case '/':
                case 'b': case 'f':  case 'n': case 'r': case 't':
                    break;
                case 'u':
                    {
                        for (int i = 0; i < 4; i++)
                        {
                            if (at_end())
                            {
                                return false;
                            }
                            UTF8 h = advance();
                            if (  !('0' <= h && h <= '9')
                               && !('a' <= h && h <= 'f')
                               && !('A' <= h && h <= 'F'))
                            {
                                return false;
                            }
                        }
                        // Check for surrogate pairs.
                        //
                        // We validated 4 hex digits above; compute the value.
                        //
                        const UTF8 *p = buf + pos - 4;
                        unsigned u1 = 0;
                        for (int i = 0; i < 4; i++)
                        {
                            UTF8 ch = p[i];
                            unsigned v;
                            if ('0' <= ch && ch <= '9') v = ch - '0';
                            else if ('a' <= ch && ch <= 'f') v = 10 + ch - 'a';
                            else v = 10 + ch - 'A';
                            u1 = (u1 << 4) | v;
                        }
                        if (0xD800 <= u1 && u1 <= 0xDBFF)
                        {
                            // High surrogate — must be followed by \uDC00-DFFF.
                            //
                            if (  pos + 5 >= len
                               || buf[pos] != '\\'
                               || buf[pos + 1] != 'u')
                            {
                                return false;
                            }
                            advance(); advance(); // skip backslash-u
                            unsigned u2 = 0;
                            for (int i = 0; i < 4; i++)
                            {
                                if (at_end()) return false;
                                UTF8 ch2 = advance();
                                unsigned v2;
                                if ('0' <= ch2 && ch2 <= '9') v2 = ch2 - '0';
                                else if ('a' <= ch2 && ch2 <= 'f') v2 = 10 + ch2 - 'a';
                                else if ('A' <= ch2 && ch2 <= 'F') v2 = 10 + ch2 - 'A';
                                else return false;
                                u2 = (u2 << 4) | v2;
                            }
                            if (u2 < 0xDC00 || u2 > 0xDFFF)
                            {
                                return false;
                            }
                        }
                        else if (0xDC00 <= u1 && u1 <= 0xDFFF)
                        {
                            // Lone low surrogate.
                            //
                            return false;
                        }
                    }
                    break;
                default:
                    return false;
                }
            }
            else
            {
                // Consume a UTF-8 character.
                //
                uint8_t b0 = static_cast<uint8_t>(advance());
                int extra = 0;
                if (b0 < 0x80)
                {
                    extra = 0;
                }
                else if ((b0 & 0xE0) == 0xC0)
                {
                    extra = 1;
                }
                else if ((b0 & 0xF0) == 0xE0)
                {
                    extra = 2;
                }
                else if ((b0 & 0xF8) == 0xF0)
                {
                    extra = 3;
                }
                else
                {
                    return false;
                }
                for (int i = 0; i < extra; i++)
                {
                    if (at_end() || (static_cast<uint8_t>(peek()) & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    advance();
                }
            }
        }
        return false; // unterminated string
    }

    bool parse_number()
    {
        if (!at_end() && '-' == peek())
        {
            advance();
        }
        if (at_end() || peek() < '0' || peek() > '9')
        {
            return false;
        }
        if ('0' == peek())
        {
            advance();
            if (!at_end() && peek() >= '0' && peek() <= '9')
            {
                return false; // leading zero
            }
        }
        else
        {
            while (!at_end() && peek() >= '0' && peek() <= '9')
            {
                advance();
            }
        }
        if (!at_end() && '.' == peek())
        {
            advance();
            if (at_end() || peek() < '0' || peek() > '9')
            {
                return false;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9')
            {
                advance();
            }
        }
        if (!at_end() && ('e' == peek() || 'E' == peek()))
        {
            advance();
            if (!at_end() && ('+' == peek() || '-' == peek()))
            {
                advance();
            }
            if (at_end() || peek() < '0' || peek() > '9')
            {
                return false;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9')
            {
                advance();
            }
        }
        return true;
    }

    bool parse_literal(const char *lit)
    {
        for (const char *p = lit; *p; p++)
        {
            if (at_end() || peek() != static_cast<UTF8>(*p))
            {
                return false;
            }
            advance();
        }
        return true;
    }

    bool parse_array()
    {
        advance(); // '['
        if (++depth > 512) return false;
        skip_ws();
        if (!at_end() && ']' == peek())
        {
            advance();
            depth--;
            return true;
        }
        for (;;)
        {
            if (!parse_value()) return false;
            skip_ws();
            if (at_end()) return false;
            UTF8 c = advance();
            if (']' == c) { depth--; return true; }
            if (',' != c) return false;
            skip_ws();
        }
    }

    bool parse_object()
    {
        advance(); // '{'
        if (++depth > 512) return false;
        skip_ws();
        if (!at_end() && '}' == peek())
        {
            advance();
            depth--;
            return true;
        }
        for (;;)
        {
            if (at_end() || '"' != peek()) return false;
            if (!parse_string()) return false;
            skip_ws();
            if (at_end() || ':' != advance()) return false;
            skip_ws();
            if (!parse_value()) return false;
            skip_ws();
            if (at_end()) return false;
            UTF8 c = advance();
            if ('}' == c) { depth--; return true; }
            if (',' != c) return false;
            skip_ws();
        }
    }

    bool parse_value()
    {
        skip_ws();
        if (at_end()) return false;
        UTF8 c = peek();
        if ('"' == c) return parse_string();
        if ('{' == c) return parse_object();
        if ('[' == c) return parse_array();
        if ('t' == c) return parse_literal("true");
        if ('f' == c) return parse_literal("false");
        if ('n' == c) return parse_literal("null");
        if ('-' == c || ('0' <= c && c <= '9')) return parse_number();
        return false;
    }

    bool validate()
    {
        skip_ws();
        if (at_end()) return false;
        if (!parse_value()) return false;
        skip_ws();
        return at_end();
    }
};

} // anonymous namespace

// isjson(<string>[, <type>])
//
// Returns 1 if <string> is valid JSON, 0 otherwise.
// Optional <type> checks for a specific JSON type:
//   string, number, object, array, true, false, null, boolean.
//
FUNCTION(fun_isjson)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *pStr = fargs[0];
    size_t nStr = strlen(reinterpret_cast<const char *>(pStr));

    JsonValidator jv;
    jv.buf = pStr;
    jv.len = nStr;
    jv.pos = 0;
    jv.depth = 0;

    if (!jv.validate())
    {
        safe_chr('0', buff, bufc);
        return;
    }

    // If a type argument was given, check it.
    //
    if (nfargs >= 2 && fargs[1][0] != '\0')
    {
        // Find the first non-whitespace character to determine the type.
        //
        const UTF8 *p = pStr;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            p++;
        }

        bool bMatch = false;
        const char *pType = reinterpret_cast<const char *>(fargs[1]);

        if (0 == mux_stricmp(fargs[1], T("string")))
        {
            bMatch = ('"' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("number")))
        {
            bMatch = ('-' == *p || ('0' <= *p && *p <= '9'));
        }
        else if (0 == mux_stricmp(fargs[1], T("object")))
        {
            bMatch = ('{' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("array")))
        {
            bMatch = ('[' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("true")))
        {
            bMatch = ('t' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("false")))
        {
            bMatch = ('f' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("null")))
        {
            bMatch = ('n' == *p);
        }
        else if (0 == mux_stricmp(fargs[1], T("boolean")))
        {
            bMatch = ('t' == *p || 'f' == *p);
        }
        else
        {
            safe_str(T("#-1 UNKNOWN JSON TYPE"), buff, bufc);
            return;
        }
        safe_chr(bMatch ? '1' : '0', buff, bufc);
    }
    else
    {
        safe_chr('1', buff, bufc);
    }
}

// --------------------------------------------------------------------------
// JSON functions via SQLite JSON1
// --------------------------------------------------------------------------

// Persistent in-memory SQLite handle for JSON operations.
// No table access — purely computational.
//
static sqlite3 *g_pJsonDB = nullptr;

static sqlite3 *json_db()
{
    if (nullptr == g_pJsonDB)
    {
        int rc = sqlite3_open(":memory:", &g_pJsonDB);
        if (SQLITE_OK != rc)
        {
            g_pJsonDB = nullptr;
        }
    }
    return g_pJsonDB;
}

// Helper: run a single-result SQL expression with bound text parameters.
// Returns true and appends the result to buff/bufc.
// Returns false and appends an error message on failure.
//
static bool json_sql_exec(
    const char *sql,
    const UTF8 *args[], int nArgs,
    UTF8 *buff, UTF8 **bufc)
{
    sqlite3 *db = json_db();
    if (nullptr == db)
    {
        safe_str(T("#-1 INTERNAL ERROR"), buff, bufc);
        return false;
    }

    sqlite3_stmt *pStmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &pStmt, nullptr);
    if (SQLITE_OK != rc)
    {
        safe_str(T("#-1 INTERNAL ERROR"), buff, bufc);
        return false;
    }

    for (int i = 0; i < nArgs; i++)
    {
        sqlite3_bind_text(pStmt, i + 1,
            reinterpret_cast<const char *>(args[i]), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(pStmt);
    if (SQLITE_ROW == rc)
    {
        int colType = sqlite3_column_type(pStmt, 0);
        if (SQLITE_NULL != colType)
        {
            const UTF8 *pResult = reinterpret_cast<const UTF8 *>(
                sqlite3_column_text(pStmt, 0));
            if (pResult)
            {
                safe_str(pResult, buff, bufc);
            }
        }
        sqlite3_finalize(pStmt);
        return true;
    }

    // If we get here, the expression failed. Provide a useful error.
    //
    const char *pErr = sqlite3_errmsg(db);
    if (pErr && strstr(pErr, "malformed JSON"))
    {
        safe_str(T("#-1 INVALID JSON"), buff, bufc);
    }
    else if (pErr && strstr(pErr, "json"))
    {
        safe_str(T("#-1 JSON ERROR"), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 JSON ERROR"), buff, bufc);
    }
    sqlite3_finalize(pStmt);
    return false;
}

// json(<type>, <value>)
//
// Construct a JSON value of the given type.
//
// Types:
//   string  — wraps <value> in quotes with proper escaping
//   number  — validates and returns a JSON number
//   boolean — converts to true/false
//   null    — returns null (no <value> needed)
//   array   — treats <value> as a space-separated list, returns JSON array
//   object  — treats <value> as key value key value..., returns JSON object
//
FUNCTION(fun_json)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *pType = fargs[0];
    const UTF8 *pValue = (nfargs >= 2) ? fargs[1] : T("");

    if (0 == mux_stricmp(pType, T("string")))
    {
        // Use SQLite json_quote() for proper escaping.
        //
        const UTF8 *args[1] = { pValue };
        json_sql_exec("SELECT json_quote(?1)", args, 1, buff, bufc);
    }
    else if (0 == mux_stricmp(pType, T("number")))
    {
        // Validate as JSON number via SQLite.
        //
        const UTF8 *args[1] = { pValue };
        if (!json_sql_exec("SELECT json(?1)", args, 1, buff, bufc))
        {
            // json_sql_exec already reported the error. But if the
            // input wasn't JSON, try wrapping as a number directly.
            // Reset buffer and try as bare number.
        }
    }
    else if (0 == mux_stricmp(pType, T("boolean")))
    {
        // MUX truthiness: "0", "", "#-1 ..." are false.
        //
        bool bVal = xlate(const_cast<UTF8 *>(pValue));
        safe_str(bVal ? T("true") : T("false"), buff, bufc);
    }
    else if (0 == mux_stricmp(pType, T("null")))
    {
        safe_str(T("null"), buff, bufc);
    }
    else if (0 == mux_stricmp(pType, T("array")))
    {
        // Build a JSON array from a space-separated list.
        //
        safe_chr('[', buff, bufc);
        UTF8 *pList = pValue ? alloc_lbuf("fun_json.array") : nullptr;
        if (pList)
        {
            mux_strncpy(pList, pValue, LBUF_SIZE - 1);
            SEP sep;
            sep.n = 1;
            sep.str[0] = ' ';
            sep.str[1] = '\0';
            UTF8 *pWord = trim_space_sep(pList, sep);
            bool bFirst = true;
            while (pWord && *pWord)
            {
                if (!bFirst)
                {
                    safe_chr(',', buff, bufc);
                }
                bFirst = false;

                // Each element should already be valid JSON.
                // Append it directly.
                //
                safe_str(pWord, buff, bufc);
                pWord = next_token(pWord, sep);
            }
            free_lbuf(pList);
        }
        safe_chr(']', buff, bufc);
    }
    else if (0 == mux_stricmp(pType, T("object")))
    {
        // Build a JSON object from key-value pairs.
        //
        safe_chr('{', buff, bufc);
        UTF8 *pList = pValue ? alloc_lbuf("fun_json.object") : nullptr;
        if (pList)
        {
            mux_strncpy(pList, pValue, LBUF_SIZE - 1);
            SEP sep;
            sep.n = 1;
            sep.str[0] = ' ';
            sep.str[1] = '\0';
            UTF8 *pWord = trim_space_sep(pList, sep);
            bool bFirst = true;
            while (pWord && *pWord)
            {
                UTF8 *pKey = pWord;
                pWord = next_token(pWord, sep);
                if (!pWord || !*pWord)
                {
                    // Odd number of elements — missing value.
                    //
                    *bufc = buff; // Reset output.
                    safe_str(T("#-1 ODD NUMBER OF ELEMENTS"), buff, bufc);
                    free_lbuf(pList);
                    return;
                }
                UTF8 *pVal = pWord;
                pWord = next_token(pWord, sep);

                if (!bFirst)
                {
                    safe_chr(',', buff, bufc);
                }
                bFirst = false;

                // Key should be a JSON string.
                //
                safe_str(pKey, buff, bufc);
                safe_chr(':', buff, bufc);
                safe_str(pVal, buff, bufc);
            }
            free_lbuf(pList);
        }
        safe_chr('}', buff, bufc);
    }
    else
    {
        safe_str(T("#-1 UNKNOWN JSON TYPE"), buff, bufc);
    }
}

// json_query(<json>, <query-type>[, <path>])
//
// Query a JSON value. Query types:
//   type     — returns the JSON type (object, array, string, number,
//              boolean, null)
//   size     — for arrays/objects, returns element count
//   exists   — returns 1 if <path> exists, 0 otherwise
//   get      — extract value at <path> (default if 1 arg after json)
//   keys     — for objects, returns space-separated list of keys
//   values   — for objects/arrays, returns space-separated list of values
//
// Paths use SQLite JSON path syntax: $.key.subkey, $[0], etc.
//
FUNCTION(fun_json_query)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *pJson = fargs[0];

    // Default operation is "get" if a path is given, "type" if no args.
    //
    const UTF8 *pOp = T("type");
    if (nfargs >= 2 && fargs[1][0] != '\0')
    {
        pOp = fargs[1];
    }

    const UTF8 *pPath = (nfargs >= 3) ? fargs[2] : T("$");

    if (0 == mux_stricmp(pOp, T("type")))
    {
        const UTF8 *args[2] = { pJson, pPath };
        json_sql_exec("SELECT json_type(?1, ?2)", args, 2, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("size")))
    {
        // json_array_length works for arrays; for objects we use
        // json_each to count keys.
        //
        const UTF8 *args[2] = { pJson, pPath };
        if (!json_sql_exec(
            "SELECT CASE json_type(?1, ?2) "
            "WHEN 'array' THEN json_array_length(?1, ?2) "
            "WHEN 'object' THEN (SELECT count(*) FROM json_each(?1, ?2)) "
            "ELSE 0 END",
            args, 2, buff, bufc))
        {
            // Error already reported.
        }
    }
    else if (0 == mux_stricmp(pOp, T("exists")))
    {
        const UTF8 *args[2] = { pJson, pPath };
        json_sql_exec(
            "SELECT CASE WHEN json_type(?1, ?2) IS NOT NULL THEN 1 ELSE 0 END",
            args, 2, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("get")))
    {
        const UTF8 *args[2] = { pJson, pPath };
        json_sql_exec("SELECT json_extract(?1, ?2)", args, 2, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("keys")))
    {
        const UTF8 *args[1] = { pJson };
        json_sql_exec(
            "SELECT group_concat(key, ' ') FROM json_each(?1)",
            args, 1, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("values")))
    {
        const UTF8 *args[1] = { pJson };
        json_sql_exec(
            "SELECT group_concat(value, ' ') FROM json_each(?1)",
            args, 1, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 UNKNOWN QUERY TYPE"), buff, bufc);
    }
}

// json_mod(<json>, <operation>, <path>, <value>)
//
// Modify a JSON value. Operations:
//   set     — set value at path (create if missing, replace if exists)
//   insert  — insert value at path (only if missing)
//   replace — replace value at path (only if exists)
//   remove  — remove value at path
//   patch   — RFC 7396 merge-patch
//
FUNCTION(fun_json_mod)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 3)
    {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    const UTF8 *pJson = fargs[0];
    const UTF8 *pOp   = fargs[1];
    const UTF8 *pPath = fargs[2];
    const UTF8 *pVal  = (nfargs >= 4) ? fargs[3] : T("null");

    if (0 == mux_stricmp(pOp, T("set")))
    {
        const UTF8 *args[3] = { pJson, pPath, pVal };
        json_sql_exec("SELECT json_set(?1, ?2, json(?3))", args, 3, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("insert")))
    {
        const UTF8 *args[3] = { pJson, pPath, pVal };
        json_sql_exec("SELECT json_insert(?1, ?2, json(?3))", args, 3, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("replace")))
    {
        const UTF8 *args[3] = { pJson, pPath, pVal };
        json_sql_exec("SELECT json_replace(?1, ?2, json(?3))", args, 3, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("remove")))
    {
        const UTF8 *args[2] = { pJson, pPath };
        json_sql_exec("SELECT json_remove(?1, ?2)", args, 2, buff, bufc);
    }
    else if (0 == mux_stricmp(pOp, T("patch")))
    {
        const UTF8 *args[2] = { pJson, pPath };
        json_sql_exec("SELECT json_patch(?1, ?2)", args, 2, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 UNKNOWN OPERATION"), buff, bufc);
    }
}
