#pragma once

#include "resource.h"

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#include <winSock2.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <netfw.h>

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include "fwCtl.h"
#include "UACElevate.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'"\
    " version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

