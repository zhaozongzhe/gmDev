#include "debugf.h"

WCHAR g_szDbgFileName[MAX_PATH] = { 0 };
HANDLE g_hFileLog = INVALID_HANDLE_VALUE;
int g_logFlags = 0;
int g_logFileSize = 0;
int g_maxLogSize = 0;
CRITICAL_SECTION g_csLogFile;
static CHAR *g_buf = NULL;

#define DEBUGF_BUFFER_SIZE  32768

static void debugf_writeStr(CHAR *str, DWORD len)
{
    if (g_logFlags & DEBUGF_FILE && g_hFileLog != INVALID_HANDLE_VALUE)
    {
        DWORD dwWritten;

        if (WriteFile(g_hFileLog, str, len, &dwWritten, NULL) && dwWritten == len)
        {
            g_logFileSize += dwWritten;

            if (g_logFileSize > g_maxLogSize)
            {
                SetFilePointer(g_hFileLog, 0, NULL, FILE_BEGIN);
                SetEndOfFile(g_hFileLog);
                g_logFileSize = 0;
            }
        }
    }
}

void debugf(CHAR *format, ...)
{
    SYSTEMTIME ct;
    va_list argList;
    int len;

    if (!g_buf) return;

    EnterCriticalSection(&g_csLogFile);

    GetLocalTime(&ct);
    len = sprintf_s(g_buf, DEBUGF_BUFFER_SIZE-1,
        "%02d-%02d %02d:%02d:%02d ",
        ct.wMonth, ct.wDay, ct.wHour, ct.wMinute, ct.wSecond);

    va_start(argList, format);
    len += vsprintf_s(&g_buf[len], DEBUGF_BUFFER_SIZE-len-1, format, argList);
    va_end(argList);

    if (g_logFlags & DEBUGF_DEBUG) OutputDebugStringA(g_buf);
    if (g_logFlags & DEBUGF_STDIO) printf("%s", g_buf);
    if (g_logFlags & DEBUGF_FILE) debugf_writeStr(g_buf, (DWORD)len);

    LeaveCriticalSection(&g_csLogFile);
}

void debugfData(CHAR *szTitle, void *data, int dataLen)
{
    SYSTEMTIME ct;
    DWORD i, len;
    unsigned char *p;

    if (!g_buf) return;

    EnterCriticalSection(&g_csLogFile);

    GetLocalTime(&ct);
    len = sprintf_s(g_buf, DEBUGF_BUFFER_SIZE-1,
        "%02d-%02d %02d:%02d:%02d %s ",
        ct.wMonth, ct.wDay, ct.wHour, ct.wMinute, ct.wSecond, szTitle);

    for (i=0, p=(unsigned char *)data; i<(DWORD)dataLen && len<DEBUGF_BUFFER_SIZE-10; i++)
        len += sprintf_s(&g_buf[len], DEBUGF_BUFFER_SIZE-len-1, "%02X", p[i]);
    len += sprintf_s(&g_buf[len], DEBUGF_BUFFER_SIZE-len-1, "%s", "\r\n");

    if (g_logFlags & DEBUGF_DEBUG) OutputDebugStringA(g_buf);
    if (g_logFlags & DEBUGF_STDIO) printf("%s", g_buf);
    if (g_logFlags & DEBUGF_FILE) debugf_writeStr(g_buf, (DWORD)len);

    LeaveCriticalSection(&g_csLogFile);
}

static void debugf_openFile()
{
    WCHAR szLogName[MAX_PATH];
    int i;
    LARGE_INTEGER li;

    if (g_hFileLog != INVALID_HANDLE_VALUE)
        CloseHandle(g_hFileLog);

    if (!wcschr(g_szDbgFileName, L':'))
    {
        WCHAR szExeName[MAX_PATH];
        GetModuleFileNameW(NULL, szExeName, MAX_PATH);
        for (i=(int)wcslen(szExeName)-1; i>0; i--)
            if (szExeName[i] == L'\\') { szExeName[i+1] = 0; break; }
        swprintf_s(szLogName, MAX_PATH-1, L"%s%s", szExeName, g_szDbgFileName);
        szLogName[MAX_PATH-1] = 0;
    }
    else
        wcscpy_s(szLogName, MAX_PATH, g_szDbgFileName);

    g_hFileLog = CreateFileW(szLogName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (g_hFileLog != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(g_hFileLog, 0, NULL, FILE_END);
        if (GetFileSizeEx(g_hFileLog, &li))
            g_logFileSize = (int)li.QuadPart;
        else
        {
            SetFilePointer(g_hFileLog, 0, NULL, FILE_BEGIN);
            SetEndOfFile(g_hFileLog);
            g_logFileSize = 0;
        }
    }
}

BOOL debugf_startup(const WCHAR *fileName, int maxSize, int flags)
{
    g_maxLogSize = maxSize;
    g_logFlags = flags;

    if (fileName && fileName[0])
    {
        g_logFlags |= DEBUGF_FILE;
        if (_wcsicmp(g_szDbgFileName, fileName))
        {
            wcscpy_s(g_szDbgFileName, MAX_PATH, fileName);
            debugf_openFile();
        }
    }
    else
    {
        g_logFlags &= ~DEBUGF_FILE;
        if (g_hFileLog != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_hFileLog);
            g_hFileLog = INVALID_HANDLE_VALUE;
        }
    }

    if (g_buf) return TRUE;

    InitializeCriticalSection(&g_csLogFile);
    g_buf = malloc(DEBUGF_BUFFER_SIZE);

    return TRUE;
}

void debugf_cleanup()
{
    if (!g_buf) return;

    g_szDbgFileName[0] = 0;

    EnterCriticalSection(&g_csLogFile);

    if (g_hFileLog != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hFileLog);
        g_hFileLog = INVALID_HANDLE_VALUE;
    }

    DeleteCriticalSection(&g_csLogFile);

    free(g_buf);
    g_buf = NULL;
}

