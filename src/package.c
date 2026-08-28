#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>
int cbs_cixpkg_compress(const char *input,const char *output){FILE *in=fopen(input,"rb"),*out;long n;void *src,*dst;size_t bound,written;if(!in||fseek(in,0,SEEK_END)|| (n=ftell(in))<0||fseek(in,0,SEEK_SET)){if(in)fclose(in);return 0;}src=malloc((size_t)n);if(!src||fread(src,1,(size_t)n,in)!=(size_t)n){free(src);fclose(in);return 0;}fclose(in);bound=ZSTD_compressBound((size_t)n);dst=malloc(bound);if(!dst){free(src);return 0;}written=ZSTD_compress(dst,bound,src,(size_t)n,19);free(src);if(ZSTD_isError(written)){free(dst);return 0;}out=fopen(output,"wb");if(!out||fwrite(dst,1,written,out)!=written||fclose(out)!=0){if(out)fclose(out);free(dst);return 0;}free(dst);return 1;}
