#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
int main(void){CbsManifestEntry e[2]={{.path="z",.type='f'},{.path="a",.type='f'}};qsort(e,2,sizeof(e[0]),cbs_manifest_compare);if(e[0].path[0]!='a'||!cbs_manifest_write("tests/fixtures/execution","/tmp/cbs-manifest"))return 1;puts("manifest tests: PASS (canonical path ordering and tree emission)");return 0;}
