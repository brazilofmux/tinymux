#include <windows.h>

void WINAPI ServiceMain(DWORD argc, LPWSTR argv[]);
void WINAPI Handler(DWORD dwControl);

int ErrorPrinter(const WCHAR *pszFcn, DWORD dwErr = GetLastError());
void LookupErrorMsg(WCHAR *pszMsg, int cch, DWORD dwError = GetLastError());
void PrintEvent(const WCHAR *psz);
void SendStatus(
    DWORD dwCurrentStatus,
    DWORD dwCheckpoint = 0,
    DWORD dwWaitHint = 0,
    DWORD dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE,
    DWORD dwExitCode = NO_ERROR
);

const WCHAR* const g_ServiceName = L"muxsvc";
SERVICE_STATUS_HANDLE g_hServiceStatus = NULL;

#define NUM_EVENTS 3
HANDLE g_hEvents[NUM_EVENTS] =
{
    NULL,
    NULL,
    NULL
};

DWORD g_state = 0;
DWORD g_status = 0;

int main(int argc, char *argv[])
{
    SERVICE_TABLE_ENTRY ServiceStartTable[] =
    {
        { const_cast<WCHAR *>(g_ServiceName), ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcher(ServiceStartTable))
    {
        ErrorPrinter(L"StartServiceCtrlDispatcher");
    }
    return 0;
}

void WINAPI ServiceMain(DWORD argc, LPWSTR argv[])
{
    g_hServiceStatus = RegisterServiceCtrlHandler(g_ServiceName, Handler);
    if (NULL == g_hServiceStatus)
    {
        ErrorPrinter(L"RegisterServiceCtrlHandler");
        return;
    }

    SendStatus(SERVICE_START_PENDING, 1, 5000, 0);

    g_hEvents[0] = CreateEvent(NULL, FALSE, FALSE, L"StopEvent");
    g_hEvents[1] = CreateEvent(NULL, FALSE, FALSE, L"PauseEvent");
    g_hEvents[2] = CreateEvent(NULL, FALSE, FALSE, L"ContinueEvent");
    bool bRun = true;

    SendStatus(SERVICE_RUNNING);

    DWORD dwWait;
    for (;;)
    {
        if (bRun)
        {
            PrintEvent(L"Doing my work - it takes 2 seconds");
            Sleep(2000);
        }

        dwWait = WaitForMultipleObjects(NUM_EVENTS, g_hEvents, FALSE, 0);
        if (  WAIT_OBJECT_0 <= dwWait
           && dwWait <= WAIT_OBJECT_0 + NUM_EVENTS - 1)
        {
            DWORD iEvent = dwWait - WAIT_OBJECT_0;
            if (0 == iEvent)
            {
                PrintEvent(L"Got the Stop Event");
                break;
            }
            else
            {
                switch (iEvent)
                {
                case 1:
                    PrintEvent(L"Got the Pause Event");
                    bRun = false;
                    SendStatus(SERVICE_PAUSED);
                    ResetEvent(g_hEvents[1]);
                    break;

                case 2:
                    PrintEvent(L"Got the Continue Event");
                    bRun = true;
                    SendStatus(SERVICE_RUNNING);
                    ResetEvent(g_hEvents[2]);
                    break;
                }
            }
        }
        else if (  WAIT_ABANDONED_0 <= dwWait
                && dwWait <= WAIT_ABANDONED_0 + NUM_EVENTS - 1)
        {
            DWORD iEvent = dwWait - WAIT_ABANDONED_0;
            PrintEvent(L"A handle was abandoned");
            break;
        }
        else if (WAIT_TIMEOUT == dwWait)
        {
            // Normal Timeout.
            //
        }
        else
        {
            PrintEvent(L"Something else happened");
            break;
        }
    }

    SendStatus(SERVICE_STOP_PENDING, 1, 2000);

    for (int i = 0; i < NUM_EVENTS; i++)
    {
        CloseHandle(g_hEvents[i]);
        g_hEvents[i] = NULL;
    }

    SendStatus(SERVICE_STOPPED);
}

void WINAPI Handler(DWORD dwControl)
{
    if (g_state == dwControl)
    {
        return;
    }

    switch (dwControl)
    {
    case SERVICE_CONTROL_STOP:
        g_state = dwControl;
        SetEvent(g_hEvents[0]);
        break;

    case SERVICE_CONTROL_PAUSE:
        g_state = dwControl;
        SetEvent(g_hEvents[1]);
        break;

    case SERVICE_CONTROL_CONTINUE:
        g_state = dwControl;
        SetEvent(g_hEvents[2]);
        break;

    default:
        SendStatus(g_status);
        break;
    }
}

void SendStatus(DWORD dwCurrentStatus, DWORD dwCheckpoint, DWORD dwWaitHint, DWORD dwControlsAccepted, DWORD dwExitCode)
{
    g_status = dwCurrentStatus;
    SERVICE_STATUS ss =
    {
        SERVICE_WIN32_OWN_PROCESS,
        g_status,
        dwControlsAccepted,
        dwExitCode,
        0,
        dwCheckpoint,
        dwWaitHint
    };

    SetServiceStatus(g_hServiceStatus, &ss);
}

int ErrorPrinter(const WCHAR *psz, DWORD dwErr)
{
    WCHAR szMsg[512];
    LookupErrorMsg(szMsg, sizeof(szMsg)/sizeof(szMsg[0]), dwErr);

    WCHAR sz[512];
    const WCHAR *rgsz[] = { sz };
    wsprintf(sz, L"%s failed: %s", psz, szMsg);

    HANDLE hes = RegisterEventSource(0, g_ServiceName);
    if (NULL != hes)
    {
        ReportEvent(hes, EVENTLOG_ERROR_TYPE, 0, 0, 0, 1, 0, rgsz, 0);
        DeregisterEventSource(hes);
    }
    return dwErr;
}

void LookupErrorMsg(WCHAR *pszMsg, int cch, DWORD dwError)
{
    if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, dwError, 0, pszMsg, cch, 0))
    {
        wsprintf(pszMsg, L"Unknown: %x", dwError);
    }
}

void PrintEvent(const WCHAR *psz)
{
    const WCHAR *rgsz[] = { psz };

    HANDLE hes = RegisterEventSource(0, g_ServiceName);
    if (NULL != hes)
    {
        ReportEvent(hes, EVENTLOG_INFORMATION_TYPE, 0, 0, 0, 1, 0, rgsz, 0);
        DeregisterEventSource(hes);
    }
}
