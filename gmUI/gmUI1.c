#include "gmUI.h"

const TCHAR *g_szAppName = _T("gmUI");

WCHAR g_workDir[MAX_PATH];
HINSTANCE g_hInstance;
HWND g_hWndMain, g_hWndTmpl, g_hWndFocus;
HWND g_hWndTree;
HWND g_hWndDownloading, g_hWndNet, g_hWndLocal, g_hWndSearch;
HWND g_hWndLog;
HWND g_hWndToolBar, g_hWndSearch, g_hWndStatusBar;
int g_treeWidth;
int g_logHeight;
int g_toolHeight;
int g_statusHeight;
int g_currPage;
int g_currSubPage;
WCHAR g_currCategory[MAX_CATEGORY_LEN];
BOOL g_defFontCreated;
HFONT g_defFont;

CHAR g_serverIp[32] = { 0 };
WORD g_serverPort = 0;
CHAR g_serversNotify[1024] = { 0 };

struct options g_options = { 0 };

static int strArray_cmp(const void *p1, const void *p2)
{
    return strcmp((CHAR *)p1, (CHAR *)p2);
}
static int idxDownloading_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_downloading *)p1)->id, ((struct idx_downloading *)p2)->id);
}
static int idxLocal_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_local *)p1)->id, ((struct idx_local *)p2)->id);
}
static int idxNet_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_net *)p1)->id, ((struct idx_net *)p2)->id);
}
static int progress_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct downloading_progress *)p1)->id, ((struct downloading_progress *)p2)->id);
}
static int category_cmp(const void *p1, const void *p2)
{
    return wcscmp((WCHAR *)p2, (WCHAR *)p1);
}
static int tackerInfo_cmp(const void *p1, const void *p2)
{
    return strcmp(((struct tracker_info *)p2)->id, ((struct tracker_info *)p1)->id);
}
static BOOL TryConnectService()
{
    SOCKET s;
    BOOL serviceOK = FALSE;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s != INVALID_SOCKET)
    {
        serviceOK = CmdSocket_GetOptions(s, &g_options);
        CmdSocket_Close(s);
    }

    return serviceOK;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine,
                       int nCmdShow)
{
    MSG msg;
    WNDCLASSEX wcex;
    WSADATA wsd = { 0 };
    int i;

    strcpy_s(g_serverIp, 32, "127.0.0.1");
    g_serverPort = 18900;
    ReadCoreOptions();

    g_downloading = NULL;
    g_waiting = NULL;
    g_uploading = NULL;
    ptrArray_init(&g_seeding, idxDownloading_cmpId);
    ptrArray_init(&g_autoUpdate, strArray_cmp);
    ptrArray_init(&g_localIdx, idxLocal_cmpId);
    ptrArray_init(&g_netIdx, idxNet_cmpId);
    ptrArray_init(&g_progress, progress_cmpId);
    ptrArray_init(&g_categories, category_cmp);
    ptrArray_init(&g_trackerInfo, tackerInfo_cmp);

    GetModuleFileNameW(NULL, g_workDir, MAX_PATH);
    for (i=(int)wcslen(g_workDir); i>0; i--)
    { if (g_workDir[i] == L'\\') { g_workDir[i] = 0; break; } }

    CoInitialize(NULL);
    WSAStartup(MAKEWORD(2, 2), &wsd);

    if (!TryConnectService())
    {
        MessageBox(NULL, _T("无法连接到下载核心服务程序。\r\n请检查服务\"gmCore\"是否正常运行。"), g_szAppName, MB_OK|MB_ICONSTOP);
        goto _main_exit;
    }

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = 0;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_BTNFACE+1);
    wcex.lpszMenuName   = NULL;//MAKEINTRESOURCE(IDC_GMUI);
    wcex.lpszClassName  = _T("gmUIWndClass");
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    RegisterClassEx(&wcex);

    if (!InitInstance (hInstance, SW_SHOWNORMAL))
        goto _main_exit;

    BeginMsgSocketThread(MsgSocketCB, g_serverIp, g_serverPort, "");

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

_main_exit:
    DeleteFonts();
    CoUninitialize();
    WSACleanup();

    ptrList_free(&g_downloading, free);
    ptrList_free(&g_waiting, free);
    ptrList_free(&g_uploading, free);
    ptrArray_free(&g_seeding, free);
    ptrArray_free(&g_autoUpdate, free);
    ptrArray_free(&g_localIdx, free);
    ptrArray_free(&g_netIdx, free);
    ptrArray_free(&g_progress, free);
    ptrArray_free(&g_categories, free);
    ptrArray_free(&g_trackerInfo, free);
    return 0;
}

void CreateFonts()
{
    NONCLIENTMETRICS ncm;

    memset(&ncm, 0, sizeof(NONCLIENTMETRICS));
    ncm.cbSize = sizeof(NONCLIENTMETRICS);
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
    {
        g_defFont = CreateFontIndirect(&ncm.lfMessageFont);
        g_defFontCreated = TRUE;
    }
    else
    {
        g_defFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_defFontCreated = FALSE;
    }
}

void DeleteFonts()
{
    if (g_defFont && g_defFontCreated)
        DeleteObject(g_defFont);
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
CHAR *CRLFToSemicolon(const CHAR *src, CHAR *dst, int dstLen)
{
    const CHAR *p, *pNext;
    int l, l1;

    p = src;
    l = 0;
    while (1)
    {
        pNext = strstr(p, "\r\n");
        if (!pNext) break;
        l1 = pNext - p; if (l+l1 >= dstLen-2) break;
        memcpy(dst+l, p, l1); l += l1;
        dst[l] = ';'; l ++;
        p = pNext + 2;
    }
    dst[l] = 0; l ++;
    return dst;
}

CHAR *SemicolonToCRLF(const CHAR *src, CHAR *dst, int dstLen)
{
    const CHAR *p, *pNext;
    int l, l1;

    p = src;
    l = 0;
    while (1)
    {
        pNext = strchr(p, ';');
        if (!pNext) break;
        l1 = pNext - p; if (l+l1 >= dstLen-3) break;
        memcpy(dst+l, p, l1); l += l1;
        dst[l] = '\r'; l ++;
        dst[l] = '\n'; l ++;
    }
    dst[l] = 0; l ++;
    return dst;
}
BOOL SaveOptions()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    CHAR servers[1024];

    swprintf_s(fileName, MAX_PATH, L"%s\\ui_options.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    ini_writeStr(hFile, "ServerIp", g_serverIp);
    ini_writeUint(hFile, "ServerPort", g_serverPort);
    CRLFToSemicolon(g_serversNotify, servers, 1024);
    ini_writeStr(hFile, "DedicatedServers", servers);

    SetEndOfFile(hFile);
    CloseHandle(hFile);

    return TRUE;
}

BOOL ReadOptions()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;

    swprintf_s(fileName, MAX_PATH, L"%s\\ui_options.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) { SaveOptions(); return FALSE; }

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;

        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        pEq = strchr(pLine, '=');
        if (!pEq) continue;
        *pEq = 0; pEq ++;

        if (0==_stricmp(pLine, "ServerIp") && strlen(pEq) < 512)
            strcpy_s(g_serverIp, 32, pEq);
        else if (0==_stricmp(pLine, "ServerPort"))
            g_serverPort = (UINT16)atoi(pEq);
        else if (0==_stricmp(pLine, "DedicatedServers") && strlen(pEq) < 1024)
            SemicolonToCRLF(pEq, g_serversNotify, 1024);
    }
    free(fileData);

    return TRUE;
}

void SetCoreOptionsDef()
{
    memset(&g_options, 0, sizeof(g_options));
    strcpy_s(g_options.svrAddr, 256, "");
    g_options.portNum = 18900;

    g_options.updateMode = 0;
    wcscpy_s(g_options.tmpDir, MAX_PATH, L"C:\\Temp");

    g_options.dirMode = 0;
    wcscpy_s(g_options.dir, MAX_PATH, L"C:\\Games");

    g_options.dirMode = 0;
    wcscpy_s(g_options.dir, MAX_PATH, L"C:\\Games");
}

BOOL SaveCoreOptions()
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

    ini_writeStr(hFile, "PID", g_options.PID);

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

BOOL ReadCoreOptions()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;

    swprintf_s(fileName, MAX_PATH, L"%s\\options.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;

        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        pEq = strchr(pLine, '=');
        if (!pEq) continue;
        *pEq = 0; pEq ++;

        if (0==_stricmp(pLine, "PortNum"))
            g_serverPort = (UINT16)atoi(pEq);
    }
    free(fileData);

    return TRUE;
}


static BOOL SetPrivilege()
{
    TOKEN_PRIVILEGES tp = { 0 };
    LUID luid;
    HANDLE hToken;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
        return FALSE;

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid))
    {
        CloseHandle(hToken);
        return FALSE; 
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Enable the privilege or disable all privileges.
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
    {
        CloseHandle(hToken);
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);
    return TRUE;
}


#define INIT_WND_WIDTH  1024
#define INIT_WND_HEIGHT 600
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    INITCOMMONCONTROLSEX initCtrls;
    RECT rcScr;

    SetPrivilege();

    g_hInstance = hInstance;

    g_treeWidth = 133;
    g_logHeight = 168;

    g_currPage = 0;
    g_currSubPage = 0;
    g_currCategory[0] = 0;

    initCtrls.dwSize = sizeof(initCtrls);
    initCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&initCtrls);

    CreateFonts();

    if (!SystemParametersInfo(SPI_GETWORKAREA, sizeof(RECT), &rcScr, 0))
    {
        GetClientRect(GetDesktopWindow(), &rcScr);
        rcScr.bottom -= 50;
    }
    g_hWndMain = CreateWindow(
        _T("gmUIWndClass"), g_szAppName,
        WS_OVERLAPPEDWINDOW,
        rcScr.right - INIT_WND_WIDTH, rcScr.bottom - INIT_WND_HEIGHT,
        INIT_WND_WIDTH, INIT_WND_HEIGHT,
        NULL, NULL, hInstance, NULL);

    if (!g_hWndMain) return FALSE;

    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

    return TRUE;
}

void CenterDlg(HWND hDlg)
{
    RECT rcMain;
    RECT rcDlg;
    int x, y;

    GetWindowRect(g_hWndMain, &rcMain);
    GetWindowRect(hDlg, &rcDlg);
    x = (rcMain.right+rcMain.left-(rcDlg.right-rcDlg.left))/2;
    y = (rcMain.bottom+rcMain.top-(rcDlg.bottom-rcDlg.top))/2;

    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);
}


// ---------------------------------------------------------------------------------------
//
HTREEITEM _AddItemToTree(HWND hwndTV, HTREEITEM hParent, HTREEITEM hPrev, LPTSTR lpszItem, LPARAM lParam)
{
    TVITEM tvi = { 0 };
    TVINSERTSTRUCT tvins = { 0 };

    tvi.mask = TVIF_TEXT|TVIF_PARAM;

    tvi.pszText = lpszItem;
    tvi.cchTextMax = sizeof(tvi.pszText)/sizeof(tvi.pszText[0]);
    tvi.lParam = (LPARAM)lParam;
    tvins.item = tvi;
    tvins.hInsertAfter = hPrev;
    tvins.hParent = hParent;

    hPrev = (HTREEITEM)SendMessage(hwndTV, TVM_INSERTITEM, 0, (LPARAM)&tvins);

    return hPrev;
}

static void Tree_RemoveCategoryItems()
{
    HTREEITEM hti, hti1;

    hti = TreeView_GetRoot(g_hWndTree);
    hti = TreeView_GetNextSibling(g_hWndTree, hti);
    if (!hti) return;

    while (1)
    {
        hti1 = TreeView_GetChild(g_hWndTree, hti);
        if (!hti1) break;
        TreeView_DeleteItem(g_hWndTree, hti1);
    }
}

static void Tree_AddCategoryItems()
{
    HTREEITEM hti, hti1;
    TCHAR szText[256];
    int i;

    hti = TreeView_GetRoot(g_hWndTree);
    hti = TreeView_GetNextSibling(g_hWndTree, hti);
    if (!hti) return;

    for (i=0; i<ptrArray_size(&g_categories); i++)
    {
        hti1 = TVI_FIRST;
        hti1 = _AddItemToTree(g_hWndTree, hti, hti1,
            UnicodeToTSTR(ptrArray_nth(&g_categories, i), szText, 256), 2001+i);
    }
    TreeView_Expand(g_hWndTree, hti, TVE_EXPAND);
}

void Tree_CreateWindow(HWND hwndParent)
{
    HTREEITEM hti;

    g_hWndTree = CreateWindowEx(0, WC_TREEVIEW, _T(""),
        WS_VISIBLE|WS_CHILD|WS_TABSTOP|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwndParent,
        (HMENU)ID_TREE, g_hInstance, NULL);
    hti = _AddItemToTree(g_hWndTree, TVI_ROOT, TVI_FIRST, _T("正在下载"), 1000);
    hti = _AddItemToTree(g_hWndTree, TVI_ROOT, hti, _T("所有资源"), 2000);
}

static void Tree_OnNotify(NMHDR *lpNmhdr)
{
    NMTREEVIEW *pnmtv;
    int newPage, oldPage, subPage;

    switch (lpNmhdr->code)
    {
    case TVN_SELCHANGED:
        pnmtv = (LPNMTREEVIEW)lpNmhdr;
        newPage = pnmtv->itemNew.lParam;
        oldPage = pnmtv->itemOld.lParam;
        subPage = newPage % 1000;
        newPage /= 1000;
        oldPage /= 1000;
        if (newPage < PAGE_DOWNLOADING || newPage > PAGE_NET_IDX) newPage = PAGE_DOWNLOADING;
        if (oldPage < PAGE_DOWNLOADING || oldPage > PAGE_NET_IDX) oldPage = PAGE_DOWNLOADING;
        if (newPage != g_currPage)
            PostMessage(g_hWndMain, WM_COMMAND, IDM_CHANGE_PAGE, newPage);
        if (newPage == PAGE_NET_IDX && subPage != g_currSubPage)
            PostMessage(g_hWndMain, WM_COMMAND, IDM_CHANGE_SUB_PAGE, subPage);
        break;
    }
}

// ---------------------------------------------------------------------------------------
//
void SetListViewColumn(HWND hwndLV, int iCol, TCHAR *title, int width, int fmt)
{
    LVCOLUMN lvc;

    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_FMT|LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM;

    lvc.iSubItem = iCol;
    lvc.pszText = title;
    lvc.cx = width;
    lvc.fmt = fmt;
    ListView_InsertColumn(hwndLV, iCol, &lvc);
}

BOOL InitListViewImageLists(HWND hwndLV)
{
    HICON hiconItem;     // icon for list-view items 
    HIMAGELIST hSmall;   // image list for other views 

    hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        ILC_MASK, 1, 1);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_DOWNLOADING));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_WAITING));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_UPLOADING));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_SEEDING));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_CHECK));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_PAUSE));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ERROR_));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_NOTHING));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_EXCLAIM));
    ImageList_AddIcon(hSmall, hiconItem);
    DestroyIcon(hiconItem);

    ListView_SetImageList(hwndLV, hSmall, LVSIL_SMALL);
    return TRUE; 
}

TCHAR *GetSizeString(INT64 size, TCHAR *sz)
{
    double fSize = (double)size;
    if (size >= 1024*1024*1024)
        _stprintf_s(sz, 64, _T("%.02fG"), fSize/(1024*1024*1024));
    else if (size >= 1024*1024)
        _stprintf_s(sz, 64, _T("%.02fM"), fSize/(1024*1024));
    else if (size >= 1024)
        _stprintf_s(sz, 64, _T("%.02fK"), fSize/1024);
    else
        _stprintf_s(sz, 64, _T("%I64d"), size);
    return sz;
}

TCHAR *GetPieceSizeString(UINT32 size, TCHAR *sz)
{
    if (size >= 1024*1024)
        _stprintf_s(sz, 64, _T("%uM"), size/(1024*1024));
    else if (size >= 1024)
        _stprintf_s(sz, 64, _T("%uK"), size/1024);
    else sz[0] = 0;
    return sz;
}

TCHAR *GetTimeString(time_t t, TCHAR *sz)
{
    struct tm tm;

    if (t)
    {
        localtime_s(&tm, &t);
        _stprintf_s(sz, 64, _T("%04d/%02d/%02d %02d:%02d:%02d"),
            tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    else sz[0] = 0;
    return sz;
}


// ---------------------------------------------------------------------------------------
//
static void Log_CreateWindow(HWND hwndParent)
{
    g_hWndLog = CreateWindowEx(0, WC_LISTVIEW, _T(""), WS_CHILD|LVS_REPORT|WS_VISIBLE|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwndParent, (HMENU)ID_TREE, g_hInstance, NULL);
    ListView_SetExtendedListViewStyle(g_hWndLog, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);

    SetListViewColumn(g_hWndLog, 0, _T("时间"), 148, LVCFMT_LEFT);
    SetListViewColumn(g_hWndLog, 1, _T("事件描述"), 700, LVCFMT_LEFT);

    InitListViewImageLists(g_hWndLog);
}

#define MAX_LOG_LINES       500

static void Log_AddLine(TCHAR *msg)
{
    LVITEM lvI = { 0 };
    time_t currTime;
    TCHAR tszTime[64];
    int pos, i, autoScroll;

    time(&currTime);
    pos = ListView_GetItemCount(g_hWndLog);

    i = ListView_GetNextItem(g_hWndLog, -1, LVNI_SELECTED);
    autoScroll = (i==pos-1 || i<0)?1:0;

    lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_STATE;
    lvI.iItem = pos;
    lvI.iImage = ICON_NOTHING;
    lvI.pszText = GetTimeString(currTime, tszTime);
    ListView_InsertItem(g_hWndLog, &lvI);

    ListView_SetItemText(g_hWndLog, pos, 1, msg);

    if (autoScroll)
    {
        ListView_SetItemState(g_hWndLog, pos, LVIS_SELECTED, -1);
        ListView_EnsureVisible(g_hWndLog, pos, FALSE);
    }

    if (pos > MAX_LOG_LINES)
    {
        for (i=0; i<MAX_LOG_LINES/3; i++)
            ListView_DeleteItem(g_hWndLog, 0);
    }
}


// ---------------------------------------------------------------------------------------
//
#define DNCOL_ID                0
#define DNCOL_NAME              1
#define DNCOL_SIZE              2
#define DNCOL_TIME              3
#define DNCOL_DIR               4
#define DNCOL_STATUS            5
#define DNCOL_SPEED             6

static void Downloading_CreateWindow(HWND hwndParent)
{
    g_hWndDownloading = CreateWindowEx(0, WC_LISTVIEW, _T(""), WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwndParent, (HMENU)ID_DOWNLOADING, g_hInstance, NULL);
    ListView_SetExtendedListViewStyle(g_hWndDownloading, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);

    SetListViewColumn(g_hWndDownloading, 0, _T("ID"), 62, LVCFMT_LEFT);
    SetListViewColumn(g_hWndDownloading, 1, _T("名称"), 150, LVCFMT_LEFT);
    SetListViewColumn(g_hWndDownloading, 2, _T("大小"), 70, LVCFMT_RIGHT);
    SetListViewColumn(g_hWndDownloading, 3, _T("最新更新时间"), 130, LVCFMT_LEFT);
    SetListViewColumn(g_hWndDownloading, 4, _T("本地路径"), 180, LVCFMT_LEFT);
    SetListViewColumn(g_hWndDownloading, 5, _T("状态"), 180, LVCFMT_LEFT);
    SetListViewColumn(g_hWndDownloading, 6, _T("下载速度"), 100, LVCFMT_RIGHT);

    InitListViewImageLists(g_hWndDownloading);
}

static TCHAR *GetActionString(struct idx_downloading *dn, TCHAR szAction[256])
{
    struct tracker_info *pti, ti = { 0 };

    if (!dn->action) strcpy_s(szAction, 256, _T("排队"));
    else if (dn->action & TS_UPLOADING) strcpy_s(szAction, 256, _T("上传"));
    else if (dn->action & TS_SEEDING)
    {
        strcpy_s(ti.id, MAX_ID_LEN, dn->id);
        pti = ptrArray_findSorted(&g_trackerInfo, &ti);
        if (pti) _stprintf_s(szAction, 256, _T("供种 [%d/%d %d]"), pti->seeders, pti->peers, pti->incoming);
        else strcpy_s(szAction, 256, _T("供种"));
    }
    else
    {
        szAction[0] = 0;
        if (dn->action & TS_CHECKING) strcpy_s(szAction, 256, _T("检查"));
        else if (dn->action & TS_CONTINUING) strcpy_s(szAction, 256, _T("恢复"));
        else if (dn->action & TS_PREPARING) strcpy_s(szAction, 256, _T("准备"));
        else if (dn->action & TS_TRANSFERING) strcpy_s(szAction, 256, _T("转储"));
        else if (dn->action & (TS_DOWNLOADING|TS_UPDATING))
        {
            strcpy_s(ti.id, MAX_ID_LEN, dn->id);
            pti = ptrArray_findSorted(&g_trackerInfo, &ti);
            if (pti) _stprintf_s(szAction, 256, _T("下载 [%d/%d %d]"), pti->seeders, pti->peers, pti->outgoing);
            strcpy_s(szAction, 256, _T("下载"));
        }

        if (dn->action & TS_ERROR) strcat_s(szAction, 256, _T(" | 出错"));
        if (dn->action & TS_PAUSED) strcat_s(szAction, 256, _T(" | 暂停"));
    }
    return szAction;
}

static int GetActionImage(struct idx_downloading *dn)
{
    if (!dn->action) return ICON_WAITING;
    if (dn->action & TS_CHECKING) return ICON_CHECKING;
    if (dn->action & TS_CONTINUING) return ICON_CHECKING;
    if (dn->action & TS_PREPARING) return ICON_CHECKING;
    if (dn->action & TS_UPLOADING) return ICON_UPLOADING;
    if (dn->action & TS_SEEDING) return ICON_SEEDING;

    if (dn->action & TS_ERROR) return ICON_ERROR;
    if (dn->action & TS_PAUSED) return ICON_PAUSE;

    return ICON_DOWNLOADING;
}

static void _Downloading_SetItemStatus(int iItem, struct idx_downloading *dn)
{
    TCHAR szAction[256];
    int iImage = ICON_NOTHING;
    LV_ITEM lvI = { 0 };

    iImage = GetActionImage(dn);
    GetActionString(dn, szAction);

    lvI.iSubItem = DNCOL_STATUS;
    lvI.pszText = szAction;
    SendMessage(g_hWndDownloading, LVM_SETITEMTEXT, iItem, (LPARAM)&lvI);

    memset(&lvI, 0, sizeof(lvI));
    lvI.iItem = iItem;
    lvI.mask = LVIF_IMAGE;
    lvI.iImage = iImage;
    SendMessage(g_hWndDownloading, LVM_SETITEM, 0, (LPARAM)&lvI);
}

static void _Downloading_SetItemText(int iItem, struct idx_downloading *dn)
{
    struct idx_net *idxn, idxnf = { 0 };
    TCHAR szText[512];
    LV_ITEM lvi = { 0 };

    strcpy_s(idxnf.id, MAX_ID_LEN, dn->id);
    idxn = ptrArray_findSorted(&g_netIdx, &idxnf);
    if (idxn)
    {
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_NAME, UnicodeToTSTR(idxn->name, szText, 512));
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_SIZE, GetSizeString(idxn->size, szText));
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_TIME, GetTimeString(idxn->lastUpdateTime, szText));
    }
    else
    {
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_NAME, _T("..."));
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_SIZE, _T("..."));
        ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_TIME, _T("..."));
    }
    ListView_SetItemText(g_hWndDownloading, iItem, DNCOL_DIR, UnicodeToTSTR(dn->dir, szText, 512));

    _Downloading_SetItemStatus(iItem, dn);
}

#define LIST_DOWNLOADING        0
#define LIST_WAITING            1
#define LIST_UPLOADING          2
#define LIST_SEEDING            3

static void _Downloading_FillListView()
{
    struct ptrList *li;
    struct idx_downloading *dn;
    LVITEM lvI = { 0 };
    TCHAR tszId[64];
    int i, j;

    ListView_DeleteAllItems(g_hWndDownloading);

    lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_STATE|LVIF_PARAM;
    for (i=0, li=g_downloading; li; i++,li=li->next)
    {
        dn = (struct idx_downloading *)li->data;

        lvI.iItem = i;
        lvI.lParam = LIST_DOWNLOADING;
        lvI.iImage = GetActionImage(dn);
        lvI.pszText = MbcsToTSTR(dn->id, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(i, dn);
    }
    for (li=g_waiting; li; i++,li=li->next)
    {
        dn = (struct idx_downloading *)li->data;

        lvI.iItem = i;
        lvI.lParam = LIST_WAITING;
        lvI.iImage = GetActionImage(dn);
        lvI.pszText = MbcsToTSTR(dn->id, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(i, dn);
    }
    for (li=g_uploading; li; i++,li=li->next)
    {
        dn = (struct idx_downloading *)li->data;

        lvI.iItem = i;
        lvI.lParam = LIST_UPLOADING;
        lvI.iImage = GetActionImage(dn);
        lvI.pszText = MbcsToTSTR(dn->id, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(i, dn);
    }
    for (j=0; j<ptrArray_size(&g_seeding); j++, i++)
    {
        dn = (struct idx_downloading *)ptrArray_nth(&g_seeding, j);

        lvI.iItem = i;
        lvI.lParam = LIST_SEEDING;
        lvI.iImage = GetActionImage(dn);
        lvI.pszText = MbcsToTSTR(dn->id, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(i, dn);
    }
}

static int _Downloading_FindItem(const CHAR *id, int startPos, int endPos)
{
    TCHAR tszId[64];
    CHAR szId[64];
    int i;

    for (i=startPos; i<endPos; i++)
    {
        tszId[0] = 0;
        ListView_GetItemText(g_hWndDownloading, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (strcmp(id, szId)==0) return i;
    }

    return -1;
}


static void OnDownloadingPaused(const CHAR *id)
{
    TCHAR szText[512];
    struct idx_downloading *dn;
    struct ptrList *li;
    int iItem;

    for (dn=NULL, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, id) == 0) break;
    }
    if (!dn || strcmp(dn->id, id)) return;
    dn->action |= TS_PAUSED;

    _stprintf_s(szText, 512, _T("%s 已暂停"), id);
    Log_AddLine(szText);

    iItem = _Downloading_FindItem(id, 0, g_downloadingCnt);
    if (iItem < 0) return;
    _Downloading_SetItemStatus(iItem, dn);
}

static void OnDownloadingResumed(const CHAR *id)
{
    TCHAR szText[512];
    struct idx_downloading *dn;
    struct ptrList *li;
    int iItem;

    for (dn=NULL, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, id) == 0) break;
    }
    if (!dn || strcmp(dn->id, id)) return;
    dn->action &= ~TS_PAUSED;

    _stprintf_s(szText, 512, _T("%s 已恢复下载"), id);
    Log_AddLine(szText);

    iItem = _Downloading_FindItem(id, 0, g_downloadingCnt);
    if (iItem < 0) return;
    _Downloading_SetItemStatus(iItem, dn);
}

extern BOOL DownloadingFromString(struct idx_downloading *dn, CHAR *id);

static void OnDownloadingAdded(CHAR *id)
{
    struct ptrList *li;
    struct idx_downloading *dn, *dnNew;
    LVITEM lvI = { 0 };
    TCHAR tszId[64];
    int pos;

    dnNew = malloc(sizeof(struct idx_downloading)); if (!dnNew) return;
    if (!DownloadingFromString(dnNew, id)) { free(dnNew); return; }
    for(li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dnNew->id, dn->id)==0)
        {
            *dn = *dnNew; free(dnNew);
            pos = _Downloading_FindItem(dn->id, 0, g_downloadingCnt);
            if (pos >= 0)
            {
                _Downloading_SetItemText(pos, dn);
                _Downloading_SetItemStatus(pos, dn);
            }
            return;
        }
    }

    ptrList_append(&g_downloading, dnNew);
    g_downloadingCnt ++;

    pos = g_downloadingCnt-1;

    lvI.mask = LVIF_TEXT|LVIF_STATE|LVIF_IMAGE|LVIF_PARAM;
    lvI.lParam = LIST_DOWNLOADING;
    lvI.iImage = GetActionImage(dnNew);
    lvI.iItem = pos;
    lvI.pszText = MbcsToTSTR(dnNew->id, tszId, 64);
    ListView_InsertItem(g_hWndDownloading, &lvI);

    _Downloading_SetItemText(pos, dnNew);
}

static void OnDownloadingDeleted(const CHAR *id)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    struct downloading_progress *prg, prgf = { 0 };
    TCHAR tszId[64];
    CHAR szId[64];
    int i;

    for (i=0; i<g_downloadingCnt; i++)
    {
        ListView_GetItemText(g_hWndDownloading, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (0==strcmp(szId, id))
        {
            ListView_DeleteItem(g_hWndDownloading, i);
            break;
        }
    }

    for (li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, id)==0)
        {
            free(dn);
            ptrList_remove_node(&g_downloading, li);
            g_downloadingCnt--;
            break;
        }
    }

    strcpy_s(prgf.id, MAX_ID_LEN, id);
    prg = ptrArray_removeSorted(&g_progress, &prgf);
    if (prg) free(prg);
}

static TCHAR *GetPercentString(TCHAR szPercent[20], INT64 completed, INT64 total)
{
    double dTotal, dCompleted, dPercent;

    if (!total) { _tcscpy_s(szPercent, 20, _T("0%")); return szPercent; }
    if (total <= completed) { _tcscpy_s(szPercent, 20, _T("100%")); return szPercent; }
    if (!completed) { _tcscpy_s(szPercent, 20, _T("0%")); return szPercent; }

    dTotal = (double)total;
    dCompleted = (double)completed;
    dPercent = dCompleted / dTotal * 100;

    _stprintf_s(szPercent, 20, _T("%.01f%%"), dPercent);
    return szPercent;
}

static TCHAR *GetSpeedString(TCHAR szSpeed[64], int speed)
{
    double fSpeed = (double)speed;

    if (speed >= 1000*1024)
        _stprintf_s(szSpeed, 64, _T("%.01fM"), fSpeed/(1024*1024));
    else if (speed >= 1000)
        _stprintf_s(szSpeed, 64, _T("%.01fK"), fSpeed/1024);
    else if (speed > 0)
        _stprintf_s(szSpeed, 64, _T("%dB"), speed);
    else szSpeed[0] = 0;
    return szSpeed;
}

static BOOL DownloadingProgressFromString(struct downloading_progress *prg, CHAR *pId)
{
    CHAR *pAction, *pDownloaded, *pTotal, *pUpSpeed, *pDnSpeed, *pSeederCnt, *pPeerCnt;

    pAction = strchr(pId, '\t'); if (!pAction) return FALSE; *pAction = 0; pAction ++;
    pDownloaded = strchr(pAction, '\t'); if (!pDownloaded) return FALSE; *pDownloaded = 0; pDownloaded ++;
    pTotal = strchr(pDownloaded, '\t'); if (!pTotal) return FALSE; *pTotal = 0; pTotal ++;
    if (strcmp(pAction, "downloading") == 0)
    {
        pUpSpeed = strchr(pTotal, '\t'); if (!pUpSpeed) return FALSE; *pUpSpeed = 0; pUpSpeed ++;
        pDnSpeed = strchr(pUpSpeed, '\t'); if (!pDnSpeed) return FALSE; *pDnSpeed = 0; pDnSpeed ++;
        pSeederCnt = strchr(pDnSpeed, '\t'); if (!pSeederCnt) return FALSE; *pSeederCnt = 0; pSeederCnt ++;
        pPeerCnt = strchr(pSeederCnt, '\t'); if (!pPeerCnt) return FALSE; *pPeerCnt = 0; pPeerCnt ++;
    }

    if (strlen(pId) >= MAX_ID_LEN) return FALSE;

    strcpy_s(prg->id, MAX_ID_LEN, pId);
    strcpy_s(prg->action, 16, pAction);
    prg->completed = _atoi64(pDownloaded);
    prg->total = _atoi64(pTotal);
    if (strcmp(pAction, "downloading") == 0)
    {
        prg->upSpeed = atoi(pUpSpeed);
        prg->dnSpeed = atoi(pDnSpeed);
        prg->seederCnt = atoi(pSeederCnt);
        prg->peerCnt = atoi(pPeerCnt);
    }
    return TRUE;
}

static void OnDownloadingCompleted(CHAR *pId)
{
    TCHAR tszTmp[256], tszId[64];

    _stprintf_s(tszTmp, 256, _T("%s 下载已完成，转至供种"), MbcsToTSTR(pId, tszId, 64));
    Log_AddLine(tszTmp);
}

static void OnUpdatingCompleted(CHAR *pId)
{
    TCHAR tszTmp[256], tszId[64];

    _stprintf_s(tszTmp, 256, _T("%s 已下载更新，等待转储"), MbcsToTSTR(pId, tszId, 64));
    Log_AddLine(tszTmp);
}

static void OnDownloadingProgress(CHAR *pId)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    struct downloading_progress *prg, *prgOld;
    TCHAR tszId[64], tszText[256], tszTmp[256], tszPercent[20], tszSpeed[64];
    CHAR szId[64];
    int i, exist;

    prg = malloc(sizeof(struct downloading_progress)); if (!prg) return;
    if (!DownloadingProgressFromString(prg, pId)) { free(prg); return; }
    prgOld = ptrArray_findSorted(&g_progress, prg);
    if (prgOld) { *prgOld = *prg; free(prg); prg = prgOld; }
    else ptrArray_insertSorted(&g_progress, prg);

    for (exist=0, i=0; i<g_downloadingCnt; i++)
    {
        ListView_GetItemText(g_hWndDownloading, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (0==strcmp(szId, pId)) { exist = 1; break; }
    }
    if (!exist) return;

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, pId)==0) { exist = 1; break; }
    }
    if (!exist) return;

    if (strcmp(prg->action, "downloading") == 0)
    {
        GetPercentString(tszPercent, prg->completed, prg->total);
        _stprintf_s(tszText, 256, _T("正在下载 %s"), tszPercent);
        ListView_GetItemText(g_hWndDownloading, i, 5, tszTmp, 64);
        if (_tcscmp(tszTmp, tszText))
            ListView_SetItemText(g_hWndDownloading, i, 5, tszText);

        GetSpeedString(tszSpeed, prg->dnSpeed);
        ListView_GetItemText(g_hWndDownloading, i, 6, tszTmp, 256);
        if (_tcscmp(tszTmp, tszSpeed))
            ListView_SetItemText(g_hWndDownloading, i, 6, tszSpeed);
    }
    else
    {
        GetPercentString(tszPercent, prg->completed, prg->total);
        _stprintf_s(tszText, 256, _T("正在检查 %s"), tszPercent);
        ListView_GetItemText(g_hWndDownloading, i, 5, tszTmp, 64);
        if (_tcscmp(tszTmp, tszText))
            ListView_SetItemText(g_hWndDownloading, i, 5, tszText);
        if (prg->total && prg->completed >= prg->total)
        {
            _stprintf_s(tszText, 256, _T("%s 检查完成"), tszId);
            Log_AddLine(tszText);
        }
    }
}

struct idx_downloading *_Downloading_FindIdx(const CHAR *id)
{
    struct idx_downloading *dn, dnf = { 0 };
    struct ptrList *li;
    int exist;

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, id)) { exist = 1; break; }
    }
    if (exist) return dn;

    strcpy_s(dnf.id, MAX_ID_LEN, id);
    dn = (struct idx_downloading *)ptrArray_findSorted(&g_seeding, &dnf);
    if (dn) return dn;

    for (exist=0, li=g_uploading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, id)) { exist = 1; break; }
    }
    if (exist) return dn;

    for (exist=0, li=g_waiting; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, id)) { exist = 1; break; }
    }
    if (exist) return dn;

    return NULL;
}

static void OnDownloadingChanged(CHAR *pId)
{
    struct idx_downloading *dn;
    int action, i;
    CHAR *pReason, *pAction;
    TCHAR tszTmp[256], tszId[64], tszAction[256];

    pReason = strchr(pId, '\t'); if (!pReason) return; *pReason = 0; pReason ++;
    pAction = strchr(pReason, '\t'); if (!pAction) return; *pAction = 0; pAction ++;
    action = atoi(pAction); if (!action) return;

    dn = _Downloading_FindIdx(pId);
    if (!dn) return;

    dn->action = action;

    i = _Downloading_FindItem(pId, 0, g_downloadingCnt);
    if (i >= 0) _Downloading_SetItemStatus(i, dn);

    if (!strcmp(pReason, "check"))
    {
        _stprintf_s(tszTmp, 256, _T("%s 检查完成，转为%s"),
            MbcsToTSTR(pId, tszId, 64), GetActionString(dn, tszAction));
        Log_AddLine(tszTmp);
    }
    else if (!strcmp(pReason, "net_idx"))
    {
        _stprintf_s(tszTmp, 256, _T("%s 网络资源已变动，转为%s"),
            MbcsToTSTR(pId, tszId, 64), GetActionString(dn, tszAction));
        Log_AddLine(tszTmp);
    }
    else if (!strcmp(pReason, "prepare"))
    {
        _stprintf_s(tszTmp, 256, _T("%s 检查完成，转为%s"),
            MbcsToTSTR(pId, tszId, 64), GetActionString(dn, tszAction));
        Log_AddLine(tszTmp);
    }
}

static TCHAR *GetErrorString(int err)
{
    switch (err)
    {
    case ERR_IDX: return _T("分析种子文件时出错");
    case ERR_NEW_IDX: return _T("生成种子文件时出错");
    case ERR_DISK_SPACE: return _T("磁盘空间不足");
    case ERR_FILE_READ: return _T("读文件出错");
    case ERR_FILE_WRITE: return _T("写文件出错");
    case ERR_FILES: return _T("文件内容出错");
    case ERR_TMP_FILE: return _T("临时文件出错");
    case ERR_NET_IDX: return _T("下载种子文件时出错");
    case ERR_NET_IDX2: return _T("上传种子文件时出错");
    case ERR_CONTINUE: return _T("恢复下载时出错");
    default: return NULL;
    }
}

static void OnDownloadingError(CHAR *pId)
{
    struct idx_downloading *dn;
    struct ptrList *li;
    int err, i, exist;
    CHAR *pAction, *pErr;
    TCHAR tszTmp[512], tszId[64], *tszErr;

    pAction = strchr(pId, '\t'); if (!pAction) return; *pAction = 0; pAction ++;
    pErr = strchr(pAction, '\t'); if (!pErr) return; *pErr = 0; pErr ++;
    err = atoi(pErr); if (!err) return;

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, pId)) { exist = 1; break; }
    }
    if (!exist) return;
    dn->err = err;

    i = _Downloading_FindItem(pId, 0, g_downloadingCnt);
    if (i < 0) return;

    tszErr = GetErrorString(err);
    if (tszErr)
    {
        ListView_SetItemText(g_hWndDownloading, i, 5, tszErr);
        _stprintf_s(tszTmp, 512, _T("%s %s"), MbcsToTSTR(pId, tszId, 64), tszErr);
        Log_AddLine(tszTmp);
    }
}

struct idx_currsel
{
    int iSel;
    int iList;
    CHAR idSel[64];
    struct idx_downloading *dn;
};
struct idx_currsel g_currIdx = { 0 };

static void _Downloading_GetCurrSel()
{
    struct ptrList *ids = NULL, *li;
    struct idx_downloading dnf = { 0 };
    TCHAR tszId[64];
    BOOL exist;

    g_currIdx.iSel = ListView_GetNextItem(g_hWndDownloading, -1, LVNI_SELECTED);
    if (g_currIdx.iSel < 0) return;

    ListView_GetItemText(g_hWndDownloading, g_currIdx.iSel, 0, tszId, 64);
    TSTRToMbcs(tszId, g_currIdx.idSel, 64);

    if (g_currIdx.iSel < g_downloadingCnt)
    {
        g_currIdx.iList = LIST_DOWNLOADING;
        for (exist=0, li=g_downloading; li; li=li->next)
        {
            g_currIdx.dn = (struct idx_downloading *)li->data;
            if (0==strcmp(g_currIdx.dn->id, g_currIdx.idSel)) { exist = 1; break; }
        }
        if (!exist) g_currIdx.iSel = -1;
    }
    else if (g_currIdx.iSel < g_downloadingCnt+g_waitingCnt)
    {
        g_currIdx.iList = LIST_WAITING;
        for (exist=0, li=g_waiting; li; li=li->next)
        {
            g_currIdx.dn = (struct idx_downloading *)li->data;
            if (0==strcmp(g_currIdx.dn->id, g_currIdx.idSel)) { exist = 1; break; }
        }
        if (!exist) g_currIdx.iSel = -1;
    }
    else if (g_currIdx.iSel < g_downloadingCnt+g_waitingCnt+g_uploadingCnt)
    {
        g_currIdx.iList = LIST_UPLOADING;
        for (exist=0, li=g_uploading; li; li=li->next)
        {
            g_currIdx.dn = (struct idx_downloading *)li->data;
            if (0==strcmp(g_currIdx.dn->id, g_currIdx.idSel)) { exist = 1; break; }
        }
        if (!exist) g_currIdx.iSel = -1;
    }
    else if (g_currIdx.iSel < g_downloadingCnt+g_waitingCnt+g_uploadingCnt+g_seedingCnt)
    {
        g_currIdx.iList = LIST_SEEDING;
        strcpy_s(dnf.id, MAX_ID_LEN, g_currIdx.idSel);
        g_currIdx.dn = (struct idx_downloading *)ptrArray_findSorted(&g_seeding, &dnf);
        if (!g_currIdx.dn) g_currIdx.iSel = -1;
    }
    else g_currIdx.iSel = -1;
}

static void Downloading_OnNotify(NMHDR *lpNmhdr)
{
    NMITEMACTIVATE *lpnmitem;
    HMENU hMenu, hMenuTrackPopup;
    POINT pt;
    int iMenu;
    TCHAR tszTmp[256], tszId[64];

    switch (lpNmhdr->code)
    {
    case NM_RCLICK:
        lpnmitem = (LPNMITEMACTIVATE)lpNmhdr;
        hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_POPUP_MENU));
        if (!hMenu) break;

        _Downloading_GetCurrSel();

        if (g_currIdx.iSel < 0) iMenu = 5;
        else if (g_currIdx.iList == LIST_DOWNLOADING) iMenu = 0;
        else if (g_currIdx.iList == LIST_WAITING) iMenu = 1;
        else if (g_currIdx.iList == LIST_UPLOADING) iMenu = 2;
        else if (g_currIdx.iList == LIST_SEEDING) iMenu = 3;

        hMenuTrackPopup = GetSubMenu(hMenu, iMenu);
        if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_SEEDING)
            EnableMenuItem(hMenuTrackPopup, IDM_UPLOAD, MF_BYCOMMAND|MF_GRAYED);
        else
        {
            _stprintf_s(tszTmp, 256, _T("上传 %s"), MbcsToTSTR(g_currIdx.idSel, tszId, 64));
            ModifyMenu(hMenuTrackPopup, IDM_UPLOAD, MF_BYCOMMAND, IDM_UPLOAD, tszTmp);
        }

        if (!g_currIdx.dn || !g_currIdx.dn->dir[0])
            EnableMenuItem(hMenuTrackPopup, IDM_OPEN_FOLDER, MF_BYCOMMAND|MF_GRAYED);

        pt = lpnmitem->ptAction;
        ClientToScreen(g_hWndDownloading, (LPPOINT)&pt);
        TrackPopupMenu(hMenuTrackPopup, TPM_LEFTALIGN|TPM_LEFTBUTTON,
            pt.x, pt.y, 0, g_hWndMain, NULL);

        DestroyMenu(hMenu);
        break;
    case NM_DBLCLK:
        lpnmitem = (LPNMITEMACTIVATE)lpNmhdr;
        _Downloading_GetCurrSel();
        if (g_currIdx.iSel >= 0)
        {
            if (g_currIdx.iList == LIST_DOWNLOADING || g_currIdx.iList == LIST_SEEDING)
                DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_PEER_INFO), g_hWndMain, PeerInfoDlgProc);
            else
                DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_IDX_INFO), g_hWndMain, IdxInfoDlgProc);
        }
        break;
    }
}

static void _Downloading_TaskInfo()
{
    _Downloading_GetCurrSel();
    if (g_currIdx.iSel >= 0 && (g_currIdx.iList == LIST_DOWNLOADING || g_currIdx.iList == LIST_SEEDING))
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_PEER_INFO), g_hWndMain, PeerInfoDlgProc);
}

static void _Downloading_OpenFolder()
{
    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList == LIST_WAITING) return;

    if (g_currIdx.dn && g_currIdx.dn->dir[0])
    {
        PIDLIST_ABSOLUTE pidl;
        SHParseDisplayName(g_currIdx.dn->dir, NULL, &pidl, 0, NULL);
        SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
    }
    //if (g_currIdx.dn && g_currIdx.dn->dir[0])
    //    ShellExecuteW(g_hWndMain, L"open", g_currIdx.dn->dir, NULL, NULL, SW_SHOWNORMAL);
}

static void _Downloading_CheckTasks()
{
    struct ptrList *selected = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 ||
        g_currIdx.iList == LIST_WAITING ||
        g_currIdx.iList == LIST_UPLOADING)
        return;

    ptrList_append(&selected, _strdup(g_currIdx.idSel));

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s != INVALID_SOCKET)
    {
        CmdSocket_CheckTasks(s, selected);
        CmdSocket_Close(s);
    }
    else
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);

    ptrList_free(&selected, free);
}

static void OnTransferBegin(CHAR *pId)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    int i, exist;

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, pId)==0) { exist = 1; break; }
    }
    if (!exist) return;

    i = _Downloading_FindItem(pId, 0, g_downloadingCnt);
    if (i < 0) return;

    ListView_SetItemText(g_hWndDownloading, i, 5, _T("正在转储临时文件"));
    ListView_SetItemText(g_hWndDownloading, i, 6, _T(""));
}

static void OnTransferError(CHAR *pId)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    CHAR *pErr;
    int err, i, exist;
    TCHAR tszTmp[256];

    pErr = strchr(pId, '\t'); if (!pErr) return; *pErr = 0; pErr ++;
    err = atoi(pErr);

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, pId)==0) { exist = 1; break; }
    }
    if (!exist) return;

    i = _Downloading_FindItem(pId, 0, g_downloadingCnt);
    if (i < 0) return;

    if (!err)
    {
        _stprintf_s(tszTmp, 256, _T("%s 转储完成，转至供种"), pId);
        Log_AddLine(tszTmp);
        return;
    }
    if (err < 0)
    {
        _stprintf_s(tszTmp, 256, _T("%s 文件被占用，将稍后再试"), pId);
        Log_AddLine(tszTmp);
        return;
    }

    _stprintf_s(tszTmp, 256, _T("%s 转储失败，需完全检查后重新下载更新"), pId);
    Log_AddLine(tszTmp);

    ListView_SetItemText(g_hWndDownloading, i, 5, _T("转储失败，需重新下载"));
}

static void OnTrackerInfo(CHAR *pId)
{
    struct tracker_info ti, *pti;
    CHAR *pSeeders, *pPeers, *pIncoming, *pOutgoing;
    struct idx_downloading *dn, dnf = { 0 };
    struct ptrList *li;
    int iItem, exist;

    pSeeders = strchr(pId, '\t'); if (!pSeeders) return; *pSeeders = 0; pSeeders ++;
    pPeers = strchr(pSeeders, '\t'); if (!pPeers) return; *pPeers = 0; pPeers ++;
    pIncoming = strchr(pPeers, '\t'); if (!pIncoming) return; *pIncoming = 0; pIncoming ++;
    pOutgoing = strchr(pIncoming, '\t'); if (!pOutgoing) return; *pOutgoing = 0; pOutgoing ++;
    if (strlen(pId) >= MAX_ID_LEN) return;

    strcpy_s(ti.id, MAX_ID_LEN, pId);
    ti.seeders = atoi(pSeeders);
    ti.peers = atoi(pPeers);
    ti.incoming = atoi(pIncoming);
    ti.outgoing = atoi(pOutgoing);

    pti = ptrArray_findSorted(&g_trackerInfo, &ti);
    if (pti) *pti = ti;
    else
    {
        pti = malloc(sizeof(struct tracker_info));
        *pti = ti;
        ptrArray_insertSorted(&g_trackerInfo, pti);
    }

    iItem = _Downloading_FindItem(pId, 0, g_downloadingCnt+g_waitingCnt+g_uploadingCnt+g_seedingCnt);
    if (iItem < 0) return;

    for (exist=0, li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, pId)==0) { exist = 1; break; }
    }
    if (!exist)
    {
        strcpy_s(dnf.id, MAX_ID_LEN, pId);
        dn = ptrArray_findSorted(&g_seeding, &dnf);
        if (!dn) return;
    }

    _Downloading_SetItemStatus(iItem, dn);
}

// ---------------------------------------------------------------------------------------
//
static void OnWaitingAdded(CHAR *id)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    int pos;
    TCHAR tszTmp[256];
    LVITEM lvI = { 0 };

    for (li=g_waiting; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, id)==0) return;
    }

    dn = malloc(sizeof(struct idx_downloading)); if (!dn) return;
    memset(dn, 0, sizeof(struct idx_downloading));
    strcpy_s(dn->id, MAX_ID_LEN, id);
    ptrList_append(&g_waiting, dn);
    g_waitingCnt ++;

    pos = g_downloadingCnt+g_waitingCnt-1;

    lvI.mask = LVIF_TEXT|LVIF_STATE|LVIF_IMAGE|LVIF_PARAM;
    lvI.lParam = LIST_WAITING;
    lvI.iImage = GetActionImage(dn);
    lvI.iItem = pos;
    lvI.pszText = MbcsToTSTR(id, tszTmp, 256);
    ListView_InsertItem(g_hWndDownloading, &lvI);

    _Downloading_SetItemText(pos, dn);
}

static void OnWaitingDeleted(CHAR *pId)
{
    struct ptrList *li;
    struct idx_downloading *dn;
    int i;

    i = _Downloading_FindItem(pId, g_downloadingCnt, g_downloadingCnt+g_waitingCnt);
    if (i >= 0) ListView_DeleteItem(g_hWndDownloading, i);

    for (li=g_waiting; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, pId)==0)
        {
            free(dn);
            ptrList_remove_node(&g_waiting, li);
            g_waitingCnt --;
            break;
        }
    }
}

static void OnUploadingAdded(CHAR *id)
{
    struct idx_downloading *dn, *dnOld;
    struct ptrList *li;
    int pos;
    TCHAR tszId[64], tszTmp[256];
    LVITEM lvI = { 0 };

    dn = malloc(sizeof(struct idx_downloading)); if (!dn) return;
    memset(dn, 0, sizeof(struct idx_downloading));
    if (!DownloadingFromString(dn, id)) { free(dn); return; }
    dn->action = TS_UPLOADING;
    for (li=g_uploading; li; li=li->next)
    {
        dnOld = li->data;
        if (0==strcmp(dn->id, dnOld->id)) { free(dn); return; }
    }
    ptrList_append(&g_uploading, dn);
    g_uploadingCnt ++;
    g_maxId = max(g_maxId, atoi(dn->id));

    pos = g_downloadingCnt+g_waitingCnt+g_uploadingCnt-1;

    lvI.mask = LVIF_TEXT|LVIF_STATE|LVIF_IMAGE|LVIF_PARAM;
    lvI.lParam = LIST_UPLOADING;
    lvI.iImage = GetActionImage(dn);
    lvI.iItem = pos;
    lvI.pszText = MbcsToTSTR(dn->id, tszId, 64);
    ListView_InsertItem(g_hWndDownloading, &lvI);

    _Downloading_SetItemText(pos, dn);

    _stprintf_s(tszTmp, 256, _T("%s 已创建上传任务，等待调度"), id);
    Log_AddLine(tszTmp);
}

static void OnUploadingDeleted(CHAR *pId)
{
    struct idx_downloading *dn;
    struct ptrList *li;
    int i;

    i = _Downloading_FindItem(pId, g_downloadingCnt+g_waitingCnt,
        g_downloadingCnt+g_waitingCnt+g_uploadingCnt);
    if (i >= 0) ListView_DeleteItem(g_hWndDownloading, i);

    for (li=g_uploading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, pId))
        {
            free(dn);
            ptrList_remove_node(&g_uploading, li);
            g_uploadingCnt--;
            break;
        }
    }
}

static void OnUploadingProgress(CHAR *pId)
{
    struct idx_downloading *dn;
    struct ptrList *li;
    int i, exist;
    CHAR *pAction, *pDone, *pTotal;
    TCHAR tszTmp[256], tszOld[256], szPercent[20];

    pAction = strchr(pId, '\t'); if (!pAction) return; *pAction = 0; pAction ++;
    pDone = strchr(pAction, '\t'); if (!pDone) return; *pDone = 0; pDone ++;
    pTotal = strchr(pDone, '\t'); if (!pTotal) return; *pTotal = 0; pTotal ++;

    for (exist=0, li=g_uploading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, pId)) { exist = 1; break; }
    }
    if (!exist) return;

    i = _Downloading_FindItem(pId, g_downloadingCnt+g_waitingCnt,
        g_downloadingCnt+g_waitingCnt+g_uploadingCnt);
    if (i < 0) return;

    if (strcmp(pAction, "creating_idx") == 0)
    {
        _stprintf_s(tszTmp, 256, _T("正生成种子文件 %s"),
            GetPercentString(szPercent, atoi(pDone), atoi(pTotal)));
        ListView_GetItemText(g_hWndDownloading, i, 5, tszOld, 256);
        if (strcmp(tszOld, tszTmp))
            ListView_SetItemText(g_hWndDownloading, i, 5, tszTmp);
    }
    if (strcmp(pAction, "uploading_idx") == 0)
        ListView_SetItemText(g_hWndDownloading, i, 5, _T("正在上传种子文件"));
}

static void OnUploadingError(CHAR *pId)
{
    struct idx_downloading *dn;
    struct ptrList *li;
    int err, i, exist;
    CHAR *pErr;
    TCHAR tszTmp[512], tszId[64], *tszErr;

    pErr = strchr(pId, '\t'); if (!pErr) return; *pErr = 0; pErr ++;
    err = atoi(pErr);
    if (!err)
    {
        _stprintf_s(tszTmp, 512, _T("%s 上传已成功，转至做种"), pId);
        Log_AddLine(tszTmp);
        return;
    }

    for (exist=0, li=g_uploading; li; li=li->next)
    {
        dn = li->data;
        if (0==strcmp(dn->id, pId)) { exist = 1; break; }
    }
    if (!exist) return;

    i = _Downloading_FindItem(pId, g_downloadingCnt+g_waitingCnt,
        g_downloadingCnt+g_waitingCnt+g_uploadingCnt);
    if (i < 0) return;

    tszErr = GetErrorString(err);
    if (tszErr)
    {
        ListView_SetItemText(g_hWndDownloading, i, 5, tszErr);
        _stprintf_s(tszTmp, 512, _T("%s %s"), MbcsToTSTR(pId, tszId, 64), tszErr);
        Log_AddLine(tszTmp);
    }
}

static void OnSeedingAdded(CHAR *id)
{
    struct idx_downloading *dn;
    struct idx_local *idxl, idxlf = { 0 };
    int i, pos;
    TCHAR tszId[64];
    CHAR szId[64];
    LVITEM lvI = { 0 };

    strcpy_s(idxlf.id, MAX_ID_LEN, id);
    idxl = ptrArray_findSorted(&g_localIdx, &idxlf);
    if (!idxl) return;

    dn = malloc(sizeof(struct idx_downloading)); if (!dn) return;
    memset(dn, 0, sizeof(struct idx_downloading));
    strcpy_s(dn->id, MAX_ID_LEN, id);
    strcpy_s(dn->hash, MAX_HASH_LEN, idxl->hash);
    wcscpy_s(dn->dir, MAX_PATH, idxl->dir);
    dn->action = TS_SEEDING;
    if (ptrArray_findSorted(&g_seeding, dn)) { free(dn); return; }
    ptrArray_insertSorted(&g_seeding, dn);
    g_seedingCnt++;

    for (pos=-1,i=g_downloadingCnt+g_waitingCnt+g_uploadingCnt;
        i<g_downloadingCnt+g_waitingCnt+g_uploadingCnt+g_seedingCnt-1;
        i++)
    {
        ListView_GetItemText(g_hWndDownloading, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (strcmp(id, szId) < 0) { pos = i; break; }
    }

    if (pos < 0) pos = g_downloadingCnt+g_waitingCnt+g_uploadingCnt+g_seedingCnt-1;

    lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
    lvI.lParam = LIST_SEEDING;
    lvI.iImage = GetActionImage(dn);
    lvI.iItem = pos;
    lvI.pszText = MbcsToTSTR(id, tszId, 64);
    ListView_InsertItem(g_hWndDownloading, &lvI);

    _Downloading_SetItemText(pos, dn);
}

static void OnSeedingDeleted(CHAR *pId)
{
    struct idx_downloading *dn, dnf = { 0 };
    int i;

    i = _Downloading_FindItem(pId, g_downloadingCnt+g_waitingCnt+g_uploadingCnt,
        g_downloadingCnt+g_waitingCnt+g_uploadingCnt+g_seedingCnt);
    if (i >= 0) ListView_DeleteItem(g_hWndDownloading, i);

    strcpy_s(dnf.id, MAX_ID_LEN, pId);
    dn = ptrArray_findSorted(&g_seeding, &dnf);
    dn = ptrArray_removeSorted(&g_seeding, &dnf);
    if (dn)
    {
        free(dn);
        g_seedingCnt --;
    }
}

static void OnSeedingTimeOut(CHAR *pId)
{
    TCHAR tszText[256];

    _stprintf_s(tszText, 256, "%s 供种时间已到" ,pId);
    Log_AddLine(tszText);
}

// ---------------------------------------------------------------------------------------
void NetIdx_CreateWindow(HWND hwndParent)
{
    g_hWndNet = CreateWindowEx(0, WC_LISTVIEW, _T(""), WS_CHILD|LVS_REPORT|LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwndParent, (HMENU)ID_NET_IDX, g_hInstance, NULL);
    ListView_SetExtendedListViewStyle(g_hWndNet, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);

    SetListViewColumn(g_hWndNet, 0, _T("ID"), 62, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 1, _T("名称"), 150, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 2, _T("大小"), 70, LVCFMT_RIGHT);
    SetListViewColumn(g_hWndNet, 3, _T("最新更新时间"), 130, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 4, _T("本地路径"), 180, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 5, _T("本地更新时间"), 130, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 6, _T("模式"), 50, LVCFMT_LEFT);
    SetListViewColumn(g_hWndNet, 7, _T("状态"), 98, LVCFMT_LEFT);

    InitListViewImageLists(g_hWndNet);
}

static void _NetIdx_SetItemText(int iItem, struct idx_net *idxn)
{
    struct idx_local *idxl, idxlf = { 0 };
    TCHAR tszText[512];

    strcpy_s(idxlf.id, MAX_ID_LEN, idxn->id);
    idxl = ptrArray_findSorted(&g_localIdx, &idxlf);

    ListView_SetItemText(g_hWndNet, iItem, 1, UnicodeToTSTR(idxn->name, tszText, 512));
    ListView_SetItemText(g_hWndNet, iItem, 2, GetSizeString(idxn->size, tszText));
    ListView_SetItemText(g_hWndNet, iItem, 3, GetTimeString(idxn->lastUpdateTime, tszText));

    if (idxl) UnicodeToTSTR(idxl->dir, tszText, 512); else tszText[0] = 0;
    ListView_SetItemText(g_hWndNet, iItem, 4, tszText);

    if (idxl) GetTimeString(idxl->completeTime, tszText); else tszText[0] = 0;
    ListView_SetItemText(g_hWndNet, iItem, 5, tszText);

    ListView_SetItemText(g_hWndNet, iItem, 6,
        ptrArray_findSorted(&g_autoUpdate, idxn->id)?_T("自动"):_T(""));

    if (idxl && strcmp(idxl->hash, idxn->hash)) strcpy_s(tszText, 512, _T("需更新"));
    else tszText[0] = 0;
    ListView_SetItemText(g_hWndNet, iItem, 7, tszText);
}

static void _NetIdx_RefreshItemText(const CHAR *id)
{
    struct idx_net *idxn, idxnf = { 0 };
    TCHAR tszId[64];
    CHAR szId[64];
    int i;

    for (i=0; i<ListView_GetItemCount(g_hWndNet); i++)
    {
        ListView_GetItemText(g_hWndNet, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (strcmp(id, szId) == 0)
        {
            strcpy_s(idxnf.id, MAX_ID_LEN, id);
            idxn = ptrArray_findSorted(&g_netIdx, &idxnf);
            if (idxn) _NetIdx_SetItemText(i, idxn);
            break;
        }
    }
}

void NetIdx_FillListView()
{
    struct idx_net *idxn;
    LVITEM lvI = { 0 };
    TCHAR tszText[512];
    int i, j, itemCount;

    itemCount = ListView_GetItemCount(g_hWndNet);

    lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_STATE;
    for (i=j=0; i<ptrArray_size(&g_netIdx); i++)
    {
        idxn = ptrArray_nth(&g_netIdx, i);
        if (!g_currSubPage || !wcscmp(idxn->category, g_currCategory) ||
            (!idxn->category[0] && !wcscmp(g_currCategory, L"~未分类")))
        {
            lvI.iItem = j;
            lvI.iImage = ICON_NOTHING;
            lvI.pszText = MbcsToTSTR(idxn->id, tszText, 512);
            if (j < itemCount) ListView_SetItem(g_hWndNet, &lvI);
            else ListView_InsertItem(g_hWndNet, &lvI);

            _NetIdx_SetItemText(j++, idxn);
        }
    }

    for (i=itemCount; i>j; i--)
        ListView_DeleteItem(g_hWndNet, i-1);
}

static void _NetIdx_GetCategories()
{
    struct ptrArray oldCategories = { 0 };
    struct idx_net *pIdxn;
    WCHAR cate[64];
    int i, changed;

    oldCategories = g_categories;
    ptrArray_init(&g_categories, category_cmp);

    for (i=0; i<ptrArray_size(&g_netIdx); i++)
    {
        pIdxn = ptrArray_nth(&g_netIdx, i);
        wcscpy_s(cate, 64, pIdxn->category);

        //if (cate[0] == L'~') continue;
        if (!cate[0]) wcscpy_s(cate, 64, L"~未分类");
        if (!ptrArray_findSorted(&g_categories, cate))
            ptrArray_insertSorted(&g_categories, _wcsdup(cate));
    }

    if (ptrArray_size(&oldCategories) != ptrArray_size(&g_categories))
        changed = 1;
    else for (changed=0, i=0; i<ptrArray_size(&oldCategories); i++)
    {
        if (!ptrArray_findSorted(&g_categories, ptrArray_nth(&oldCategories, i)))
        { changed = 1; break; }
    }
    ptrArray_free(&oldCategories, free);
    if (!changed) return;

    Tree_RemoveCategoryItems();
    Tree_AddCategoryItems();
}

static void OnGeneralInfo(CHAR *pId)
{
    TCHAR szTmp[512], szAddr[256];

    if (0==strcmp(pId, "server not connected"))
    {
        _stprintf_s(szTmp, 512, _T("系统尚未连接到服务器(%s)"),
            MbcsToTSTR(g_options.svrAddr, szAddr, 256));
        Log_AddLine(szTmp);
    }
    else if (0==strcmp(pId, "server connected"))
    {
        _stprintf_s(szTmp, 512, _T("系统已成功连接到服务器(%s)"),
            MbcsToTSTR(g_options.svrAddr, szAddr, 256));
        Log_AddLine(szTmp);
    }
    else if (0==strcmp(pId, "another admin"))
        PostMessage(g_hWndMain, WM_COMMAND, IDM_ANOTHER_INST, 0);
}

static void OnNetIdxAdded(CHAR *id)
{
    int i, pos, itemCnt;
    TCHAR tszId[64];
    CHAR szId[64];
    LVITEM lvI = { 0 };
    struct idx_net *idxn, *idxnOld;

    idxn = malloc(sizeof(struct idx_net)); if (!idxn) return;
    memset(idxn, 0, sizeof(struct idx_net));
    if (!IdxNetFromUtf8String(idxn, id)) { free(idxn); return; }

    idxnOld = ptrArray_findSorted(&g_netIdx, idxn);
    if (idxnOld)
    {
        *idxnOld = *idxn;
        free(idxn);

        _NetIdx_GetCategories();

        if (!g_currSubPage || !wcscmp(idxnOld->category, g_currCategory) ||
            (!idxnOld->category[0] && !wcscmp(g_currCategory, L"~未分类")))
        {
            itemCnt = ListView_GetItemCount(g_hWndNet);
            for (i=0; i<itemCnt; i++)
            {
                ListView_GetItemText(g_hWndNet, i, 0, tszId, 64);
                TSTRToMbcs(tszId, szId, 64);
                if (strcmp(idxnOld->id, szId) == 0)
                {
                    _NetIdx_SetItemText(i, idxnOld);
                    break;
                }
            }
        }
        return;
    }

    ptrArray_insertSorted(&g_netIdx, idxn);

    _NetIdx_GetCategories();

    if (!g_currSubPage || !wcscmp(idxn->category, g_currCategory) ||
        (!idxn->category[0] && !wcscmp(g_currCategory, L"~未分类")))
    {
        itemCnt = ListView_GetItemCount(g_hWndNet);
        for (i=0,pos=-1; i<itemCnt; i++)
        {
            ListView_GetItemText(g_hWndNet, i, 0, tszId, 64);
            TSTRToMbcs(tszId, szId, 64);
            if (strcmp(idxn->id, szId) < 0) { pos = i; break; }
        }
        if (pos < 0) pos = itemCnt;

        lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_STATE;
        lvI.iItem = pos;
        lvI.iImage = ICON_NOTHING;
        lvI.pszText = MbcsToTSTR(idxn->id, tszId, 64);
        ListView_InsertItem(g_hWndNet, &lvI);

        _NetIdx_SetItemText(pos, idxn);
    }
}

static void OnNetIdxDeleted(CHAR *id)
{
    struct idx_net *idxn, idxnf = { 0 };
    TCHAR tszId[64];
    TCHAR szId[64];
    int i;

    strcpy_s(idxnf.id, MAX_ID_LEN, id);
    idxn = (struct idx_net *)ptrArray_removeSorted(&g_netIdx, &idxnf);
    if (idxn)
    {
        free(idxn);
        _NetIdx_GetCategories();
    }

    for (i=0; i<ListView_GetItemCount(g_hWndNet); i++)
    {
        ListView_GetItemText(g_hWndNet, i, 0, tszId, 64);
        TSTRToMbcs(tszId, szId, 64);
        if (strcmp(id, szId) == 0)
        { ListView_DeleteItem(g_hWndNet, i); break; }
    }
}

static void _NetIdx_GetSelectedItems(struct ptrList **selected)
{
    TCHAR tszId[MAX_ID_LEN];
    CHAR szId[MAX_ID_LEN];
    int iSel;

    iSel = ListView_GetNextItem(g_hWndNet, -1, LVNI_SELECTED);
    while (iSel >= 0)
    {
        ListView_GetItemText(g_hWndNet, iSel, 0, tszId, MAX_ID_LEN);
        TSTRToMbcs(tszId, szId, MAX_ID_LEN);
        ptrList_append(selected, _strdup(szId));

        iSel = ListView_GetNextItem(g_hWndNet, iSel, LVNI_SELECTED);
    }
}

extern BOOL LocalIdxFromString(struct idx_local *idxl, CHAR *pId);

static void OnLocalIdxAdded(CHAR *id)
{
    struct idx_local *idxl, *idxlOld;

    idxl = (struct idx_local *)malloc(sizeof(struct idx_local)); if (!idxl) return;
    memset(idxl, 0, sizeof(struct idx_local));
    if (!IdxLocalFromUtf8String(idxl, id)) { free(idxl); return; }

    idxlOld = (struct idx_local *)ptrArray_findSorted(&g_localIdx, idxl);
    if (!idxlOld) ptrArray_insertSorted(&g_localIdx, idxl);
    else { *idxlOld = *idxl; free(idxl); }
    _NetIdx_RefreshItemText(idxl->id);
}

static void OnLocalIdxDeleted(CHAR *id)
{
    struct idx_local *idxl, idxlf = { 0 };

    strcpy_s(idxlf.id, MAX_ID_LEN, id);
    idxl = (struct idx_local *)ptrArray_removeSorted(&g_localIdx, &idxlf);
    if (idxl) free(idxl);

    _NetIdx_RefreshItemText(id);
}

static void OnAutoUpdateAdded(CHAR *id)
{
    if (ptrArray_findSorted(&g_autoUpdate, id)) return;
    ptrArray_insertSorted(&g_autoUpdate, _strdup(id));

    _NetIdx_RefreshItemText(id);
}

static void OnAutoUpdateDeleted(CHAR *id)
{
    CHAR *pId;

    pId = (CHAR *)ptrArray_removeSorted(&g_autoUpdate, id);
    if (!pId) return; free(pId);

    _NetIdx_RefreshItemText(id);
}

static void ChangeSubPage(HWND hWndParent, int subPage)
{
    if (g_currSubPage == subPage) return;

    g_currSubPage = subPage;

    if (!g_currSubPage) g_currCategory[0] = 0;
    else wcscpy_s(g_currCategory, MAX_CATEGORY_LEN,
        (WCHAR *)ptrArray_nth(&g_categories, g_currSubPage-1));

    NetIdx_FillListView();
}

static void ChangePage(HWND hWndParent, int newPage)
{
    HMENU hMenu;

    if (g_currPage == newPage)
        return;

    hMenu = GetMenu(hWndParent);

    switch(g_currPage)
    {
    case PAGE_DOWNLOADING: ShowWindow(g_hWndDownloading, SW_HIDE); break;
    case PAGE_NET_IDX: ShowWindow(g_hWndNet, SW_HIDE); break;
    }

    g_currPage = newPage;

    switch(g_currPage)
    {
    case PAGE_DOWNLOADING: ShowWindow(g_hWndDownloading, SW_SHOW); break;
    case PAGE_NET_IDX: ShowWindow(g_hWndNet, SW_SHOW); break;
    }
}

static void _Downloading_SuspendTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_DOWNLOADING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_SuspendTasks(s, ids);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Downloading_ResumeTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_DOWNLOADING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_ResumeTasks(s, ids);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Downloading_RemoveTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_DOWNLOADING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_RemoveDownloadingTasks(s, ids);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Downloading_UploadIdx()
{
    struct idx_local *idxl, idxlf = { 0 };

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_SEEDING) return;

    strcpy_s(idxlf.id, MAX_ID_LEN, g_currIdx.idSel);
    idxl = ptrArray_findSorted(&g_localIdx, &idxlf);
    if (!idxl) return;

    DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_UPLOAD), g_hWndMain, UploadDlgProc, (LPARAM)idxl);
}

static void _Waiting_RemoveTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_WAITING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_RemoveWaitingTasks(s, ids);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Uploading_RemoveTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_UPLOADING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_RemoveUploadingTasks(s, ids);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Seeding_RemoveTask()
{
    struct ptrList *ids = NULL;
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_SEEDING) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    CmdSocket_RemoveSeedingTasks(s, ids, FALSE);
    CmdSocket_Close(s);

    ptrList_free(&ids, free);
}

static void _Downloading_MoveItem(int updown)
{
    struct ptrList *ids = NULL;
    SOCKET s;
    LVITEM lvI = { 0 };
    TCHAR tszId[64];
    int iNew;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_DOWNLOADING) return;

    if (updown < 0 && g_currIdx.iSel == 0) return;
    if (updown > 0 && g_currIdx.iSel == g_downloadingCnt-1) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    if (CmdSocket_SetDownloadingPriority(s, ids, updown))
    {
        if (updown <= -10000) iNew = 0;
        else if (updown >= 10000) iNew = g_downloadingCnt-1;
        else iNew = g_currIdx.iSel + updown;

        ListView_DeleteItem(g_hWndDownloading, g_currIdx.iSel);

        lvI.mask = LVIF_TEXT|LVIF_PARAM|LVIF_IMAGE|LVIF_STATE;
        lvI.iItem = iNew;
        lvI.lParam = LIST_DOWNLOADING;
        lvI.iImage = GetActionImage(g_currIdx.dn);
        lvI.state = LVIS_SELECTED|LVIS_FOCUSED;
        lvI.stateMask = (UINT)-1;
        lvI.pszText = MbcsToTSTR(g_currIdx.idSel, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(iNew, g_currIdx.dn);
    }

    CmdSocket_Close(s);
    ptrList_free(&ids, free);
}

static void _Waiting_MoveItem(int updown)
{
    struct ptrList *ids = NULL;
    TCHAR tszId[64];
    int iNew;
    LVITEM lvI = { 0 };
    SOCKET s;

    _Downloading_GetCurrSel();
    if (g_currIdx.iSel < 0 || g_currIdx.iList != LIST_WAITING) return;

    if (updown < 0 && g_currIdx.iSel == g_downloadingCnt) return;
    if (updown > 0 && g_currIdx.iSel == g_downloadingCnt+g_waitingCnt-1) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
    {
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return;
    }

    ptrList_append(&ids, _strdup(g_currIdx.idSel));

    if (CmdSocket_SetWaitingPriority(s, ids, updown))
    {
        if (updown <= -10000) iNew = g_downloadingCnt;
        else if (updown >= 10000) iNew = g_downloadingCnt+g_waitingCnt-1;
        else iNew = g_currIdx.iSel + updown;

        ListView_DeleteItem(g_hWndDownloading, g_currIdx.iSel);

        lvI.mask = LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM|LVIF_STATE;
        lvI.iItem = iNew;
        lvI.lParam = LIST_WAITING;
        lvI.iImage = GetActionImage(g_currIdx.dn);
        lvI.state = LVIS_SELECTED|LVIS_FOCUSED;
        lvI.stateMask = (UINT)-1;
        lvI.pszText = MbcsToTSTR(g_currIdx.idSel, tszId, 64);
        ListView_InsertItem(g_hWndDownloading, &lvI);

        _Downloading_SetItemText(iNew, g_currIdx.dn);
    }

    CmdSocket_Close(s);
    ptrList_free(&ids, free);
}

static void _NetIdx_AddTasks()
{
    struct ptrList *selected = NULL;
    SOCKET s;

    _NetIdx_GetSelectedItems(&selected);
    if (!ptrList_size(selected)) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    else
    {
        CmdSocket_AddTasks(s, selected);
        CmdSocket_Close(s);
    }

    ptrList_free(&selected, free);
}

static void _NetIdx_AddTask1(const CHAR *id)
{
    struct ptrList *selected = NULL;
    SOCKET s;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    else
    {
        ptrList_append(&selected, _strdup(id));
        CmdSocket_AddTasks(s, selected);
        CmdSocket_Close(s);
    }

    ptrList_free(&selected, free);
}

static void _NetIdx_OpenFolder()
{
    struct ptrList *selected = NULL;
    struct idx_local *idxl, idxlf = { 0 };

    _NetIdx_GetSelectedItems(&selected);
    if (1 != ptrList_size(selected)) { ptrList_free(&selected, free); return; }
    strcpy_s(idxlf.id, MAX_ID_LEN, (CHAR *)selected->data);
    idxl = (struct idx_local *)ptrArray_findSorted(&g_localIdx, &idxlf);
    if (idxl)
    {
        PIDLIST_ABSOLUTE pidl;
        SHParseDisplayName(idxl->dir, NULL, &pidl, 0, NULL);
        SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
    }
    //if (idxl) ShellExecuteW(g_hWndMain, L"open", idxl->dir, NULL, NULL, SW_SHOWNORMAL);
    ptrList_free(&selected, free);
}

static void _NetIdx_CheckTasks()
{
    struct ptrList *selected = NULL;
    SOCKET s;

    _NetIdx_GetSelectedItems(&selected);
    if (ptrList_size(selected))
    {
        s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
        if (s != INVALID_SOCKET)
        {
            CmdSocket_CheckTasks(s, selected);
            CmdSocket_Close(s);
        }
        else
            MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    }
    ptrList_free(&selected, free);
}

static void _NetIdx_SetAutoupdate(BOOL autoUpdate)
{
    struct ptrList *selected = NULL;
    SOCKET s;

    _NetIdx_GetSelectedItems(&selected);
    if (!ptrList_size(selected)) return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    else
    {
        CmdSocket_SetAutoUpdateTasks(s, selected, autoUpdate);
        CmdSocket_Close(s);
    }

    ptrList_free(&selected, free);
}

static void _NetIdx_RemoveLocal()
{
    struct ptrList *selected = NULL;
    SOCKET s;
    TCHAR szTmp[512];

    _NetIdx_GetSelectedItems(&selected);
    if (!ptrList_size(selected)) return;

    _stprintf_s(szTmp, 512, _T("确定要删除所选的 %d 项记录?\r\n(目录和文件都将被删除!)"), ptrList_size(selected));
    if (IDYES!=MessageBox(g_hWndMain, szTmp, g_szAppName, MB_YESNO|MB_ICONQUESTION))
    { ptrList_free(&selected, free); return; }

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    else
    {
        CmdSocket_RemoveLocalIdx(s, selected);
        CmdSocket_Close(s);
    }

    ptrList_free(&selected, free);
}

static void _NetIdx_GetIdxStat(const CHAR *id)
{
    struct ptrList *li;
    struct idx_downloading dnf = { 0 };

    strcpy_s(g_currIdx.idSel, MAX_ID_LEN, id);
    g_currIdx.iList = -1;

    for (li=g_downloading; li; li=li->next)
    {
        g_currIdx.dn = (struct idx_downloading *)li->data;
        if (0==strcmp(g_currIdx.dn->id, id))
        { g_currIdx.iList = LIST_DOWNLOADING; break; }
    }
    if (g_currIdx.iList < 0) for (li=g_waiting; li; li=li->next)
    {
        g_currIdx.dn = (struct idx_downloading *)li->data;
        if (0==strcmp(g_currIdx.dn->id, id))
        { g_currIdx.iList = LIST_WAITING; break; }
    }
    if (g_currIdx.iList < 0) for (li=g_uploading; li; li=li->next)
    {
        g_currIdx.dn = (struct idx_downloading *)li->data;
        if (0==strcmp(g_currIdx.dn->id, id))
        { g_currIdx.iList = LIST_UPLOADING; break; }
    }
    if (g_currIdx.iList < 0)
    {
        strcpy_s(dnf.id, MAX_ID_LEN, id);
        g_currIdx.dn = (struct idx_downloading *)ptrArray_findSorted(&g_seeding, &dnf);
        if (g_currIdx.dn) g_currIdx.iList = LIST_SEEDING;
    }
}

static void NetIdx_OnNotify(NMHDR *lpNmhdr)
{
    struct ptrList *selected;
    struct idx_local idxlf = { 0 };
    NMITEMACTIVATE *lpnmitem;
    HMENU hMenu, hMenuTrackPopup;
    POINT pt;
    TCHAR tszTmp[256], tszId[64];

    switch (lpNmhdr->code)
    {
    case NM_RCLICK:
        lpnmitem = (LPNMITEMACTIVATE)lpNmhdr;
        hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_POPUP_MENU));
        if (hMenu)
        {
            hMenuTrackPopup = GetSubMenu(hMenu, 4);
            pt = lpnmitem->ptAction;
            ClientToScreen(g_hWndNet, (LPPOINT)&pt);

            selected = NULL;
            _NetIdx_GetSelectedItems(&selected);
            if (!ptrList_size(selected))
            {
                EnableMenuItem(hMenuTrackPopup, IDM_ADD_TASK, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_AUTO_UPDATE, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_AUTO_UPDATE0, MF_BYCOMMAND|MF_GRAYED);
            }
            if (1!=ptrList_size(selected))
            {
                EnableMenuItem(hMenuTrackPopup, IDM_OPEN_FOLDER, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_CHECK, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, 7, MF_BYPOSITION|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_REMOVE_LOCAL, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_REMOVE_NET, MF_BYCOMMAND|MF_GRAYED);
                EnableMenuItem(hMenuTrackPopup, IDM_UPLOAD, MF_BYCOMMAND|MF_GRAYED);
            }
            else
            {
                strcpy_s(idxlf.id, MAX_ID_LEN, (CHAR *)selected->data);
                if (!ptrArray_findSorted(&g_localIdx, &idxlf))
                {
                    EnableMenuItem(hMenuTrackPopup, IDM_OPEN_FOLDER, MF_BYCOMMAND|MF_GRAYED);
                    EnableMenuItem(hMenuTrackPopup, IDM_CHECK, MF_BYCOMMAND|MF_GRAYED);
                }
                _stprintf_s(tszTmp, 256, _T("上传 %s"), MbcsToTSTR((CHAR *)selected->data, tszId, 64));
                ModifyMenu(hMenuTrackPopup, IDM_UPLOAD, MF_BYCOMMAND, IDM_UPLOAD, tszTmp);
            }
            TrackPopupMenu(hMenuTrackPopup, TPM_LEFTALIGN|TPM_LEFTBUTTON,
                pt.x, pt.y, 0, g_hWndMain, NULL);
            DestroyMenu(hMenu);
            ptrList_free(&selected, free);
        }
        break;

    case NM_DBLCLK:
        lpnmitem = (LPNMITEMACTIVATE)lpNmhdr;
        selected = NULL;
        _NetIdx_GetSelectedItems(&selected);
        if (ptrList_size(selected) == 1)
        {
            _NetIdx_GetIdxStat((CHAR *)selected->data);
            switch (g_currIdx.iList)
            {
            case LIST_WAITING:
            case LIST_UPLOADING:
            case -1:
                DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_IDX_INFO), g_hWndMain, IdxInfoDlgProc);
                break;
            case LIST_DOWNLOADING:
            case LIST_SEEDING:
                DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_PEER_INFO), g_hWndMain, PeerInfoDlgProc);
                break;
            }

        }
        ptrList_free(&selected, free);
        break;
    }
}

static CHAR g_pwd[32] = { 0 };
INT_PTR CALLBACK DelNetIdxDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR tszTmp[256], tszId[64];

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        _stprintf_s(tszTmp, 256, _T("确认删除服务器记录: %s"), MbcsToTSTR((CHAR *)lParam, tszId, 64));
        SetWindowText(hDlg, tszTmp);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            GetDlgItemTextA(hDlg, IDC_PWD, g_pwd, 32);
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void _NetIdx_RemoveNet()
{
    struct ptrList *selected = NULL;
    SOCKET s;

    _NetIdx_GetSelectedItems(&selected);
    if (1!=ptrList_size(selected)) return;

    g_pwd[0] = 0;
    if (IDOK!=DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_DEL_NETIDX), g_hWndMain, DelNetIdxDlgProc, (LPARAM)selected->data))
        return;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET)
        MessageBox(g_hWndMain, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    else
    {
        CmdSocket_DeleteResource(s, selected->data, g_pwd);
        CmdSocket_Close(s);
    }

    ptrList_free(&selected, free);
}

static void _NetIdx_UploadIdx()
{
    struct ptrList *selected = NULL;
    struct idx_local *idxl, idxlf = { 0 };

    _NetIdx_GetSelectedItems(&selected);
    if (1 != ptrList_size(selected)) return;

    strcpy_s(idxlf.id, MAX_ID_LEN, selected->data);
    idxl = ptrArray_findSorted(&g_localIdx, &idxlf);
    if (!idxl) return;

    DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_UPLOAD), g_hWndMain, UploadDlgProc, (LPARAM)idxl);

    ptrList_free(&selected, free);
}

static BOOL GetOptions()
{
    SOCKET s;
    BOOL success;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET) return FALSE;

    success = CmdSocket_GetOptions(s, &g_options);
    CmdSocket_Close(s);

    return success;
}

static void SendStopCoreService()
{
    SOCKET s;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s == INVALID_SOCKET) return;

    CmdSocket_StopService(s);
    CmdSocket_Close(s);
}

static void NetIdx_OnSearch()
{
}

static WNDPROC g_oldEditProc;
static int g_searchEditSelected = 0;

LRESULT CALLBACK SearchEditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR szText[512];

    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (!g_searchEditSelected)
        {
            SetWindowText(hWnd, _T(""));
            g_searchEditSelected = 1;
        }
        break;
    case WM_KILLFOCUS:
        if (!GetWindowText(hWnd, szText, 512))
        {
            SetWindowText(hWnd, _T("搜索..."));
            g_searchEditSelected = 0;
        }
        break;
    default:
        break;
    }
    return CallWindowProc(g_oldEditProc, hWnd, message, wParam, lParam);
}

static void ToolBar_Create(HWND hWnd)
{
    #define NUM_BUTTONS 10
    HIMAGELIST hImageList;
    HICON hiconItem;
    TBBUTTON tbButtons[NUM_BUTTONS] =
    {
        { 6, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { MAKELONG(0, 0), IDM_UPLOAD_NEW, TBSTATE_ENABLED, 
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("上传资源") },
        { MAKELONG(1, 0), IDM_UPLOAD_BATCH, TBSTATE_ENABLED, 
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("批量上传") },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(2, 0), IDM_WORKING, TBSTATE_ENABLED,
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("正在下载") },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(3, 0), IDM_OPTIONS, TBSTATE_ENABLED, 
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("系统选项") },
        { MAKELONG(4, 0), IDM_ABOUT, TBSTATE_ENABLED, 
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("帮助信息") },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { MAKELONG(5, 0), IDM_EXIT, TBSTATE_ENABLED, 
            BTNS_AUTOSIZE, {0}, 0, (INT_PTR)_T("退出") }
    };
    RECT rc;

    g_hWndToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, 
        WS_CHILD|WS_VISIBLE,
        0, 0, 0, 0,
        hWnd, NULL, g_hInstance, NULL);
    if (g_hWndToolBar == NULL) return;

    SendMessage(g_hWndToolBar, TB_SETBITMAPSIZE, 0, MAKELONG(48, 48));
    SendMessage(g_hWndToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    hImageList = ImageList_Create(48, 48, ILC_COLOR32|ILC_MASK, 5, 0);

    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_UPLOAD_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);
    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_UPLOAD2_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);
    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_WORKING_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);
    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_OPTIONS_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);
    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_HELP_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);
    hiconItem = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_EXIT_));
    ImageList_AddIcon(hImageList, hiconItem); DestroyIcon(hiconItem);

    SendMessage(g_hWndToolBar, TB_SETIMAGELIST, 0, (LPARAM)hImageList);

    SendMessage(g_hWndToolBar, TB_ADDBUTTONS, (WPARAM)NUM_BUTTONS, (LPARAM)&tbButtons);

    SendMessage(g_hWndToolBar, TB_AUTOSIZE, 0, 0); 
    ShowWindow(g_hWndToolBar, TRUE);

    GetWindowRect(g_hWndToolBar, &rc);
    g_toolHeight = rc.bottom - rc.top + 1;

    SendMessage(g_hWndToolBar, TB_GETRECT, IDM_EXIT, (LPARAM)&rc);
    g_hWndSearch = CreateWindowEx(0, _T("Edit"), _T("搜索..."),
        WS_CHILD|WS_BORDER|WS_VISIBLE|ES_LEFT,
        rc.right+20, (rc.top+rc.bottom-22)/2, 140, 22,
        hWnd, (HMENU)ID_SEARCH, g_hInstance, 0);
    SetParent(g_hWndSearch, g_hWndToolBar);
    SendMessage(g_hWndSearch, WM_SETFONT, (WPARAM)g_defFont, 0);
    g_oldEditProc = (WNDPROC)SetWindowLongPtr(g_hWndSearch, GWL_WNDPROC, (LONG_PTR)SearchEditProc);

}

static void StatusBar_Create(HWND hWnd)
{
    RECT rc;
    LPINT lpParts;
    int i, nWidth;

    g_hWndStatusBar = CreateWindowEx(0, STATUSCLASSNAME, _T(""),
        SBARS_SIZEGRIP|WS_CHILD|WS_VISIBLE,
        0, 0, 0, 0, hWnd,
        (HMENU)ID_STATUSBAR, g_hInstance, NULL);

    GetClientRect(hWnd, &rc);

    lpParts = (LPINT)calloc(2, sizeof(int));

    nWidth = rc.right / 2;
    for (i = 0; i < 2; i++)
    {
        lpParts[i] = nWidth;
        nWidth += nWidth;
    }
    SendMessage(g_hWndStatusBar, SB_SETPARTS, (WPARAM)2, (LPARAM)lpParts);
    free(lpParts);

    GetWindowRect(g_hWndStatusBar, &rc);
    g_statusHeight = rc.bottom - rc.top;
}

// ---------------------------------------------------------------------------------------
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    MINMAXINFO *mmi;
    HDWP hdwp;
    time_t currTime;
    int cx, cy;

    switch (message)
    {
    case WM_CREATE:
        g_hWndTmpl = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_TMPL), hWnd, TmplDlgProc);
        if (!g_hWndTmpl) return -1;

        ToolBar_Create(hWnd);
        StatusBar_Create(hWnd);

        Tree_CreateWindow(hWnd);
        Log_CreateWindow(hWnd);

        Downloading_CreateWindow(hWnd);
        NetIdx_CreateWindow(hWnd);

        // 初始页面
        ChangePage(hWnd, PAGE_DOWNLOADING);
        // 定时器
        SetTimer(hWnd, 8, 1000, NULL);
        PostMessage(hWnd, WM_TIMER, 0, 0);
        break;

    case WM_TIMER:
        time(&currTime);
        break;

    case WM_SIZE:
        cx = LOWORD(lParam);
        cy = HIWORD(lParam);
        hdwp = BeginDeferWindowPos(6);
        DeferWindowPos(hdwp, g_hWndToolBar, NULL, 0, 0, 0, 0, 0);
        DeferWindowPos(hdwp, g_hWndStatusBar, NULL, 0, 0, 0, 0, 0);
        DeferWindowPos(hdwp, g_hWndTree, NULL,
            0, g_toolHeight, g_treeWidth-3, cy-g_toolHeight-g_statusHeight,
            SWP_NOZORDER);
        DeferWindowPos(hdwp, g_hWndLog, NULL,
            g_treeWidth, cy-g_statusHeight-g_logHeight+3,
            cx-g_treeWidth, g_logHeight-3,
            SWP_NOZORDER);
        DeferWindowPos(hdwp, g_hWndDownloading, NULL,
            g_treeWidth, g_toolHeight, cx-g_treeWidth, cy-g_toolHeight-g_logHeight-g_statusHeight,
            SWP_NOZORDER);
        DeferWindowPos(hdwp, g_hWndNet, NULL,
            g_treeWidth, g_toolHeight, cx-g_treeWidth, cy-g_toolHeight-g_logHeight-g_statusHeight,
            SWP_NOZORDER);
        EndDeferWindowPos(hdwp);
        break;

    case WM_GETMINMAXINFO:
        mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = INIT_WND_WIDTH;
        mmi->ptMinTrackSize.y = INIT_WND_HEIGHT;
        break;

    case WM_ACTIVATEAPP:
        if (!wParam) g_hWndFocus = GetFocus();
        break;

    case WM_SETFOCUS:
        if (IsWindow(g_hWndFocus) && IsChild(hWnd, g_hWndFocus))
            SetFocus(g_hWndFocus);
        break;

    case WM_NOTIFY:
        switch (wParam)
        {
        case ID_TREE: Tree_OnNotify((NMHDR *)lParam); break;
        case ID_DOWNLOADING: Downloading_OnNotify((NMHDR *)lParam); break;
        case ID_NET_IDX: NetIdx_OnNotify((NMHDR *)lParam); break;
        }
        break;

    case WM_COMMAND:
        wmId = LOWORD(wParam);
        wmEvent = HIWORD(wParam);

        switch (wmId)
        {
        case IDM_WORKING:
            ChangePage(hWnd, PAGE_DOWNLOADING);
            TreeView_SelectItem(g_hWndTree, NULL);
            break;

        case ID_SEARCH:
            if (wmEvent == EN_CHANGE)
                NetIdx_OnSearch();
            break;

        case IDM_CHANGE_PAGE:
            ChangePage(hWnd, lParam);
            break;
        case IDM_CHANGE_SUB_PAGE:
            ChangeSubPage(hWnd, lParam);
            break;

        case IDM_MOVE_UP:
            _Downloading_MoveItem(-1);
            break;
        case IDM_MOVE_DOWN:
            _Downloading_MoveItem(1);
            break;
        case IDM_MOVE_TOP:
            _Downloading_MoveItem(-10000);
            break;
        case IDM_MOVE_BOTTOM:
            _Downloading_MoveItem(10000);
            break;
        case IDM_PAUSE:
            _Downloading_SuspendTask();
            break;
        case IDM_RESUME:
            _Downloading_ResumeTask();
            break;
        case IDM_REMOVE_DOWNLOADING:
            _Downloading_RemoveTask();
            break;

        case IDM_TASK_INFO:
            _Downloading_TaskInfo();
            break;

        case IDM_MOVE_UP_WAITING:
            _Waiting_MoveItem(-1);
            break;
        case IDM_MOVE_DOWN_WAITING:
            _Waiting_MoveItem(1);
            break;
        case IDM_MOVE_TOP_WAITING:
            _Waiting_MoveItem(-10000);
            break;
        case IDM_MOVE_BOTTOM_WAITING:
            _Waiting_MoveItem(10000);
            break;
        case IDM_REMOVE_WAITING:
            _Waiting_RemoveTask();
            break;

        case IDM_REMOVE_UPLOADING:
            _Uploading_RemoveTask();
            break;
        case IDM_REMOVE_SEEDING:
            _Seeding_RemoveTask();
            break;

        case IDM_ADD_TASK:
            _NetIdx_AddTasks();
            break;
        case IDM_AUTO_UPDATE:
            _NetIdx_SetAutoupdate(TRUE);
            break;
        case IDM_AUTO_UPDATE0:
            _NetIdx_SetAutoupdate(FALSE);
            break;
        case IDM_OPEN_FOLDER:
            if (g_currPage == PAGE_DOWNLOADING)
                _Downloading_OpenFolder();
            else if (g_currPage == PAGE_NET_IDX)
                _NetIdx_OpenFolder();
            break;
        case IDM_CHECK:
            if (g_currPage == PAGE_DOWNLOADING)
                _Downloading_CheckTasks();
            else if (g_currPage == PAGE_NET_IDX)
                _NetIdx_CheckTasks();
            break;
        case IDM_REMOVE_LOCAL:
            _NetIdx_RemoveLocal();
            break;
        case IDM_REMOVE_NET:
            _NetIdx_RemoveNet();
            break;

        case IDM_UPLOAD:
            if (g_currPage == PAGE_DOWNLOADING)
                _Downloading_UploadIdx();
            else if (g_currPage == PAGE_NET_IDX)
                _NetIdx_UploadIdx();
            break;
        case IDM_UPLOAD_NEW:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_UPLOAD), hWnd, UploadNewDlgProc);
            break;
        case IDM_UPLOAD_BATCH:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_UPLOAD_BATCH), hWnd, UploadBatchDlgProc);
            break;

        case IDM_OPTIONS:
            if (GetOptions())
                DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_OPTIONS), hWnd, OptionsDlgProc);
            else MessageBox(hWnd, _T("无法与服务建立连接!\r\n操作不能继续。"), _T("..."),
                MB_OK|MB_ICONINFORMATION);
            break;
        case IDM_LOCAL_MAN:
            break;
        case IDM_ABOUT:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_DISCONNECT:
            DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_QUIT), hWnd, Quit,
                (LPARAM)_T("与下载服务失去连接，本程序即将退出。\n请检查下载服务是否已正常退出。"));
            DestroyWindow(hWnd);
            break;
        case IDM_ANOTHER_INST:
            DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_QUIT), hWnd, Quit,
                (LPARAM)_T("管理程序从其它位置登录，本程序即将退出。"));
            DestroyWindow(hWnd);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        StopMsgSocketThread();
        KillTimer(hWnd, 8);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK TmplDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK LoginDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT rcScr, rcDlg;
    SOCKET s;
    BOOL loginOK;

    switch (message)
    {
    case WM_INITDIALOG:
        if (!SystemParametersInfo(SPI_GETWORKAREA, sizeof(RECT), &rcScr, 0))
        {
            GetClientRect(GetDesktopWindow(), &rcScr);
            rcScr.bottom -= 50;
        }
        GetWindowRect(hDlg, &rcDlg);
        SetWindowPos(hDlg, NULL, (rcScr.right-rcDlg.right)/2, (rcScr.bottom-rcDlg.bottom)/2, 0, 0,
            SWP_NOZORDER|SWP_NOSIZE);
        SetDlgItemInt(hDlg, IDC_PORT, g_serverPort, FALSE);
        SetDlgItemTextA(hDlg, IDC_IP, g_serverIp);
        if (strcmp(g_serverIp, "127.0.0.1"))
        {
            SendDlgItemMessage(hDlg, IDC_REMOTE, BM_SETCHECK, BST_CHECKED, 0);
            ShowWindow(GetDlgItem(hDlg, IDC_IP), SW_SHOW);
        }
        else
        {
            SendDlgItemMessage(hDlg, IDC_REMOTE, BM_SETCHECK, BST_UNCHECKED, 0);
            ShowWindow(GetDlgItem(hDlg, IDC_IP), SW_HIDE);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_REMOTE:
            if (SendDlgItemMessage(hDlg, IDC_REMOTE, BM_GETCHECK, 0, 0)==BST_CHECKED)
                ShowWindow(GetDlgItem(hDlg, IDC_IP), SW_SHOW);
            else ShowWindow(GetDlgItem(hDlg, IDC_IP), SW_HIDE);
            break;
        case IDOK:
            g_serverPort = (WORD)GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
            if (SendDlgItemMessage(hDlg, IDC_REMOTE, BM_GETCHECK, 0, 0)==BST_CHECKED)
                GetDlgItemTextA(hDlg, IDC_IP, g_serverIp, 32);
            else strcpy_s(g_serverIp, 32, "127.0.0.1");
            SaveOptions();
            loginOK = FALSE;
            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                loginOK = CmdSocket_GetOptions(s, &g_options);
                CmdSocket_Close(s);
            }
            if (!loginOK)
            {
                MessageBox(hDlg, _T("无法与服务建立连接!"), g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

static WCHAR *GetCategoryFromPath(const WCHAR *szPath, WCHAR *szCate)
{
    int i, l, start, end;

    l = wcslen(szPath);
    szCate[0] = 0;
    start = end = -1;

    for (i=l-2; i>1; i--) { if (szPath[i] == L'\\') { end = i; break; } }
    for (i=end-1; i>1; i--) { if (szPath[i] == L'\\') { start = i+1; break; } }
    if (start < 0 || end < 0) return szCate;
    l = end - start; if (l >= MAX_CATEGORY_LEN) return szCate;
    memcpy(szCate, &szPath[start], l*sizeof(WCHAR));
    szCate[l] = 0;
    return szCate;
}

static WCHAR *GetCategoryFromPath1(const WCHAR *szPath, WCHAR *szCate)
{
    int i, l, start, end;

    l = wcslen(szPath);
    szCate[0] = 0;
    start = -1;
    end = l;

    for (i=l-1; i>1; i--) { if (szPath[i] == L'\\') { start = i+1; break; } }
    if (start < 0 || end < 0) return szCate;
    l = end - start; if (l >= MAX_CATEGORY_LEN) return szCate;
    memcpy(szCate, &szPath[start], l*sizeof(WCHAR));
    szCate[l] = 0;
    return szCate;
}

static void Upload_ShowCategoriesMenu(HWND hDlg)
{
    RECT rc;
    HMENU hMenu;
    int i;
    TCHAR tszTmp[256];

    GetWindowRect(GetDlgItem(hDlg, IDC_BROWSE2), &rc);
    hMenu = CreatePopupMenu();
    for (i=0; i<ptrArray_size(&g_categories); i++)
        AppendMenu(hMenu, MF_STRING, i+1, UnicodeToTSTR((WCHAR *)ptrArray_nth(&g_categories, i), tszTmp, 256));
    i = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_LEFTALIGN, rc.left, rc.bottom, 0, hDlg, NULL);
    if (i>0) SetDlgItemTextW(hDlg, IDC_CATEGORY, (WCHAR *)ptrArray_nth(&g_categories, i-1));
    DestroyMenu(hMenu);
}

INT_PTR CALLBACK UploadNewDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    BROWSEINFO bi;
    PIDLIST_ABSOLUTE pidl;
    TCHAR szPath[MAX_PATH];
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN];
    WCHAR cate[MAX_CATEGORY_LEN], dir[MAX_PATH];
    SOCKET s;

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        SetDlgItemInt(hDlg, IDC_ID, g_maxId+1, FALSE);
        SetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE:
            memset(&bi, 0, sizeof(BROWSEINFO));
            bi.hwndOwner = hDlg;
            bi.lpszTitle = "选择游戏目录";
            bi.pszDisplayName = szPath;
            bi.ulFlags = BIF_BROWSEFORCOMPUTER|BIF_DONTGOBELOWDOMAIN|
                BIF_NEWDIALOGSTYLE|BIF_NONEWFOLDERBUTTON|BIF_RETURNONLYFSDIRS;
            pidl = SHBrowseForFolder(&bi);
            if (pidl)
            {
                SHGetPathFromIDList(pidl, szPath);
                SetDlgItemText(hDlg, IDC_DIR, szPath);
                TSTRToUnicode(szPath, dir, MAX_PATH);
                SetDlgItemTextW(hDlg, IDC_CATEGORY, GetCategoryFromPath(dir, cate));
            }
            break;
        case IDC_BROWSE2:
            Upload_ShowCategoriesMenu(hDlg);
            break;
        case IDOK:
            GetDlgItemTextA(hDlg, IDC_ID, id, MAX_ID_LEN);
            GetDlgItemTextW(hDlg, IDC_DIR, dir, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_CATEGORY, cate, MAX_CATEGORY_LEN);
            GetDlgItemTextA(hDlg, IDC_PWD, pwd, MAX_PWD_LEN);
            GetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify, 1024);
            SaveOptions();
            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                if (!CmdSocket_UploadResource(s, id, dir, cate, pwd, g_serversNotify))
                {
                    MessageBox(hDlg, _T("无法建立上传任务，原因是参数不正确或者任务正在运行。"),
                        g_szAppName, MB_OK|MB_ICONINFORMATION);
                    CmdSocket_Close(s);
                    break;
                }
                CmdSocket_Close(s);
            }
            else
            {
                MessageBox(hDlg, _T("无法与服务建立连接!\r\n操作不能继续。"),
                    g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

void FindSubDirs(const WCHAR *path, struct ptrList **subDirs)
{
    WIN32_FIND_DATAW ffd = { 0 };
    HANDLE hFind;
    WCHAR szDir[MAX_PATH], szSearch[MAX_PATH];

    swprintf_s(szSearch, MAX_PATH, L"%s\\*", path);

    hFind = FindFirstFileW(szSearch, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    while (1)
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (wcscmp(ffd.cFileName, L".") && wcscmp(ffd.cFileName, L".."))
            {
                swprintf_s(szDir, MAX_PATH, L"%s\\%s", path, ffd.cFileName);
                ptrList_append(subDirs, _wcsdup(szDir));
            }
        }

        if (!FindNextFileW(hFind, &ffd)) break;
    }
    FindClose(hFind);
}

INT_PTR CALLBACK UploadBatchDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    BROWSEINFO bi;
    PIDLIST_ABSOLUTE pidl;
    TCHAR szPath[MAX_PATH], tszTmp[256];
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN];
    WCHAR cate[MAX_CATEGORY_LEN], dir[MAX_PATH];
    struct ptrList *subDirs, *li;
    LVITEM lvI = { 0 };
    HWND hwndItems;
    RECT rc;
    SOCKET s;
    int i, iId;

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);

        SetDlgItemInt(hDlg, IDC_ID, g_maxId+1, FALSE);
        SetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify);
        hwndItems = GetDlgItem(hDlg, IDC_ITEMS);
        ListView_SetExtendedListViewStyle(hwndItems, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        GetClientRect(hwndItems, &rc);
        SetListViewColumn(hwndItems, 0, _T("序号"), 62, LVCFMT_CENTER);
        SetListViewColumn(hwndItems, 1, _T("名称/子目录"), rc.right-90, LVCFMT_LEFT);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE:
            memset(&bi, 0, sizeof(BROWSEINFO));
            bi.hwndOwner = hDlg;
            bi.lpszTitle = "选择游戏目录";
            bi.pszDisplayName = szPath;
            bi.ulFlags = BIF_BROWSEFORCOMPUTER|BIF_DONTGOBELOWDOMAIN|
                BIF_NEWDIALOGSTYLE|BIF_NONEWFOLDERBUTTON|BIF_RETURNONLYFSDIRS;
            pidl = SHBrowseForFolder(&bi);
            if (pidl)
            {
                SHGetPathFromIDList(pidl, szPath);
                SetDlgItemText(hDlg, IDC_DIR, szPath);
                TSTRToUnicode(szPath, dir, MAX_PATH);
                SetDlgItemTextW(hDlg, IDC_CATEGORY, GetCategoryFromPath1(dir, cate));
                subDirs = NULL;
                FindSubDirs(dir, &subDirs);
                EnableWindow(GetDlgItem(hDlg, IDOK), subDirs?TRUE:FALSE);
                hwndItems = GetDlgItem(hDlg, IDC_ITEMS);
                lvI.mask = LVIF_TEXT;
                for (i=0,li=subDirs; li; i++,li=li->next)
                {
                    lvI.iItem = i;
                    _stprintf_s(tszTmp, 256, _T("%d"), i+1);
                    lvI.pszText = tszTmp;
                    ListView_InsertItem(hwndItems, &lvI);

                    UnicodeToTSTR(li->data, szPath, MAX_PATH);
                    ListView_SetItemText(hwndItems, i, 1, szPath);
                }
                ptrList_free(&subDirs, free);
            }
            break;
        case IDC_BROWSE2:
            Upload_ShowCategoriesMenu(hDlg);
            break;
        case IDOK:
            GetDlgItemTextA(hDlg, IDC_ID, id, MAX_ID_LEN);
            GetDlgItemTextW(hDlg, IDC_CATEGORY, cate, MAX_CATEGORY_LEN);
            GetDlgItemTextA(hDlg, IDC_PWD, pwd, MAX_PWD_LEN);
            GetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify, 1024); SaveOptions();
            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                iId = atoi(id);
                hwndItems = GetDlgItem(hDlg, IDC_ITEMS);
                for (i=0; i<ListView_GetItemCount(hwndItems); i++)
                {
                    ListView_GetItemText(hwndItems, i, 1, szPath, MAX_PATH);
                    TSTRToUnicode(szPath, dir, MAX_PATH);
                    sprintf_s(id, MAX_ID_LEN, "%d", iId+i);
                    if (!CmdSocket_UploadResource(s, id, dir, cate, pwd, g_serversNotify))
                    {
                        _stprintf_s(tszTmp, 256, _T("%d 添加上传任务失败：任务正在运行或ID冲突"), iId+i);
                        Log_AddLine(tszTmp);
                    }
                }
                CmdSocket_Close(s);
            }
            else
            {
                MessageBox(hDlg, _T("无法与服务建立连接!\r\n操作不能继续。"),
                    g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK UploadDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct idx_local *idxl;
    struct idx_net *idxn, idxnf = { 0 };
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN];
    WCHAR cate[MAX_CATEGORY_LEN], dir[MAX_PATH];
    SOCKET s;

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);

        idxl = (struct idx_local *)lParam;
        strcpy_s(idxnf.id, MAX_ID_LEN, idxl->id);
        idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdx, &idxnf);

        SetDlgItemTextA(hDlg, IDC_ID, idxl->id);
        SetDlgItemTextW(hDlg, IDC_DIR, idxl->dir);
        SetDlgItemTextW(hDlg, IDC_CATEGORY, idxn->category);
        SetDlgItemText(hDlg, IDC_PWD, _T(""));
        SetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify);
        SendDlgItemMessage(hDlg, IDC_ID, EM_SETREADONLY, TRUE, 0);
        SendDlgItemMessage(hDlg, IDC_DIR, EM_SETREADONLY, TRUE, 0);
        SendDlgItemMessage(hDlg, IDC_CATEGORY, EM_SETREADONLY, TRUE, 0);
        EnableWindow(GetDlgItem(hDlg, IDC_BROWSE), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BROWSE2), FALSE);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            GetDlgItemTextA(hDlg, IDC_ID, id, MAX_ID_LEN);
            GetDlgItemTextW(hDlg, IDC_DIR, dir, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_CATEGORY, cate, MAX_CATEGORY_LEN);
            GetDlgItemTextA(hDlg, IDC_PWD, pwd, MAX_PWD_LEN);
            GetDlgItemTextA(hDlg, IDC_PEERS, g_serversNotify, 1024); SaveOptions();
            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                if (!CmdSocket_UploadResource(s, id, dir, cate, pwd, g_serversNotify))
                {
                    MessageBox(hDlg, _T("无法建立上传任务，原因是参数不正确或者任务正在运行。"),
                        g_szAppName, MB_OK|MB_ICONINFORMATION);
                    CmdSocket_Close(s);
                    break;
                }
                CmdSocket_Close(s);
            }
            else
            {
                MessageBox(hDlg, _T("无法与服务建立连接!\r\n操作不能继续。"),
                    g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

static void PeerInfo_FillListView(HWND hDlg, struct ptrList *peers)
{
    struct idx_net *idxn, idxnf = { 0 };
    HWND hwndPeers;
    struct ptrList *li;
    struct peer_info *pr;
    int currCount, i, j;
    LV_ITEM lvI = { 0 };
    TCHAR szText[256], szTmp[256];

    strcpy_s(idxnf.id, MAX_ID_LEN, g_currIdx.idSel);
    idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdx, &idxnf);
    if (!idxn) return;

    hwndPeers = GetDlgItem(hDlg, IDC_PEER_INFO);
    currCount = ListView_GetItemCount(hwndPeers);

    for (li=peers, i=0; li; li=li->next, i++)
    {
        pr = (struct peer_info *)li->data;

        lvI.mask = LVIF_TEXT;
        lvI.iItem = i;
        lvI.pszText = pr->pid;
        if (i < currCount) ListView_SetItem(hwndPeers, &lvI);
        else ListView_InsertItem(hwndPeers, &lvI);

        _stprintf_s(szText, 256, _T("%s %s"), pr->isOutgoing?_T("出"):_T("入"),
            MbcsToTSTR(pr->ipport, szTmp, 256));
        ListView_SetItemText(hwndPeers, i, 1, szText);
        if (!pr->isConnected) _tcscpy_s(szText, 256, _T(""));
        else _stprintf_s(szText, 256, _T("%u/%u %s"), pr->piecesHave, idxn->pieceCnt,
            GetPercentString(szTmp, pr->piecesHave, idxn->pieceCnt));
        ListView_SetItemText(hwndPeers, i, 2, szText);
        if (!pr->isConnected) _tcscpy_s(szText, 256, _T(""));
        else GetSpeedString(szText, g_currIdx.iList==LIST_SEEDING?pr->upSpeed:pr->dnSpeed);
        ListView_SetItemText(hwndPeers, i, 3, szText);
    }

    for (j=currCount; j>i; j--)
        ListView_DeleteItem(hwndPeers, j-1);
}

static HANDLE g_hEventStopPeerInfo = NULL;
static HANDLE g_hEventStoppedPeerInfo = NULL;
static HWND g_hDlgPeerInfo = NULL;

static unsigned __stdcall PeerInfoThreadProc(LPVOID param)
{
    struct ptrList *peers = NULL;
    SOCKET s = INVALID_SOCKET;
    time_t lastCheckTime = 0, currTime;
    DWORD dwWait;

    while (1)
    {
        dwWait = WaitForSingleObject(g_hEventStopPeerInfo, 300);
        if (dwWait == WAIT_TIMEOUT)
        {
            time(&currTime);
            if (currTime - lastCheckTime < 2) continue;
            lastCheckTime = currTime;

            if (s == INVALID_SOCKET)
                s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
                CmdSocket_GetPeerInfo(s, g_currIdx.idSel, &peers);

            PeerInfo_FillListView(g_hDlgPeerInfo, peers);
            ptrList_free(&peers, free);
        }
        else break;
    }

    CmdSocket_Close(s);

    SetEvent(g_hEventStoppedPeerInfo);

    return 0;
}

static void StartPeerInfoThread(HWND hDlg)
{
    g_hDlgPeerInfo = hDlg;
    g_hEventStopPeerInfo = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_hEventStoppedPeerInfo = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, PeerInfoThreadProc, NULL, 0, NULL));
}
static void StopPeerInfoThread()
{
    SetEvent(g_hEventStopPeerInfo);

    while(TRUE)
    {
        if (WAIT_OBJECT_0==MsgWaitForMultipleObjects(1, &g_hEventStoppedPeerInfo, FALSE, INFINITE, QS_ALLINPUT))
            break;
        else
        {
            MSG msg;
            PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
            DispatchMessage(&msg);
        }
    }

    CloseHandle(g_hEventStopPeerInfo);
    CloseHandle(g_hEventStoppedPeerInfo);
    g_hEventStopPeerInfo = NULL;
    g_hEventStoppedPeerInfo = NULL;
}

INT_PTR CALLBACK PeerInfoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct idx_net *idxn, idxnf = { 0 };
    LVITEM lvI = { 0 };
    HWND hwndLV;
    RECT rc;
    TCHAR szText[512], szId[64], szHash[64];

    switch (message)
    {
    case WM_INITDIALOG:
        StartPeerInfoThread(hDlg);

        CenterDlg(hDlg);

        hwndLV = GetDlgItem(hDlg, IDC_TASK_INFO);
        ListView_SetExtendedListViewStyle(hwndLV, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        GetClientRect(hwndLV, &rc);
        SetListViewColumn(hwndLV, 0, _T("项目"), 80, LVCFMT_CENTER);
        SetListViewColumn(hwndLV, 1, _T("值"), rc.right-110, LVCFMT_LEFT);

        hwndLV = GetDlgItem(hDlg, IDC_PEER_INFO);
        ListView_SetExtendedListViewStyle(hwndLV, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        GetClientRect(hwndLV, &rc);
        SetListViewColumn(hwndLV, 0, _T("已连接的节点"), 180, LVCFMT_CENTER);
        SetListViewColumn(hwndLV, 1, _T("IP:端口"), 160, LVCFMT_LEFT);
        SetListViewColumn(hwndLV, 2, _T("进度"), 130, LVCFMT_RIGHT);
        SetListViewColumn(hwndLV, 3, _T("速度"), 115, LVCFMT_RIGHT);

        strcpy_s(idxnf.id, MAX_ID_LEN, g_currIdx.idSel);
        idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdx, &idxnf);
        if (idxn)
        {
            hwndLV = GetDlgItem(hDlg, IDC_TASK_INFO);

            lvI.mask = LVIF_TEXT;
            lvI.iItem = 0; lvI.pszText = "ID/Hash";
            ListView_InsertItem(hwndLV, &lvI);
            _stprintf_s(szText, 512, _T("%s  %s"),
                MbcsToTSTR(idxn->id, szId, MAX_ID_LEN),
                MbcsToTSTR(idxn->hash, szHash, MAX_HASH_LEN));
            ListView_SetItemText(hwndLV, 0, 1, szText);

            lvI.iItem = 1; lvI.pszText = "名称";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 1, 1, UnicodeToTSTR(idxn->name, szText, MAX_NAME_LEN));

            lvI.iItem = 2; lvI.pszText = "大小";
            ListView_InsertItem(hwndLV, &lvI);
            _stprintf_s(szText, 512, _T("%s  块大小:%s 总块数:%u"),
                GetSizeString(idxn->size, szId),
                GetPieceSizeString(idxn->pieceLen, szHash),
                idxn->pieceCnt);
            ListView_SetItemText(hwndLV, 2, 1, szText);

            lvI.iItem = 3; lvI.pszText = "分类";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 3, 1, UnicodeToTSTR(idxn->category, szText, 64));

            lvI.iItem = 4; lvI.pszText = "更新时间";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 4, 1, GetTimeString(idxn->lastUpdateTime, szText));

            lvI.iItem = 5; lvI.pszText = "本地路径";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 5, 1, UnicodeToTSTR(g_currIdx.dn->dir, szText, MAX_PATH));
        }

        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            StopPeerInfoThread();
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK IdxInfoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct idx_net *idxn, idxnf = { 0 };
    struct idx_local *idxl, idxlf = { 0 };
    LVITEM lvI = { 0 };
    HWND hwndLV;
    RECT rc;
    TCHAR szText[512], szId[64], szHash[64];

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);

        hwndLV = GetDlgItem(hDlg, IDC_TASK_INFO);
        ListView_SetExtendedListViewStyle(hwndLV, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        GetClientRect(hwndLV, &rc);
        SetListViewColumn(hwndLV, 0, _T("项目"), 80, LVCFMT_CENTER);
        SetListViewColumn(hwndLV, 1, _T("值"), rc.right-110, LVCFMT_LEFT);

        strcpy_s(idxnf.id, MAX_ID_LEN, g_currIdx.idSel);
        idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdx, &idxnf);
        strcpy_s(idxlf.id, MAX_ID_LEN, g_currIdx.idSel);
        idxl = (struct idx_local *)ptrArray_findSorted(&g_localIdx, &idxlf);
        if (idxn)
        {
            lvI.mask = LVIF_TEXT;
            lvI.iItem = 0; lvI.pszText = "ID/Hash";
            ListView_InsertItem(hwndLV, &lvI);
            _stprintf_s(szText, 512, _T("%s  %s"),
                MbcsToTSTR(idxn->id, szId, MAX_ID_LEN),
                MbcsToTSTR(idxn->hash, szHash, MAX_HASH_LEN));
            ListView_SetItemText(hwndLV, 0, 1, szText);

            lvI.iItem = 1; lvI.pszText = "名称";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 1, 1, UnicodeToTSTR(idxn->name, szText, MAX_NAME_LEN));

            lvI.iItem = 2; lvI.pszText = "大小";
            ListView_InsertItem(hwndLV, &lvI);
            _stprintf_s(szText, 512, _T("%s  块大小:%s 总块数:%u"),
                GetSizeString(idxn->size, szId),
                GetPieceSizeString(idxn->pieceLen, szHash),
                idxn->pieceCnt);
            ListView_SetItemText(hwndLV, 2, 1, szText);

            lvI.iItem = 3; lvI.pszText = "分类";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 3, 1, UnicodeToTSTR(idxn->category, szText, 64));

            lvI.iItem = 4; lvI.pszText = "更新时间";
            ListView_InsertItem(hwndLV, &lvI);
            ListView_SetItemText(hwndLV, 4, 1, GetTimeString(idxn->lastUpdateTime, szText));

            lvI.iItem = 5; lvI.pszText = "本地路径";
            ListView_InsertItem(hwndLV, &lvI);
            if (idxl) ListView_SetItemText(hwndLV, 5, 1, UnicodeToTSTR(idxl->dir, szText, MAX_PATH));
        }

        switch (g_currIdx.iList)
        {
        case LIST_DOWNLOADING:
        case LIST_SEEDING:
        case LIST_WAITING:
        case LIST_UPLOADING:
            break;
        default:
            EnableWindow(GetDlgItem(hDlg, IDC_ADD_TASK), TRUE);
            break;
        }

        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_ADD_TASK:
            _NetIdx_AddTask1(g_currIdx.idSel);
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Quit(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int _timer = 4;
    TCHAR tszTmp[256];

    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_INFO, (TCHAR *)lParam);
        SetTimer(hDlg, 8, 1000, NULL);
        CenterDlg(hDlg);
        return (INT_PTR)TRUE;

    case WM_TIMER:
        -- _timer;
        if (_timer <= 0)
        {
            KillTimer(hDlg, 8);
            PostMessage(hDlg, WM_COMMAND, IDOK, 0);
        }
        else
        {
            _stprintf_s(tszTmp, 256, _T("确定 (%d)"), _timer);
            SetDlgItemText(hDlg, IDOK, tszTmp);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


//static void FillIdxInfo(HWND hDlg, const WCHAR *fileName)
//{
//    HWND hwndInfo;
//    struct idx idx;
//    CHAR *p, sz[65536];
//    WCHAR szTmp[512];
//    int i, len, maxLen;
//
//    hwndInfo = GetDlgItem(hDlg, IDC_INFO);
//    if (!idx_load(fileName, &idx))
//    {
//        swprintf_s(szTmp, 512, "Cannot open file: %s\r\n", fileName);
//        SetWindowTextW(hwndInfo, szTmp);
//    }
//    else
//    {
//        p = sz;
//        maxLen = 65536;
//
//        len = sprintf_s(szTmp, 128, "id: %s", si.id);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "hash: %s", si.hash);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "name: %s", si.name);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "pieceLength: %d", si.pieceLength);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "totalBytes: %I64d", si.bytes);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "totalFiles: %d", si.fileCount);
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        len = sprintf_s(szTmp, 128, "%s", "----------------------------");
//        memcpy(p, szTmp, len); p += len;
//        memcpy(p, "\r\n", 3); p += 2;
//
//        for (i=0; i<(int)si.fileCount; i++)
//        {
//            len = strlen(si.files[i]->fileName);
//            memcpy(p, si.files[i]->fileName, len); p += len;
//            memcpy(p, "\r\n", 3); p += 2;
//
//            if (maxLen - MAX_PATH < (int)(p-sz)) break;
//        }
//
//        SetWindowText(hwndInfo, sz);
//    }
//}
//
//INT_PTR CALLBACK SeedInfoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
//{
//    OPENFILENAME ofn;
//    char szFile[MAX_PATH];
//
//    switch (message)
//    {
//    case WM_INITDIALOG:
//        return (INT_PTR)TRUE;
//
//    case WM_COMMAND:
//        switch (LOWORD(wParam))
//        {
//        case IDOK:
//            ZeroMemory(&ofn, sizeof(ofn));
//            ofn.lStructSize = sizeof(ofn);
//            ofn.hwndOwner = hDlg;
//            ofn.lpstrFile = szFile;
//            ofn.lpstrFile[0] = '\0';
//            ofn.nMaxFile = sizeof(szFile);
//            ofn.lpstrFilter = "GmSeed\0*.GmSeed\0";
//            ofn.nFilterIndex = 0;
//            ofn.lpstrFileTitle = NULL;
//            ofn.nMaxFileTitle = 0;
//            ofn.lpstrInitialDir = NULL;
//            ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
//            if (GetOpenFileName(&ofn) == TRUE)
//                FillSeedInfo(hDlg, ofn.lpstrFile);
//            break;
//        case IDCANCEL:
//            EndDialog(hDlg, LOWORD(wParam));
//            return (INT_PTR)TRUE;
//        }
//        break;
//    }
//    return (INT_PTR)FALSE;
//}
//

struct prvc
{
    int id;
    TCHAR *name;
};

#define MAX_PRVC        31

const struct prvc g_prvc[MAX_PRVC] = {
    { 11, _T("北京") },
    { 12, _T("天津") },
    { 13, _T("河北") },
    { 14, _T("山西") },
    { 15, _T("内蒙") },
    { 21, _T("辽宁") },
    { 22, _T("吉林") },
    { 23, _T("黑龙江") },
    { 31, _T("上海") },
    { 32, _T("江苏") },
    { 33, _T("浙江") },
    { 34, _T("安徽") },
    { 35, _T("福建") },
    { 36, _T("江西") },
    { 37, _T("山东") },
    { 41, _T("河南") },
    { 42, _T("湖北") },
    { 43, _T("湖南") },
    { 44, _T("广东") },
    { 45, _T("广西") },
    { 46, _T("海南") },
    { 50, _T("重庆") },
    { 51, _T("四川") },
    { 52, _T("贵州") },
    { 53, _T("云南") },
    { 54, _T("西藏") },
    { 61, _T("陕西") },
    { 62, _T("甘肃") },
    { 63, _T("青海") },
    { 64, _T("宁夏") },
    { 65, _T("新疆") }
};

static BOOL BrowseForDir(HWND hDlg, TCHAR *szPath)
{
    BROWSEINFO bi;
    PIDLIST_ABSOLUTE pidl;

    memset(&bi, 0, sizeof(BROWSEINFO));
    bi.hwndOwner = hDlg;
    bi.lpszTitle = _T("选择目录");
    bi.pszDisplayName = szPath;
    bi.ulFlags = BIF_BROWSEFORCOMPUTER|BIF_DONTGOBELOWDOMAIN|
        BIF_NEWDIALOGSTYLE|BIF_RETURNONLYFSDIRS;
    pidl = SHBrowseForFolder(&bi);
    if (pidl)
    {
        SHGetPathFromIDList(pidl, szPath);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    SOCKET s;
    int i, j;
    TCHAR tszPath[MAX_PATH];

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);

        SetDlgItemInt(hDlg, IDC_PORT_NUM, g_options.portNum, FALSE);
        SetDlgItemTextA(hDlg, IDC_SVR_ADDR, g_options.svrAddr);
        if (g_options.updateMode)
        {
            SendDlgItemMessage(hDlg, IDC_UPDATE_MODE, BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemTextW(hDlg, IDC_TMP_DIR, g_options.tmpDir);
        }
        else
        {
            SendDlgItemMessage(hDlg, IDC_UPDATE_MODE, BM_SETCHECK, BST_UNCHECKED, 0);
            SetDlgItemText(hDlg, IDC_TMP_DIR, _T(""));
            EnableWindow(GetDlgItem(hDlg, IDC_TMP_DIR), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_TMP), FALSE);
        }
        SendDlgItemMessage(hDlg, IDC_DIR_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" .. \\ 游戏类别 \\ 游戏目录"));
        SendDlgItemMessage(hDlg, IDC_DIR_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" .. \\ 游戏目录"));
        SendDlgItemMessage(hDlg, IDC_DIR_MODE, CB_SETCURSEL, g_options.dirMode, 0);
        SetDlgItemTextW(hDlg, IDC_DIR, g_options.dir);
        SetDlgItemInt(hDlg, IDC_DISK_SPACE_RESERVE, g_options.diskSpaceReserve, FALSE);

        for (i=0, j=0; i<MAX_PRVC; i++)
        {
            if (g_options.userPrvc == g_prvc[i].id) j = i;
            SendDlgItemMessage(hDlg, IDC_USER_PRVC, CB_ADDSTRING, 0, (LPARAM)g_prvc[i].name);
        }
        SendDlgItemMessage(hDlg, IDC_USER_PRVC, CB_SETCURSEL, j, 0);
        SendDlgItemMessage(hDlg, IDC_LINE_TYPE, CB_ADDSTRING, 0, (LPARAM)_T(" 电信"));
        SendDlgItemMessage(hDlg, IDC_LINE_TYPE, CB_ADDSTRING, 0, (LPARAM)_T(" 网通"));
        SendDlgItemMessage(hDlg, IDC_LINE_TYPE, CB_SETCURSEL, g_options.lineType, 0);

        SetDlgItemInt(hDlg, IDC_MAX_DOWN, g_options.downLimit, FALSE);
        SetDlgItemInt(hDlg, IDC_MAX_UP, g_options.upLimit, FALSE);
        SetDlgItemInt(hDlg, IDC_MAX_CONCURRENT_TASKS, g_options.maxConcurrentTasks, FALSE);
        SetDlgItemInt(hDlg, IDC_MAX_CACHE_PER_TASK, g_options.maxCachesPerTask, FALSE);
        SetDlgItemInt(hDlg, IDC_MAX_DOWN_PEERS_PER_TASK, g_options.maxDownPeersPerTask, FALSE);
        SetDlgItemInt(hDlg, IDC_MAX_UP_PEERS_PER_TASK, g_options.maxUpPeersPerTask, FALSE);
        SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" 不使用优先级方案(平均分配带宽)"));
        SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" 第1任务使用70%带宽, 第2任务20% ..."));
        SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" 第1任务使用50%带宽, 第2任务20% ..."));
        SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_ADDSTRING, 0, (LPARAM)_T(" 第1任务使用35%带宽, 第2任务20% ..."));
        SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_SETCURSEL, g_options.priorityMode, 0);
        SetDlgItemInt(hDlg, IDC_SEED_MINUTES, g_options.seedMinutes, FALSE);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE_TMP:
            if (BrowseForDir(hDlg, tszPath))
                SetDlgItemText(hDlg, IDC_TMP_DIR, tszPath);
            break;
        case IDC_BROWSE:
            if (BrowseForDir(hDlg, tszPath))
                SetDlgItemText(hDlg, IDC_DIR, tszPath);
            break;
        case IDC_UPDATE_MODE:
            if (SendDlgItemMessage(hDlg, IDC_UPDATE_MODE, BM_GETCHECK, 0, 0)==BST_CHECKED)
            {
                EnableWindow(GetDlgItem(hDlg, IDC_TMP_DIR), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_TMP), TRUE);
            }
            else
            {
                EnableWindow(GetDlgItem(hDlg, IDC_TMP_DIR), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_TMP), FALSE);
            }
            break;
        case IDOK:
            g_options.portNum = (WORD)GetDlgItemInt(hDlg, IDC_PORT_NUM, NULL, FALSE);
            GetDlgItemTextA(hDlg, IDC_SVR_ADDR, g_options.svrAddr, 256);
            g_options.updateMode = (UCHAR)SendDlgItemMessage(hDlg, IDC_UPDATE_MODE, BM_GETCHECK, 0, 0);
            GetDlgItemTextW(hDlg, IDC_TMP_DIR, g_options.tmpDir, MAX_PATH);
            g_options.dirMode = (UCHAR)SendDlgItemMessage(hDlg, IDC_DIR_MODE, CB_GETCURSEL, 0, 0);
            GetDlgItemTextW(hDlg, IDC_DIR, g_options.dir, MAX_PATH);
            g_options.diskSpaceReserve = (DWORD)GetDlgItemInt(hDlg, IDC_DISK_SPACE_RESERVE, NULL, FALSE);

            j = (int)SendDlgItemMessage(hDlg, IDC_USER_PRVC, CB_GETCURSEL, 0, 0);
            if (j>=0 && j<MAX_PRVC) g_options.userPrvc = g_prvc[j].id;
            g_options.lineType = (UCHAR)SendDlgItemMessage(hDlg, IDC_LINE_TYPE, CB_GETCURSEL, 0, 0);

            g_options.downLimit = GetDlgItemInt(hDlg, IDC_MAX_DOWN, NULL, FALSE);
            g_options.upLimit = GetDlgItemInt(hDlg, IDC_MAX_UP, NULL, FALSE);
            g_options.maxConcurrentTasks = GetDlgItemInt(hDlg, IDC_MAX_CONCURRENT_TASKS, NULL, FALSE);
            g_options.maxCachesPerTask = GetDlgItemInt(hDlg, IDC_MAX_CACHE_PER_TASK, NULL, FALSE);
            g_options.maxDownPeersPerTask = GetDlgItemInt(hDlg, IDC_MAX_DOWN_PEERS_PER_TASK, NULL, FALSE);
            g_options.maxUpPeersPerTask = GetDlgItemInt(hDlg, IDC_MAX_UP_PEERS_PER_TASK, NULL, FALSE);
            g_options.priorityMode = (UCHAR)SendDlgItemMessage(hDlg, IDC_PRIORITY_MODE, CB_GETCURSEL, 0, 0);
            g_options.seedMinutes = GetDlgItemInt(hDlg, IDC_SEED_MINUTES, NULL, FALSE);

            if (g_options.priorityMode && !g_options.downLimit)
            {
                MessageBox(hDlg, _T("\"下载优先级\" 必须配合 \"下载限速\" 一起使用。\r\n请设置 \"下载限速\"。"),
                    g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }

            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                CmdSocket_SetOptions(s, &g_options);
                CmdSocket_Close(s);
            }
            else
            {
                MessageBox(hDlg, _T("无法保存信息，因为无法与服务器建立连接!"), _T("..."), MB_OK|MB_ICONINFORMATION);
                break;
            }
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void OnPrepareResult(const CHAR *cmd, const CHAR *id)
{
    TCHAR szText[512];
    struct idx_downloading *dn;
    struct ptrList *li;
    int iItem;

    for (dn=NULL,li=g_downloading; li; li=li->next)
    {
        dn = li->data;
        if (strcmp(dn->id, id) == 0) break;
    }
    if (!dn || strcmp(dn->id, id)) return;
    dn->action |= TS_ERROR;

    if (0==strcmp(cmd, "prepare_failed_dir"))
        _stprintf_s(szText, 512, _T("准备任务(%s)失败:无法创建目录"), id);
    else if (0==strcmp(cmd, "prepare_failed_disk"))
        _stprintf_s(szText, 512, _T("准备任务(%s)失败:磁盘空间不够"), id);
    else if (0==strcmp(cmd, "prepare_failed_create_idx"))
        _stprintf_s(szText, 512, _T("准备任务(%s)失败:无法创建种子文件"), id);
    else if (0==strcmp(cmd, "prepare_failed_get_idx"))
        _stprintf_s(szText, 512, _T("准备任务(%s)失败:无法从服务器获取种子文件"), id);
    else if (0==strcmp(cmd, "prepare_failed_open_idx"))
        _stprintf_s(szText, 512, _T("准备任务(%s)失败:无法打开种子文件"), id);
    else return;

    Log_AddLine(szText);

    iItem = _Downloading_FindItem(id, 0, g_downloadingCnt);
    if (iItem < 0) return;
    _Downloading_SetItemStatus(iItem, dn);
}

void CALLBACK MsgSocketCB(CHAR *msgData, int msgLen)
{
    struct ptrList *keyVals = NULL;
    CHAR *pCmd, *pResult, *pId;

    if (!msgData && msgLen < 0)
    {
        PostMessage(g_hWndMain, WM_COMMAND, IDM_DISCONNECT, 0);
        return;
    }

    KeyValueFromXml(msgData+10, &keyVals);

    pCmd = find_kv(keyVals, "command");
    pResult = find_kv(keyVals, "result");
    pId = find_kv(keyVals, "id");

    if (strcmp(pResult, "ok") && strcmp(pResult, ""))
    {
        ptrList_free(&keyVals, free_kv);
        return;
    }

    if (0==strcmp(pCmd, STR_GET_NET_IDX_LIST))
        MsgSocket_OnNetIdxList(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_LOCAL_IDX_LIST))
        MsgSocket_OnLocalIdxList(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_DOWNLOADING_TASKS))
        MsgSocket_OnDownloadingTasks(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_WAITING_TASKS))
        MsgSocket_OnWaitingTasks(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_SEEDING_TASKS))
        MsgSocket_OnSeedingTasks(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_UPLOADING_TASKS))
        MsgSocket_OnUploadingTasks(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_GET_AUTO_UPDATE_TASKS))
        MsgSocket_OnAutoUpdateTasks(msgData, msgLen, keyVals);
    else if (0==strcmp(pCmd, STR_REGISTER_SOCKET))
    {
        Tree_AddCategoryItems();
        NetIdx_FillListView();
        _Downloading_FillListView();
    }
    else
    {
        if (0==strcmp(pCmd, STR_GENERAL_INFO))
            OnGeneralInfo(pId);

        else if (0==strcmp(pCmd, STR_NET_IDX_ADDED))
            OnNetIdxAdded(pId);
        else if (0==strcmp(pCmd, STR_NET_IDX_DELETED))
            OnNetIdxDeleted(pId);

        if (0==strcmp(pCmd, STR_LOCAL_IDX_ADDED))
            OnLocalIdxAdded(pId);
        else if (0==strcmp(pCmd, STR_LOCAL_IDX_DELETED))
            OnLocalIdxDeleted(pId);

        else if (0==strcmp(pCmd, STR_UPLOADING_ADDED))
            OnUploadingAdded(pId);
        else if (0==strcmp(pCmd, STR_UPLOADING_DELETED))
            OnUploadingDeleted(pId);
        else if (0==strcmp(pCmd, STR_UPLOADING_PROGRESS))
            OnUploadingProgress(pId);
        else if (0==strcmp(pCmd, STR_UPLOADING_ERROR))
            OnUploadingError(pId);

        else if (0==strcmp(pCmd, STR_SEEDING_ADDED))
            OnSeedingAdded(pId);
        else if (0==strcmp(pCmd, STR_SEEDING_DELETED))
            OnSeedingDeleted(pId);
        else if (0==strcmp(pCmd, STR_SEEDING_TIME_OUT))
            OnSeedingTimeOut(pId);

        else if (0==strcmp(pCmd, STR_AUTOUPDATE_ADDED))
            OnAutoUpdateAdded(pId);
        else if (0==strcmp(pCmd, STR_AUTOUPDATE_DELETED))
            OnAutoUpdateDeleted(pId);

        else if (0==strcmp(pCmd, STR_WAITING_ADDED))
            OnWaitingAdded(pId);
        else if (0==strcmp(pCmd, STR_WAITING_DELETED))
            OnWaitingDeleted(pId);

        else if (0==strcmp(pCmd, STR_DOWNLOADING_PAUSED))
            OnDownloadingPaused(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_RESUMED))
            OnDownloadingResumed(pId);

        else if (0==strcmp(pCmd, STR_DOWNLOADING_ADDED))
            OnDownloadingAdded(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_DELETED))
            OnDownloadingDeleted(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_COMPLETED))
            OnDownloadingCompleted(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_UPDATED))
            OnUpdatingCompleted(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_PROGRESS))
            OnDownloadingProgress(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_CHANGED))
            OnDownloadingChanged(pId);
        else if (0==strcmp(pCmd, STR_DOWNLOADING_ERROR))
            OnDownloadingError(pId);

        else if (0==strcmp(pCmd, STR_TRANSFER_BEGIN))
            OnTransferBegin(pId);
        else if (0==strcmp(pCmd, STR_TRANSFER_ERROR))
            OnTransferError(pId);

        else if (0==strcmp(pCmd, STR_TRACKER_INFO))
            OnTrackerInfo(pId);
    }

    ptrList_free(&keyVals, free_kv);
}

