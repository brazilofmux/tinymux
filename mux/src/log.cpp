/*! \file log.cpp
 * \brief Logging routines.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "command.h"
#include "mathutil.h"

NAMETAB logdata_nametab[] =
{
    {T("flags"),           1,  0,  LOGOPT_FLAGS},
    {T("location"),        1,  0,  LOGOPT_LOC},
    {T("owner"),           1,  0,  LOGOPT_OWNER},
    {T("timestamp"),       1,  0,  LOGOPT_TIMESTAMP},
    {(UTF8 *) nullptr,     0,  0,  0}
};

NAMETAB logoptions_nametab[] =
{
    {T("accounting"),      2,  0,  LOG_ACCOUNTING},
    {T("all_commands"),    2,  0,  LOG_ALLCOMMANDS},
    {T("bad_commands"),    2,  0,  LOG_BADCOMMANDS},
    {T("buffer_alloc"),    3,  0,  LOG_ALLOCATE},
    {T("bugs"),            3,  0,  LOG_BUGS},
    {T("checkpoints"),     2,  0,  LOG_DBSAVES},
    {T("config_changes"),  2,  0,  LOG_CONFIGMODS},
    {T("create"),          2,  0,  LOG_PCREATES},
    {T("killing"),         1,  0,  LOG_KILLS},
    {T("logins"),          1,  0,  LOG_LOGIN},
    {T("network"),         1,  0,  LOG_NET},
    {T("problems"),        1,  0,  LOG_PROBLEMS},
    {T("security"),        2,  0,  LOG_SECURITY},
    {T("shouts"),          2,  0,  LOG_SHOUTS},
    {T("startup"),         2,  0,  LOG_STARTUP},
    {T("suspect"),         2,  0,  LOG_SUSPECTCMDS},
    {T("time_usage"),      1,  0,  LOG_TIMEUSE},
    {T("wizard"),          1,  0,  LOG_WIZARD},
    {(UTF8 *) nullptr,     0,  0,  0}
};

/* ---------------------------------------------------------------------------
 * start_log: see if it is OK to log something, and if so, start writing the
 * log entry.
 */

bool start_log(const UTF8 * primary, const UTF8 * secondary)
{
    mudstate.logging++;
    if (  1 <= mudstate.logging
       && mudstate.logging <= 2)
    {
        if (!mudstate.bStandAlone)
        {
            // Format the timestamp.
            //
            UTF8 buffer[256];
            buffer[0] = '\0';
            if (mudconf.log_info & LOGOPT_TIMESTAMP)
            {
                CLinearTimeAbsolute ltaNow;
                ltaNow.GetLocal();
                FIELDEDTIME ft;
                ltaNow.ReturnFields(&ft);
                mux_sprintf(buffer, sizeof(buffer),
                    T("%d.%02d%02d:%02d%02d%02d "), ft.iYear, ft.iMonth,
                    ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond);
            }

            // Write the header to the log.
            //
            if (  secondary
               && *secondary)
            {
                Log.tinyprintf(T("%s%s %3s/%-5s: "), buffer, mudconf.mud_name,
                    primary, secondary);
            }
            else
            {
                Log.tinyprintf(T("%s%s %-9s: "), buffer, mudconf.mud_name,
                    primary);
            }
        }

        // If a recursive call, log it and return indicating no log.
        //
        if (mudstate.logging == 1)
        {
            return true;
        }
        Log.WriteString(T("Recursive logging request." ENDLINE));
    }
    mudstate.logging--;
    return false;
}

/* ---------------------------------------------------------------------------
 * end_log: Finish up writing a log entry
 */

void end_log(void)
{
    Log.WriteString((UTF8 *) ENDLINE);
    Log.Flush();
    mudstate.logging--;
}

/* ---------------------------------------------------------------------------
 * log_perror: Write perror message to the log
 */

void log_perror(const UTF8 * primary, const UTF8 * secondary,
    const UTF8 * extra, const UTF8 * failing_object)
{
    start_log(primary, secondary);
    if (extra && *extra)
    {
        log_text(T("("));
        log_text(extra);
        log_text(T(") "));
    }

    // <Failing_object text>: <strerror() text>
    //
    Log.WriteString(failing_object);
    Log.WriteString(T(": "));
    Log.WriteString(mux_strerror(errno));
#ifndef WIN32
    Log.WriteString((UTF8 *) ENDLINE);
#endif // !WIN32
    Log.Flush();
    mudstate.logging--;
}

/* ---------------------------------------------------------------------------
 * log_text, log_number: Write text or number to the log file.
 */

void log_text(const UTF8 * text)
{
    Log.WriteString(strip_color(text));
}

void log_number(int num)
{
    Log.WriteInteger(num);
}

void DCL_CDECL log_printf(__in_z const UTF8 * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    UTF8 aTempBuffer[SIZEOF_LOG_BUFFER];
    size_t nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    Log.WriteBuffer(nString, aTempBuffer);
}

/* ---------------------------------------------------------------------------
 * log_name: write the name, db number, and flags of an object to the log.
 * If the object does not own itself, append the name, db number, and flags
 * of the owner.
 */

void log_name(dbref target)
{
    if (mudstate.bStandAlone)
    {
        Log.tinyprintf(T("%s(#%d)"), PureName(target), target);
    }
    else
    {
        UTF8 *tp;

        if (mudconf.log_info & LOGOPT_FLAGS)
        {
            tp = unparse_object(GOD, target, false);
        }
        else
        {
            tp = unparse_object_numonly(target);
        }
        Log.WriteString(strip_color(tp));
        free_lbuf(tp);
        if (  (mudconf.log_info & LOGOPT_OWNER)
           && target != Owner(target))
        {
            if (mudconf.log_info & LOGOPT_FLAGS)
            {
                tp = unparse_object(GOD, Owner(target), false);
            }
            else
            {
                tp = unparse_object_numonly(Owner(target));
            }
            Log.tinyprintf(T("[%s]"), strip_color(tp));
            free_lbuf(tp);
        }
    }
}

/* ---------------------------------------------------------------------------
 * log_name_and_loc: Log both the name and location of an object
 */

void log_name_and_loc(dbref player)
{
    log_name(player);
    if (  (mudconf.log_info & LOGOPT_LOC)
       && Has_location(player))
    {
        log_text(T(" in "));
        log_name(Location(player));
    }
    return;
}

static const UTF8 *OBJTYP(dbref thing)
{
    if (!Good_dbref(thing))
    {
        return T("??OUT-OF-RANGE??");
    }
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
        return T("PLAYER");
    case TYPE_THING:
        return T("THING");
    case TYPE_ROOM:
        return T("ROOM");
    case TYPE_EXIT:
        return T("EXIT");
    case TYPE_GARBAGE:
        return T("GARBAGE");
    default:
        return T("??ILLEGAL??");
    }
}

void log_type_and_name(dbref thing)
{
    Log.tinyprintf(T("%s #%d(%s)"), OBJTYP(thing), thing,
        Good_obj(thing) ? PureName(thing) : T(""));
    return;
}

void do_log
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *whichlog,
    UTF8 *logtext,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bValid = true;

    // Strip the filename of all ANSI.
    //
    UTF8 *pFilename = strip_color(whichlog);

    // Restrict filename to a subdirectory to reduce the possibility
    // of a security hole.
    //
    UTF8 *temp_ptr = (UTF8 *) strrchr((char *) pFilename, '/');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }
    temp_ptr = (UTF8 *) strrchr((char *) pFilename, '\\');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }

    // Check for and disallow leading periods, empty strings
    // and filenames over 30 characters.
    //
    size_t n = strlen((char *) pFilename);
    if (  n == 0
       || 30 < n)
    {
        bValid = false;
    }
    else
    {
        unsigned int i;
        for (i = 0; i < n; i++)
        {
            if (!mux_isalnum(pFilename[i]))
            {
                bValid = false;
                break;
            }
        }
    }

    UTF8 *pFullName = nullptr;
    const UTF8 *pMessage = T("");
    if (bValid)
    {
        pFullName = alloc_lbuf("do_log_filename");
        mux_sprintf(pFullName, LBUF_SIZE, T("logs/M-%s.log"), pFilename);

        // Strip the message of all ANSI.
        //
        pMessage = strip_color(logtext);

        // Check for and disallow empty messages.
        //
        if (pMessage[0] == '\0')
        {
            bValid = false;
        }
    }

    if (!bValid)
    {
        if (pFullName)
        {
            free_lbuf(pFullName);
        }
        notify(executor, T("Syntax: @log file=message"));
        return;
    }

    FILE *hFile;
    if (mux_fopen(&hFile, pFullName, T("r")))
    {
        fclose(hFile);
        if (mux_fopen(&hFile, pFullName, T("a")))
        {
            // Okay, at this point, the file exists.
            //
            free_lbuf(pFullName);
            mux_fprintf(hFile, T("%s" ENDLINE), pMessage);
            fclose(hFile);
            return;
        }
    }
    free_lbuf(pFullName);

    notify(executor, T("Not a valid log file."));
    return;
}

CLogFile Log;
void CLogFile::WriteInteger(int iNumber)
{
    UTF8 aTempBuffer[I32BUF_SIZE];
    size_t nTempBuffer = mux_ltoa(iNumber, aTempBuffer);
    WriteBuffer(nTempBuffer, aTempBuffer);
}

void CLogFile::WriteBuffer(size_t nString, const UTF8 * pString)
{
    if (!bEnabled)
    {
        return;
    }

#if defined(WINDOWS_THREADS)
    EnterCriticalSection(&csLog);
#endif // WINDOWS_THREADS

    while (nString > 0)
    {
        size_t nAvailable = SIZEOF_LOG_BUFFER - m_nBuffer;
        if (nAvailable == 0)
        {
            Flush();
            continue;
        }

        size_t nToMove = nAvailable;
        if (nString < nToMove)
        {
            nToMove = nString;
        }

        // Move nToMove bytes from pString to aBuffer+nBuffer
        //
        memcpy(m_aBuffer + m_nBuffer, pString, nToMove);
        pString += nToMove;
        nString -= nToMove;
        m_nBuffer += nToMove;
    }
    Flush();

#if defined(WINDOWS_THREADS)
    LeaveCriticalSection(&csLog);
#endif // WINDOWS_THREADS
}

void CLogFile::WriteString(const UTF8 * pString)
{
    size_t nString = strlen((char *) pString);
    WriteBuffer(nString, pString);
}

void DCL_CDECL CLogFile::tinyprintf(const UTF8 * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    UTF8 aTempBuffer[SIZEOF_LOG_BUFFER];
    size_t nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    WriteBuffer(nString, aTempBuffer);
}

static void MakeLogName
(
    const UTF8 *pBasename,
    const UTF8 *szPrefix,
    CLinearTimeAbsolute lta,
    UTF8 *szLogName,
    size_t nLogName
)
{
    UTF8 szTimeStamp[18];
    lta.ReturnUniqueString(szTimeStamp, sizeof(szTimeStamp));
    if (  pBasename
       && pBasename[0] != '\0')
    {
        mux_sprintf(szLogName, nLogName, T("%s/%s-%s.log"),
            pBasename, szPrefix, szTimeStamp);
    }
    else
    {
        mux_sprintf(szLogName, nLogName, T("%s-%s.log"), szPrefix, szTimeStamp);
    }
}

bool CLogFile::CreateLogFile(void)
{
    CloseLogFile();

    m_nSize = 0;

    bool bSuccess;
#if defined(WINDOWS_FILES)
    size_t nFilename;
    UTF16 *pFilename = ConvertFromUTF8ToUTF16(m_szFilename, &nFilename);
    if (nullptr == pFilename)
    {
        return false;
    }

    m_hFile = CreateFile(pFilename, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    bSuccess = (INVALID_HANDLE_VALUE != m_hFile);
#elif defined(UNIX_FILES)
    bSuccess = mux_open(&m_fdFile, m_szFilename,
        O_RDWR | O_BINARY | O_CREAT | O_TRUNC);
#endif // UNIX_FILES
    return bSuccess;
}

void CLogFile::AppendLogFile(void)
{
    CloseLogFile();

    bool bSuccess;
#if defined(WINDOWS_FILES)
    size_t nFilename;
    UTF16 *pFilename = ConvertFromUTF8ToUTF16(m_szFilename, &nFilename);
    if (nullptr == pFilename)
    {
        return;
    }

    m_hFile = CreateFile(pFilename, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    bSuccess = (INVALID_HANDLE_VALUE != m_hFile);
#elif defined(UNIX_FILES)
    bSuccess = mux_open(&m_fdFile, m_szFilename, O_RDWR | O_BINARY);
#endif // UNIX_FILES

    if (bSuccess)
    {
#if defined(WINDOWS_FILES)
        SetFilePointer(m_hFile, 0, 0, FILE_END);
#elif defined(UNIX_FILES)
        mux_lseek(m_fdFile, 0, SEEK_END);
#endif // UNIX_FILES
    }
}

void CLogFile::CloseLogFile(void)
{
#if defined(WINDOWS_FILES)
    if (INVALID_HANDLE_VALUE != m_hFile)
    {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
#elif defined(UNIX_FILES)
    if (MUX_OPEN_INVALID_HANDLE_VALUE != m_fdFile)
    {
        mux_close(m_fdFile);
        m_fdFile = MUX_OPEN_INVALID_HANDLE_VALUE;
    }
#endif // UNIX_FILES
}

#define FILE_SIZE_TRIGGER (512*1024UL)

void CLogFile::Flush(void)
{
    if (  m_nBuffer <= 0
       || !bEnabled)
    {
        return;
    }

    if (bUseStderr)
    {
        // There is no recourse if the following fails.
        //
        (void) fwrite(m_aBuffer, m_nBuffer, 1, stderr);
    }
    else
    {
        m_nSize += m_nBuffer;
#if defined(WINDOWS_FILES)
        unsigned long nWritten;
        bool fSuccess = true;
        if (!WriteFile(m_hFile, m_aBuffer, (DWORD) m_nBuffer, &nWritten, nullptr))
        {
            fSuccess = false;
        }
#elif defined(UNIX_FILES)
        ssize_t written = mux_write(m_fdFile, m_aBuffer, m_nBuffer);
        bool fSuccess = (0 < written && m_nBuffer == (size_t) written);
#endif // UNIX_FILES

        if (!fSuccess)
        {
            raw_broadcast(WIZARD,
                T("GAME: Unable to write to the log.  The disk may be full."));
        }

        if (m_nSize > FILE_SIZE_TRIGGER)
        {
            CloseLogFile();

            m_ltaStarted.GetLocal();
            MakeLogName(m_pBasename, m_szPrefix, m_ltaStarted, m_szFilename,
                sizeof(m_szFilename));

            CreateLogFile();
        }
    }
    m_nBuffer = 0;
}

void CLogFile::SetPrefix(const UTF8 * szPrefix)
{
    if (  !bUseStderr
       && strcmp((char *) szPrefix, (char *) m_szPrefix) != 0)
    {
        if (bEnabled)
        {
            CloseLogFile();
        }

        UTF8 szNewName[SIZEOF_PATHNAME];
        MakeLogName(m_pBasename, szPrefix, m_ltaStarted, szNewName,
            sizeof(szNewName));
        if (bEnabled)
        {
            ReplaceFile(m_szFilename, szNewName);
        }
        mux_strncpy(m_szPrefix, szPrefix, sizeof(m_szPrefix)-1);
        mux_strncpy(m_szFilename, szNewName, sizeof(m_szFilename)-1);

        if (bEnabled)
        {
            AppendLogFile();
        }
    }
}

void CLogFile::SetBasename(const UTF8 * pBasename)
{
    if (m_pBasename)
    {
        MEMFREE(m_pBasename);
        m_pBasename = nullptr;
    }

    if (  pBasename
       && strcmp((char *) pBasename, "-") == 0)
    {
        bUseStderr = true;
    }
    else
    {
        bUseStderr = false;
        if (pBasename)
        {
            m_pBasename = StringClone(pBasename);
        }
        else
        {
            m_pBasename = StringClone(T(""));
        }
    }
}

CLogFile::CLogFile(void)
{
#if defined(WINDOWS_THREADS)
    InitializeCriticalSection(&csLog);
#endif // WINDOWS_THREADS

    m_ltaStarted.GetLocal();
#if defined(WINDOWS_FILES)
    m_hFile = INVALID_HANDLE_VALUE;
#elif defined(UNIX_FILES)
    m_fdFile = MUX_OPEN_INVALID_HANDLE_VALUE;
#endif // UNIX_FILES
    m_nSize = 0;
    m_nBuffer = 0;
    bEnabled = false;
    bUseStderr = true;
    m_pBasename = nullptr;
    m_szPrefix[0] = '\0';
    m_szFilename[0] = '\0';
}

void CLogFile::StartLogging()
{
    if (!bUseStderr)
    {
        m_ltaStarted.GetLocal();
        MakeLogName(m_pBasename, m_szPrefix, m_ltaStarted, m_szFilename,
            sizeof(m_szFilename));
        CreateLogFile();
    }
    bEnabled = true;
}

void CLogFile::StopLogging(void)
{
    Flush();
    bEnabled = false;
    if (!bUseStderr)
    {
        CloseLogFile();
        m_szPrefix[0] = '\0';
        m_szFilename[0] = '\0';
        SetBasename(nullptr);
    }
}

CLogFile::~CLogFile(void)
{
    StopLogging();
#if defined(WINDOWS_THREADS)
    DeleteCriticalSection(&csLog);
#endif // WINDOWS_THREADS
}

// CLog component which is not directly accessible.
//
class CLog : public mux_ILog
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_ILog
    //
    virtual MUX_RESULT start_log(bool *pStarted, int key, const UTF8 *primary, const UTF8 *secondary);
    virtual MUX_RESULT log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object);
    virtual MUX_RESULT log_text(const UTF8 *text);
    virtual MUX_RESULT log_number(int num);
    virtual MUX_RESULT log_name(dbref target);
    virtual MUX_RESULT log_name_and_loc(dbref player);
    virtual MUX_RESULT log_type_and_name(dbref thing);
    virtual MUX_RESULT end_log(void);

    CLog(void);
    virtual ~CLog();

private:
    UINT32 m_cRef;
};

CLog::CLog(void) : m_cRef(1)
{
}

CLog::~CLog()
{
}

MUX_RESULT CLog::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else if (IID_ILog == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CLog::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLog::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CLog::start_log(bool *fStarted, int key, const UTF8 *primary, const UTF8 *secondary)
{
    if (  ((key) & mudconf.log_options) != 0
       && ::start_log(primary, secondary))
    {
        *fStarted = true;
    }
    else
    {
        *fStarted = false;
    }
    return MUX_S_OK;
}

MUX_RESULT CLog::log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object)
{
    ::log_perror(primary, secondary, extra, failing_object);
    return MUX_S_OK;
}

MUX_RESULT CLog::log_text(const UTF8 *text)
{
    ::log_text(text);
    return MUX_S_OK;
}

MUX_RESULT CLog::log_number(int num)
{
    ::log_number(num);
    return MUX_S_OK;
}

MUX_RESULT CLog::log_name(dbref target)
{
    ::log_name(target);
    return MUX_S_OK;
}

MUX_RESULT CLog::log_name_and_loc(dbref player)
{
    ::log_name_and_loc(player);
    return MUX_S_OK;
}

MUX_RESULT CLog::log_type_and_name(dbref thing)
{
    ::log_type_and_name(thing);
    return MUX_S_OK;
}

MUX_RESULT CLog::end_log(void)
{
    ::end_log();
    return MUX_S_OK;
}

// Factory for CLog component which is not directly accessible.
//
CLogFactory::CLogFactory(void) : m_cRef(1)
{
}

CLogFactory::~CLogFactory()
{
}

MUX_RESULT CLogFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CLogFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLogFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CLogFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CLog *pLog = nullptr;
    try
    {
        pLog = new CLog;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pLog)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pLog->QueryInterface(iid, ppv);
    pLog->Release();
    return mr;
}

MUX_RESULT CLogFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
