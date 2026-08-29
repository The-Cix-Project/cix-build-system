#include "cbs.h"
#include <sys/stat.h>
#include <stdio.h>
int cbs_workspace_prepare(const char *root){char path[4096];const char*parts[]={"src","build","dest","cache","tmp"};size_t i;if(!root)return 0;for(i=0;i<5;++i){if(snprintf(path,sizeof(path),"%s/%s",root,parts[i])>=(int)sizeof(path))return 0;if(mkdir(path,0700)!=0){FILE*f=fopen(path,"rb");if(!f)return 0;fclose(f);}}return 1;}
