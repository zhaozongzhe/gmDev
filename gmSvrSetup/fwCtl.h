#ifndef _FW_CONTROL_H
#define _FW_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <windows.h>
#include <netfw.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

BOOL fwAddApp(const WCHAR *szFileName, const WCHAR *szRegisterName);

void fwRemoveApp(const WCHAR *szFileName);


#ifdef __cplusplus
}
#endif

#endif

