#include <winsock2.h>
#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <time.h>
#include <string.h>

#include "ptrList.h"
#include "ptrArray.h"

struct recitem
{
    INT64 offset;
    INT32 capacity;
    INT32 dataLen;
    CHAR *data;
};

struct recfile
{
    CRITICAL_SECTION cs;
    CHAR sig[32];
    WCHAR fileName[MAX_PATH];
    HANDLE hFile;
    INT32 itemCount;
    INT64 offset;
    struct ptrArray items;
    struct ptrList *freeItems;
};

struct recfile_header
{
    CHAR sig[32];
    INT32 itemCount;
    INT64 offset;
};

#define ITEM_FREE   0x0001
#define MAX_ID      32

static int itemCmp(const void *p1, const void *p2)
{
    return strcmp(((struct recitem *)p1)->data, ((struct recitem *)p2)->data);
}

static void recitem_free(void *p)
{
    free(((struct recitem *)p)->data);
    free((struct recitem *)p);
}

void recfile_close(struct recfile *rf)
{
    EnterCriticalSection(&rf->cs);

    if (rf->hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(rf->hFile);
        rf->hFile = INVALID_HANDLE_VALUE;
    }
    rf->itemCount = 0;
    rf->offset = 0;
    ptrArray_free(&rf->items, recitem_free);
    ptrList_free(&rf->freeItems, recitem_free);

    LeaveCriticalSection(&rf->cs);
    DeleteCriticalSection(&rf->cs);
}

static BOOL recfile_readItem(struct recfile *rf)
{
    DWORD dwRead;
    INT capacity, dataLen;
    WORD wFlags;
    CHAR *data;
    struct recitem *ri;

    if (!ReadFile(rf->hFile, &capacity, sizeof(INT32), &dwRead, NULL) ||
        dwRead != sizeof(INT32))
        return FALSE;
    if (!ReadFile(rf->hFile, &dataLen, sizeof(INT32), &dwRead, NULL) ||
        dwRead != sizeof(INT32))
        return FALSE;
    if (!ReadFile(rf->hFile, &wFlags, sizeof(WORD), &dwRead, NULL) ||
        dwRead != sizeof(WORD))
        return FALSE;
    if (capacity < 0 || capacity > 2*1024*1024) return FALSE;
    data = malloc(capacity);
    if (!data) return FALSE;
    if (!ReadFile(rf->hFile, data, capacity, &dwRead, NULL) ||
        dwRead != capacity ||
        strlen(data) >= MAX_ID)
    {
        free(data);
        return FALSE;
    }

    ri = malloc(sizeof(struct recitem));
    if (!ri) return FALSE;
    memset(ri, 0, sizeof(struct recitem));
    ri->offset = rf->offset;
    ri->capacity = capacity;
    ri->dataLen = dataLen;
    ri->data = data;

    if (wFlags & ITEM_FREE)
        ptrList_append(&rf->freeItems, ri);
    else
        ptrArray_insertSorted(&rf->items, ri);

    rf->offset += 2*sizeof(INT32) + sizeof(WORD) + capacity;

    return TRUE;
}

static struct recfile *recfile_create(const WCHAR *fileName, const CHAR *sig)
{
    struct recfile *rf;
    HANDLE hFile;
    struct recfile_header header;
    DWORD dwWritten;

    if (wcslen(fileName) >= MAX_PATH) return NULL;
    if (strlen(sig) >= 32) return NULL;

    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    memset(&header, 0, sizeof(header));
    strcpy_s(header.sig, 32, sig);
    if (!WriteFile(hFile, &header, sizeof(header), &dwWritten, NULL) ||
        dwWritten != sizeof(header))
    {
        CloseHandle(hFile);
        return NULL;
    }

    rf = malloc(sizeof(struct recfile));
    if (!rf)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    memset(rf, 0, sizeof(struct recfile));
    InitializeCriticalSection(&rf->cs);
    strcpy_s(rf->sig, 32, sig);
    wcscpy_s(rf->fileName, MAX_PATH, fileName);
    rf->hFile = hFile;
    rf->offset = sizeof(header);
    ptrArray_init(&rf->items, itemCmp);

    return rf;
}

struct recfile *recfile_open(const WCHAR *fileName, const CHAR *sig)
{
    struct recfile *rf;
    HANDLE hFile;
    struct recfile_header header;
    DWORD dwRead;
    INT i;

    if (wcslen(fileName) >= MAX_PATH) return NULL;
    if (strlen(sig) >= 32) return NULL;

    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return recfile_create(fileName, sig);

    if (!ReadFile(hFile, &header, sizeof(header), &dwRead, NULL) ||
        dwRead != sizeof(header) ||
        strcmp(header.sig, sig))
    {
        CloseHandle(hFile);
        return NULL;
    }

    rf = malloc(sizeof(struct recfile));
    if (!rf)
    {
        CloseHandle(hFile);
        return NULL;
    }

    memset(rf, 0, sizeof(struct recfile));
    InitializeCriticalSection(&rf->cs);
    strcpy_s(rf->sig, 32, sig);
    wcscpy_s(rf->fileName, MAX_PATH, fileName);
    rf->hFile = hFile;
    rf->itemCount = header.itemCount;
    rf->offset = sizeof(header);
    ptrArray_init(&rf->items, itemCmp);

    for (i=0; i<rf->itemCount; i++)
    {
        if (!recfile_readItem(rf))
        {
            recfile_close(rf);
            free(rf);
            return NULL;
        }
    }

    return rf;
}

static BOOL recFile_writeHeader(struct recfile *rf)
{
    struct recfile_header header;
    LARGE_INTEGER lrgInt;
    DWORD dwWritten;

    memset(&header, 0, sizeof(header));
    strcpy_s(header.sig, 32, rf->sig);
    header.offset = rf->offset;
    header.itemCount = rf->itemCount;
    lrgInt.QuadPart = 0;
    SetFilePointerEx(rf->hFile, lrgInt, NULL, FILE_BEGIN);
    return WriteFile(rf->hFile, &header, sizeof(header), &dwWritten, NULL) &&
        dwWritten == sizeof(header);
}

static BOOL recfile_writeItemFlags(struct recfile *rf, struct recitem *ri, WORD wFlags)
{
    LARGE_INTEGER lrgInt;
    DWORD dwWritten;

    lrgInt.QuadPart = ri->offset + sizeof(INT32) + sizeof(INT32);
    SetFilePointerEx(rf->hFile, lrgInt, NULL, FILE_BEGIN);
    return WriteFile(rf->hFile, &wFlags, sizeof(WORD), &dwWritten, NULL) &&
        dwWritten == sizeof(WORD);
}

static BOOL recfile_writeItemData(struct recfile *rf, struct recitem *ri, WORD wFlags)
{
    LARGE_INTEGER lrgInt;
    DWORD dwWritten;

    lrgInt.QuadPart = ri->offset;
    SetFilePointerEx(rf->hFile, lrgInt, NULL, FILE_BEGIN);

    if (!WriteFile(rf->hFile, &ri->capacity, sizeof(INT32), &dwWritten, NULL) ||
        dwWritten != sizeof(INT32))
        return FALSE;
    if (!WriteFile(rf->hFile, &ri->dataLen, sizeof(INT32), &dwWritten, NULL) ||
        dwWritten != sizeof(INT32))
        return FALSE;
    if (!WriteFile(rf->hFile, &wFlags, sizeof(WORD), &dwWritten, NULL) ||
        dwWritten != sizeof(WORD))
        return FALSE;
    if (!WriteFile(rf->hFile, ri->data, ri->dataLen, &dwWritten, NULL) ||
        dwWritten != ri->dataLen)
        return FALSE;

    return TRUE;
}

static BOOL recfile_makeNewItem(struct recfile *rf, CHAR *data, INT dataLen)
{
    struct recitem *ri;

    ri = malloc(sizeof(struct recitem));
    if (!ri) return FALSE;
    memset(ri, 0, sizeof(struct recitem));
    ri->offset = rf->offset;
    ri->capacity = dataLen;
    ri->data = malloc(ri->capacity);
    if (!ri->data) { free(ri); return FALSE; }
    memcpy(ri->data, data, dataLen);
    ri->dataLen = dataLen;

    if (!recfile_writeItemData(rf, ri, 0))
    {
        recitem_free(ri);
        return FALSE;
    }

    ptrArray_insertSorted(&rf->items, ri);

    rf->itemCount ++;
    rf->offset += 2*sizeof(INT32) + sizeof(WORD) + ri->capacity;
    return recFile_writeHeader(rf);
}

static BOOL recFile_saveToFreeSpace(struct recfile *rf, CHAR *data, INT dataLen)
{
    struct recitem *ri;
    struct ptrList *list;

    for (list=rf->freeItems; list; list=list->next)
    {
        ri = list->data;
        if (ri->capacity >= dataLen)
        {
            memcpy(ri->data, data, dataLen);
            ri->dataLen = dataLen;
            if (!recfile_writeItemData(rf, ri, 0))
                return FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL recfile_updateItem(struct recfile *rf, CHAR *data, INT dataLen)
{
    struct recitem *ri, rif = { 0 };
    DWORD idLen;
    BOOL success;

    if (dataLen <= 0 || dataLen > 2*1024*1024) return FALSE;

    idLen = (DWORD)strlen(data);
    if (idLen >= MAX_ID || idLen < 1) return FALSE;

    EnterCriticalSection(&rf->cs);

    rif.data = data;
    ri = ptrArray_findSorted(&rf->items, &rif);

    if (!ri)
    {
        success = recFile_saveToFreeSpace(rf, data, dataLen);
        if (!success)
            success = recfile_makeNewItem(rf, data, dataLen);
        LeaveCriticalSection(&rf->cs);
        return success;
    }
    else
    {
        if (dataLen <= ri->capacity)
        {
            memcpy(ri->data, data, dataLen);
            ri->dataLen = dataLen;
            success = recfile_writeItemData(rf, ri, 0);
        }
        else
        {
            ptrArray_removeSorted(&rf->items, &rif);
            ptrList_append(&rf->freeItems, ri);
            success = recfile_writeItemFlags(rf, ri, ITEM_FREE); // ±ê¼ÇÉ¾³ý
            if (success)
            {
                success = recFile_saveToFreeSpace(rf, data, dataLen);
                if (!success)
                    success = recfile_makeNewItem(rf, data, dataLen);
            }
        }
        LeaveCriticalSection(&rf->cs);
        return success;
    }
}

INT recfile_getItemCount(struct recfile *rf)
{
    INT size;
    EnterCriticalSection(&rf->cs);
    size = ptrArray_size(&rf->items);
    LeaveCriticalSection(&rf->cs);
    return size;
}

BOOL recfile_getItem(struct recfile *rf, INT nth, CHAR **data, INT *dataLen)
{
    struct recitem *ri;

    EnterCriticalSection(&rf->cs);

    if (nth < 0 || nth > ptrArray_size(&rf->items))
    {
        LeaveCriticalSection(&rf->cs);
        return FALSE;
    }

    ri = ptrArray_nth(&rf->items, nth);
    if (!ri || !ri->dataLen)
    {
        LeaveCriticalSection(&rf->cs);
        return FALSE;
    }

    *data = malloc(ri->dataLen);
    if (!*data)
    {
        LeaveCriticalSection(&rf->cs);
        return FALSE;
    }
    memcpy(*data, ri->data, ri->dataLen);
    *dataLen = ri->dataLen;

    LeaveCriticalSection(&rf->cs);
    return TRUE;
}

BOOL recfile_findItem(struct recfile *rf, const CHAR *id, CHAR **data, INT *dataLen)
{
    struct recitem *ri, rif = { 0 };

    EnterCriticalSection(&rf->cs);

    rif.data = (CHAR *)id;
    ri = ptrArray_findSorted(&rf->items, &rif);
    if (!ri || !ri->dataLen)
    {
        LeaveCriticalSection(&rf->cs);
        return FALSE;
    }

    *data = malloc(ri->dataLen);
    memcpy(*data, ri->data, ri->dataLen);
    *dataLen = ri->dataLen;

    LeaveCriticalSection(&rf->cs);
    return TRUE;
}

BOOL recfile_deleteItem(struct recfile *rf, const CHAR *id)
{
    struct recitem *ri, rif = { 0 };

    EnterCriticalSection(&rf->cs);

    rif.data = (CHAR *)id;
    ri = ptrArray_findSorted(&rf->items, &rif);
    if (!ri || !ri->dataLen)
    {
        LeaveCriticalSection(&rf->cs);
        return FALSE;
    }

    ptrArray_removeSorted(&rf->items, &rif);
    ptrList_append(&rf->freeItems, ri);

    recfile_writeItemFlags(rf, ri, ITEM_FREE);

    LeaveCriticalSection(&rf->cs);
    return TRUE;
}

