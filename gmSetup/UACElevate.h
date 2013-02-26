#ifndef _UAC_ELEVATE_H
#define _UAC_ELEVATE_H

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

BOOL IsUserInAdminGroup();
BOOL IsRunAsAdmin();
BOOL IsProcessElevated();
DWORD GetProcessIntegrityLevel();


#ifdef __cplusplus
}
#endif

#endif


