#include "gmCore.h"

TCHAR *g_serviceName = _T("GmCore");
TCHAR *g_serviceDesc = _T("GmCore service");
TCHAR *g_serviceDep = _T("rpcss");
TCHAR *g_serviceParams = _T("");

WCHAR g_workDir[MAX_PATH] = { 0 };         // ¹¤×÷Ä¿Â¼
struct options g_options = { 0 };


void Service_Init(int argc, TCHAR *argv[])
{
}

int Service_Run()
{
    WSADATA wsd ={ 0 };

    WSAStartup(MAKEWORD(2, 2), &wsd);

    task_startup();

    //getchar();
    while (1)
    {
        DWORD waitRes = WaitForSingleObject(g_hEventStopService, 1000);
        if (WAIT_OBJECT_0 == waitRes)
            break;
        else if (WAIT_TIMEOUT == waitRes)
        {
        }
    }

    task_cleanup();

    WSACleanup();

    return 0;
}
