#include "cbs.h"
#include <stdio.h>
int main(void)
{
    if (cbs_effective_jobs(8, 2, 4) != 2 || cbs_effective_jobs(2, 8, 1) != 1 ||
        cbs_effective_jobs(0, 0, 0) != 1) return 1;
    puts("jobs policy tests: PASS (shared ceiling)");
    return 0;
}
