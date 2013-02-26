#include "getMacIp.h"

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

BOOL IsX64()
{
    SYSTEM_INFO si;
    PGNSI pGNSI;
    HMODULE hMod = GetModuleHandle(TEXT("kernel32"));

    if (!hMod) return FALSE;

    pGNSI = (PGNSI)GetProcAddress(hMod, "GetNativeSystemInfo");
    if(NULL != pGNSI)
        pGNSI(&si);
    else
        GetSystemInfo(&si);

    return (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64);
}

int IsVista()
{
    OSVERSIONINFOEX osvi;
    BOOL bOsVersionInfoEx;

    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (!(bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO *)&osvi)))
    {
        osvi.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
        if (!GetVersionEx((OSVERSIONINFO *)&osvi))
            return FALSE;
    }
    if (osvi.dwPlatformId != VER_PLATFORM_WIN32_NT)
        return FALSE;

    return (osvi.dwMajorVersion > 5);
}

#define KEY_NETWORK_CARDS TEXT("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards")
#define KEY_NETWORK_3358C TEXT("SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\{AD498944-762F-11D0-8DCB-00C04FC3358C}")

static int funcCompare_NetworkCards(const void *p1, const void *p2)
{
    return strcmp((CHAR *)p1, (CHAR *)p2);
}

static BOOL IsNetworkCardLinked(CHAR *szService)
{
    HKEY hKeyNetworkCards;
    int i, done = 0;
    BOOL linked = FALSE;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, KEY_NETWORK_3358C, 0, KEY_READ, &hKeyNetworkCards) != ERROR_SUCCESS)
        return FALSE;

    for (i=0; !done; i++)
    {
        HKEY hKeyItem;
        CHAR szItemName[1024];
        int j;

        if (RegEnumKey(hKeyNetworkCards, i, szItemName, 1024) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyEx(hKeyNetworkCards, szItemName, 0, KEY_READ, &hKeyItem) != ERROR_SUCCESS)
            continue;

        for (j=0; !done; j++)
        {
            HKEY hKeyItem2, hKeyItem3;
            DWORD dwBufferSize, dwType;
            DWORD dwLinked;

            if (RegEnumKey(hKeyItem, j, szItemName, 1024) != ERROR_SUCCESS)
                break;
            if (!strstr(szItemName, szService))
                continue;
            done = 1;
            if (RegOpenKeyEx(hKeyItem, szItemName, 0, KEY_READ, &hKeyItem2) != ERROR_SUCCESS)
                continue;
            if (RegOpenKeyEx(hKeyItem2, TEXT("Control"), 0, KEY_READ, &hKeyItem3) != ERROR_SUCCESS)
            {
                RegCloseKey(hKeyItem2);
                continue;
            }

            dwBufferSize = sizeof(DWORD);
            dwType = REG_DWORD;
            if (RegQueryValueEx(hKeyItem3, TEXT("Linked"), NULL, &dwType, (LPBYTE)&dwLinked, &dwBufferSize) == ERROR_SUCCESS)
                linked = dwLinked?TRUE:FALSE;
            RegCloseKey(hKeyItem3);
            RegCloseKey(hKeyItem2);
        }

        RegCloseKey(hKeyItem);
    }
    RegCloseKey(hKeyNetworkCards);

    return linked;
}

static BOOL GetNetworkCards(struct ptrArray *networkCards)
{
    HKEY hKeyNetworkCards;
    int i;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, KEY_NETWORK_CARDS, 0, KEY_READ, &hKeyNetworkCards) != ERROR_SUCCESS)
        return FALSE;

    for (i=0; ;i++)
    {
        DWORD dwBufferSize, dwType;
        CHAR szItemName[1024], szService[1024], *pService;
        HKEY hKeyItem;

        if (RegEnumKey(hKeyNetworkCards, i, szItemName, 1024) != ERROR_SUCCESS)
            break;

        if (RegOpenKeyEx(hKeyNetworkCards, szItemName, 0, KEY_READ, &hKeyItem) != ERROR_SUCCESS)
            continue;

        dwBufferSize = 1024;
        dwType = REG_SZ;
        if (RegQueryValueEx(hKeyItem, TEXT("ServiceName"), NULL, &dwType, (LPBYTE)szService, &dwBufferSize) != ERROR_SUCCESS)
        {
            RegCloseKey(hKeyItem);
            continue;
        }

        pService = (CHAR *)HeapAlloc(GetProcessHeap(), 0, strlen(szService)+1);
        strcpy_s(pService, strlen(szService)+1, szService);
        ptrArray_insertSorted(networkCards, pService);

        RegCloseKey(hKeyItem);
    }
    RegCloseKey(hKeyNetworkCards);

    return TRUE;
}

struct ptrList *SysGetMacIps()
{
    struct ptrList *macIps = NULL;
    struct ptrArray networkCards;
    PIP_ADAPTER_INFO ipAdapterInfo;
    DWORD dwBufferSize, dwRes, i;

    ptrArray_init(&networkCards, funcCompare_NetworkCards);
    ipAdapterInfo = NULL;
    if (!GetNetworkCards(&networkCards))
        goto error_GetMacIps;

    dwBufferSize = 0;
    dwRes = GetAdaptersInfo(NULL, &dwBufferSize);
    if (dwRes != ERROR_BUFFER_OVERFLOW) goto error_GetMacIps;
    ipAdapterInfo = (PIP_ADAPTER_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwBufferSize);
    dwRes = GetAdaptersInfo(ipAdapterInfo, &dwBufferSize);
    if (dwRes != ERROR_SUCCESS) goto error_GetMacIps;

    //////////////////////////////////////////////////////////////////////////////////////////////
    if (!IsVista())
    {
        PIP_ADAPTER_INFO ai;
        struct MAC_IP *pMacIp;

        for (ai = ipAdapterInfo; ai != NULL; ai = ai->Next)
        {
            if (ptrArray_findSorted(&networkCards, ai->AdapterName) == NULL)
                continue;
            pMacIp = (struct MAC_IP *)malloc(sizeof(struct MAC_IP));
            memcpy(pMacIp->mac, ai->Address, 6);
            pMacIp->ip = ntohl(inet_addr(ai->IpAddressList.IpAddress.String));
            ptrList_append(&macIps, pMacIp);
        }
    }
    else
    {
        MIB_IFTABLE *ifTable;

        ifTable = (MIB_IFTABLE *)HeapAlloc(GetProcessHeap(), 0, sizeof(MIB_IFTABLE));
        dwBufferSize = 0;
        if (GetIfTable(ifTable, &dwBufferSize, 0) == ERROR_INSUFFICIENT_BUFFER)
        {
            HeapFree(GetProcessHeap(), 0, ifTable);
            ifTable = (MIB_IFTABLE *)HeapAlloc(GetProcessHeap(), 0, dwBufferSize);
        }
        if ((GetIfTable(ifTable, &dwBufferSize, 0)) != NO_ERROR)
        {
            HeapFree(GetProcessHeap(), 0, ifTable);
            goto error_GetMacIps;
        }
        for (i=0; i<(int)ifTable->dwNumEntries; i++)
        {
            CHAR szService[256], *pTmp;
            struct MAC_IP *macIp;
            PIP_ADAPTER_INFO ai;

            if (ifTable->table[i].dwType != IF_TYPE_ETHERNET_CSMACD || ifTable->table[i].dwPhysAddrLen != 6)
                continue;
            #ifdef UNICODE
            _tcsncpy(szService, ifTable->table[i].wszName, 255);
            #else
            WideCharToMultiByte(CP_ACP, 0, ifTable->table[i].wszName, -1, szService, 256, NULL, NULL);
            #endif
            pTmp = strchr(szService, '{');
            if (!pTmp) pTmp = szService;
            if (ptrArray_findSorted(&networkCards, pTmp) == NULL)
                continue;

            if (!IsNetworkCardLinked(pTmp))
                continue;

            macIp = (struct MAC_IP *)malloc(sizeof(struct MAC_IP));
            memcpy(macIp->mac, ifTable->table[i].bPhysAddr, 6);
            macIp->ip = 0;
            for (ai = ipAdapterInfo; ai != NULL; ai = ai->Next)
                if (memcmp(macIp->mac, ai->Address, 6) == 0)
                    macIp->ip = ntohl(inet_addr(ai->IpAddressList.IpAddress.String));
            ptrList_append(&macIps, macIp);
        }
        HeapFree(GetProcessHeap(), 0, ifTable);
    }

error_GetMacIps:

    for (i=0; i<(DWORD)ptrArray_size(&networkCards); i++)
        HeapFree(GetProcessHeap(), 0, (char *)ptrArray_nth(&networkCards, i));
    ptrArray_free(&networkCards, NULL);

    if (ipAdapterInfo)
        HeapFree(GetProcessHeap(), 0, ipAdapterInfo);

    return macIps;
}

