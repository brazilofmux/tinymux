/*! \file mdbx_backend.cpp
 * \brief libmdbx implementation of IStorageBackend (attributes only).
 *
 * Key:   uint32_le object + uint32_le attrnum  (8 bytes)
 * Value: uint32_le owner  + uint32_le flags + uint32_le mod_count + raw bytes
 *
 * All integers are stored little-endian regardless of host byte order.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mdbx_backend.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Portable little-endian encode/decode for on-disk format.
//
static inline void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static inline uint32_t get_u32le(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// Key encoding: 8 bytes (object LE, attrnum LE).
//
void CMdbxBackend::encode_key(unsigned int object, unsigned int attrnum,
                              uint8_t buf[8])
{
    put_u32le(buf, object);
    put_u32le(buf + 4, attrnum);
}

// ---------------------------------------------------------------------------
// Lifecycle.
//

CMdbxBackend::CMdbxBackend()
    : m_env(nullptr), m_dbi(0), m_open(false)
{
}

CMdbxBackend::~CMdbxBackend()
{
    Close();
}

bool CMdbxBackend::Open(const char *path)
{
    if (m_open)
    {
        return false;
    }

    int rc = mdbx_env_create(&m_env);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    // Set geometry: 64 KB lower, 4 GB upper, 1 MB growth step.
    //
    rc = mdbx_env_set_geometry(m_env,
        64 * 1024,          // size_lower
        -1,                 // size_now (let libmdbx decide)
        4LL * 1024 * 1024 * 1024, // size_upper (4 GB)
        1024 * 1024,        // growth_step (1 MB)
        -1,                 // shrink_threshold (default)
        -1);                // pagesize (default)
    if (MDBX_SUCCESS != rc)
    {
        mdbx_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    rc = mdbx_env_open(m_env, path,
        MDBX_NOSUBDIR | MDBX_LIFORECLAIM,
        0644);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // Open the default (unnamed) database within a write transaction
    // so the DBI handle is created.
    //
    MDBX_txn *txn = nullptr;
    rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_READWRITE, &txn);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    rc = mdbx_dbi_open(txn, nullptr, MDBX_CREATE, &m_dbi);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        mdbx_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    rc = mdbx_txn_commit(txn);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    m_open = true;
    return true;
}

void CMdbxBackend::Close()
{
    if (m_open)
    {
        mdbx_env_close(m_env);
        m_env = nullptr;
        m_dbi = 0;
        m_open = false;
    }
}

bool CMdbxBackend::IsOpen() const
{
    return m_open;
}

// ---------------------------------------------------------------------------
// Single-attribute operations.
//

bool CMdbxBackend::Get(unsigned int object, unsigned int attrnum,
                       UTF8 *buf, size_t buflen, size_t *pLen,
                       int *owner, int *flags)
{
    if (!m_open)
    {
        *pLen = 0;
        return false;
    }

    uint8_t keybuf[8];
    encode_key(object, attrnum, keybuf);
    MDBX_val key = { keybuf, sizeof(keybuf) };
    MDBX_val data;

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        *pLen = 0;
        return false;
    }

    rc = mdbx_get(txn, m_dbi, &key, &data);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        *pLen = 0;
        return false;
    }

    if (data.iov_len < VALUE_HEADER_SIZE)
    {
        mdbx_txn_abort(txn);
        *pLen = 0;
        return false;
    }

    const uint8_t *p = static_cast<const uint8_t *>(data.iov_base);
    if (owner) *owner = static_cast<int>(get_u32le(p));
    if (flags) *flags = static_cast<int>(get_u32le(p + 4));
    // p + 8 is mod_count, not returned by Get.

    size_t vlen = data.iov_len - VALUE_HEADER_SIZE;
    if (vlen > buflen)
    {
        vlen = buflen;
    }
    memcpy(buf, p + VALUE_HEADER_SIZE, vlen);
    *pLen = vlen;

    mdbx_txn_abort(txn);
    return true;
}

bool CMdbxBackend::Put(unsigned int object, unsigned int attrnum,
                       const UTF8 *value, size_t len,
                       int owner, int flags)
{
    if (!m_open)
    {
        return false;
    }

    uint8_t keybuf[8];
    encode_key(object, attrnum, keybuf);
    MDBX_val key = { keybuf, sizeof(keybuf) };

    // Read existing mod_count to increment it.
    //
    uint32_t mod_count = 0;

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_READWRITE, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    MDBX_val old_data;
    rc = mdbx_get(txn, m_dbi, &key, &old_data);
    if (MDBX_SUCCESS == rc && old_data.iov_len >= VALUE_HEADER_SIZE)
    {
        const uint8_t *old_p = static_cast<const uint8_t *>(old_data.iov_base);
        mod_count = get_u32le(old_p + 8);
    }
    mod_count++;

    // Build new value: header + attribute bytes.
    //
    size_t total = VALUE_HEADER_SIZE + len;
    uint8_t header[VALUE_HEADER_SIZE];
    put_u32le(header, static_cast<uint32_t>(owner));
    put_u32le(header + 4, static_cast<uint32_t>(flags));
    put_u32le(header + 8, mod_count);

    // Use a single buffer for the put.
    //
    std::vector<uint8_t> valbuf(total);
    memcpy(valbuf.data(), header, VALUE_HEADER_SIZE);
    memcpy(valbuf.data() + VALUE_HEADER_SIZE, value, len);

    MDBX_val data = { valbuf.data(), total };
    rc = mdbx_put(txn, m_dbi, &key, &data, MDBX_UPSERT);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    rc = mdbx_txn_commit(txn);
    return MDBX_SUCCESS == rc;
}

bool CMdbxBackend::Del(unsigned int object, unsigned int attrnum)
{
    if (!m_open)
    {
        return false;
    }

    uint8_t keybuf[8];
    encode_key(object, attrnum, keybuf);
    MDBX_val key = { keybuf, sizeof(keybuf) };

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_READWRITE, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    rc = mdbx_del(txn, m_dbi, &key, nullptr);
    if (MDBX_SUCCESS != rc && MDBX_NOTFOUND != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    rc = mdbx_txn_commit(txn);
    return MDBX_SUCCESS == rc;
}

// ---------------------------------------------------------------------------
// Bulk operations.
//

bool CMdbxBackend::DelAll(unsigned int object)
{
    if (!m_open)
    {
        return false;
    }

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_READWRITE, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    MDBX_cursor *cursor = nullptr;
    rc = mdbx_cursor_open(txn, m_dbi, &cursor);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    // Seek to the first key with this object prefix.
    //
    uint8_t lo[8];
    encode_key(object, 0, lo);
    MDBX_val key = { lo, sizeof(lo) };
    MDBX_val data;

    rc = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE);
    while (MDBX_SUCCESS == rc)
    {
        if (key.iov_len != 8)
        {
            break;
        }
        uint32_t key_obj = get_u32le(static_cast<const uint8_t *>(key.iov_base));
        if (key_obj != object)
        {
            break;
        }
        rc = mdbx_cursor_del(cursor, MDBX_CURRENT);
        if (MDBX_SUCCESS != rc)
        {
            break;
        }
        rc = mdbx_cursor_get(cursor, &key, &data, MDBX_GET_CURRENT);
        if (MDBX_SUCCESS != rc)
        {
            rc = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT);
        }
    }

    mdbx_cursor_close(cursor);

    rc = mdbx_txn_commit(txn);
    return MDBX_SUCCESS == rc;
}

bool CMdbxBackend::GetAll(unsigned int object, AttrCallback cb)
{
    if (!m_open)
    {
        return false;
    }

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    MDBX_cursor *cursor = nullptr;
    rc = mdbx_cursor_open(txn, m_dbi, &cursor);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    uint8_t lo[8];
    encode_key(object, 0, lo);
    MDBX_val key = { lo, sizeof(lo) };
    MDBX_val data;

    rc = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE);
    while (MDBX_SUCCESS == rc)
    {
        if (key.iov_len != 8)
        {
            break;
        }
        const uint8_t *kp = static_cast<const uint8_t *>(key.iov_base);
        uint32_t key_obj = get_u32le(kp);
        if (key_obj != object)
        {
            break;
        }
        if (data.iov_len >= VALUE_HEADER_SIZE)
        {
            uint32_t attrnum = get_u32le(kp + 4);
            const uint8_t *vp = static_cast<const uint8_t *>(data.iov_base);
            int attr_owner = static_cast<int>(get_u32le(vp));
            int attr_flags = static_cast<int>(get_u32le(vp + 4));
            size_t vlen = data.iov_len - VALUE_HEADER_SIZE;
            cb(attrnum,
               static_cast<const UTF8 *>(static_cast<const void *>(vp + VALUE_HEADER_SIZE)),
               vlen, attr_owner, attr_flags);
        }
        rc = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT);
    }

    mdbx_cursor_close(cursor);
    mdbx_txn_abort(txn);
    return true;
}

bool CMdbxBackend::GetBuiltin(unsigned int object, AttrCallback cb)
{
    if (!m_open)
    {
        return false;
    }

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    MDBX_cursor *cursor = nullptr;
    rc = mdbx_cursor_open(txn, m_dbi, &cursor);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    uint8_t lo[8];
    encode_key(object, 0, lo);
    MDBX_val key = { lo, sizeof(lo) };
    MDBX_val data;

    rc = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE);
    while (MDBX_SUCCESS == rc)
    {
        if (key.iov_len != 8)
        {
            break;
        }
        const uint8_t *kp = static_cast<const uint8_t *>(key.iov_base);
        uint32_t key_obj = get_u32le(kp);
        if (key_obj != object)
        {
            break;
        }
        uint32_t attrnum = get_u32le(kp + 4);
        if (attrnum >= 256)
        {
            break;  // Keys are sorted; attrnums >= 256 means we're done.
        }
        if (data.iov_len >= VALUE_HEADER_SIZE)
        {
            const uint8_t *vp = static_cast<const uint8_t *>(data.iov_base);
            int attr_owner = static_cast<int>(get_u32le(vp));
            int attr_flags = static_cast<int>(get_u32le(vp + 4));
            size_t vlen = data.iov_len - VALUE_HEADER_SIZE;
            cb(attrnum,
               static_cast<const UTF8 *>(static_cast<const void *>(vp + VALUE_HEADER_SIZE)),
               vlen, attr_owner, attr_flags);
        }
        rc = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT);
    }

    mdbx_cursor_close(cursor);
    mdbx_txn_abort(txn);
    return true;
}

// ---------------------------------------------------------------------------
// Count and mod_count.
//

int CMdbxBackend::Count(unsigned int object)
{
    if (!m_open)
    {
        return 0;
    }

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return 0;
    }

    MDBX_cursor *cursor = nullptr;
    rc = mdbx_cursor_open(txn, m_dbi, &cursor);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return 0;
    }

    int count = 0;
    uint8_t lo[8];
    encode_key(object, 0, lo);
    MDBX_val key = { lo, sizeof(lo) };
    MDBX_val data;

    rc = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE);
    while (MDBX_SUCCESS == rc)
    {
        if (key.iov_len != 8)
        {
            break;
        }
        uint32_t key_obj = get_u32le(static_cast<const uint8_t *>(key.iov_base));
        if (key_obj != object)
        {
            break;
        }
        count++;
        rc = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT);
    }

    mdbx_cursor_close(cursor);
    mdbx_txn_abort(txn);
    return count;
}

uint32_t CMdbxBackend::GetModCount(unsigned int object, unsigned int attrnum)
{
    if (!m_open)
    {
        return 0;
    }

    uint8_t keybuf[8];
    encode_key(object, attrnum, keybuf);
    MDBX_val key = { keybuf, sizeof(keybuf) };
    MDBX_val data;

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return 0;
    }

    rc = mdbx_get(txn, m_dbi, &key, &data);
    uint32_t mc = 0;
    if (MDBX_SUCCESS == rc && data.iov_len >= VALUE_HEADER_SIZE)
    {
        mc = get_u32le(static_cast<const uint8_t *>(data.iov_base) + 8);
    }

    mdbx_txn_abort(txn);
    return mc;
}

bool CMdbxBackend::GetAllModCounts(unsigned int object, ModCountCallback cb)
{
    if (!m_open)
    {
        return false;
    }

    MDBX_txn *txn = nullptr;
    int rc = mdbx_txn_begin(m_env, nullptr, MDBX_TXN_RDONLY, &txn);
    if (MDBX_SUCCESS != rc)
    {
        return false;
    }

    MDBX_cursor *cursor = nullptr;
    rc = mdbx_cursor_open(txn, m_dbi, &cursor);
    if (MDBX_SUCCESS != rc)
    {
        mdbx_txn_abort(txn);
        return false;
    }

    uint8_t lo[8];
    encode_key(object, 0, lo);
    MDBX_val key = { lo, sizeof(lo) };
    MDBX_val data;

    rc = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE);
    while (MDBX_SUCCESS == rc)
    {
        if (key.iov_len != 8)
        {
            break;
        }
        const uint8_t *kp = static_cast<const uint8_t *>(key.iov_base);
        uint32_t key_obj = get_u32le(kp);
        if (key_obj != object)
        {
            break;
        }
        if (data.iov_len >= VALUE_HEADER_SIZE)
        {
            uint32_t attrnum = get_u32le(kp + 4);
            uint32_t mc = get_u32le(
                static_cast<const uint8_t *>(data.iov_base) + 8);
            cb(attrnum, mc);
        }
        rc = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT);
    }

    mdbx_cursor_close(cursor);
    mdbx_txn_abort(txn);
    return true;
}

// ---------------------------------------------------------------------------
// Maintenance.
//

void CMdbxBackend::Sync()
{
    if (m_open)
    {
        mdbx_env_sync_ex(m_env, true, false);
    }
}

void CMdbxBackend::Tick()
{
    // libmdbx is self-maintaining. No periodic work needed.
}
