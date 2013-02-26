#ifndef _SEARCHCHINESE_H
#define _SEARCHCHINESE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>

WCHAR *GetSpellString(WCHAR *_in, WCHAR *_out, int _outLen);

#ifdef __cplusplus
}
#endif

#endif
