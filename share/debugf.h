#ifndef _DEBUGF_H_
#define _DEBUGF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <windows.h>
#include <time.h>
#include <stdio.h>
#include <malloc.h>

#define DEBUGF_STDIO    0x0001
#define DEBUGF_DEBUG    0x0002
#define DEBUGF_FILE     0x0004

BOOL debugf_startup(const WCHAR *fileName, int maxSize, int flags);//DEBUGF_STDIO|DEBUGF_DEBUG|DEBUGF_FILE
void debugf_cleanup();
void debugf(CHAR *format, ...);
void debugfData(CHAR *szTitle, void *data, int dataLen);

#ifdef __cplusplus
}
#endif

#endif
