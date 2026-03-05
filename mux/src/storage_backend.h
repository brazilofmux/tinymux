/*! \file storage_backend.h
 * \brief Abstract storage backend interface.
 *
 * This defines the contract between the attribute cache layer (attrcache.cpp)
 * and the durable storage implementation. Two backends exist:
 *
 *   - CHashFile (legacy .dir/.pag files)
 *   - SQLite (new unified store)
 *
 * The LRU cache sits above this interface in attrcache.cpp and is the same
 * regardless of backend. The backend handles only durable storage.
 */

#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include <cstddef>
#include <cstdint>
#include <functional>

#if defined(TINYMUX_TYPES_DEFINED)
typedef int           dbref;
typedef unsigned char UTF8;
#endif

class IStorageBackend
{
public:
    virtual ~IStorageBackend() = default;

    // Lifecycle
    //
    virtual bool Open(const char *path) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // Single-attribute operations.
    //
    // Get: returns true if found, copies value into buf, sets *pLen.
    //      returns false if not found, *pLen = 0.
    //
    // Put: inserts or replaces the attribute value.
    //      returns true on success.
    //
    // Del: removes the attribute.
    //      returns true on success (including if it didn't exist).
    //
    virtual bool Get(unsigned int object, unsigned int attrnum,
                     UTF8 *buf, size_t buflen, size_t *pLen) = 0;
    virtual bool Put(unsigned int object, unsigned int attrnum,
                     const UTF8 *value, size_t len) = 0;
    virtual bool Del(unsigned int object, unsigned int attrnum) = 0;

    // Bulk operations.
    //
    // DelAll: removes all attributes on an object (for @destroy).
    //
    // GetAll: iterates all attributes on an object. The callback receives
    //         each (attrnum, value, len) tuple. Used for preloading and
    //         @search.
    //
    typedef std::function<void(unsigned int attrnum, const UTF8 *value, size_t len)> AttrCallback;
    virtual bool DelAll(unsigned int object) = 0;
    virtual bool GetAll(unsigned int object, AttrCallback cb) = 0;

    // Maintenance.
    //
    // Sync: ensure all data is durable. For CHashFile, flushes all dirty
    //       pages. For SQLite, this is a WAL checkpoint.
    //
    // Tick: periodic maintenance. For CHashFile, dribbles dirty pages.
    //       For SQLite write-through, this is a no-op or light maintenance.
    //
    virtual void Sync() = 0;
    virtual void Tick() = 0;
};

#endif // !STORAGE_BACKEND_H
