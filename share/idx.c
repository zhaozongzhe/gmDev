#include "idx.h"
#include "debugf.h"


static DWORD idx_calcPieceLen(INT64 totalSize)
{
    #define SIZE_128_KB     128*1024
    #define SIZE_4_MB       4*1024*1024
    DWORD dwBlockSize = SIZE_128_KB;

    totalSize /= 1024;
    while ((totalSize >= dwBlockSize) && (dwBlockSize < SIZE_4_MB))
        dwBlockSize *= 2;
    if (dwBlockSize < SIZE_128_KB)
        dwBlockSize = SIZE_128_KB;
    return dwBlockSize;
}

// create struct file while create idx
static struct file *file_new(const WCHAR *fileName)
{
    HANDLE hFile;
    struct file *sf;
    LARGE_INTEGER liSize;
    ULARGE_INTEGER liTime;
    DWORD dwAttr;
    FILETIME writeTime;

    hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(hFile, &liSize))
    { CloseHandle(hFile); return NULL; }

    sf = malloc(sizeof(struct file));
    if (!sf) return NULL;
    memset(sf, 0, sizeof(struct file));
    wcscpy_s(sf->fileName, MAX_PATH, fileName);
    sf->bytes = (INT64)liSize.QuadPart;
    dwAttr = GetFileAttributesW(fileName);
    /* 0x0001-只读、0x0002-系统、0x0004-隐藏 */
    if (dwAttr & FILE_ATTRIBUTE_READONLY) sf->fileAttr |= 0x0001;
    if (dwAttr & FILE_ATTRIBUTE_SYSTEM) sf->fileAttr |= 0x0002;
    if (dwAttr & FILE_ATTRIBUTE_HIDDEN) sf->fileAttr |= 0x0004;
    GetFileTime(hFile, NULL, NULL, &writeTime);
    liTime.LowPart = writeTime.dwLowDateTime;
    liTime.HighPart = writeTime.dwHighDateTime;
    sf->fileTime = (UINT64)liTime.QuadPart;

    CloseHandle(hFile);
    return sf;
}

static void file_free(void *sf)
{
    if (((struct file *)sf)->hash) free(((struct file *)sf)->hash);
    free((struct file *)sf);
}

__inline static BOOL idx_IsIdxFile(WCHAR *fileName)
{
    int i;
    for (i=(int)wcslen(fileName); i>0; i--)
        if (fileName[i] == L'.')
            return (0==_wcsicmp(&fileName[i], IDX_EXTNAME));
    return FALSE;
}

// string in indxFile: always utf-8
#define IDXFILE_SIGNATURE       "idxFile 1.0\r\n\0\0\0"

static BOOL idx_WriteData(HANDLE hFile, const UCHAR *data, int dataLen)
{
    DWORD dwWritten;
    return (WriteFile(hFile, data, dataLen, &dwWritten, NULL) &&
        dwWritten == dataLen);
}
static BOOL idx_WriteStringA(HANDLE hFile, const CHAR *str)
{
    DWORD strLen, dwWritten;

    strLen = (DWORD)strlen(str)+1;
    return (WriteFile(hFile, str, strLen, &dwWritten, NULL) &&
        dwWritten == strLen);
}
static BOOL idx_WriteStringW(HANDLE hFile, const WCHAR *str)
{
    DWORD dwWritten;
    int utf8Len;
    CHAR *utf8;
    BOOL success;

    utf8Len = 6*wcslen(str)+1;
    utf8 = (CHAR *)malloc(utf8Len);
    UnicodeToUtf8(str, utf8, utf8Len);
    utf8Len = strlen(utf8)+1;
    success = (WriteFile(hFile, utf8, utf8Len, &dwWritten, NULL) &&
        dwWritten == utf8Len);
    free(utf8);
    return success;
}
static BOOL idx_WriteInt64(HANDLE hFile, INT64 i64)
{
    DWORD dwWritten, len;
    CHAR szTmp[256];
    len = 1 + sprintf_s(szTmp, 256, "%I64d", i64);
    return (WriteFile(hFile, szTmp, len, &dwWritten, NULL) &&
        dwWritten == len);
}
static BOOL idx_WriteUInt32(HANDLE hFile, UINT32 ui32)
{
    DWORD dwWritten, len;
    CHAR szTmp[256];
    len = 1 + sprintf_s(szTmp, 256, "%u", ui32);
    return (WriteFile(hFile, szTmp, len, &dwWritten, NULL) &&
        dwWritten == len);
}
static BOOL idx_WriteFileInfo(HANDLE hFile, struct file *fi)
{
    if (!idx_WriteStringW(hFile, fi->fileName)) return FALSE;
    if (!idx_WriteInt64(hFile, fi->bytes)) return FALSE;
    if (!idx_WriteUInt32(hFile, fi->pieceCount)) return FALSE;
    if (!idx_WriteUInt32(hFile, fi->fileAttr)) return FALSE;
    if (!idx_WriteInt64(hFile, fi->fileTime)) return FALSE;
    if (fi->pieceCount)
        return idx_WriteData(hFile, fi->hash, 20*fi->pieceCount);
    return TRUE;
}

static void encryptIdxFile(HANDLE hFile)
{
    arc4_context ctx;
    UCHAR buf[4096], hash[20];
    DWORD tick, dwRead, dwWritten, pt, crc;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, buf, 64, &dwRead, NULL) || dwRead!=64) return;
    tick = (DWORD)strtoul((CHAR *)(buf+16), NULL, 10);

    sprintf_s((CHAR *)buf, 64, "%u", tick);
    sha1(buf, strlen((CHAR *)buf)+1, hash);

    arc4_setup(&ctx, hash, 20);

    crc = crc32(0L, Z_NULL, 0);

    pt = 64;
    while (1)
    {
        if (!ReadFile(hFile, buf, 4096, &dwRead, NULL) || !dwRead) break;

        crc = crc32(crc, buf, dwRead);

        arc4_crypt(&ctx, dwRead, buf, buf);

        SetFilePointer(hFile, pt, NULL, FILE_BEGIN);
        if (!WriteFile(hFile, buf, dwRead, &dwWritten, NULL) ||
            dwWritten != dwRead) break;

        if (dwRead < 4096) break;
        pt += dwRead;
    }

    memset(buf, 0, 16);
    sprintf_s((CHAR *)buf, 16, "%u", crc);
    SetFilePointer(hFile, 32, NULL, FILE_BEGIN);
    WriteFile(hFile, buf, 16, &dwWritten, NULL);
}

static void encryptIdxData(UCHAR *data, int dataLen)
{
    arc4_context ctx;
    unsigned char buf[100], hash[20];
    DWORD tick, crc;

    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, data+64, dataLen-64);
    sprintf_s((CHAR *)(data+32), 16, "%u", crc);

    tick = (DWORD)strtoul((CHAR *)(data+16), NULL, 10);
    if (tick)
    {
        sprintf_s((CHAR *)buf, 64, "%u", tick);
        sha1(buf, strlen((CHAR *)buf)+1, hash);

        arc4_setup(&ctx, hash, 20);
        arc4_crypt(&ctx, dataLen-64, data+64, data+64);
    }
}

BOOL idx_save(WCHAR *fileName, UCHAR *data, int dataLen)
{
    encryptIdxData(data, dataLen);
    return SetFileContent(fileName, data, dataLen);
}

static BOOL idx_Write(const WCHAR *fileName, struct idx *idx)
{
    HANDLE hFile;
    DWORD dwWritten, tick, i;
    CHAR szTmp[64];

    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (!WriteFile(hFile, IDXFILE_SIGNATURE, 16, &dwWritten, NULL) || dwWritten != 16) { CloseHandle(hFile); return FALSE; }
    tick = GetTickCount(); memset(szTmp, 0, 48); sprintf_s(szTmp, 48, "%u", tick);
    if (!WriteFile(hFile, szTmp, 48, &dwWritten, NULL) || dwWritten != 48) { CloseHandle(hFile); return FALSE; }

    if (!idx_WriteStringA(hFile, idx->id)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteStringA(hFile, idx->hash)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteStringW(hFile, idx->name)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteStringW(hFile, idx->category)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteInt64(hFile, idx->creationDate)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteUInt32(hFile, idx->pieceLength)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteUInt32(hFile, idx->pieceCount)) { CloseHandle(hFile); return FALSE; }
    if (!idx_WriteInt64(hFile, idx->bytes)) { CloseHandle(hFile); return FALSE; }

    if (!idx_WriteUInt32(hFile, idx->fileCount)) { CloseHandle(hFile); return FALSE; }
    for (i=0; i<idx->fileCount; i++)
        if (!idx_WriteFileInfo(hFile, idx->files[i])) { CloseHandle(hFile); return FALSE; }

    if (!idx_WriteUInt32(hFile, idx->directoryCount)) { CloseHandle(hFile); return FALSE; }
    for (i=0; i<idx->directoryCount; i++)
        if (!idx_WriteStringW(hFile, idx->directories[i])) { CloseHandle(hFile); return FALSE; }

    if (!idx_WriteStringA(hFile, idx->extraInfo)) { CloseHandle(hFile); return FALSE; }

    SetEndOfFile(hFile);

    encryptIdxFile(hFile);

    CloseHandle(hFile);
    return TRUE;
}

static BOOL idx_calcHash(struct idx *idx, CHAR szHash[MAX_HASH_LEN])
{
    UCHAR tmp[1024], hash[20];
    UINT32 i, len;
    sha1_context ctx;

    memset(szHash, 0, MAX_HASH_LEN);

    sha1_starts(&ctx);

    sha1_update(&ctx, (UCHAR *)idx->id, strlen(idx->id));

    UnicodeToUtf8(idx->name, (CHAR *)tmp, 512);
    sha1_update(&ctx, tmp, strlen((CHAR *)tmp));

    UnicodeToUtf8(idx->category, (CHAR *)tmp, 64);
    sha1_update(&ctx, tmp, strlen((CHAR *)tmp));

    for (i=0; i<idx->fileCount; i++)
    {
        UnicodeToUtf8(idx->files[i]->fileName, (CHAR *)tmp, 512);
        sha1_update(&ctx, tmp, strlen((CHAR *)tmp));
        len = 1 + sprintf_s((CHAR *)tmp, 1024, "%I64d %u %u %I64d",
            idx->files[i]->bytes,
            idx->files[i]->pieceCount,
            idx->files[i]->fileAttr,
            idx->files[i]->fileTime);
        sha1_update(&ctx, tmp, len);
        if (idx->files[i]->pieceCount)
            sha1_update(&ctx, idx->files[i]->hash, 20*idx->files[i]->pieceCount);
    }

    for (i=0; i<idx->directoryCount; i++)
    {
        UnicodeToUtf8(idx->directories[i], (CHAR *)tmp, 512);
        sha1_update(&ctx, tmp, strlen((CHAR *)tmp));
    }

    sha1_update(&ctx, (UCHAR *)idx->extraInfo, strlen(idx->extraInfo));

    sha1_finish(&ctx, hash);
    memset(&ctx, 0, sizeof(sha1_context));

    base64Encode(szHash, MAX_HASH_LEN, hash, 20);

    return TRUE;
}

static BOOL CALLBACK idx_FindFileCB(struct find_files_stat *ffs)
{
    struct create_idx *cs = (struct create_idx *)ffs->cbParam;
    return (WaitForSingleObject(cs->hEventStop, 0)!=WAIT_OBJECT_0);
}

int createIdx(struct create_idx *cs)
{
    struct idx idx;
    struct file *sf;
    struct find_files ff;
    struct ptrList *files = NULL;
    struct ptrList *dirs = NULL;
    struct ptrList *fileInfos = NULL;
    struct ptrList *list;
    UINT32 i, pieceLen, read, dirLen;
    UCHAR *pieceData, hash[20];
    HANDLE hFile;
    WCHAR fileName[MAX_PATH], wszId[MAX_ID_LEN];
    int success = ERR_CREATE_IDX_SUCCESS;

    cs->hEventStop = CreateEvent(NULL, TRUE, FALSE, NULL);

    cs->status = 1;
    cs->createCB(cs);

    memset(&idx, 0, sizeof(idx));
    strcpy_s(idx.id, MAX_ID_LEN, cs->id);
    wcscpy_s(idx.category, MAX_CATEGORY_LEN, cs->category);
    strcpy_s(idx.extraInfo, MAX_EXTRA_LEN, cs->extraInfo);

    // 校验参数
    if (cs->dir[wcslen(cs->dir)-1] == L'\\')
        cs->dir[wcslen(cs->dir)-1] = 0;
    for (i=wcslen(cs->dir); i>0; i--)
    {
        if (cs->dir[i] == L'\\')
        {
            wcscpy_s(idx.name, MAX_NAME_LEN, cs->dir+i+1);
            break;
        }
    }
    if (!idx.name[0])
    {
        CloseHandle(cs->hEventStop);
        return ERR_CREATE_IDX_PARAM;
    }

    if (!cs->creationDate) time(&cs->creationDate);
    idx.creationDate = cs->creationDate;

    // 查找目录和文件
    memset(&ff, 0, sizeof(ff));
    ff.files = &files;
    ff.directories = &dirs;
    ff.dir = cs->dir;
    ff.findFilesCB = idx_FindFileCB;
    ff.cbParam = cs;
    FindFiles(&ff);
    if (WaitForSingleObject(cs->hEventStop, 0)==WAIT_OBJECT_0)
    {
        ptrList_free(&files, free);
        ptrList_free(&dirs, free);
        CloseHandle(cs->hEventStop);
        return ERR_CREATE_IDX_USER_BREAK;
    }

    // 生成file列表，计算总字节数
    for (idx.bytes=0, list=files; list; list=list->next)
    {
        if (idx_IsIdxFile((WCHAR *)list->data))
        { DeleteFileW((WCHAR *)list->data); continue; }
        sf = file_new((WCHAR *)list->data);
        if (!sf) { success = ERR_CREATE_IDX_READ; break; }
        ptrList_append(&fileInfos, sf);
        idx.bytes += sf->bytes;
    }
    ptrList_free(&files, free);

    if (success || WaitForSingleObject(cs->hEventStop, 0)==WAIT_OBJECT_0)
    {
        ptrList_free(&dirs, free);
        ptrList_free(&fileInfos, file_free);
        CloseHandle(cs->hEventStop);
        return success ? success : ERR_CREATE_IDX_USER_BREAK;
    }

    // 计算块大小
    if (!cs->pieceLength)
        cs->pieceLength = idx_calcPieceLen(idx.bytes);

    for (cs->totalPieces=0, list=fileInfos; list; list=list->next)
    {
        sf = (struct file *)list->data;
        sf->pieceCount = (int)((sf->bytes+cs->pieceLength-1)/cs->pieceLength);
        if (sf->pieceCount)
            sf->hash = (UCHAR *)malloc(20*sf->pieceCount);
        cs->totalPieces += sf->pieceCount;
    }

    cs->status = 2;
    cs->createCB(cs);

    pieceData = (UCHAR *)malloc(cs->pieceLength);

    for (list=fileInfos; list; list=list->next)
    {
        if (WaitForSingleObject(cs->hEventStop, 0)==WAIT_OBJECT_0)
            break;

        sf = (struct file *)list->data;
        if (!sf->pieceCount) continue;

        hFile = CreateFileW(sf->fileName, GENERIC_READ,
            FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        { success = ERR_CREATE_IDX_READ; break; }

        for (i=0; i<sf->pieceCount; i++)
        {
            if (WaitForSingleObject(cs->hEventStop, 0)==WAIT_OBJECT_0)
                break;

            if (i < sf->pieceCount - 1)
                pieceLen = cs->pieceLength;
            else
                pieceLen = sf->bytes % cs->pieceLength;

            if (!ReadFile(hFile, pieceData, pieceLen, &read, NULL) || read != pieceLen)
            { CloseHandle(hFile); success = ERR_CREATE_IDX_READ; break; }

            sha1(pieceData, (int)read, hash);
            memcpy(sf->hash+20*i, hash, 20);
            cs->completedPieces ++;
            cs->createCB(cs);
        }

        CloseHandle(hFile);
    }

    free(pieceData);

    if (success || WaitForSingleObject(cs->hEventStop, 0)==WAIT_OBJECT_0)
    {
        ptrList_free(&dirs, free);
        ptrList_free(&fileInfos, file_free);
        CloseHandle(cs->hEventStop);
        return success ? success : ERR_CREATE_IDX_USER_BREAK;
    }

    dirLen = wcslen(cs->dir) + 1;

    idx.pieceLength = cs->pieceLength;
    idx.pieceCount = cs->totalPieces;

    idx.fileCount = ptrList_size(fileInfos);
    idx.files = (struct file **)calloc(idx.fileCount, sizeof(struct file *));
    for (list=fileInfos, i=0; list; list=list->next, i++)
    {
        idx.files[i] = (struct file *)list->data;
        wcscpy_s(fileName, MAX_PATH, idx.files[i]->fileName+dirLen);
        wcscpy_s(idx.files[i]->fileName, MAX_PATH, fileName);
    }

    idx.directoryCount = ptrList_size(dirs);
    idx.directories = (WCHAR **)calloc(idx.directoryCount, sizeof(WCHAR *));
    for (list=dirs, i=0; list; list=list->next, i++)
    {
        idx.directories[i] = (WCHAR *)list->data;
        read = 1 + wcslen(idx.directories[i]);
        wcscpy_s(fileName, MAX_PATH, idx.directories[i]+dirLen);
        wcscpy_s(idx.directories[i], read, fileName);
    }

    idx_calcHash(&idx, idx.hash);

    // save the idxFile to: cs->dir
    swprintf_s(fileName, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        cs->dir, MbcsToUnicode(cs->id, wszId, MAX_ID_LEN));
    if (!SureCreateDir(fileName))
    {
        debugf("cannot mkdir: %S\r\n", fileName);
        success = ERR_CREATE_IDX_WRITE;
    }
    else if (!idx_Write(fileName, &idx))
        success = ERR_CREATE_IDX_WRITE;

    free(idx.files);
    free(idx.directories);

    ptrList_free(&dirs, free);
    ptrList_free(&fileInfos, file_free);

    CloseHandle(cs->hEventStop);

    return success;
}

// --------------------------------------------------------------
//
static BOOL idx_ReadData(CHAR **p, CHAR *pEnd, CHAR *data, int dataLen)
{
    if (*p+dataLen > pEnd) return FALSE;
    memcpy(data, *p, dataLen);
    *p += dataLen;
    return TRUE;
}
static BOOL idx_ReadStringA(CHAR **p, CHAR *pEnd, CHAR *str, int strLen)
{
    int len;

    len = 1 + strlen(*p);
    if (len > strLen || *p+len > pEnd) return FALSE;
    memcpy(str, *p, len);
    *p += len;
    return TRUE;
}
static BOOL idx_ReadStringW(CHAR **p, CHAR *pEnd, WCHAR *str, int strLen)
{
    int utf8Len;

    utf8Len = 1 + strlen(*p);
    if (*p+utf8Len > pEnd) return FALSE;
    Utf8ToUnicode(*p, str, strLen);
    *p += utf8Len;
    return TRUE;
}

static BOOL idx_ReadFileInfo(CHAR **p, CHAR *pEnd, struct file *fi)
{
    CHAR buf[256];

    memset(fi, 0, sizeof(struct file));
    if (!idx_ReadStringW(p, pEnd, fi->fileName, MAX_PATH)) return FALSE;
    if (!idx_ReadStringA(p, pEnd, buf, 256)) return FALSE; fi->bytes = (UINT64)_atoi64(buf);
    if (!idx_ReadStringA(p, pEnd, buf, 256)) return FALSE; fi->pieceCount = (UINT32)atoi(buf);
    if (!idx_ReadStringA(p, pEnd, buf, 256)) return FALSE; fi->fileAttr = (UINT32)atoi(buf);
    if (!idx_ReadStringA(p, pEnd, buf, 256)) return FALSE; fi->fileTime = (UINT64)_atoi64(buf);
    if (fi->pieceCount)
    {
        fi->hash = (UCHAR *)malloc(20*fi->pieceCount);
        if (!fi->hash) return FALSE;
        return idx_ReadData(p, pEnd, (CHAR *)fi->hash, 20*fi->pieceCount);
    }
    return TRUE;
}

static BOOL decryptIdxData(UCHAR *data, int dataLen)
{
    arc4_context ctx;
    unsigned char buf[100], hash[20];
    DWORD tick, crc, crcOriginal;

    tick = (DWORD)strtoul((CHAR *)(data+16), NULL, 10);
    if (tick)
    {
        sprintf_s((CHAR *)buf, 64, "%u", tick);
        sha1(buf, strlen((CHAR *)buf)+1, hash);

        arc4_setup(&ctx, hash, 20);
        arc4_crypt(&ctx, dataLen-64, data+64, data+64);
    }

    crcOriginal = (DWORD)strtoul((CHAR *)(data+32), NULL, 10);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, data+64, dataLen-64);
    //if (crc != crcOriginal)
    //    debugf("[decryptIdxData] crc:%u crcOriginal:%u\r\n", crc, crcOriginal);
    return (crc == crcOriginal);
}

BOOL idx_load(UCHAR *fileData, int fileDataLen, struct idx *idx)
{
    CHAR buf[512];
    CHAR *p, *pEnd;
    DWORD i, pieceOffset;

    memset(idx, 0, sizeof(struct idx));

    if (strcmp((CHAR *)fileData, IDXFILE_SIGNATURE)) return FALSE;

    if (!decryptIdxData(fileData, fileDataLen)) return FALSE;

    p = (CHAR *)fileData + 64;
    pEnd = (CHAR *)fileData + fileDataLen;

    if (!idx_ReadStringA(&p, pEnd, idx->id, MAX_ID_LEN)) return FALSE;
    if (!idx_ReadStringA(&p, pEnd, idx->hash, MAX_HASH_LEN)) return FALSE;
    if (!idx_ReadStringW(&p, pEnd, idx->name, MAX_NAME_LEN)) return FALSE;
    if (!idx_ReadStringW(&p, pEnd, idx->category, MAX_CATEGORY_LEN)) return FALSE;
    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->creationDate = _atoi64(buf);
    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->pieceLength = (UINT32)atoi(buf);
    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->pieceCount = (UINT32)atoi(buf);
    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->bytes = _atoi64(buf);

    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->fileCount = (UINT32)atoi(buf);
    if (idx->fileCount > 200000) return FALSE;
    if (idx->fileCount)
    {
        idx->files = (struct file **)calloc(idx->fileCount, sizeof(void *));
        if (!idx->files) return FALSE;
        for (i=pieceOffset=0; i<idx->fileCount; i++)
        {
            idx->files[i] = (struct file *)malloc(sizeof(struct file));
            if (!idx->files[i]) return FALSE;
            if (!idx_ReadFileInfo(&p, pEnd, idx->files[i])) return FALSE;
            idx->files[i]->pieceOffset = pieceOffset;
            pieceOffset += idx->files[i]->pieceCount;
            idx->files[i]->idxInFiles = i;
        }
    }

    if (!idx_ReadStringA(&p, pEnd, buf, 512)) return FALSE; idx->directoryCount = (UINT32)atoi(buf);
    if (idx->directoryCount > 10000) return FALSE;
    if (idx->directoryCount)
    {
        idx->directories = (WCHAR **)calloc(idx->directoryCount, sizeof(WCHAR *));
        if (!idx->directories) return FALSE;
        for (i=0; i<idx->directoryCount; i++)
        {
            idx->directories[i] = (WCHAR *)malloc(MAX_PATH*sizeof(WCHAR));
            if (!idx->directories[i]) return FALSE;
            if (!idx_ReadStringW(&p, pEnd, idx->directories[i], MAX_PATH)) return FALSE;
        }
    }

    if (!idx_ReadStringA(&p, pEnd, idx->extraInfo, MAX_EXTRA_LEN)) return FALSE;

    return TRUE;
}

BOOL idx_open(const WCHAR *fileName, struct idx *idx)
{
    struct __stat64 stat;
    UCHAR *fileData;
    DWORD fileSize;

    memset(idx, 0, sizeof(struct idx));

    if (_wstat64(fileName, &stat)) return FALSE;
    if (stat.st_size < 128 || stat.st_size > MAX_IDXFILE_SIZE) return FALSE;

    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    if (!idx_load((UCHAR *)fileData, fileSize, idx))
    {
        free(fileData);
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }

    free(fileData);
    return TRUE;
}

void idx_free(struct idx *idx)
{
    DWORD i;

    if (idx->files)
    {
        for (i=0; i<idx->fileCount; i++)
        {
            if (idx->files[i]->hFile != NULL)
                CloseHandle(idx->files[i]->hFile);
            if (idx->files[i]->hash)
                free(idx->files[i]->hash);
            free(idx->files[i]);
        }
        free(idx->files);
        idx->files = NULL;
    }

    if (idx->directories)
    {
        for (i=0; i<idx->directoryCount; i++)
        {
            if (idx->directories[i]) free(idx->directories[i]);
        }
        free(idx->directories);
        idx->directories = NULL;
    }
}


static BOOL idx_checkFileTimeAndSize(struct idx *idx, const WCHAR *dir, struct file *fi)
{
    // 检查文件的大小和日期
    WCHAR filePath[MAX_PATH];
    LARGE_INTEGER li;
    HANDLE hFile;
    UINT64 ft;

    swprintf_s(filePath, MAX_PATH, L"%s\\%s", dir, fi->fileName);
    hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (!GetFileSizeEx(hFile, &li) ||
        (UINT64)li.QuadPart != fi->bytes)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    ft = FileGetLastWriteTime(hFile);
    if (ft != fi->fileTime)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    CloseHandle(hFile);
    return TRUE;
}

BOOL idx_checkFilesTimeAndSize(struct idx *idx, const WCHAR *dir)
{
    UINT32 i;

    for (i=0; i<idx->fileCount; i++)
    {
        if (!idx_checkFileTimeAndSize(idx, dir, idx->files[i]))
            return FALSE;
    }

    return TRUE;
}

static BOOL CALLBACK idxClean_FindFileCB(struct find_files_stat *ffs)
{
    return TRUE;
}
void idx_cleanDirectory(struct idx *idx, WCHAR *dir)
{
    struct find_files ff;
    struct ptrList *files = NULL;
    struct ptrList *dirs = NULL;
    struct file *fi;
    struct ptrList *li;
    UINT32 i;
    WCHAR wszPath[MAX_PATH], wszId[64];

    // 查找目录和文件
    memset(&ff, 0, sizeof(ff));
    ff.files = &files;
    ff.directories = &dirs;
    ff.dir = dir;
    ff.findFilesCB = idxClean_FindFileCB;
    ff.cbParam = NULL;
    FindFiles(&ff);

    for (i=0; i<idx->directoryCount; i++)
    {
        swprintf_s(wszPath, MAX_PATH, L"%s\\%s", dir, idx->directories[i]);
        for (li=dirs; li; li=li->next)
        {
            if (0==_wcsicmp((WCHAR *)li->data, wszPath))
            {
                free(li->data);
                ptrList_remove_node(&dirs, li);
                break;
            }
        }
    }
    for (i=0; i<idx->fileCount; i++)
    {
        fi = idx->files[i];
        swprintf_s(wszPath, MAX_PATH, L"%s\\%s", dir, fi->fileName);
        for (li=files; li; li=li->next)
        {
            if (0==_wcsicmp((WCHAR *)li->data, wszPath))
            {
                free(li->data);
                ptrList_remove_node(&files, li);
                break;
            }
        }
    }

    swprintf_s(wszPath, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        dir, MbcsToUnicode(idx->id, wszId, 64));
    for (li=files; li; li=li->next)
    {
        if (_wcsicmp((WCHAR *)li->data, wszPath)) DeleteFileW((WCHAR *)li->data);
    }
    ptrList_free(&files, free);

    for (li=dirs; li; li=li->next)
        RemoveDirectoryW(li->data);
    ptrList_free(&dirs, free);
}

