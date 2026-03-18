/*! \file lua_bytecode.cpp
 * \brief Lua 5.4 bytecode deserializer — standalone, no Lua headers.
 *
 * Reads the binary format produced by lua_dump().  Reference:
 *   Lua 5.4 source: ldump.c / lundump.c
 */

#include "lua_bytecode.h"

#include <cstring>

// Lua 5.4 dump signature and magic values.
//
static const uint8_t LUA_SIGNATURE[] = { 0x1B, 'L', 'u', 'a' };
static const uint8_t LUAC_DATA[] = { 0x19, 0x93, '\r', '\n', 0x1A, '\n' };
static constexpr uint8_t LUAC_VERSION = 0x54;  // Lua 5.4
static constexpr uint8_t LUAC_FORMAT  = 0;
static constexpr int64_t  LUAC_INT    = 0x5678;
static constexpr double    LUAC_NUM   = 370.5;

// ---------------------------------------------------------------
// Reader helper — a simple cursor over a byte buffer.
// ---------------------------------------------------------------

struct bc_reader {
    const uint8_t *data;
    size_t len;
    size_t pos;
    bool ok;

    bc_reader(const uint8_t *d, size_t l)
        : data(d), len(l), pos(0), ok(true) {}

    bool has(size_t n) const { return pos + n <= len; }

    uint8_t read_byte() {
        if (!has(1)) { ok = false; return 0; }
        return data[pos++];
    }

    void read_bytes(void *dst, size_t n) {
        if (!has(n)) { ok = false; return; }
        memcpy(dst, data + pos, n);
        pos += n;
    }

    bool match(const void *expected, size_t n) {
        if (!has(n)) { ok = false; return false; }
        bool m = (memcmp(data + pos, expected, n) == 0);
        pos += n;
        if (!m) ok = false;
        return m;
    }

    // Lua uses a variable-length unsigned integer encoding for sizes.
    // lundump.c LoadUnsigned: accumulate 7-bit groups, high bit = more.
    //
    size_t read_size() {
        size_t x = 0;
        int b = read_byte();
        if (!ok) return 0;
        while ((b & 0x80) == 0) {
            x = (x << 7) | b;
            b = read_byte();
            if (!ok) return 0;
        }
        x = (x << 7) | (b & 0x7F);
        return x;
    }

    int64_t read_integer() {
        int64_t v = 0;
        read_bytes(&v, 8);
        return v;
    }

    double read_number() {
        double v = 0;
        read_bytes(&v, 8);
        return v;
    }

    int read_int() {
        int v = 0;
        read_bytes(&v, 4);
        return v;
    }

    std::string read_string() {
        size_t sz = read_size();
        if (!ok) return "";
        if (sz == 0) return "";
        // In Lua 5.4 dump format, sz is the length + 1.
        // The actual string length is sz - 1.
        sz -= 1;
        if (!has(sz)) { ok = false; return ""; }
        std::string s(reinterpret_cast<const char *>(data + pos), sz);
        pos += sz;
        return s;
    }
};

// ---------------------------------------------------------------
// Proto loader (recursive).
// ---------------------------------------------------------------

static bool load_proto(bc_reader &r, lua_bc_proto *p, const std::string &parent_source);

static bool load_constants(bc_reader &r, lua_bc_proto *p) {
    size_t n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    p->constants.resize(n);
    for (size_t i = 0; i < n; i++) {
        uint8_t t = r.read_byte();
        if (!r.ok) return false;
        lua_bc_constant &k = p->constants[i];
        k.type = static_cast<lua_bc_const_type>(t);
        k.ival = 0;
        k.fval = 0.0;
        switch (t) {
        case LUA_BC_TNIL:
        case LUA_BC_TFALSE:
        case LUA_BC_TTRUE:
            break;
        case LUA_BC_TINT:
            k.ival = r.read_integer();
            break;
        case LUA_BC_TFLOAT:
            k.fval = r.read_number();
            break;
        case LUA_BC_TSHRSTR:
        case LUA_BC_TLNGSTR:
            k.sval = r.read_string();
            break;
        default:
            r.ok = false;
            return false;
        }
    }
    return r.ok;
}

static bool load_upvalues(bc_reader &r, lua_bc_proto *p) {
    size_t n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    p->upvalues.resize(n);
    for (size_t i = 0; i < n; i++) {
        p->upvalues[i].instack = r.read_byte();
        p->upvalues[i].idx = r.read_byte();
        p->upvalues[i].kind = r.read_byte();
        if (!r.ok) return false;
    }
    return true;
}

static bool load_protos(bc_reader &r, lua_bc_proto *p,
                         const std::string &parent_source) {
    size_t n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    p->protos.resize(n);
    for (size_t i = 0; i < n; i++) {
        if (!load_proto(r, &p->protos[i], parent_source))
            return false;
    }
    return true;
}

static bool skip_debug(bc_reader &r, lua_bc_proto *p) {
    // Line info.
    size_t n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    for (size_t i = 0; i < n; i++) {
        r.read_byte();
        if (!r.ok) return false;
    }

    // Abs line info.
    n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    for (size_t i = 0; i < n; i++) {
        r.read_int();  // pc
        r.read_int();  // line
        if (!r.ok) return false;
    }

    // Local variables.
    n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    for (size_t i = 0; i < n; i++) {
        r.read_string();  // varname
        r.read_int();     // startpc
        r.read_int();     // endpc
        if (!r.ok) return false;
    }

    // Upvalue names.
    n = r.read_size();
    if (!r.ok || n > 1000000) return false;
    for (size_t i = 0; i < n; i++) {
        r.read_string();
        if (!r.ok) return false;
    }

    (void)p;
    return true;
}

static bool load_proto(bc_reader &r, lua_bc_proto *p,
                        const std::string &parent_source) {
    // Source name.
    p->source = r.read_string();
    if (!r.ok) return false;
    if (p->source.empty()) p->source = parent_source;

    p->linedefined = r.read_int();
    p->lastlinedefined = r.read_int();
    p->numparams = r.read_byte();
    p->is_vararg = r.read_byte();
    p->maxstacksize = r.read_byte();
    if (!r.ok) return false;

    // Instructions.
    size_t ncode = r.read_size();
    if (!r.ok || ncode > 1000000) return false;
    p->code.resize(ncode);
    for (size_t i = 0; i < ncode; i++) {
        uint32_t w = 0;
        r.read_bytes(&w, 4);
        if (!r.ok) return false;
        p->code[i].raw = w;
    }

    // Constants.
    if (!load_constants(r, p)) return false;

    // Upvalues.
    if (!load_upvalues(r, p)) return false;

    // Nested protos.
    if (!load_protos(r, p, p->source)) return false;

    // Debug info (skip).
    if (!skip_debug(r, p)) return false;

    return r.ok;
}

// ---------------------------------------------------------------
// Top-level entry point.
// ---------------------------------------------------------------

bool lua_bc_load(const uint8_t *data, size_t len, lua_bc_chunk *out) {
    if (nullptr == data || nullptr == out) return false;
    if (len < 4) return false;

    bc_reader r(data, len);

    // Header: signature.
    if (!r.match(LUA_SIGNATURE, sizeof(LUA_SIGNATURE))) return false;

    // Version.
    out->version = r.read_byte();
    if (!r.ok || out->version != LUAC_VERSION) return false;

    // Format.
    out->format = r.read_byte();
    if (!r.ok || out->format != LUAC_FORMAT) return false;

    // LUAC_DATA magic.
    if (!r.match(LUAC_DATA, sizeof(LUAC_DATA))) return false;

    // Type sizes.
    out->insn_size     = r.read_byte();  // sizeof(Instruction) = 4
    out->lua_int_size  = r.read_byte();  // sizeof(lua_Integer) = 8
    out->lua_num_size  = r.read_byte();  // sizeof(lua_Number) = 8
    if (!r.ok) return false;
    if (out->insn_size != 4 || out->lua_int_size != 8 || out->lua_num_size != 8)
        return false;

    // Check integer.
    int64_t check_int = r.read_integer();
    if (!r.ok || check_int != LUAC_INT) return false;

    // Check number.
    double check_num = r.read_number();
    if (!r.ok || check_num != LUAC_NUM) return false;

    // Number of upvalues for main proto.
    out->num_upvalues = r.read_byte();
    if (!r.ok) return false;

    // Load main proto.
    if (!load_proto(r, &out->main, "")) return false;

    return r.ok;
}
