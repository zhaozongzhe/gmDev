#include "gmCore.h"

// -------------------------------------------------------------------------------------------------
// delete files
// -------------------------------------------------------------------------------------------------
static CRITICAL_SECTION g_csDeleteDir;
static struct ptrList *g_dirsToDelete = NULL;
static HANDLE g_hEventDeleteDir = NULL;
static HANDLE g_hEventStopDeleteDir = NULL;
static HANDLE g_hEventStoppedDeleteDir = NULL;

static unsigned __stdcall DeleteDirThreadProc(LPVOID param)
{
    WCHAR *dir;
    HANDLE eh[2];
    DWORD dwWait;

    eh[0] = g_hEventDeleteDir;
    eh[1] = g_hEventStopDeleteDir;

    while (1)
    {
        dwWait = WaitForMultipleObjects(2, eh, FALSE, 500);
        if (dwWait!=WAIT_OBJECT_0 && dwWait!=WAIT_TIMEOUT) break;

        EnterCriticalSection(&g_csDeleteDir);
        dir = ptrList_pop_front(&g_dirsToDelete);
        LeaveCriticalSection(&g_csDeleteDir);

        if (dir)
        {
            SureDeleteDir(dir);
            free(dir);
        }
        else ResetEvent(g_hEventDeleteDir);
    }

    SetEvent(g_hEventStoppedDeleteDir);

    return 0;
}

BOOL deleteDir_begin(const WCHAR *dir)
{
    struct ptrList *list;

    EnterCriticalSection(&g_csDeleteDir);

    for (list=g_dirsToDelete; list; list=list->next)
    {
        if (_wcsicmp(list->data, dir)==0)
        {
            LeaveCriticalSection(&g_csDeleteDir);
            return TRUE;
        }
    }

    ptrList_append(&g_dirsToDelete, _wcsdup(dir));

    SetEvent(g_hEventDeleteDir);
    LeaveCriticalSection(&g_csDeleteDir);

    return TRUE;
}

void deleteDir_end(const WCHAR *dir)
{
    struct ptrList *list;

    EnterCriticalSection(&g_csDeleteDir);

    for (list=g_dirsToDelete; list; list=list->next)
    {
        if (_wcsicmp(list->data, dir)==0)
        {
            free(list->data);
            ptrList_remove_node(&g_dirsToDelete, list);
            break;
        }
    }

    LeaveCriticalSection(&g_csDeleteDir);
}

BOOL deleteDir_startup()
{
    if (g_hEventStopDeleteDir) return TRUE;

    InitializeCriticalSection(&g_csDeleteDir);
    g_hEventDeleteDir = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_hEventStopDeleteDir = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_hEventStoppedDeleteDir = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, DeleteDirThreadProc, NULL, 0, NULL));

    return TRUE;
}

void deleteDir_cleanup()
{
    if (!g_hEventStopDeleteDir) return;

    SetEvent(g_hEventStopDeleteDir);
    WaitForSingleObject(g_hEventStoppedDeleteDir, INFINITE);

    CloseHandle(g_hEventDeleteDir);
    CloseHandle(g_hEventStopDeleteDir);
    CloseHandle(g_hEventStoppedDeleteDir);
    g_hEventDeleteDir = NULL;
    g_hEventStopDeleteDir = NULL;
    g_hEventStoppedDeleteDir = NULL;
    DeleteCriticalSection(&g_csDeleteDir);

    ptrList_free(&g_dirsToDelete, free);
}
