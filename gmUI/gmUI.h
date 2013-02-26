#ifndef _GMUI_H
#define _GMUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "targetver.h"

#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <Commdlg.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <time.h>
#include <string.h>
#include <tchar.h>
#include <shlobj.h>

#include "ptrArray.h"
#include "ptrList.h"
#include "debugf.h"
#include "adminCli.h"
#include "helper.h"
#include "idx.h"

#include "SearchChinese.h"

#include "resource.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls'"\
" version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib,"ws2_32")


#define ID_TREE             1000

#define ID_DOWNLOADING      1001
#define ID_NET_IDX          1002

#define ID_LOG              1003

#define ID_TOOLBAR          1007
#define ID_SEARCH           1008
#define ID_STATUSBAR        1009

#define PAGE_DOWNLOADING    1
#define PAGE_NET_IDX        2
#define PAGE_LOCAL_IDX      3
#define PAGE_SEARCH         4

#define IDM_CHANGE_PAGE     10

extern const TCHAR *g_szAppName;

extern WCHAR g_workDir[];
extern HINSTANCE g_hInstance;
extern HWND g_hWndMain, g_hWndTmpl, g_hWndFocus;
extern HWND g_hDlgWorking;
extern HWND g_hWndTree;
extern HWND g_hWndDownloading, g_hWndNetIdx;
extern HWND g_hWndLog;
extern HWND g_hWndToolBar, g_hWndSearch, g_hWndStatusBar;
extern int g_treeWidth;
extern int g_logHeight;
extern int g_statusHeight;
extern int g_toolHeight;
extern int g_currPage;
extern int g_currSubPage;
extern WCHAR g_currCategory[];
extern BOOL g_defFontCreated;
extern HFONT g_defFont;

extern CHAR g_serverIp[32];
extern WORD g_serverPort;
extern CHAR g_serversNotify[];
extern CHAR g_serverPwd[];

extern struct options g_options;


BOOL SaveOptions();
BOOL ReadOptions();
BOOL ReadCoreOptions();

BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK TmplDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK Quit(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK LoginDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK OptionsDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK UploadNewDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK UploadDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK UploadBatchDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PeerInfoDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK IdxInfoDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK WorkingDlgProc(HWND, UINT, WPARAM, LPARAM);

void CenterDlg(HWND hDlg);

void CreateFonts();
void DeleteFonts();

void CALLBACK MsgSocketCB(CHAR *msgData, int msgLen);


TCHAR *GetSizeString(INT64 size, TCHAR *sz);
static TCHAR *GetPieceSizeString(UINT32 size, TCHAR *sz);
TCHAR *GetTimeString(time_t t, TCHAR *sz);
TCHAR *GetPercentString(TCHAR szPercent[20], INT64 completed, INT64 total);
TCHAR *GetSpeedString(TCHAR szSpeed[64], int speed);

void SetListViewColumn(HWND hwndLV, int iCol, TCHAR *title, int width, int fmt);
BOOL InitListViewImageLists(HWND hwndLV);

TCHAR *GetActionString(struct idx_downloading *dn, TCHAR szAction[256]);
int GetActionImage(struct idx_downloading *dn);

#define ICON_DOWNLOADING    0
#define ICON_WAITING        1
#define ICON_UPLOADING      2
#define ICON_SEEDING        3
#define ICON_CHECKING       4
#define ICON_PAUSE          5
#define ICON_ERROR          6
#define ICON_NOTHING        7
#define ICON_EXCLAIM        8


#ifdef __cplusplus
}
#endif
#endif
