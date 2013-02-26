#include "gmSvrUI.h"

HINSTANCE g_hInst;
HWND g_hwndMain, g_hwndLV;
TCHAR g_szAppName[] = _T("网游更新服务器 - 控制台");

TCHAR g_workDir[MAX_PATH] = { 0 };
CHAR g_serverIp[32] = { 0 };
UINT16 g_serverPort = 8900;
CHAR g_currIdx[MAX_ID_LEN] = { 0 };

BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK OptionsDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PeersDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

void CALLBACK MsgSocketCB(UCHAR *msgData, int msgLen);

struct idxn
{
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
    WCHAR name[MAX_NAME_LEN];
    WCHAR category[MAX_CATEGORY_LEN];
    CHAR extraInfo[MAX_EXTRA_LEN];
    UINT32 pieceLen, pieceCnt;
    INT64 size;
    time_t lastUpdateTime;
    INT32 seeders, leechers;
};
struct ptrArray g_idx = { 0 };

struct svr_options
{
    UINT16 port;
    UINT32 interval;

    CHAR uploadPwd[30];
    CHAR uploadIps[512];

    CHAR servers[1024];
};
struct svr_options g_options = { 0 };

struct peer_info
{
    CHAR ip[24];
    UINT16 port;
    int connectable;
    int completed;
};

BOOL CmdSocket_GetOptions(SOCKET cmdSock);
BOOL CmdSocket_SetOptions(SOCKET cmdSock);

// ---------------------------------------------------------------------------
static BOOL ReadSvrOptions()
{
    TCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;

    _stprintf_s(fileName, MAX_PATH, _T("%s\\options.txt"), g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;

        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break; *pTmp = 0; pTmp += 2;

        pEq = strchr(pLine, '=');
        if (!pEq) continue; *pEq = 0; pEq ++;

        if (0==_stricmp(pLine, "port_num"))
            g_serverPort = (UINT16)atoi(pEq);
    }
    free(fileData);

    return TRUE;
}

static int idxCmp(const void *p1, const void *p2)
{
    return strcmp(((struct idx *)p1)->id, ((struct idx *)p2)->id);
}
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    WNDCLASSEX wcex;
    WSADATA wsd = { 0 };
    int i;

    GetModuleFileName(NULL, g_workDir, MAX_PATH);
    for (i=(int)_tcslen(g_workDir); i>0; i--)
    { if (g_workDir[i] == _T('\\')) { g_workDir[i] = 0; break; } }

    ptrArray_init(&g_idx, idxCmp);

    strcpy_s(g_serverIp, 32, "127.0.0.1");
    g_serverPort = 8900;
    ReadSvrOptions();

    WSAStartup(MAKEWORD(2, 2), &wsd);

    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = 0;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_GMSVRUI));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_GMSVRUI);
    wcex.lpszClassName  = _T("gmSvrUIWindowClass");
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    RegisterClassEx(&wcex);

    if (!InitInstance (hInstance, nCmdShow))
    {
        WSACleanup();
        ptrArray_free(&g_idx, free);
        return FALSE;
    }

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WSACleanup();
    ptrArray_free(&g_idx, free);

    return (int) msg.wParam;
}

static BOOL TryConnectService()
{
    SOCKET s;
    BOOL serviceOK = FALSE;

    s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
    if (s != INVALID_SOCKET)
    {
        serviceOK = CmdSocket_GetOptions(s);
        CmdSocket_Close(s);
    }

    return serviceOK;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    INITCOMMONCONTROLSEX initCtrls;
    TCHAR szTmp[512];

    initCtrls.dwSize = sizeof(initCtrls);
    initCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&initCtrls);

    g_hInst = hInstance;

    if (!TryConnectService())
    {
        _stprintf_s(szTmp, 512, _T("无法连接到服务器(端口:%u)。\r\n请检查本机服务\"gmSvr\"是否正常运行。"), g_serverPort);
        MessageBox(NULL, szTmp, g_szAppName, MB_OK|MB_ICONINFORMATION);
    }

    g_hwndMain = CreateWindow(_T("gmSvrUIWindowClass"), g_szAppName, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

    if (!g_hwndMain) return FALSE;

    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    BeginMsgSocketThread(MsgSocketCB, g_serverIp, g_serverPort, "");

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

static void idxList_SetItems(int i, struct idxn *idx)
{
    TCHAR szText[512];

    MbcsToTSTR(idx->hash, szText, 64);
    ListView_SetItemText(g_hwndLV, i, 1, szText);
    UnicodeToTSTR(idx->name, szText, 256);
    ListView_SetItemText(g_hwndLV, i, 2, szText);
    UnicodeToTSTR(idx->category, szText, 64);
    ListView_SetItemText(g_hwndLV, i, 3, szText);
    GetSizeString(idx->size, szText);
    ListView_SetItemText(g_hwndLV, i, 4, szText);
    GetTimeString(idx->lastUpdateTime, szText);
    ListView_SetItemText(g_hwndLV, i, 5, szText);
    if (idx->seeders) _stprintf_s(szText, 64, _T("%d"), idx->seeders);
    else szText[0] = 0;
    ListView_SetItemText(g_hwndLV, i, 6, szText);
    if (idx->leechers) _stprintf_s(szText, 64, _T("%d"), idx->leechers);
    else szText[0] = 0;
    ListView_SetItemText(g_hwndLV, i, 7, szText);
    MbcsToTSTR(idx->extraInfo, szText, 512);
    ListView_SetItemText(g_hwndLV, i, 8, szText);
}

static void idxList_UpdateItem(struct idxn *idx)
{
    LV_ITEM lvI = { 0 };
    TCHAR szText[512];
    CHAR szId[64];
    int i;

    if (!IsWindow(g_hwndLV)) return;

    for (i=0; i<ListView_GetItemCount(g_hwndLV); i++)
    {
        ListView_GetItemText(g_hwndLV, i, 0, szText, 64);
        TSTRToMbcs(szText, szId, MAX_ID_LEN);
        if (strcmp(szId, idx->id)) continue;

        idxList_SetItems(i, idx);
        break;
    }
}

static void idxList_InsertItem(struct idxn *idx)
{
    LV_ITEM lvI = { 0 };
    TCHAR szText[512];
    CHAR szId[64];
    int i;

    if (!IsWindow(g_hwndLV)) return;

    for (i=0; i<ListView_GetItemCount(g_hwndLV); i++)
    {
        ListView_GetItemText(g_hwndLV, i, 0, szText, 64);
        TSTRToMbcs(szText, szId, MAX_ID_LEN);
        if (strcmp(szId, idx->id) >= 0) break;
    }

    lvI.mask = LVIF_TEXT;
    lvI.iItem = i;
    lvI.pszText = szText;
    MbcsToTSTR(idx->id, szText, 32);
    ListView_InsertItem(g_hwndLV, &lvI);

    idxList_SetItems(i, idx);
}

static void idxList_DeleteItem(struct idxn *idx)
{
    LV_ITEM lvI = { 0 };
    TCHAR szText[512];
    CHAR szId[64];
    int i;

    if (!IsWindow(g_hwndLV)) return;

    for (i=0; i<ListView_GetItemCount(g_hwndLV); i++)
    {
        ListView_GetItemText(g_hwndLV, i, 0, szText, 64);
        TSTRToMbcs(szText, szId, MAX_ID_LEN);
        if (strcmp(szId, idx->id)) continue;

        ListView_DeleteItem(g_hwndLV, i);
        break;
    }
}

static void IdxLIst_FillListView()
{
    struct idxn *idx;
    int currCount, i, j;
    LV_ITEM lvI = { 0 };
    TCHAR szText[512];

    if (!IsWindow(g_hwndLV)) return;

    currCount = ListView_GetItemCount(g_hwndLV);

    for (i=0; i<ptrArray_size(&g_idx); i++)
    {
        idx = (struct idxn *)ptrArray_nth(&g_idx, i);

        lvI.mask = LVIF_TEXT;
        lvI.iItem = i;
        lvI.pszText = szText;
        MbcsToTSTR(idx->id, szText, 32);
        if (i < currCount) ListView_SetItem(g_hwndLV, &lvI);
        else ListView_InsertItem(g_hwndLV, &lvI);

        idxList_SetItems(i, idx);
    }

    for (j=currCount; j>i; j--)
        ListView_DeleteItem(g_hwndLV, j-1);
}

void _SetListViewColumn(HWND hwndLV, int iCol, TCHAR *title, int width, int fmt)
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

static void _ListView_OnEvent(NMHDR *lpNmhdr)
{
    int currSel;
    TCHAR tszId[64];

    switch (lpNmhdr->code)
    {
    case NM_DBLCLK:
        currSel = ListView_GetNextItem(g_hwndLV, -1, LVNI_SELECTED);
        if (currSel < 0) break;
        ListView_GetItemText(g_hwndLV, currSel, 0, tszId, MAX_ID_LEN);
        TSTRToMbcs(tszId, g_currIdx, MAX_ID_LEN);

        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_PEERS), g_hwndMain, PeersDlgProc);
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    HDWP hdwp;

    switch (message)
    {
    case WM_CREATE:
        g_hwndLV = CreateWindowEx(0, WC_LISTVIEW, _T(""), WS_CHILD|LVS_REPORT|WS_VISIBLE|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            0, 0, 0, 0, hWnd, (HMENU)100, g_hInst, NULL);
        ListView_SetExtendedListViewStyle(g_hwndLV, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        _SetListViewColumn(g_hwndLV, 0, _T("ID"), 60, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndLV, 1, _T("Hash"), 220, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndLV, 2, _T("名称"), 190, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndLV, 3, _T("分类"), 80, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndLV, 4, _T("大小"), 80, LVCFMT_RIGHT);
        _SetListViewColumn(g_hwndLV, 5, _T("更新时间"), 135, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndLV, 6, _T("种子数"), 50, LVCFMT_RIGHT);
        _SetListViewColumn(g_hwndLV, 7, _T("下载数"), 50, LVCFMT_RIGHT);
        _SetListViewColumn(g_hwndLV, 8, _T("备注"), 230, LVCFMT_LEFT);
        break;

    case WM_SIZE:
        hdwp = BeginDeferWindowPos(1);
        DeferWindowPos(hdwp, g_hwndLV, NULL,
            0, 0, LOWORD(lParam), HIWORD(lParam),
            SWP_NOZORDER|SWP_NOMOVE);
        EndDeferWindowPos(hdwp);
        break;

    case WM_NOTIFY:
        if (wParam == 100) _ListView_OnEvent((NMHDR *)lParam);
        break;

    case WM_COMMAND:
        wmId = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_OPTIONS:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_OPTIONS), hWnd, OptionsDlgProc);
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
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

static void CenterDlg(HWND hDlg)
{
    RECT rcMain;
    RECT rcDlg;
    int x, y;

    GetWindowRect(g_hwndMain, &rcMain);
    GetWindowRect(hDlg, &rcDlg);
    x = (rcMain.right+rcMain.left-(rcDlg.right-rcDlg.left))/2;
    y = (rcMain.bottom+rcMain.top-(rcDlg.bottom-rcDlg.top))/2;

    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
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

BOOL CmdSocket_GetOptions(SOCKET cmdSock)
{
    UCHAR *p, buf[64], *recvBuf;
    int recvLen;

    p = buf;
    *((UINT32 *)p) = htonl(6); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_OPTIONS; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    if (!CmdSocket_Send(cmdSock, buf, 10)) return FALSE;

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen)) return FALSE;
    if (recvLen < 10) { free(recvBuf); return FALSE; }

    p = recvBuf;
    if (*(p+4) != CMD_ADMIN || *(p+5) != GM_GET_SVR_OPTIONS_RESP) { free(recvBuf); return FALSE; }
    p += 10;
    g_options.port = ntohs(*((UINT16 *)p)); p += 2;
    g_options.interval = ntohl(*((UINT32 *)p)); p += 4;
    strcpy_s(g_options.uploadPwd, 30, (CHAR *)p); p += strlen(g_options.uploadPwd) + 1;
    strcpy_s(g_options.uploadIps, 512, (CHAR *)p); p += strlen(g_options.uploadIps) + 1;
    strcpy_s(g_options.servers, 1024, (CHAR *)p); p += strlen(g_options.servers) + 1;

    free(recvBuf);

    return TRUE;
}

BOOL CmdSocket_SetOptions(SOCKET cmdSock)
{
    UCHAR *p, buf[2048], *recvBuf;
    int recvLen, l;

    p = buf;
    *((UINT32 *)p) = htonl(0); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_SET_SVR_OPTIONS; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    *((UINT16 *)p) = htons(g_options.port); p += 2;
    *((UINT32 *)p) = htonl(g_options.interval); p += 4;
    strcpy_s((CHAR *)p, 32, g_options.uploadPwd); p += strlen(g_options.uploadPwd) + 1;
    strcpy_s((CHAR *)p, 512, g_options.uploadIps); p += strlen(g_options.uploadIps) + 1;
    strcpy_s((CHAR *)p, 1024, g_options.servers); p += strlen(g_options.servers) + 1;

    l = (int)(p-buf);
    *((UINT32 *)buf) = htonl(l-4);

    if (!CmdSocket_Send(cmdSock, buf, l)) return FALSE;

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen)) return FALSE;
    if (recvLen < 10) { free(recvBuf); return FALSE; }

    p = recvBuf;
    if (*(p+4) != CMD_ADMIN || *(p+5) != GM_SET_SVR_OPTIONS_RESP) { free(recvBuf); return FALSE; }

    free(recvBuf);

    return TRUE;
}

INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    SOCKET s;
    BOOL setOK;

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        SetDlgItemInt(hDlg, IDC_PORT, g_options.port, FALSE);
        SetDlgItemInt(hDlg, IDC_INTERVAL, g_options.interval, FALSE);
        SetDlgItemTextA(hDlg, IDC_PWD, g_options.uploadPwd);
        SetDlgItemTextA(hDlg, IDC_IPS, g_options.uploadIps);
        SetDlgItemTextA(hDlg, IDC_SERVERS, g_options.servers);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            g_options.port = (UINT16)GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
            g_options.interval = (UINT32)GetDlgItemInt(hDlg, IDC_INTERVAL, NULL, FALSE);
            GetDlgItemTextA(hDlg, IDC_PWD, g_options.uploadPwd, 30);
            GetDlgItemTextA(hDlg, IDC_IPS, g_options.uploadIps, 512);
            GetDlgItemTextA(hDlg, IDC_SERVERS, g_options.servers, 1024);
            strcat_s(g_options.servers, 1024, "\r\n");
            setOK = FALSE;
            s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
            if (s != INVALID_SOCKET)
            {
                setOK = CmdSocket_SetOptions(s);
                CmdSocket_Close(s);
            }
            if (!setOK)
            {
                MessageBox(NULL, _T("无法连接到服务器。\r\n请检查本机服务\"gmSvr\"是否正常运行。"), g_szAppName, MB_OK|MB_ICONINFORMATION);
                break;
            }
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

HANDLE g_hEventStopPeerInfo = NULL;
HANDLE g_hEventStoppedPeerInfo = NULL;
HWND g_hDlgPeers, g_hwndPeers;

static void PeerInfo_FillListView(struct ptrList *peers)
{
    struct ptrList *li;
    struct peer_info *pr;
    int currCount, i, j;
    LV_ITEM lvI = { 0 };
    TCHAR szText[256], szIp[32];

    if (!IsWindow(g_hwndPeers)) return;

    currCount = ListView_GetItemCount(g_hwndPeers);

    for (li=peers, i=0; li; li=li->next, i++)
    {
        pr = (struct peer_info *)li->data;

        _stprintf_s(szText, _T("%s:%d"), MbcsToTSTR(pr->ip, szIp, 32), pr->port);
        lvI.mask = LVIF_TEXT;
        lvI.iItem = i;
        lvI.pszText = szText;
        if (i < currCount) ListView_SetItem(g_hwndPeers, &lvI);
        else ListView_InsertItem(g_hwndPeers, &lvI);

        switch (pr->connectable)
        {
        case 1: _tcscpy_s(szText, 64, _T("可连接")); break;
        case 2: _tcscpy_s(szText, 64, _T("不可连接")); break;
        default: _tcscpy_s(szText, 64, _T("未确定")); break;
        }
        ListView_SetItemText(g_hwndPeers, i, 1, szText);

        if (pr->completed) _tcscpy_s(szText, 64, _T("供种"));
        else _tcscpy_s(szText, 64, _T("下载"));
        ListView_SetItemText(g_hwndPeers, i, 2, szText);
    }

    for (j=currCount; j>i; j--)
        ListView_DeleteItem(g_hwndPeers, j-1);
}


static BOOL _PeerInfoFromString(CHAR *pIp, struct peer_info *pi)
{
    CHAR *pPort, *pConn, *pCmpl;

    pPort = strchr(pIp, ':'); if (!pPort) return FALSE; *pPort = 0; pPort ++;
    pConn = strchr(pPort, ':'); if (!pConn) return FALSE; *pConn = 0; pConn ++;
    pCmpl = strchr(pConn, ':'); if (!pCmpl) return FALSE; *pCmpl = 0; pCmpl ++;

    if (strlen(pIp) >= 24) return FALSE;

    strcpy_s(pi->ip, 20, pIp);
    pi->port = (UINT16)atoi(pPort);
    pi->connectable = atoi(pConn);
    pi->completed = atoi(pCmpl);
    return TRUE;
}

static BOOL CmdSocket_GetPeerInfo(SOCKET cmdSock, struct ptrList **peers)
{
    UCHAR *p, buf[128], *recvBuf;
    CHAR *pTmp;
    int recvLen;
    struct peer_info *pi;

    p = buf;
    *((UINT32 *)p) = htonl(6+MAX_ID_LEN); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_PEERS; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt
    strcpy_s((CHAR *)p, MAX_ID_LEN, g_currIdx); p += MAX_ID_LEN;

    if (!CmdSocket_Send(cmdSock, buf, 10+MAX_ID_LEN)) return FALSE;

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen)) return FALSE;
    if (recvLen < 10+MAX_ID_LEN) { free(recvBuf); return FALSE; }

    p = recvBuf;
    if (*(p+4) != CMD_ADMIN ||
        *(p+5) != GM_GET_SVR_PEERS_RESP ||
        strcmp((CHAR *)(p+10), g_currIdx))
    { free(recvBuf); return FALSE; }

    if (recvLen > 10+MAX_ID_LEN)
    {
        p += 10+MAX_ID_LEN;
        while (1)
        {
            pTmp = strchr((CHAR *)p, ';');
            if (!pTmp) break; *pTmp = 0; pTmp ++;
            pi = (struct peer_info *)malloc(sizeof(struct peer_info)); if (!pi) break;
            if (!_PeerInfoFromString((CHAR *)p, pi)) { free(pi); break; }
            ptrList_append(peers, pi);

            p = (UCHAR *)pTmp;
        }
    }

    free(recvBuf);

    return TRUE;
}

static unsigned __stdcall PeerInfoThreadProc(LPVOID param)
{
    struct ptrList *peers = NULL;
    SOCKET s = INVALID_SOCKET;
    time_t lastCheckTime = 0, currTime;
    DWORD dwWait;

    while (1)
    {
        dwWait = WaitForSingleObject(g_hEventStopPeerInfo, 800);
        if (dwWait == WAIT_TIMEOUT)
        {
            time(&currTime);
            if (currTime - lastCheckTime < 3) continue;
            lastCheckTime = currTime;

            if (s == INVALID_SOCKET)
            {
                s = CmdSocket_CreateAndConnect(g_serverIp, g_serverPort);
                if (s == INVALID_SOCKET) continue;
            }

            if (CmdSocket_GetPeerInfo(s, &peers))
            {
                PeerInfo_FillListView(peers);
                ptrList_free(&peers, free);
            }
        }
        else break;
    }

    CmdSocket_Close(s);

    SetEvent(g_hEventStoppedPeerInfo);

    return 0;
}

static void StartPeerInfoThread()
{
    g_hEventStopPeerInfo = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_hEventStoppedPeerInfo = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, PeerInfoThreadProc, NULL, 0, NULL));
}
static void StopPeerInfoThread()
{
    if (!g_hEventStopPeerInfo) return;

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


INT_PTR CALLBACK PeersDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR szText[512], szTmp[256];

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);

        g_hDlgPeers = hDlg;
        g_hwndPeers = GetDlgItem(hDlg, IDC_PEERS);
        _SetListViewColumn(g_hwndPeers, 0, _T("IP:端口"), 140, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndPeers, 1, _T("连接"), 70, LVCFMT_LEFT);
        _SetListViewColumn(g_hwndPeers, 2, _T("状态"), 60, LVCFMT_LEFT);
        ListView_SetExtendedListViewStyle(g_hwndPeers, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);

        _stprintf_s(szText, 256, _T("%s 节点"), MbcsToTSTR(g_currIdx, szTmp, 64));
        SetWindowText(hDlg, szText);

        StartPeerInfoThread();
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            StopPeerInfoThread();
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static BOOL _IdxnFromUtf8String(struct idxn *idx, CHAR *id)
{
    CHAR *hash, *name, *cate, *extra, *pieceLen, *pieceCnt, *size, *lastUpd, *seeders, *leechers, *pCrlf;

    pCrlf = strstr(id, "\r\n"); if (pCrlf) *pCrlf = 0;
    hash = strchr(id, '\t'); if (!hash) return FALSE; *hash = 0; hash ++;
    name = strchr(hash, '\t'); if (!name) return FALSE; *name = 0; name ++;
    cate = strchr(name, '\t'); if (!cate) return FALSE; *cate = 0; cate ++;
    extra = strchr(cate, '\t'); if (!extra) return FALSE; *extra = 0; extra ++;
    pieceLen = strchr(extra, '\t'); if (!pieceLen) return FALSE; *pieceLen = 0; pieceLen ++;
    pieceCnt = strchr(pieceLen, '\t'); if (!pieceCnt) return FALSE; *pieceCnt = 0; pieceCnt ++;
    size = strchr(pieceCnt, '\t'); if (!size) return FALSE; *size = 0; size ++;
    lastUpd = strchr(size, '\t'); if (!lastUpd) return FALSE; *lastUpd = 0; lastUpd ++;
    seeders = strchr(lastUpd, '\t'); if (!seeders) return FALSE; *seeders = 0; seeders ++;
    leechers = strchr(seeders, '\t'); if (!leechers) return FALSE; *leechers = 0; leechers ++;

    strcpy_s(idx->id ,MAX_ID_LEN, id);
    strcpy_s(idx->hash, MAX_HASH_LEN, hash);
    Utf8ToUnicode(name, idx->name, MAX_NAME_LEN);
    Utf8ToUnicode(cate, idx->category, MAX_CATEGORY_LEN);
    strcpy_s(idx->extraInfo, MAX_EXTRA_LEN, extra);
    idx->pieceLen = (UINT32)atoi(pieceLen);
    idx->pieceCnt = (UINT32)atoi(pieceCnt);
    idx->size = (INT64)_atoi64(size);
    idx->lastUpdateTime = (time_t)_atoi64(lastUpd);
    idx->seeders = (INT32)atoi(seeders);
    idx->leechers = (INT32)atoi(leechers);

    return TRUE;
}

static void _OnSvrIdxListResp(UCHAR *msgData, int msgLen)
{
    UCHAR *p = (UCHAR *)msgData;
    CHAR *pTmp;
    struct idxn *idx, idxf = { 0 };

    p += 10;
    while (1)
    {
        pTmp = strstr((CHAR *)p, "\r\n");
        if (!pTmp) break; *pTmp = 0; pTmp += 2;
        idx = (struct idxn *)malloc(sizeof(struct idxn)); if (!idx) break;
        if (!_IdxnFromUtf8String(idx, (CHAR *)p)) { free(idx); break; }
        ptrArray_insertSorted(&g_idx, idx);

        p = (UCHAR *)pTmp;
    }

    IdxLIst_FillListView();
}
static void _OnSvrIdxListChanged(UCHAR *msgData, int msgLen)
{
    UCHAR *p = (UCHAR *)(msgData+10);
    struct idxn *idx, *idxOld, idxf = { 0 };

    idx = (struct idxn *)malloc(sizeof(struct idxn)); if (!idx) return;
    if (!_IdxnFromUtf8String(idx, (CHAR *)p)) { free(idx); return; }

    strcpy_s(idxf.id, MAX_ID_LEN, idx->id);
    idxOld = (struct idxn *)ptrArray_findSorted(&g_idx, &idxf);
    if (idxOld)
    {
        *idxOld = *idx;
        free(idx);
        if (0==strcmp(idxOld->hash, "delete"))
        {
            ptrArray_removeSorted(&g_idx, idxOld);
            idxList_DeleteItem(idxOld);
            free(idxOld);
        }
        else idxList_UpdateItem(idxOld);
    }
    else
    {
        ptrArray_insertSorted(&g_idx, idx);
        idxList_InsertItem(idx);
    }

}

void CALLBACK MsgSocketCB(UCHAR *msgData, int msgLen)
{
    if (!msgData)
    {
        PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
        return;
    }

    if (*(msgData+4) != CMD_ADMIN) return;

    switch (*(msgData+5))
    {
    case GM_GET_SVR_IDX_LIST_RESP:
        if (msgLen >= 10) _OnSvrIdxListResp(msgData, msgLen);
        break;

    case GM_SVR_IDX_CHANGED:
        if (msgLen >= 10) _OnSvrIdxListChanged(msgData, msgLen);
        break;

    default:
        break;
    }
}

