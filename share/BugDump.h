#ifndef _BUGDUMP_H_
#define _BUGDUMP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <dbghelp.h>

extern LPTOP_LEVEL_EXCEPTION_FILTER defaultExceptionCallBack;
LONG WINAPI DeBug_CreateDump(EXCEPTION_POINTERS* pExceptionPointers);

#ifdef __cplusplus
}
#endif

#endif
