#include "cbs.h"
#include <stdio.h>
int main(void){CbsStagePolicy p={1,1,1};if(!cbs_validate_stage_path("usr/bin/cbs",&p)||cbs_validate_stage_path("/etc/passwd",&p)||cbs_validate_stage_path("usr/../x",&p)||cbs_validate_stage_path("",&p))return 1;puts("stage policy tests: PASS (data-driven path rejection)");return 0;}
