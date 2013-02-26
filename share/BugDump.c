#include "BugDump.h"
#include <dbghelp.h>
#include <stdio.h>
#pragma comment(lib, "dbghelp.lib")

LPTOP_LEVEL_EXCEPTION_FILTER defaultExceptionCallBack = NULL;

LONG WINAPI DeBug_CreateDump(EXCEPTION_POINTERS* pExceptionPointers)
{
    BOOL bMiniDumpSuccessful;
    WCHAR szFileName[MAX_PATH];
    HANDLE hDumpFile;
    MINIDUMP_EXCEPTION_INFORMATION ExpParam;
    SYSTEMTIME sys_time;

    GetLocalTime(&sys_time);
    swprintf_s(szFileName, MAX_PATH, L"%04d%02d%02d%02d%02d%02d.dmp",
        sys_time.wYear,sys_time.wMonth,sys_time.wDay,sys_time.wHour,
        sys_time.wMinute,sys_time.wSecond);
    hDumpFile = CreateFileW(szFileName, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

    if (hDumpFile != INVALID_HANDLE_VALUE)
    {
        ExpParam.ThreadId = GetCurrentThreadId();
        ExpParam.ExceptionPointers = pExceptionPointers;
        ExpParam.ClientPointers = TRUE;
        bMiniDumpSuccessful = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
            hDumpFile, MiniDumpNormal, &ExpParam, NULL, NULL);
        CloseHandle(hDumpFile);
    }
    ExitProcess(pExceptionPointers->ExceptionRecord->ExceptionCode);

    return 0;
}

