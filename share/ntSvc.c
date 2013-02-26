#include "NtSvc.h"
#include "BugDump.h"

SERVICE_STATUS_HANDLE g_sshStatusHandle;
SERVICE_STATUS g_ssStatus;
HANDLE g_hEventStopService = NULL;

void Service_SetStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;
    BOOL fResult = TRUE;

    if (dwCurrentState == SERVICE_START_PENDING)
        g_ssStatus.dwControlsAccepted = 0;
    else
        g_ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    g_ssStatus.dwCurrentState = dwCurrentState;
    g_ssStatus.dwWin32ExitCode = dwWin32ExitCode;
    g_ssStatus.dwWaitHint = dwWaitHint;

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
        g_ssStatus.dwCheckPoint = 0;
    else
        g_ssStatus.dwCheckPoint = dwCheckPoint ++;

    SetServiceStatus(g_sshStatusHandle, &g_ssStatus);
}

BOOL Service_Install(const TCHAR *serviceName,
                     const TCHAR *serviceDisplayName,
                     const TCHAR *serviceDependencies,
                     const TCHAR *cmdParams)
{
    SC_HANDLE schService;
    SC_HANDLE schSCManager;
    SERVICE_STATUS ssStatus;
    TCHAR szPath[MAX_PATH], szBinaryPath[MAX_PATH];
    BOOL bSuccess = FALSE;

    GetModuleFileName(NULL, szPath, MAX_PATH);
    _stprintf_s(szBinaryPath, MAX_PATH, _T("\"%s\" %s"), szPath, cmdParams);

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (schSCManager)
    {
        schService = CreateService(schSCManager,
            serviceName, serviceDisplayName,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            szBinaryPath, NULL, NULL, serviceDependencies, NULL, NULL);
        if (schService)
        {
            bSuccess = TRUE;
            CloseServiceHandle(schService);

            if ((schService = OpenService(schSCManager, serviceName, SERVICE_ALL_ACCESS)) != NULL)
            {
                if (StartService(schService, 0,  NULL))
                {
                    Sleep(1000);
                    while (QueryServiceStatus(schService, &ssStatus))
                    {
                        if (ssStatus.dwCurrentState == SERVICE_START_PENDING)
                            Sleep(1000);
                        else
                            break;
                    }
                }
                bSuccess = (ssStatus.dwCurrentState == SERVICE_RUNNING);
                CloseServiceHandle(schService);
            }
        }

        CloseServiceHandle(schSCManager);
    }
    return bSuccess;
}

BOOL Service_Remove(const TCHAR *serviceName)
{
    SC_HANDLE schService;
    SC_HANDLE schSCManager;
    SERVICE_STATUS ssStatus;
    BOOL bSuccess = FALSE;

    if ((schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) != NULL)
    {
        if ((schService = OpenService(schSCManager, serviceName, DELETE|SERVICE_STOP|SERVICE_QUERY_STATUS)) != NULL)
        {
            if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus))
            {
                Sleep(1000);
                while(QueryServiceStatus(schService, &ssStatus))
                {
                    if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING)
                        Sleep(1000);
                    else
                        break;
                }
            }
            bSuccess = DeleteService(schService);
            CloseServiceHandle(schService);
        }
        CloseServiceHandle(schSCManager);
    }
    return bSuccess;
}

VOID WINAPI Service_Ctrl(DWORD dwCtrlCode)
{
    switch (dwCtrlCode)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        Service_SetStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(g_hEventStopService);
        return;
    case SERVICE_CONTROL_INTERROGATE:
        Service_SetStatus(SERVICE_RUNNING, NO_ERROR, 0);
        break;
    default:
        break;
    }
}

BOOL WINAPI ProgramHandlerRoutine(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        SetEvent(g_hEventStopService);
        return TRUE;
    }
    return FALSE;
}

VOID WINAPI Service_Main(DWORD dwArgc, LPTSTR *lpszArgv)
{
    g_sshStatusHandle = RegisterServiceCtrlHandler(g_serviceName, Service_Ctrl);

    g_ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ssStatus.dwServiceSpecificExitCode = 0;
    Service_SetStatus(SERVICE_RUNNING, NO_ERROR, 0);

    Service_Run();

    Service_SetStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

static BOOL SetPrivilege()
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    HANDLE hToken;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
        return FALSE;

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid))
    {
        CloseHandle(hToken);
        return FALSE; 
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Enable the privilege or disable all privileges.
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
    {
        CloseHandle(hToken);
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);
    return TRUE;
}

int _tmain(int argc, TCHAR *argv[])
{
    int i, runType = 1;

    SetPrivilege();

    defaultExceptionCallBack = SetUnhandledExceptionFilter(&DeBug_CreateDump);

    setlocale(LC_ALL, "");

    Service_Init(argc, argv);

    for (i=0; i<argc; i++)
    {
        if (_tcsicmp(argv[i], _T("-debug")) == 0)
            runType = 0;
        else if (_tcsicmp(argv[i], _T("-install")) == 0)
            runType = 1;
        else if (_tcsicmp(argv[i], _T("-remove")) == 0 ||
            _tcsicmp(argv[i], _T("-uninstall")) == 0)
            runType = 2;
        else if (_tcsicmp(argv[i], _T("-service")) == 0)
            runType = 3;
    }

    if (runType == 1)
        return Service_Install(g_serviceName, g_serviceDesc, g_serviceDep, g_serviceParams);

    if (runType == 2)
        return Service_Remove(g_serviceName);

    g_hEventStopService = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (runType == 3)
    {
        SERVICE_TABLE_ENTRY dispatchTable[] =
        {
            { g_serviceName, (LPSERVICE_MAIN_FUNCTION)Service_Main },
            { NULL, NULL }
        };
        return StartServiceCtrlDispatcher(dispatchTable);
    }

    SetConsoleCtrlHandler(ProgramHandlerRoutine, TRUE);

    return Service_Run();
}

