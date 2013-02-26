#ifndef _GMUI_H
#define _GMUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "targetver.h"

#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <Commdlg.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <time.h>
#include <string.h>
#include <tchar.h>
#include <shlobj.h>

#include "ptrArray.h"
#include "ptrList.h"
#include "debugf.h"
#include "helper.h"
#include "idx.h"
#include "adminSock.h"

#include "resource.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'"\
    " version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib,"ws2_32")

#ifdef __cplusplus
}
#endif
#endif
