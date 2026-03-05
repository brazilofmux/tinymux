/*! \file sqlite_backend.cpp
 * \brief SQLite implementation of IStorageBackend.
 *
 */

#if !defined(TINYMUX_TYPES_DEFINED)
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#endif

#include "sqlite_backend.h"

CSQLiteBackend::CSQLiteBackend()
{
}

CSQLiteBackend::~CSQLiteBackend()
{
    Close();
}

bool CSQLiteBackend::Open(const char *path)
{
    return m_db.Open(path);
}

void CSQLiteBackend::Close()
{
    m_db.Close();
}

bool CSQLiteBackend::IsOpen() const
{
    return m_db.IsOpen();
}

bool CSQLiteBackend::Get(unsigned int object, unsigned int attrnum,
                         UTF8 *buf, size_t buflen, size_t *pLen)
{
    return m_db.GetAttribute(static_cast<dbref>(object), static_cast<int>(attrnum),
                             buf, buflen, pLen);
}

bool CSQLiteBackend::Put(unsigned int object, unsigned int attrnum,
                         const UTF8 *value, size_t len)
{
    return m_db.PutAttribute(static_cast<dbref>(object), static_cast<int>(attrnum),
                             value, len);
}

bool CSQLiteBackend::Del(unsigned int object, unsigned int attrnum)
{
    return m_db.DelAttribute(static_cast<dbref>(object), static_cast<int>(attrnum));
}

bool CSQLiteBackend::DelAll(unsigned int object)
{
    return m_db.DelAllAttributes(static_cast<dbref>(object));
}

bool CSQLiteBackend::GetAll(unsigned int object, AttrCallback cb)
{
    return m_db.GetAllAttributes(static_cast<dbref>(object),
        [&cb](int attrnum, const UTF8 *value, size_t len)
        {
            cb(static_cast<unsigned int>(attrnum), value, len);
        });
}

void CSQLiteBackend::Sync()
{
    m_db.Checkpoint();
}

void CSQLiteBackend::Tick()
{
    // Write-through means SQLite is always up to date.
    // Periodic optimize is light maintenance.
    //
    m_db.Optimize();
}
