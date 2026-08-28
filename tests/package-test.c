#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <stdio.h>
int main(void){char in[]="/tmp/cixpkg-in",out[]="/tmp/cixpkg-out",round[]="/tmp/cixpkg-round",pkg[]="/tmp/cixpkg-v1",identity[32];FILE*f=fopen(in,"wb");if(!f)return 1;fputs("payload",f);fclose(f);if(!cbs_cixpkg_compress(in,out)||!cbs_cixpkg_decompress(out,round)||!cbs_cixpkg_write(in,pkg,"cbs-0.1-1")||!cbs_cixpkg_verify(pkg,identity,sizeof(identity))||identity[0]=='\0'||!cbs_build_package("cbs.cbs","tests/fixtures/execution","/tmp/cixpkg-build"))return 1;puts("CIXPKG tests: PASS (writer, reader, pipeline)");return 0;}
