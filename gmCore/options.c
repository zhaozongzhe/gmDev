#include "gmCore.h"

static __inline void CheckPath(WCHAR *szPath)
{
    int len = (int)wcslen(szPath);
    if (len > 0 && szPath[len-1] == L'\\')
        szPath[len-1] = L'\0';
}

static BOOL _IsDiskExist(const WCHAR *szDisk)
{
    WCHAR szFileName[MAX_PATH];
    HANDLE hFile;

    swprintf_s(szFileName, MAX_PATH, L"%s\\___temp___.tmp", szDisk);
    hFile = CreateFileW(szFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(hFile);
    DeleteFileW(szFileName);
    return TRUE;
}

void SetOptionsDefault()
{
    BOOL haveDiskD = _IsDiskExist(L"D:");

    g_options.myIp;
    g_options.myMac;
    g_options.myId;
    g_options.svrAddr;
    g_options.portNum;

    g_options.updateMode = 0;
    wcscpy_s(g_options.tmpDir, MAX_PATH, L"C:\\Temp");
    if (haveDiskD) g_options.tmpDir[0] = L'D';

    g_options.dirMode = 0;
    wcscpy_s(g_options.dir, MAX_PATH, L"C:\\GAMES");
    if (haveDiskD) g_options.dir[0] = L'D';

    g_options.userPrvc = 0;
    g_options.userType = 0;
    g_options.userAttr = 0;
    g_options.lineType = 0;
    g_options.lineSpeed = 0;

    g_options.priorityMode = 2;

    g_options.downLimit = 5000;
    g_options.upLimit = 0;
    g_options.maxConcurrentTasks = 5;
    g_options.minDownloadSpeed = 1024*5;
    g_options.maxDownPeersPerTask = 40;
    g_options.maxUpPeersPerTask= 10;
    g_options.maxCachesPerTask = 10;
    g_options.seedMinutes = 0;
    g_options.diskSpaceReserve = 4096;
}

void SetOptions(struct options *o)
{
    WCHAR szTmp[MAX_PATH];

    g_options.myIp;
    g_options.myMac;
    g_options.myId;

    if (strcmp(g_options.svrAddr, o->svrAddr))
    {
        strcpy_s(g_options.svrAddr, 256, o->svrAddr);
        svr_restart();
    }

    if (g_options.portNum != o->portNum)
    {
        g_options.portNum = o->portNum;
        tcp_close(g_listenSock);
        g_listenSock = tcp_listen("", g_options.portNum, NULL);
    }

    g_options.updateMode = o->updateMode;
    CheckPath(o->tmpDir);
    wcscpy_s(g_options.tmpDir, MAX_PATH, o->tmpDir);
    swprintf_s(szTmp, MAX_PATH, L"%s\\", o->tmpDir);
    SureCreateDir(szTmp);

    g_options.dirMode = o->dirMode;
    CheckPath(o->dir);
    wcscpy_s(g_options.dir, MAX_PATH, o->dir);

    g_options.userPrvc = o->userPrvc;
    g_options.userType = o->userType;
    g_options.userAttr = o->userAttr;
    g_options.lineType = o->lineType;
    g_options.lineSpeed = o->lineSpeed;

    if (g_options.priorityMode != o->priorityMode)
    {
        g_options.priorityMode = o->priorityMode;
        task_arrangePriorities();
    }

    if (g_options.downLimit != o->downLimit)
    {
        g_options.downLimit = o->downLimit;
        task_arrangePriorities();
    }
    g_options.upLimit = o->upLimit;
    g_options.maxConcurrentTasks = o->maxConcurrentTasks;
    g_options.minDownloadSpeed = o->minDownloadSpeed;
    g_options.maxDownPeersPerTask = max(40, o->maxDownPeersPerTask);
    g_options.maxUpPeersPerTask= max(5, o->maxUpPeersPerTask);
    g_options.maxCachesPerTask = max(2, o->maxCachesPerTask);
    g_options.seedMinutes = max(0, o->seedMinutes);
    g_options.diskSpaceReserve = o->diskSpaceReserve;

    SaveOptions();
}

void ReadOptions()
{
    WCHAR fileName[MAX_PATH], wszTmp[512];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;

    swprintf_s(fileName, MAX_PATH, L"%s\\options.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) { SaveOptions(); return; }

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;

        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break; *pTmp = 0; pTmp += 2;

        pEq = strchr(pLine, '=');
        if (!pEq) continue; *pEq = 0; pEq ++;

        if (0==_stricmp(pLine, "SvrAddr") && strlen(pEq) < 256)             // SvrAddr
            strcpy_s(g_options.svrAddr, 256, pEq);
        else if (0==_stricmp(pLine, "PortNum"))                             // PortNum
            g_options.portNum = (UINT16)atoi(pEq);
        else if (0==_stricmp(pLine, "UpdateMode"))                          // UpdateMode
            g_options.updateMode = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "TmpDir") && strlen(pEq) < MAX_PATH)    // TmpDir
            wcscpy_s(g_options.tmpDir, MAX_PATH, MbcsToUnicode(pEq, wszTmp, 512));
        else if (0==_stricmp(pLine, "DirMode"))                             // DirMode
            g_options.dirMode = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "Dir") && strlen(pEq) < MAX_PATH)       // Dir
            wcscpy_s(g_options.dir, MAX_PATH, MbcsToUnicode(pEq, wszTmp, 512));
        else if (0==_stricmp(pLine, "UserPrvc"))                            // UserPrvc
            g_options.userPrvc = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "UserType"))                            // UserType
            g_options.userType = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "UserAttr"))                            // UserAttr
            g_options.userAttr = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "LineType"))                            // LineType
            g_options.lineType = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "LineSpeed"))                           // LineSpeed
            g_options.lineSpeed = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "PriorityMode"))                        // PriorityMode
            g_options.priorityMode = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "DownLimit"))                           // DownLimit
            g_options.downLimit = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "UpLimit"))                             // UpLimit
            g_options.upLimit = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "MaxConcurrentTasks"))                  // MaxConcurrentTasks
            g_options.maxConcurrentTasks = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "MinDownloadSpeed"))                    // MinDownloadSpeed
            g_options.minDownloadSpeed = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "MaxDownPeersPerTask"))                 // MaxDownPeersPerTask
            g_options.maxDownPeersPerTask = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "MaxUpPeersPerTask"))                   // MaxUpPeersPerTask
            g_options.maxUpPeersPerTask = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "MaxCachesPerTask"))                    // MaxCachesPerTask
            g_options.maxCachesPerTask = (DWORD)atoi(pEq);
        else if (0==_stricmp(pLine, "SeedMinutes"))                         // SeedMinutes
            g_options.seedMinutes = (UCHAR)atoi(pEq);
        else if (0==_stricmp(pLine, "DiskSpaceReserve"))                    // DiskSpaceReserve
            g_options.diskSpaceReserve = (DWORD)atoi(pEq);
    }
    free(fileData);

    swprintf_s(wszTmp, MAX_PATH, L"%s\\", g_options.tmpDir);
    SureCreateDir(wszTmp);

    if (g_options.portNum <=0 || g_options.portNum > 65535)
        g_options.portNum = DEFAULT_PORT_NUM;

    g_options.maxConcurrentTasks = max(1, g_options.maxConcurrentTasks);
    g_options.minDownloadSpeed = max(g_options.minDownloadSpeed, 1024);
}

static BOOL ini_writeUint(HANDLE hFile, const CHAR *key, UINT32 val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024];

    bufLen = sprintf_s(buf, 1024, "%s=%u\r\n", key, val);
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}
static BOOL ini_writeStr(HANDLE hFile, const CHAR *key, const CHAR *val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024];

    bufLen = sprintf_s(buf, 1024, "%s=%s\r\n", key, val);
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}
static BOOL ini_writeWstr(HANDLE hFile, const CHAR *key, const WCHAR *val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024], szTmp[512];

    bufLen = sprintf_s(buf, 1024, "%s=%s\r\n", key, UnicodeToMbcs(val, szTmp, 512));
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}

BOOL SaveOptions()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];

    swprintf_s(fileName, MAX_PATH, L"%s\\options.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    ini_writeStr(hFile, "SvrAddr", g_options.svrAddr);
    ini_writeUint(hFile, "PortNum", g_options.portNum);

    ini_writeUint(hFile, "UpdateMode", g_options.updateMode);
    ini_writeWstr(hFile, "TmpDir", g_options.tmpDir);

    ini_writeUint(hFile, "DirMode", g_options.dirMode);
    ini_writeWstr(hFile, "Dir", g_options.dir);

    ini_writeStr(hFile, "PID", g_options.myId);

    ini_writeUint(hFile, "UserPrvc", g_options.userPrvc);
    ini_writeUint(hFile, "UserType", g_options.userType);
    ini_writeUint(hFile, "UserAttr", g_options.userAttr);
    ini_writeUint(hFile, "LineType", g_options.lineType);
    ini_writeUint(hFile, "LineSpeed", g_options.lineSpeed);

    ini_writeUint(hFile, "priorityMode", g_options.priorityMode);

    ini_writeUint(hFile, "DownLimit", g_options.downLimit);
    ini_writeUint(hFile, "UpLimit", g_options.upLimit);
    ini_writeUint(hFile, "MaxConcurrentTasks", g_options.maxConcurrentTasks);
    ini_writeUint(hFile, "MinDownloadSpeed", g_options.minDownloadSpeed);
    ini_writeUint(hFile, "MaxDownPeersPerTask", g_options.maxDownPeersPerTask);
    ini_writeUint(hFile, "MaxUpPeersPerTask", g_options.maxUpPeersPerTask);
    ini_writeUint(hFile, "MaxCachesPerTask", g_options.maxCachesPerTask);
    ini_writeUint(hFile, "SeedMinutes", g_options.seedMinutes);

    ini_writeUint(hFile, "DiskSpaceReserve", g_options.diskSpaceReserve);

    SetEndOfFile(hFile);
    CloseHandle(hFile);

    return TRUE;
}

