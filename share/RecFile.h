#ifndef _RECFILE_H
#define _RECFILE_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct recfile *recfile_open(const WCHAR *fileName, const CHAR *sig);
void recfile_close(struct recfile *rf);

INT recfile_getItemCount(struct recfile *rf);
BOOL recfile_getItem(struct recfile *rf, INT nth, CHAR **data, INT *dataLen);
BOOL recfile_findItem(struct recfile *rf, const CHAR *id, CHAR **data, INT *dataLen);
BOOL recfile_updateItem(struct recfile *rf, CHAR *data, INT dataLen);
BOOL recfile_deleteItem(struct recfile *rf, const CHAR *id);

#ifdef __cplusplus
}
#endif

#endif
