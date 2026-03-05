/*! \file hashfile_backend.h
 * \brief CHashFile implementation of IStorageBackend (declaration only).
 *
 * This backend wraps the existing CHashFile (.dir/.pag) storage. The
 * implementation lives in mux/src/ where it has access to CHashFile,
 * CRC32_ProcessInteger2, and the attribute_record struct.
 *
 * The implementation will extract the CHashFile-specific logic currently
 * inlined in attrcache.cpp (hash computation, FindFirstKey/FindNextKey
 * chain walking, key matching, temp_record handling) into this class.
 *
 * The cache_redirect/cache_pass2/cache_cleanup path for database
 * conversion stays in attrcache.cpp as a CHashFile-specific concern
 * that doesn't go through the backend interface.
 */

#ifndef HASHFILE_BACKEND_H
#define HASHFILE_BACKEND_H

#include "storage_backend.h"

class CHashFileBackend : public IStorageBackend
{
public:
    CHashFileBackend();
    ~CHashFileBackend() override;

    // Open: path is the base name; .dir and .pag are appended.
    // The second parameter (nCachePages) is passed via SetCachePages()
    // before Open().
    //
    void SetCachePages(int nCachePages);

    bool Open(const char *path) override;
    void Close() override;
    bool IsOpen() const override;

    bool Get(unsigned int object, unsigned int attrnum,
             UTF8 *buf, size_t buflen, size_t *pLen) override;
    bool Put(unsigned int object, unsigned int attrnum,
             const UTF8 *value, size_t len) override;
    bool Del(unsigned int object, unsigned int attrnum) override;

    bool DelAll(unsigned int object) override;
    bool GetAll(unsigned int object, AttrCallback cb) override;

    void Sync() override;
    void Tick() override;

    // Implementation will be in mux/src/hashfile_backend.cpp
    // using CHashFile, CRC32_ProcessInteger2, etc.
};

#endif // !HASHFILE_BACKEND_H
