#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
int main(void){CbsManifestEntry e[2]={{"z",'-',0,0,""},{"a",'-',0,0,""}};qsort(e,2,sizeof(e[0]),cbs_manifest_compare);if(e[0].path[0]!='a')return 1;puts("manifest tests: PASS (canonical path ordering)");return 0;}
