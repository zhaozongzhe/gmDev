#ifndef __HELPER_H_
#define __HELPER_H_

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static __inline DWORD CmpTickCount(DWORD tStart, DWORD tEnd)
{
    if (tEnd >= tStart) return (tEnd - tStart);
    else return (0xFFFFFFFF - tStart + tEnd);
}

UINT64 FileGetLastWriteTime(HANDLE hFile);
void FileSetLastWriteTime(HANDLE hFile, UINT64 t);

struct find_files_stat
{
    const WCHAR *pszFileFound;
    const WCHAR *pszDirFound;
    const WCHAR *pszDirGoingTo;
    void *cbParam;
};
// 回调函数, 返回值为FALSE表示终止操作
typedef BOOL (CALLBACK FINDFILESCB)(struct find_files_stat *);
struct find_files
{
    WCHAR *dir;
    struct ptrList **files;
    struct ptrList **directories;
    FINDFILESCB *findFilesCB; // 回调函数
    void *cbParam;
};
void FindFiles(struct find_files *ff);

BOOL SureDeleteDir(const WCHAR *szDir);

BOOL SureCreateDir(const WCHAR *fullPath);

HANDLE SureCreateFile(const WCHAR *fileName);

BOOL ParseFilePath(const WCHAR *filePath, WCHAR *dir, WCHAR *fileName, WCHAR *fileExt);

UCHAR *GetFileContent(const WCHAR *path, DWORD *size);
BOOL SetFileContent(const WCHAR *path, void *buf, DWORD size);


CHAR *UnicodeToMbcs(const WCHAR *in_, CHAR *out_, int out_len_);
WCHAR *MbcsToUnicode(const CHAR *in_, WCHAR *out_, int out_len_);

CHAR *UnicodeToUtf8(const WCHAR *in_, CHAR *out_, int out_len_);
CHAR *MbcsToUtf8(const CHAR *in_, CHAR *out_, int out_len_);

WCHAR *Utf8ToUnicode(const CHAR *in_, WCHAR *out_, int out_len_);
CHAR *Utf8ToMbcs(const CHAR *in_, CHAR *out_, int out_len_);

TCHAR *MbcsToTSTR(const CHAR *in_, TCHAR *out_, int out_len_);
CHAR *TSTRToMbcs(const TCHAR *in_, CHAR *out_, int out_len_);

TCHAR *UnicodeToTSTR(const WCHAR *in_, TCHAR *out_, int out_len_);
WCHAR *TSTRToUnicode(const TCHAR *in_, WCHAR *out_, int out_len_);

TCHAR *Utf8ToTSTR(const CHAR *in_, TCHAR *out_, int out_len_);
CHAR *TSTRToUtf8(const TCHAR *in_, CHAR *out_, int out_len_);

#ifdef __cplusplus
}
#endif

#endif
