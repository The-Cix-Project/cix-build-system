#include "cbs.h"
#include <stdio.h>
int main(void){FILE*a=fopen("/tmp/repro-a","wb"),*b=fopen("/tmp/repro-b","wb");if(!a||!b)return 1;fputs("same",a);fputs("same",b);fclose(a);fclose(b);if(!cbs_compare_files("/tmp/repro-a","/tmp/repro-b"))return 1;puts("reproducibility tests: PASS (byte mismatch gate)");return 0;}
