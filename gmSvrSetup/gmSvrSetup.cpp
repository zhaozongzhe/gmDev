#include "gmSvrSetup.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'"\
    " version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const TCHAR *g_szAppName = _T("游戏更新系统 - 服务器安装");

HINSTANCE g_hInst;
HWND g_hwndMain;

BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK WelcomeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SetupServerDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

BOOL fwAddApp(const WCHAR *szFileName, const WCHAR *szRegisterName);
void fwRemoveApp(const WCHAR *szFileName);

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    WNDCLASSEX wcex;

    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = 0;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_GMSVRSETUP));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = _T("gmSvrSetupWindowClass");
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    RegisterClassEx(&wcex);

    if (!InitInstance (hInstance, nCmdShow)) return FALSE;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
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

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    RECT rcScr;
    int left, top, width, height;

    SetPrivilege();

    g_hInst = hInstance;

    if (!SystemParametersInfo(SPI_GETWORKAREA, sizeof(RECT), &rcScr, 0))
    {
        GetClientRect(GetDesktopWindow(), &rcScr);
        rcScr.bottom -= 50;
    }
    if (rcScr.right <= 800) { width = 640; height = 450; }
    else if (rcScr.right <= 1024) { width = 800; height = 580; }
    else { width = 1024; height = 720; }
    left = (rcScr.right-width)/2;
    top = (rcScr.bottom-height)/2;

    g_hwndMain = CreateWindow(_T("gmSvrSetupWindowClass"), g_szAppName,
        WS_OVERLAPPEDWINDOW, left, top, width, height, NULL, NULL, g_hInst, NULL);

    if (!g_hwndMain) return FALSE;

    ShowWindow(g_hwndMain, nCmdShow);

    UpdateWindow(g_hwndMain);

    PostMessage(g_hwndMain, WM_COMMAND, IDD_WELCOME, 0);

    return TRUE;
}

void _MainWnd_OnEraseBkgnd(HWND hwnd, HDC hDC)
{
    RECT rc, rc1;
    int r1=127,g1=127,b1=56; //Any start color
    int r2=5,g2=55,b2=165; //Any stop color
    int i;

    GetClientRect(hwnd, &rc);

    for(i=0; i<rc.bottom; i++)
    { 
        int r,g,b;
        r = r1 + (i * (r2-r1) / rc.bottom);
        g = g1 + (i * (g2-g1) / rc.bottom);
        b = b1 + (i * (b2-b1) / rc.bottom);

        SetBkColor(hDC, RGB(r,g,b));
        SetRect(&rc1, 0, i, rc.right, i+1);
        ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rc1, NULL, 0, NULL);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;

    switch (message)
    {
    case WM_ERASEBKGND:
        _MainWnd_OnEraseBkgnd(hWnd, (HDC)wParam);
        return 1;
    case WM_COMMAND:
        wmId = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        switch (wmId)
        {
        case IDD_WELCOME:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_WELCOME), hWnd, WelcomeDlgProc);
            break;
        case IDD_SETUP_SERVER:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SETUP_SERVER), hWnd, SetupServerDlgProc);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_DESTROY:
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

WCHAR *MbcsToUnicode(const CHAR *in_, WCHAR *out_, int out_len_)
{
    int size;

    if (in_ == NULL || in_[0] == 0) { out_[0] = 0; return out_; }

    size = MultiByteToWideChar(CP_ACP, 0, in_, -1, NULL, 0);
    if (out_len_ < size+1) { out_[0] = 0; return out_; }
    MultiByteToWideChar(CP_ACP, 0, in_, -1, out_, out_len_);

    return out_;
}
TCHAR *MbcsToTSTR(const CHAR *in_, TCHAR *out_, int out_len_)
{
#ifdef UNICODE
    return MbcsToUnicode(in_, out_, out_len_);
#else
    if (out_len_ <= (int)strlen(in_)) { out_[0] = 0; return out_; }
    strcpy_s(out_, out_len_, in_);
    return out_;
#endif
}

WCHAR *TSTRToUnicode(const TCHAR *in_, WCHAR *out_, int out_len_)
{
#ifdef UNICODE
    if (out_len_ <= (int)wcslen(in_)) { out_[0] = 0; return out_; }
    wcscpy_s(out_, out_len_, in_);
    return out_;
#else
    return MbcsToUnicode(in_, out_, out_len_);
#endif
}

static BOOL CopyResourceFile(const TCHAR *id, const TCHAR *szPath)
{
    HRSRC hRsrc;
    HGLOBAL hGlgl;
    CHAR *fileData;
    DWORD fileSize;
    TCHAR szTmp[256];
    HANDLE hFile;
    DWORD dwWritten;

    hRsrc = FindResource(NULL, id, _T("file"));
    if (!hRsrc) return FALSE;

    hGlgl= LoadResource(NULL, hRsrc);
    if (!hGlgl) return FALSE;

    fileData = (CHAR *)LockResource(hGlgl);
    fileSize = SizeofResource(NULL, hRsrc);

    _stprintf_s(szTmp, 256, _T("---resource loaded OK: %u, size: %u\r\n"), id, fileSize);
    OutputDebugString(szTmp);

    hFile = CreateFile(szPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile(hFile, fileData, fileSize, &dwWritten, NULL) || dwWritten != fileSize)
    {
        CloseHandle(hFile);
        return FALSE;
    }
    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

static BOOL _RemoveService(SC_HANDLE schSCManager, const TCHAR *svcName)
{
    SC_HANDLE schService;
    SERVICE_STATUS ssStatus = { 0 };
    BOOL bSuccess = FALSE;

    if ((schService = OpenService(schSCManager, svcName, DELETE|SERVICE_STOP|SERVICE_QUERY_STATUS)) != NULL)
    {
        if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus))
        {
            Sleep(1000);
            while(QueryServiceStatus(schService, &ssStatus))
            {
                if (ssStatus.dwCurrentState == SERVICE_STOPPED) break;
                else if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) Sleep(1000);
                else OutputDebugString(_T("status NOT stop\r\n"));
            }
        }
        bSuccess = DeleteService(schService);
        CloseServiceHandle(schService);
    }

    return bSuccess;
}

static BOOL _StopService(SC_HANDLE schSCManager, const TCHAR *svcName)
{
    SC_HANDLE schService;
    SERVICE_STATUS ssStatus = { 0 };
    BOOL bSuccess = FALSE;

    if ((schService = OpenService(schSCManager, svcName, DELETE|SERVICE_STOP|SERVICE_QUERY_STATUS)) != NULL)
    {
        if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus))
        {
            Sleep(1000);
            while(QueryServiceStatus(schService, &ssStatus))
            {
                if (ssStatus.dwCurrentState == SERVICE_STOPPED) break;
                else if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) Sleep(1000);
                else OutputDebugString(_T("status NOT stop\r\n"));
            }
        }
        bSuccess = ssStatus.dwCurrentState == SERVICE_STOPPED;
        CloseServiceHandle(schService);
    }

    return bSuccess;
}

static BOOL _StartService(SC_HANDLE schSCManager, const TCHAR *svcName)
{
    SC_HANDLE schService;
    SERVICE_STATUS ssStatus = { 0 };
    BOOL bRunning = FALSE;

    if ((schService = OpenService(schSCManager, svcName, SERVICE_ALL_ACCESS)) != NULL)
    {
        if (StartService(schService, 0, NULL))
        {
            Sleep(1000);
            while (QueryServiceStatus(schService, &ssStatus))
            {
                if (ssStatus.dwCurrentState == SERVICE_RUNNING) break;
                else if (ssStatus.dwCurrentState == SERVICE_START_PENDING) Sleep(1000);
                else OutputDebugString(_T("status NOT run\r\n"));
            }
        }
        else
        {
            TCHAR szTmp[256];
            _stprintf_s(szTmp, 256, _T("_StartService StartService failed %d\r\n"), GetLastError());
            OutputDebugString(szTmp);
        }
        bRunning = (ssStatus.dwCurrentState == SERVICE_RUNNING);
        CloseServiceHandle(schService);
    }
    else OutputDebugString(_T("StartService OpenService failed\r\n"));

    return bRunning;
}

static BOOL _InstallService(SC_HANDLE schSCManager, const TCHAR *svcName,
                            const TCHAR *svcDesc, const TCHAR *szPath,
                            const TCHAR *svcDesc2)
{
    SC_HANDLE schService;
    SERVICE_STATUS ssStatus = { 0 };
    SERVICE_DESCRIPTION ssDesc = { 0 };
    BOOL bSuccess = FALSE;

    schService = CreateService(schSCManager,
        svcName, svcDesc,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        szPath, NULL, NULL, NULL, NULL, NULL);
    if (schService)
    {
        OutputDebugString(_T("service installed\r\n"));
        ssDesc.lpDescription = (TCHAR *)svcDesc2;
        ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &ssDesc);
        if (StartService(schService, 0, NULL))
        {
            Sleep(1000);
            while (QueryServiceStatus(schService, &ssStatus))
            {
                if (ssStatus.dwCurrentState == SERVICE_RUNNING) break;
                else if (ssStatus.dwCurrentState == SERVICE_START_PENDING) Sleep(1000);
                else OutputDebugString(_T("status NOT run\r\n"));
            }
        }
        else
        {
            TCHAR szTmp[256];
            _stprintf_s(szTmp, 256, _T("_StartService StartService failed %d\r\n"), GetLastError());
            OutputDebugString(szTmp);
        }
        bSuccess = (ssStatus.dwCurrentState == SERVICE_RUNNING);
        CloseServiceHandle(schService);
    }

    return bSuccess;
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
//static BOOL ini_writeWstr(HANDLE hFile, const CHAR *key, const WCHAR *val)
//{
//    DWORD dwWritten, bufLen;
//    CHAR buf[1024], szTmp[512];
//
//    bufLen = sprintf_s(buf, 1024, "%s=%s\r\n", key, UnicodeToMbcs(val, szTmp, 512));
//    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
//}

BOOL SaveServerOptions(const TCHAR *szPath, UINT16 port)
{
    HANDLE hFile;
    TCHAR fileName[MAX_PATH];

    _stprintf_s(fileName, MAX_PATH, _T("%s\\options.txt"), szPath);
    hFile = CreateFile(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    ini_writeUint(hFile, "port_num", port);

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

static TCHAR *GetSpecialFolderLocation(int nFolder, TCHAR szPath[MAX_PATH])
{
    LPITEMIDLIST pidl;
    HRESULT hr;

    szPath[0] = 0;

    hr = SHGetSpecialFolderLocation(NULL, nFolder, &pidl);
    if (SUCCEEDED(hr)) SHGetPathFromIDList(pidl, szPath);

    return szPath;
}

static void _CreateShortcut(const TCHAR *ExePath, const TCHAR *LinkFilename,
    const TCHAR *WorkingDirectory, const TCHAR *Description, int nFolder)
{
    IShellLink* psl;
    HRESULT hres;
    TCHAR szPath[MAX_PATH], PathLink[MAX_PATH];

    GetSpecialFolderLocation(nFolder, szPath);

    _tcscat_s(szPath, MAX_PATH, _T("\\网游更新服务器"));
    CreateDirectory(szPath, NULL);

    _stprintf_s(PathLink, _T("%s\\%s.lnk"), szPath, LinkFilename);

    hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (PVOID *)&psl); 
    if (SUCCEEDED(hres)) 
    { 
        IPersistFile* ppf; 

        psl->SetPath(ExePath); 
        psl->SetWorkingDirectory(WorkingDirectory);
        psl->SetDescription(Description); 

        hres = psl->QueryInterface(IID_IPersistFile, (PVOID *)&ppf); 
        if (SUCCEEDED(hres)) 
        {
            WCHAR wszTemp[MAX_PATH];

            TSTRToUnicode(PathLink, wszTemp, MAX_PATH);
            hres = ppf->Save(wszTemp, TRUE); 
            ppf->Release(); 
        } 
        psl->Release(); 
    } 
}

static BOOL RemoveServer(HWND hDlg)
{
    const TCHAR *svcName = _T("gmSvr");
    SC_HANDLE schSCManager, schService;
    DWORD cbBufSize, dwBytesNeeded;
    LPQUERY_SERVICE_CONFIG lpsc;
    WCHAR szTmp[MAX_PATH], szFileName[MAX_PATH], *pTmp;
    HKEY hKey;
    const TCHAR *szKey = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");

    if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_LOCAL_MACHINE,
        szKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegDeleteKey(hKey, _T("gmSvr"));
        RegCloseKey(hKey);
    }

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        MessageBox(hDlg, _T("安装过程出错：无法打开服务管理器。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }

    schService = OpenService(schSCManager, svcName, SERVICE_QUERY_CONFIG);
    if (schService)
    {
        if (!QueryServiceConfig(schService, NULL, 0, &dwBytesNeeded) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            cbBufSize = dwBytesNeeded;
            lpsc = (LPQUERY_SERVICE_CONFIG)malloc(cbBufSize);
            if (QueryServiceConfig(schService, lpsc, cbBufSize, &dwBytesNeeded))
            {
                TSTRToUnicode(lpsc->lpBinaryPathName, szTmp, MAX_PATH);
                pTmp = wcsstr(szTmp, L" -service");
                if (pTmp) { pTmp --; *pTmp = 0; }
                wcscpy_s(szFileName, MAX_PATH, szTmp+1);

                fwRemoveApp(szFileName);
            }
            free(lpsc);
        }

        CloseServiceHandle(schService);
    }


    _RemoveService(schSCManager, svcName);

    CloseServiceHandle(schSCManager);

    GetSpecialFolderLocation(CSIDL_PROGRAMS, szTmp);
    _tcscat_s(szTmp, MAX_PATH, _T("\\网游更新服务器"));

    _stprintf_s(szFileName, _T("%s\\网游更新服务器控制台.lnk"), szTmp);
    DeleteFile(szFileName);
    _stprintf_s(szFileName, _T("%s\\卸载.lnk"), szTmp);
    DeleteFile(szFileName);

    RemoveDirectory(szTmp);

    MessageBox(hDlg, _T("gmSvr服务已删除。\r\n您需要手动删除目录。"), g_szAppName, MB_OK|MB_ICONINFORMATION);
    return TRUE;
}

static BOOL InstallServer(const TCHAR *szPath, UINT16 port, HWND hDlg)
{
    const TCHAR *svcName = _T("gmSvr");
    const TCHAR *svcDesc = _T("gmSvr service");
    const TCHAR *svcExec = _T("gmSvr.exe");
    const TCHAR *uiExec = _T("gmSvrUI.exe");
    TCHAR szFileName[MAX_PATH];
    WCHAR wszFileName[MAX_PATH];
    SC_HANDLE schSCManager;
    HKEY hKey;
    DWORD dwTmp;
    const TCHAR *szKey = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\gmSvr");

    CreateDirectory(szPath, NULL);

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        MessageBox(hDlg, _T("安装过程出错：无法打开服务管理器。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }

    _RemoveService(schSCManager, svcName);

    _stprintf_s(szFileName, MAX_PATH, _T("%s\\%s"), szPath, svcExec);
    if (!CopyResourceFile(_T("SVR"), szFileName))
    {
        MessageBox(hDlg, _T("安装过程出错：无法复制文件。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }
    _stprintf_s(szFileName, MAX_PATH, _T("%s\\%s"), szPath, uiExec);
    if (!CopyResourceFile(_T("SVRUI"), szFileName))
    {
        MessageBox(hDlg, _T("安装过程出错：无法复制文件。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }
    _stprintf_s(szFileName, MAX_PATH, _T("%s\\uninstall.exe"), szPath);
    if (!CopyResourceFile(_T("UNINSTALL"), szFileName))
    {
        MessageBox(hDlg, _T("安装过程出错：无法复制文件。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }

    fwAddApp(TSTRToUnicode(szFileName, wszFileName, MAX_PATH), L"gmSvr");

    if (!SaveServerOptions(szPath, port))
    {
        MessageBox(hDlg, _T("安装过程出错：无法创建ini文件。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }

    _stprintf_s(szFileName, MAX_PATH, _T("\"%s\\%s\" -service"), szPath, svcExec);
    if (!_InstallService(schSCManager, svcName, svcDesc, szFileName,
        _T("为游戏更新客户端提供种子文件存取服务、节点交换服务，以及动态补种调度等")))
    {
        CloseServiceHandle(schSCManager);
        MessageBox(hDlg, _T("安装过程出错：无法安装gmSvr服务。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }

    CloseServiceHandle(schSCManager);

    MessageBox(hDlg, _T("安装成功完成！\r\ngmSvr服务已启动。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);

    _stprintf_s(szFileName, MAX_PATH, _T("\"%s\\%s\""), szPath, uiExec);
    ShellExecute(NULL, _T("open"), szFileName, NULL, NULL, SW_SHOWNORMAL);

    if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_LOCAL_MACHINE,
        szKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegSetValueEx(hKey, _T("DisplayName"), NULL, REG_SZ, (BYTE *)(_T("gmSvr")), 5*sizeof(TCHAR));
        RegSetValueEx(hKey, _T("DisplayVersion"), NULL, REG_SZ, (BYTE *)(_T("1.0")), 3*sizeof(TCHAR));
        RegSetValueEx(hKey, _T("Publisher"), NULL, REG_SZ, (BYTE *)(_T("691833901@qq.com")), 16*sizeof(TCHAR));
        dwTmp = 1;
        RegSetValueEx(hKey, _T("NoModify"), NULL, REG_DWORD, (BYTE *)&dwTmp, sizeof(DWORD));
        RegSetValueEx(hKey, _T("NoRepair"), NULL, REG_DWORD, (BYTE *)&dwTmp, sizeof(DWORD));
        _stprintf_s(szFileName, MAX_PATH, _T("\"%s\\uninstall.exe\""), szPath);
        RegSetValueEx(hKey, _T("UninstallString"), NULL, REG_SZ,
            (BYTE *)szFileName, _tcslen(szFileName)*sizeof(TCHAR));
        RegCloseKey(hKey);
    }

    _stprintf_s(szFileName, MAX_PATH, _T("\"%s\\gmSvrUI.exe\""), szPath);
    _CreateShortcut(szFileName, _T("网游更新服务器控制台"), szPath, _T("网游更新服务器控制台程序"), CSIDL_PROGRAMS);

    _stprintf_s(szFileName, MAX_PATH, _T("\"%s\\uninstall.exe\""), szPath);
    _CreateShortcut(szFileName, _T("卸载"), szPath, _T("卸载网游更新服务器"), CSIDL_PROGRAMS);

    return TRUE;
}

static void LoadIntroText(HWND hDlg)
{
    HRSRC hRsrc;
    HGLOBAL hGlgl;
    CHAR *fileData, *szIntro;
    DWORD fileSize;
    TCHAR *tszIntro;
    DWORD introSize;

    hRsrc = FindResource(NULL, _T("INTRO"), _T("file"));
    if (!hRsrc) return;

    hGlgl= LoadResource(NULL, hRsrc);
    if (!hGlgl) return;

    fileData = (CHAR *)LockResource(hGlgl);
    fileSize = SizeofResource(NULL, hRsrc);

    szIntro = (CHAR *)malloc(fileSize);
    memcpy(szIntro, fileData, fileSize);
    szIntro[fileSize] = 0;

    introSize = (fileSize+100);
    tszIntro = (TCHAR *)malloc(introSize*sizeof(TCHAR));
    MbcsToTSTR(szIntro, tszIntro, introSize);

    SetDlgItemText(hDlg, IDC_WELCOME, tszIntro);
    SendDlgItemMessage(hDlg, IDC_WELCOME, EM_SETSEL, 0, 0);
    free(tszIntro);
    free(szIntro);
}

INT_PTR CALLBACK WelcomeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        LoadIntroText(hDlg);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            PostMessage(g_hwndMain, WM_COMMAND, IDD_SETUP_SERVER, 0);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK SetupServerDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR szPath[MAX_PATH];
    UINT16 port;

    switch (message)
    {
    case WM_INITDIALOG:
        CenterDlg(hDlg);
        SetDlgItemText(hDlg, IDC_PATH, _T("c:\\gmSvr"));
        SetDlgItemInt(hDlg, IDC_PORT, 8900, FALSE);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE:
            if (BrowseForDir(hDlg, szPath))
                SetDlgItemText(hDlg, IDC_PATH, szPath);
            break;
        case IDC_REMOVE:
            RemoveServer(hDlg);
            break;
        case IDOK:
            GetDlgItemText(hDlg, IDC_PATH, szPath, MAX_PATH);
            port = (UINT16)GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
            if (!szPath[0]) { MessageBox(hDlg, _T("必须选择安装目录!"), g_szAppName, MB_OK|MB_ICONEXCLAMATION); break; }
            if (!port) { MessageBox(hDlg, _T("必须选择端口号!"), g_szAppName, MB_OK|MB_ICONEXCLAMATION); break; }
            if (!InstallServer(szPath, port, hDlg)) break;
        case IDCANCEL:
            PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
