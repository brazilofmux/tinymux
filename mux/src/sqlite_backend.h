/*! \file sqlite_backend.h
 * \brief SQLite implementation of IStorageBackend.
 *
 */

#ifndef SQLITE_BACKEND_H
#define SQLITE_BACKEND_H

#include "storage_backend.h"
#include "sqlitedb.h"

class CSQLiteBackend : public IStorageBackend
{
public:
    CSQLiteBackend();
    ~CSQLiteBackend() override;

    // IStorageBackend interface.
    //
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

    // Access the underlying CSQLiteDB for object metadata operations
    // and statistics that are outside the IStorageBackend scope.
    //
    CSQLiteDB &GetDB() { return m_db; }

private:
    CSQLiteDB m_db;
};

#endif // !SQLITE_BACKEND_H
