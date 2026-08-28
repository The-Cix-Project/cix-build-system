#include "cbs.h"
#include <string.h>
int cbs_manifest_compare(const void *left, const void *right){const CbsManifestEntry *a=left,*b=right;return strcmp(a->path,b->path);}
