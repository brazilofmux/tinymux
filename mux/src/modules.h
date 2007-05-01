/*! \file modules.h
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 * $Id$
 *
 */

#ifndef MODULES_H
#define MODULES_H
#ifdef HAVE_DLOPEN

#ifdef WIN32
const UINT64 CID_Log        = 0x000000020CE18E7Ai64;
const UINT64 IID_ILog       = 0x000000028B9DC13Ai64;
#else
const UINT64 CID_Log        = 0x000000020CE18E7Aull;
const UINT64 IID_ILog       = 0x000000028B9DC13Aull;
#endif

interface ILog : public mux_IUnknown
{
public:
    virtual bool start_log(int key, const UTF8 *primary, const UTF8 *secondary) = 0;

    virtual void log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object) = 0;
    virtual void log_text(const UTF8 *text) = 0;
    virtual void log_number(int num) = 0;
    virtual void DCL_CDECL log_printf(const char *fmt, ...) = 0;
    virtual void log_name(dbref target) = 0;
    virtual void log_name_and_loc(dbref player) = 0;
    virtual void log_type_and_name(dbref thing) = 0;

    virtual void end_log(void) = 0;
};

class CLog : public ILog
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // ILog
    //
    virtual bool start_log(int key, const UTF8 *primary, const UTF8 *secondary);
    virtual void log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object);
    virtual void log_text(const UTF8 *text);
    virtual void log_number(int num);
    virtual void DCL_CDECL log_printf(const char *fmt, ...);
    virtual void log_name(dbref target);
    virtual void log_name_and_loc(dbref player);
    virtual void log_type_and_name(dbref thing);
    virtual void end_log(void);

    CLog(void);
    virtual ~CLog();

private:
    UINT32 m_cRef;
};

class CLogFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(UINT64 iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CLogFactory(void);
    virtual ~CLogFactory();

private:
    UINT32 m_cRef;
};

extern void init_modules(void);
extern void final_modules(void);

#endif // HAVE_DLOPEN
#endif // MODULES_H
