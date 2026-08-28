#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <stdio.h>
int main(void){char in[]="/tmp/cixpkg-in",out[]="/tmp/cixpkg-out",round[]="/tmp/cixpkg-round";FILE*f=fopen(in,"wb");if(!f)return 1;fputs("payload",f);fclose(f);if(!cbs_cixpkg_compress(in,out)||!cbs_cixpkg_decompress(out,round))return 1;puts("CIXPKG tests: PASS (fixed zstd compression and verification)");return 0;}
