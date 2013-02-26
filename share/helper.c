#include "ptrList.h"
#include "helper.h"

UINT64 FileGetLastWriteTime(HANDLE hFile)
{
    ULARGE_INTEGER li;
    FILETIME lastWriteTime;

    if (!GetFileTime(hFile, NULL, NULL, &lastWriteTime)) return 0;

    li.LowPart = lastWriteTime.dwLowDateTime;
    li.HighPart = lastWriteTime.dwHighDateTime;
    return li.QuadPart;
}

void FileSetLastWriteTime(HANDLE hFile, UINT64 t)
{
    ULARGE_INTEGER li;
    FILETIME ft;

    li.QuadPart = t;
    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;
    SetFileTime(hFile, &ft, NULL, &ft);
}

void FindFiles(struct find_files *ff)
{
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    WCHAR szSearch[MAX_PATH], dir0[MAX_PATH], dir1[MAX_PATH];
    struct ptrList *dirs = NULL, *list;
    BOOL exist;
    struct find_files_stat st = { 0 };

    st.cbParam = ff->cbParam;
    st.pszDirGoingTo = ff->dir;
    if (!ff->findFilesCB(&st)) return;

    if (!wcslen(ff->dir)) return;

    wcscpy_s(dir0, MAX_PATH, ff->dir);
    if (dir0[wcslen(dir0)-1] == L'\\')
        dir0[wcslen(dir0)-1] = 0;
    swprintf_s(szSearch, MAX_PATH, L"%s\\*", dir0);
    hFind = FindFirstFileW(szSearch, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    while (1)
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (wcscmp(ffd.cFileName, L".") && wcscmp(ffd.cFileName, L".."))
            {
                swprintf_s(dir1, MAX_PATH, L"%s\\%s", dir0, ffd.cFileName);
                ptrList_append(&dirs, _wcsdup(dir1));

                for (list=*ff->directories, exist=FALSE; list; list=list->next)
                { if (_wcsicmp((WCHAR *)list->data, dir1) == 0) { exist = TRUE; break; } }
                if (!exist) ptrList_append(ff->directories, _wcsdup(dir1));

                st.pszDirFound = dir1;
                st.pszFileFound = NULL;
            }
        }
        else
        {
            swprintf_s(dir1, MAX_PATH, L"%s\\%s", dir0, ffd.cFileName);
            ptrList_append(ff->files, _wcsdup(dir1));

            st.pszDirFound = NULL;
            st.pszFileFound = dir1;
        }

        if (!ff->findFilesCB(&st)) break;

        if (!FindNextFileW(hFind, &ffd)) break;
    }
    FindClose(hFind);

    while (1)
    {
        struct find_files ffNext;

        ffNext = *ff;
        ffNext.dir = (WCHAR *)ptrList_pop_front(&dirs);
        if (!ffNext.dir) break;
        FindFiles(&ffNext);
        free(ffNext.dir);
    }

    ptrList_free(&dirs, free);
}

static int dirCmpFunc(const void *p1, const void *p2) /* ptrList_sort */
{
    return ((int)wcslen(*((WCHAR **)p2)) - (int)wcslen(*((WCHAR **)p1)));
}

static BOOL CALLBACK FindFileCB(struct find_files_stat *ffs)
{
    return TRUE;
}

BOOL SureDeleteDir(const WCHAR *szDir)
{
    struct find_files ff;
    struct ptrList *files = NULL;
    struct ptrList *directories = NULL;
    struct ptrList *list;
    WCHAR dir[MAX_PATH];
    DWORD dwAttr;

    wcscpy_s(dir, MAX_PATH, szDir);
    if (dir[wcslen(dir)-1] == L'\\')
        dir[wcslen(dir)-1] = 0;
    dwAttr = GetFileAttributesW(dir);
    if (!(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;

    memset(&ff, 0, sizeof(ff));
    ff.files = &files;
    ff.directories = &directories;
    ff.dir = dir;
    ff.findFilesCB = FindFileCB;
    ff.cbParam = NULL;
    FindFiles(&ff);

    for (list=files; list; list=list->next)
        DeleteFileW((WCHAR *)list->data);

    ptrList_sort(directories, dirCmpFunc);
    for (list=directories; list; list=list->next)
        RemoveDirectoryW((WCHAR *)list->data);
    RemoveDirectoryW(dir);

    ptrList_free(&files, free);
    ptrList_free(&directories, free);

    //{
    //    SHFILEOPSTRUCT fileOp;

    //    memset(&fileOp, 0, sizeof(SHFILEOPSTRUCT));
    //    fileOp.hwnd = NULL;
    //    fileOp.wFunc = FO_DELETE;
    //    dir[strlen(dir)+1] = 0;
    //    fileOp.pFrom = dir;
    //    fileOp.pTo = "\0";
    //    fileOp.fFlags = FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
    //    SHFileOperation(&fileOp);
    //}
    return TRUE;
}

// Create directory like C:\dir1\dir2\dir3\filename.txt
BOOL SureCreateDir(const WCHAR *fullPath)
{
    WCHAR path[MAX_PATH];
    const WCHAR *p;
    DWORD dwAttr, needCreate;
    BOOL success = TRUE;

    p = fullPath;
    p = wcschr(p, L'\\');
    if (!p) return FALSE;
    p ++;
    while (1)
    {
        p = wcschr(p, L'\\');
        if (!p) break;
        wcscpy_s(path, MAX_PATH, fullPath);
        path[(int)(p-fullPath)] = 0;
        DeleteFileW(path); // If a file exist, delete it!

        dwAttr = GetFileAttributesW(path);
        needCreate = 1;
        if (dwAttr != INVALID_FILE_ATTRIBUTES)
        {
            if (dwAttr & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(path, dwAttr&~FILE_ATTRIBUTE_READONLY);

            if (dwAttr & FILE_ATTRIBUTE_DIRECTORY)
                needCreate = 0;
            else DeleteFileW(path);
        }

        if (needCreate &&
            !CreateDirectoryW(path, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            success = FALSE;

        p ++;
    }
    return success;
}

HANDLE SureCreateFile(const WCHAR *fileName)
{
    DWORD dwAttr;

    dwAttr = GetFileAttributesW(fileName);
    if (dwAttr != INVALID_FILE_ATTRIBUTES)
    {
        if (dwAttr & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(fileName, dwAttr & ~FILE_ATTRIBUTE_READONLY);
        if (dwAttr & FILE_ATTRIBUTE_DIRECTORY && !SureDeleteDir(fileName))
            return INVALID_HANDLE_VALUE;
    }

    SureCreateDir(fileName);
    return CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
}

// C:\dir1\dir2\abc.rxt:
// C:\dir1\dir2
// abc
// txt
BOOL ParseFilePath(const WCHAR *filePath, WCHAR *dir, WCHAR *fileName, WCHAR *fileExt)
{
    WCHAR filePath1[MAX_PATH], dir1[MAX_PATH], fileName1[MAX_PATH], fileExt1[MAX_PATH];
    int len = (int)wcslen(filePath), i;

    wcscpy_s(filePath1, MAX_PATH, filePath);
    dir1[0] = fileName1[0] = fileExt1[0] = 0;

    for (i=len-1; i>=0; i--)
    {
        if (filePath1[i] == L'.')
        {
            wcscpy_s(fileExt1, MAX_PATH, filePath1+i+1);
            filePath1[i] = 0;
            break;
        }
    }
    for (; i>=0; i--)
    {
        if (filePath1[i] == L'\\' || filePath1[i] == L'/')
        {
            wcscpy_s(fileName1, MAX_PATH, filePath1+i+1);
            filePath1[i+1] = 0;
            break;
        }
    }
    if (!fileName1[0] || !fileExt1[0])
    {
        if (dir) dir[0] = 0;
        if (fileName) fileName[0] = 0;
        if (fileExt) fileExt[0] = 0;
        return FALSE;
    }
    if (dir) wcscpy_s(dir, MAX_PATH, filePath1);
    if (fileName) wcscpy_s(fileName, MAX_PATH, fileName1);
    if (fileExt) wcscpy_s(fileExt, MAX_PATH, fileExt1);
    return TRUE;
}

UCHAR *GetFileContent(const WCHAR *path, DWORD *size)
{
    HANDLE hFile;
    UCHAR *buf;
    LARGE_INTEGER li;
    DWORD dwRead;

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart > 50*1024*1024)
    {
        CloseHandle(hFile);
        return NULL;
    }
    buf = (UCHAR *)malloc((size_t)li.QuadPart + 80);
    if (!buf) return NULL;
    if (!ReadFile(hFile, buf, (DWORD)li.QuadPart, &dwRead, NULL) || dwRead != li.QuadPart)
    {
        CloseHandle(hFile);
        free(buf);
        return NULL;
    }
    buf[dwRead] = 0;
    CloseHandle(hFile);
    if (size) *size = (DWORD)li.QuadPart;
    return (buf);
}

BOOL SetFileContent(const WCHAR *path, void *buf, DWORD size)
{
    HANDLE hFile;
    DWORD dwWritten;

    hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile(hFile, buf, size, &dwWritten, NULL) || dwWritten != size)
    {
        CloseHandle(hFile);
        return FALSE;
    }
    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

CHAR *UnicodeToMbcs(const WCHAR *in_, CHAR *out_, int out_len_)
{
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = WideCharToMultiByte(CP_ACP, 0, in_, -1, NULL, 0, NULL, NULL);
    if (out_len_ < size+1) { out_[0] = 0; return out_; }
    WideCharToMultiByte(CP_ACP, 0, in_, -1, out_, out_len_, NULL, NULL);

    return out_;
}

WCHAR *MbcsToUnicode(const CHAR *in_, WCHAR *out_, int out_len_)
{
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = MultiByteToWideChar(CP_ACP, 0, in_, -1, NULL, 0);
    if (out_len_ < size+1) { out_[0] = 0; return out_; }
    MultiByteToWideChar(CP_ACP, 0, in_, -1, out_, out_len_);

    return out_;
}

CHAR *UnicodeToUtf8(const WCHAR *in_, CHAR *out_, int out_len_)
{
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = WideCharToMultiByte(CP_UTF8, 0, in_, -1, NULL, 0, NULL, NULL);
    if (out_len_ < size+1) { out_[0] = 0; return out_; }
    WideCharToMultiByte(CP_UTF8, 0, in_, -1, out_, out_len_, NULL, NULL);

    return out_;
}

CHAR *MbcsToUtf8(const CHAR *in_, CHAR *out_, int out_len_)
{
    WCHAR *wstr;
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = MultiByteToWideChar(CP_ACP, 0, in_, -1, NULL, 0);
    wstr = malloc(sizeof(WCHAR)*(size+1));
    MultiByteToWideChar(CP_ACP, 0, in_, -1, wstr, size);

    size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (out_len_ < size+1) { free(wstr); out_[0] = 0; return out_; }
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out_, out_len_, NULL, NULL);
    free(wstr);

    return out_;
}

WCHAR *Utf8ToUnicode(const CHAR *in_, WCHAR *out_, int out_len_)
{
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = MultiByteToWideChar(CP_UTF8, 0, in_, -1, NULL, 0);
    if (out_len_ < size+1) { out_[0] = 0; return out_; }
    MultiByteToWideChar(CP_UTF8, 0, in_, -1, out_, out_len_);

    return out_;
}

CHAR *Utf8ToMbcs(const CHAR *in_, CHAR *out_, int out_len_)
{
    WCHAR *wstr;
    int size;

    out_[0] = 0;
    if (in_ == NULL || in_[0] == 0) return out_;

    size = MultiByteToWideChar(CP_UTF8, 0, in_, -1, NULL, 0);
    wstr = malloc(sizeof(wchar_t)*(size+1));
    MultiByteToWideChar(CP_UTF8, 0, in_, -1, wstr, size);

    size = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (out_len_ < size+1) { free(wstr); out_[0] = 0; return out_; }
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, out_, size, NULL, NULL);
    free(wstr);

    return out_;
}

TCHAR *MbcsToTSTR(const CHAR *in_, TCHAR *out_, int out_len_)
{
#ifdef UNICODE
    return MbcsToUnicode(in_, out_, out_len_);
#else
    out_[0] = 0;
    if (out_len_ <= (int)strlen(in_)) return out_;
    strcpy_s(out_, out_len_, in_);
    return out_;
#endif
}

CHAR *TSTRToMbcs(const TCHAR *in_, CHAR *out_, int out_len_)
{
#ifdef UNICODE
    return UnicodeToMbcs(in_, out_, out_len_);
#else
    out_[0] = 0;
    if (out_len_ <= (int)strlen(in_)) return out_;
    strcpy_s(out_, out_len_, in_);
    return out_;
#endif
}

TCHAR *UnicodeToTSTR(const WCHAR *in_, TCHAR *out_, int out_len_)
{
#ifdef UNICODE
    out_[0] = 0;
    if (out_len_ <= (int)wcslen(in_)) return out_;
    wcscpy_s(out_, out_len_, in_);
    return out_;
#else
    return UnicodeToMbcs(in_, out_, out_len_);
#endif
}

WCHAR *TSTRToUnicode(const TCHAR *in_, WCHAR *out_, int out_len_)
{
#ifdef UNICODE
    out_[0] = 0;
    if (out_len_ <= (int)wcslen(in_)) return out_;
    wcscpy_s(out_, out_len_, in_);
    return out_;
#else
    return MbcsToUnicode(in_, out_, out_len_);
#endif
}

TCHAR *Utf8ToTSTR(const CHAR *in_, TCHAR *out_, int out_len_)
{
#ifdef UNICODE
    return Utf8ToUnicode(in_, out_, out_len_);
#else
    return Utf8ToMbcs(in_, out_, out_len_);
#endif
}

CHAR *TSTRToUtf8(const TCHAR *in_, CHAR *out_, int out_len_)
{
#ifdef UNICODE
    return UnicodeToUtf8(in_, out_, out_len_);
#else
    return MbcsToUtf8(in_, out_, out_len_);
#endif
}

