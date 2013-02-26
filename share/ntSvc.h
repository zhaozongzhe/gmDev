#ifndef __NTSERVICE_INCLUDED__
#define __NTSERVICE_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <locale.h>

extern TCHAR *g_serviceName;
extern TCHAR *g_serviceDesc;
extern TCHAR *g_serviceDep;
extern TCHAR *g_serviceParams;

extern HANDLE g_hEventStopService;

void Service_Init(int argc, TCHAR *argv[]);
int Service_Run();

#ifdef __cplusplus
}
#endif

#endif
