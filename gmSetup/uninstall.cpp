#include <winsock2.h>
#include <windows.h>
#include <TCHAR.h>
#include <Shlobj.h>
#include "fwCtl.h"
#include "resource.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'"\
    " version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const TCHAR *g_szAppName = _T("网游更新系统 - 卸载");

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

static BOOL RemoveClient(HWND hDlg)
{
    const TCHAR *svcName = _T("gmCore");
    SC_HANDLE schSCManager, schService;
    DWORD cbBufSize, dwBytesNeeded;
    LPQUERY_SERVICE_CONFIG lpsc;
    WCHAR szTmp[MAX_PATH], szFileName[MAX_PATH], *pTmp;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        MessageBox(hDlg, _T("卸载过程出错：无法打开服务管理器。\r\n"), g_szAppName, MB_OK|MB_ICONINFORMATION);
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

    MessageBox(hDlg, _T("gmCore服务已删除。\r\n您需要手动删除目录。"), g_szAppName, MB_OK|MB_ICONINFORMATION);
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

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine,
                       int nCmdShow)
{
    HKEY hKey;
    TCHAR szPath[MAX_PATH], szFileName[MAX_PATH];
    const TCHAR *szKey = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");

    CoInitialize(NULL);

    if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_LOCAL_MACHINE,
        szKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL))
    {
        RegDeleteKey(hKey, _T("gm"));
        RegCloseKey(hKey);
    }

    RemoveClient(NULL);

    GetSpecialFolderLocation(CSIDL_PROGRAMS, szPath);
    _tcscat_s(szPath, MAX_PATH, _T("\\网游更新"));

    _stprintf_s(szFileName, _T("%s\\网游更新控制台.lnk"), szPath);
    DeleteFile(szFileName);
    _stprintf_s(szFileName, _T("%s\\卸载.lnk"), szPath);
    DeleteFile(szFileName);

    RemoveDirectory(szPath);

    CoUninitialize();

    return 0;
}

