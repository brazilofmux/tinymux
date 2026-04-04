/*! \file mdbx_backend.h
 * \brief libmdbx implementation of IStorageBackend (attributes only).
 *
 */

#ifndef MDBX_BACKEND_H
#define MDBX_BACKEND_H

#include "storage_backend.h"
#include "mdbx.h"

class CMdbxBackend : public IStorageBackend
{
public:
    CMdbxBackend();
    ~CMdbxBackend() override;

    // IStorageBackend interface.
    //
    bool Open(const char *path) override;
    void Close() override;
    bool IsOpen() const override;

    bool Get(unsigned int object, unsigned int attrnum,
             UTF8 *buf, size_t buflen, size_t *pLen,
             int *owner, int *flags) override;
    bool Put(unsigned int object, unsigned int attrnum,
             const UTF8 *value, size_t len,
             int owner, int flags) override;
    bool Del(unsigned int object, unsigned int attrnum) override;

    bool DelAll(unsigned int object) override;
    bool GetAll(unsigned int object, AttrCallback cb) override;
    bool GetBuiltin(unsigned int object, AttrCallback cb) override;

    int Count(unsigned int object) override;
    uint32_t GetModCount(unsigned int object, unsigned int attrnum) override;
    bool GetAllModCounts(unsigned int object, ModCountCallback cb) override;

    void Sync() override;
    void Tick() override;

private:
    // Key encoding: 8 bytes, little-endian (object, attrnum).
    //
    static void encode_key(unsigned int object, unsigned int attrnum,
                           uint8_t buf[8]);

    // Value header: 12 bytes (owner, flags, mod_count), then value bytes.
    //
    static const size_t VALUE_HEADER_SIZE = 12;

    MDBX_env *m_env;
    MDBX_dbi  m_dbi;
    bool      m_open;
};

#endif // !MDBX_BACKEND_H
