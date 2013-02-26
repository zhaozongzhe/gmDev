#include <windows.h>
#include <wininet.h>
#include <stdio.h>

#include "helper.h"

BOOL FtpUploadFile(CHAR *localFileName, CHAR *remoteFileName, CHAR *url, CHAR *userName, CHAR *password)
{
    HINTERNET hInternet, hConnect, hFtp;
    UCHAR data[8192];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD dwRead, dwWritten, success;
    LARGE_INTEGER fileSize;

    hFile = CreateFile(localFileName, GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    GetFileSizeEx(hFile, &fileSize);

    hInternet = InternetOpen("KBCORE", INTERNET_OPEN_TYPE_PRECONFIG, (""), (""), 0);
    if (!hInternet)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    hConnect = InternetConnect(hInternet, url, INTERNET_DEFAULT_FTP_PORT,
        userName, password, INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, (DWORD_PTR)NULL);
    if (!hConnect)
    {
        CloseHandle(hFile);
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    hFtp = FtpOpenFile(hConnect, remoteFileName, GENERIC_WRITE, INTERNET_FLAG_TRANSFER_BINARY, (DWORD_PTR)NULL);
    if (!hFtp)
    {
        CloseHandle(hFile);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    success = TRUE;
    while (1)
    {
        if (!ReadFile(hFile, data, 8192, &dwRead, NULL) ||
            (dwRead > 0 &&
            (!InternetWriteFile(hFtp, data, dwRead, &dwWritten) ||
            dwRead != dwWritten)))
        {
            success = FALSE;
            break;
        }
        if (dwRead < 8192) break;
    }

    CloseHandle(hFile);
    InternetCloseHandle(hFtp);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return success;
}

//http://cms.131wanwan.com:8001/Private/Soft/GetResourceList.ashx
BOOL HttpDownloadFile(CHAR *localFileName, CHAR *remoteFileName)
{
    HINTERNET hInternet = NULL;
    HINTERNET hURL = NULL;
    UCHAR data[8192];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD dwRead, dwWritten, success, totalBytes;

    hInternet = InternetOpen("KBCORE", INTERNET_OPEN_TYPE_DIRECT, (""), (""), 0);
    if (!hInternet) return FALSE;

    hURL = InternetOpenUrl(hInternet, remoteFileName, "", 0,
        INTERNET_FLAG_NO_AUTO_REDIRECT|INTERNET_FLAG_NO_UI|INTERNET_FLAG_PRAGMA_NOCACHE|INTERNET_FLAG_RELOAD, 88);
    if (!hURL)
    {
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    hFile = SureCreateFile(localFileName);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        InternetCloseHandle(hURL);
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    success = TRUE;
    totalBytes = 0;
    while (1)
    {
        if (!InternetReadFile(hURL, data, 8192, &dwRead) ||
            (dwRead > 0 &&
            (!WriteFile(hFile, data, dwRead, &dwWritten, NULL) ||
            dwRead != dwWritten)))
        {
            success = FALSE;
            break;
        }
        totalBytes += dwRead;
        if (dwRead < 8192) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    InternetCloseHandle(hURL);
    InternetCloseHandle(hInternet);

    return success;
}

