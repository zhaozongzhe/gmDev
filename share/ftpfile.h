#ifndef __FTPFILE_H_
#define __FTPFILE_H_

#ifdef __cplusplus
extern "C" {
#endif

BOOL HttpDownloadFile(CHAR *localFileName, CHAR *remoteFileName);
BOOL FtpUploadFile(CHAR *localFileName, CHAR *remoteFileName, CHAR *url, CHAR *userName, CHAR *password);

#ifdef __cplusplus
}
#endif

#endif
