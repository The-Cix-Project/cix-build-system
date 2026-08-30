#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
int cbs_cixpkg_compress(const char *input,const char *output){FILE *in=fopen(input,"rb"),*out;long n;void *src,*dst;size_t bound,written;if(!in||fseek(in,0,SEEK_END)|| (n=ftell(in))<0||fseek(in,0,SEEK_SET)){if(in)fclose(in);return 0;}src=malloc((size_t)n);if(!src||fread(src,1,(size_t)n,in)!=(size_t)n){free(src);fclose(in);return 0;}fclose(in);bound=ZSTD_compressBound((size_t)n);dst=malloc(bound);if(!dst){free(src);return 0;}written=ZSTD_compress(dst,bound,src,(size_t)n,19);free(src);if(ZSTD_isError(written)){free(dst);return 0;}out=fopen(output,"wb");if(!out||fwrite(dst,1,written,out)!=written||fclose(out)!=0){if(out)fclose(out);free(dst);return 0;}free(dst);return 1;}

int cbs_cixpkg_decompress(const char *input,const char *output){FILE*in=fopen(input,"rb"),*out;long n;void*src,*dst;unsigned long long size;size_t written;if(!in||fseek(in,0,SEEK_END)||(n=ftell(in))<0||fseek(in,0,SEEK_SET)){if(in)fclose(in);return 0;}src=malloc((size_t)n);if(!src||fread(src,1,(size_t)n,in)!=(size_t)n){free(src);if(in)fclose(in);return 0;}fclose(in);size=ZSTD_getFrameContentSize(src,(size_t)n);if(size==ZSTD_CONTENTSIZE_ERROR||size==ZSTD_CONTENTSIZE_UNKNOWN||size>1024ULL*1024ULL*1024ULL){free(src);return 0;}dst=malloc((size_t)size+1);if(!dst){free(src);return 0;}written=ZSTD_decompress(dst,(size_t)size,src,(size_t)n);free(src);if(ZSTD_isError(written)||written!=(size_t)size){free(dst);return 0;}out=fopen(output,"wb");if(!out||fwrite(dst,1,written,out)!=written||fclose(out)!=0){if(out)fclose(out);free(dst);return 0;}free(dst);return 1;}

int cbs_install_atomic(const char *staged,const char *destination,unsigned mode){if(staged==NULL||destination==NULL||chmod(staged,mode)!=0)return 0;return rename(staged,destination)==0;}
int cbs_compare_files(const char *left,const char *right){FILE*a=fopen(left,"rb"),*b=fopen(right,"rb");int x,y;if(!a||!b){if(a)fclose(a);if(b)fclose(b);return 0;}do{x=fgetc(a);y=fgetc(b);if(x!=y){fclose(a);fclose(b);return 0;}}while(x!=EOF);fclose(a);fclose(b);return 1;}

static int package_blob(const char *path, unsigned char **data, size_t *size){FILE*f=fopen(path,"rb");long n;if(!f||fseek(f,0,SEEK_END)||(n=ftell(f))<0||fseek(f,0,SEEK_SET)){if(f)fclose(f);return 0;}*data=malloc((size_t)n);if(*data==NULL||fread(*data,1,(size_t)n,f)!=(size_t)n){free(*data);fclose(f);return 0;}fclose(f);*size=(size_t)n;return 1;}
static void put64(unsigned char *p,uint64_t v){size_t i;for(i=0;i<8;++i)p[i]=(unsigned char)(v>>(i*8));}
static uint64_t get64(const unsigned char *p){uint64_t v=0;size_t i;for(i=0;i<8;++i)v|=(uint64_t)p[i]<<(i*8);return v;}
int cbs_cixpkg_write(const char *payload,const char *package_path,const char *identity){
    unsigned char *src,*dst,manifest_digest[65],header[352];
    size_t size,bound,written,idlen; FILE *f;
    if(!payload||!package_path||!identity||!cbs_digest_file(payload,(char*)manifest_digest)||!package_blob(payload,&src,&size))return 0;
    bound=ZSTD_compressBound(size); dst=malloc(bound); if(!dst){free(src);return 0;}
    written=ZSTD_compress(dst,bound,src,size,19); free(src);
    if(ZSTD_isError(written)){free(dst);return 0;}
    memset(header,0,sizeof(header)); memcpy(header,"CIXPKG\0\1",8);
    put64(header+8,352); put64(header+16,size); put64(header+24,0);
    memcpy(header+32,manifest_digest,64); memset(header+96,'0',64);
    idlen=strlen(identity); if(idlen>127)idlen=127; memcpy(header+160,identity,idlen);
    f=fopen(package_path,"wb");
    if(!f||fwrite(header,1,sizeof(header),f)!=sizeof(header)||fwrite(dst,1,written,f)!=written||fclose(f)!=0){if(f)fclose(f);free(dst);return 0;}
    free(dst); return 1;
}

int cbs_cixpkg_verify(const char *package_path,char *identity,size_t identity_size){
    unsigned char h[352],*compressed,*raw; size_t size,written; uint64_t manifest_size,payload_size; FILE*f; char digest[65]; long n;
    f=fopen(package_path,"rb"); if(!f||fread(h,1,sizeof(h),f)!=sizeof(h)||memcmp(h,"CIXPKG\0\1",8)!=0){if(f)fclose(f);return 0;}
    if(get64(h+8)!=352||(manifest_size=get64(h+16))>1024ULL*1024ULL*1024ULL||(payload_size=get64(h+24))!=0){fclose(f);return 0;}
    if(fseek(f,0,SEEK_END)!=0||(n=ftell(f))<352||fseek(f,352,SEEK_SET)!=0){fclose(f);return 0;} size=(size_t)n-352;
    compressed=malloc(size); raw=malloc((size_t)manifest_size+1); if(!compressed||!raw||fread(compressed,1,size,f)!=size){free(compressed);free(raw);fclose(f);return 0;} fclose(f);
    written=ZSTD_decompress(raw,(size_t)manifest_size,compressed,size); free(compressed);
    if(ZSTD_isError(written)||written!=(size_t)manifest_size||!cbs_digest_text((char*)raw,written,digest)||memcmp(h+32,digest,64)!=0){free(raw);return 0;}
    if(identity&&identity_size){size_t ncopy=identity_size-1; if(ncopy>127)ncopy=127; memcpy(identity,h+160,ncopy); identity[ncopy]='\0';}
    free(raw); return 1;
}

int cbs_build_package(const char *recipe,const char *staged_root,const char *package_path){FILE*f;long n;char*text;size_t length;CbsTokenList tokens={0};CbsNode*document;char manifest[4096];int ok;if(!recipe||!staged_root||!package_path)return 0;f=fopen(recipe,"rb");if(!f||fseek(f,0,SEEK_END)||(n=ftell(f))<0||fseek(f,0,SEEK_SET)){if(f)fclose(f);return 0;}text=malloc((size_t)n+1);if(!text||fread(text,1,(size_t)n,f)!=(size_t)n){free(text);fclose(f);return 0;}fclose(f);text[n]='\0';length=(size_t)n;ok=cbs_lex(recipe,text,length,&tokens);document=ok?cbs_parse(recipe,text,length,&tokens):NULL;ok=document!=NULL&&cbs_validate(document,recipe,text);snprintf(manifest,sizeof(manifest),"%s/.cbs-manifest",staged_root);if(ok)ok=cbs_manifest_write(staged_root,manifest);if(ok)ok=cbs_cixpkg_write(manifest,package_path,"cbs");unlink(manifest);if(document)cbs_node_destroy(document);cbs_token_list_destroy(&tokens);free(text);return ok;}

int cbs_build_standalone(const char *recipe,const char *workspace,const char *package_path,const char *architecture,const CbsFetchService *fetch_service){FILE*f;long n;char*text;CbsTokenList tokens={0};CbsNode*document;CbsSourceSet sources={0};CbsBuildPlan plan;CbsPackageIdentity identity;CbsExecutionContext context;char src[4096],build[4096],dest[4096],cache[4096],manifest[4096],*package_identity;int ok;if(!recipe||!workspace||!package_path||!architecture)return 0;f=fopen(recipe,"rb");if(!f||fseek(f,0,SEEK_END)||(n=ftell(f))<0||fseek(f,0,SEEK_SET)){if(f)fclose(f);return 0;}text=malloc((size_t)n+1);if(!text||fread(text,1,(size_t)n,f)!=(size_t)n){free(text);if(f)fclose(f);return 0;}fclose(f);text[n]='\0';ok=cbs_lex(recipe,text,(size_t)n,&tokens);document=ok?cbs_parse(recipe,text,(size_t)n,&tokens):NULL;ok=document!=NULL&&cbs_validate(document,recipe,text)&&cbs_build_plan(document,&plan)&&cbs_workspace_prepare(workspace)&&cbs_sources_from_document(document,&sources);snprintf(src,sizeof(src),"%s/src",workspace);snprintf(build,sizeof(build),"%s/build",workspace);snprintf(dest,sizeof(dest),"%s/dest",workspace);snprintf(cache,sizeof(cache),"%s/cache",workspace);if(ok&&sources.count>0)ok=cbs_prepare_sources(&sources,cache,src,fetch_service,recipe,text,document->location);memset(&context,0,sizeof(context));if(ok){if(!cbs_identity_from_document(document,architecture,&identity))ok=0;else{package_identity=cbs_identity_string(&identity);context.recipe_path=recipe;context.recipe_source=text;context.name=identity.name;context.version=identity.version;context.release=identity.release;context.arch=identity.architecture;context.src=src;context.build=build;context.dest=dest;context.jobs=1;context.working_directory=build;ok=cbs_sources_apply_execution_context(&sources,&context)&&cbs_execute_plan(&plan,&context);}}snprintf(manifest,sizeof(manifest),"%s/.cbs-manifest",dest);if(ok)ok=cbs_manifest_write(dest,manifest)&&cbs_cixpkg_write(manifest,package_path,package_identity);unlink(manifest);free(package_identity);cbs_source_set_destroy(&sources);if(document)cbs_node_destroy(document);cbs_token_list_destroy(&tokens);free(text);return ok;}
