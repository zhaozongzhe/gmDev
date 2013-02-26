#ifndef __MAC_IP_H
#define __MAC_IP_H

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <locale.h>
#include <iphlpapi.h>

#include "ptrArray.h"
#include "ptrList.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MAC_IP
{
    UCHAR mac[6];
    UINT32 ip;
};

struct ptrList *SysGetMacIps();

#ifdef __cplusplus
}
#endif

#endif
