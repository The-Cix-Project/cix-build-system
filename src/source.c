#include "cbs.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} Sha256;

static uint32_t rotate_right(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32U - count));
}

static void transform(Sha256 *sha, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64],a,b,c,d,e,f,g,h;
    size_t i;
    for (i=0;i<16;++i) w[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for (;i<64;++i) { uint32_t x=rotate_right(w[i-15],7)^rotate_right(w[i-15],18)^(w[i-15]>>3); uint32_t y=rotate_right(w[i-2],17)^rotate_right(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+x+w[i-7]+y; }
    a=sha->state[0];b=sha->state[1];c=sha->state[2];d=sha->state[3];e=sha->state[4];f=sha->state[5];g=sha->state[6];h=sha->state[7];
    for(i=0;i<64;++i){uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);uint32_t t1=h+s1+((e&f)^((~e)&g))+k[i]+w[i];uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);uint32_t t2=s0+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    sha->state[0]+=a;sha->state[1]+=b;sha->state[2]+=c;sha->state[3]+=d;sha->state[4]+=e;sha->state[5]+=f;sha->state[6]+=g;sha->state[7]+=h;
}

static void initialize(Sha256 *sha)
{
    static const uint32_t state[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memset(sha,0,sizeof(*sha));memcpy(sha->state,state,sizeof(state));
}

static void update(Sha256 *sha,const unsigned char *data,size_t length)
{
    while(length){size_t room=64-sha->used;size_t take=length<room?length:room;memcpy(sha->block+sha->used,data,take);sha->used+=take;data+=take;length-=take;sha->bits+=(uint64_t)take*8U;if(sha->used==64){transform(sha,sha->block);sha->used=0;}}
}

static void finish(Sha256 *sha,unsigned char digest[32])
{
    size_t i;sha->block[sha->used++]=0x80;if(sha->used>56){while(sha->used<64)sha->block[sha->used++]=0;transform(sha,sha->block);sha->used=0;}while(sha->used<56)sha->block[sha->used++]=0;for(i=0;i<8;++i)sha->block[56+i]=(unsigned char)(sha->bits>>(56-8*i));transform(sha,sha->block);for(i=0;i<32;++i)digest[i]=(unsigned char)(sha->state[i/4]>>(24-8*(i%4)));
}

static int file_digest(const char *path,char output[65])
{
    static const char hex[]="0123456789abcdef";unsigned char buffer[32768],digest[32];Sha256 sha;FILE *file=fopen(path,"rb");size_t length,i;if(file==NULL)return 0;initialize(&sha);while((length=fread(buffer,1,sizeof(buffer),file))>0)update(&sha,buffer,length);if(ferror(file)||fclose(file)!=0)return 0;finish(&sha,digest);for(i=0;i<32;++i){output[i*2]=hex[digest[i]>>4];output[i*2+1]=hex[digest[i]&15];}output[64]='\0';return 1;
}

int cbs_sources_from_document(const CbsNode *document,CbsSourceSet *set)
{
    const CbsNode *package,*block=NULL;size_t i,j;memset(set,0,sizeof(*set));if(document==NULL||document->child_count!=1)return 0;package=document->children[0];for(i=0;i<package->child_count;++i)if(package->children[i]->kind==CBS_NODE_SOURCES)block=package->children[i];if(block==NULL)return 1;set->items=cbs_allocate(block->child_count*sizeof(*set->items));set->count=block->child_count;for(i=0;i<set->count;++i){const CbsNode *node=block->children[i];CbsSource *source=&set->items[i];source->kind=node->name;source->name=node->value;source->url_count=node->child_count-1;source->urls=cbs_allocate(source->url_count*sizeof(*source->urls));for(j=0;j<source->url_count;++j)source->urls[j]=node->children[j]->value;source->sha256=node->children[node->child_count-1]->value;}return 1;
}

void cbs_source_set_destroy(CbsSourceSet *set)
{
    size_t i;for(i=0;i<set->count;++i){free(set->items[i].urls);free(set->items[i].verified_path);}free(set->bindings);free(set->items);memset(set,0,sizeof(*set));
}

int cbs_source_verify(CbsSource *source,const char *path,const char *recipe_path,
                      const char *recipe_source,CbsLocation location)
{
    char actual[65],message[512];free(source->verified_path);source->verified_path=NULL;if(!file_digest(path,actual)){snprintf(message,sizeof(message),"source `%s` could not be hashed; errno=%d",source->name,errno);cbs_diagnostic(recipe_path,recipe_source,location,"error","CPDL-E5001",CBS_DIAG_SOURCE,message);return 0;}if(strcmp(actual,source->sha256)!=0){snprintf(message,sizeof(message),"source `%s` checksum mismatch; expected %s; computed %s",source->name,source->sha256,actual);cbs_diagnostic(recipe_path,recipe_source,location,"error","CPDL-E5001",CBS_DIAG_SOURCE,message);return 0;}source->verified_path=cbs_duplicate(path);return 1;
}

int cbs_sources_apply_execution_context(CbsSourceSet *set,CbsExecutionContext *context)
{
    size_t i;for(i=0;i<set->count;++i)if(set->items[i].verified_path==NULL)return 0;free(set->bindings);set->bindings=cbs_allocate(set->count*sizeof(*set->bindings));for(i=0;i<set->count;++i){set->bindings[i].name=set->items[i].name;set->bindings[i].path=set->items[i].verified_path;}context->sources=set->bindings;context->source_count=set->count;return 1;
}

int cbs_sources_fetch(CbsSourceSet *set, const char *cache_directory,
                      const CbsFetchService *service, const char *recipe_path,
                      const char *recipe_source, CbsLocation location)
{
    size_t source_index, url_index;
    char cache_path[4096], temporary_path[4096], error[256], message[768];
    const char *last_url = "none";
    int descriptor;

    if (cache_directory == NULL)
        return 0;
    for (source_index = 0; source_index < set->count; ++source_index) {
        CbsSource *source = &set->items[source_index];
        int verified = 0;
        if (snprintf(cache_path, sizeof(cache_path), "%s/%s",
                     cache_directory, source->sha256) >= (int)sizeof(cache_path))
            return 0;
        if (access(cache_path, R_OK) == 0 &&
            cbs_source_verify(source, cache_path, recipe_path, recipe_source,
                              location))
            continue;
        unlink(cache_path);
        if (service == NULL || service->fetch == NULL) {
            snprintf(message, sizeof(message),
                     "source `%s` is not cached and networking is unavailable",
                     source->name);
            cbs_diagnostic(recipe_path, recipe_source, location, "error",
                           "CPDL-E5001", CBS_DIAG_SOURCE, message);
            return 0;
        }
        for (url_index = 0; url_index < source->url_count; ++url_index) {
            last_url = source->urls[url_index];
            if (snprintf(temporary_path, sizeof(temporary_path),
                         "%s/.cbs-fetch-XXXXXX", cache_directory) >=
                (int)sizeof(temporary_path))
                return 0;
            descriptor = mkstemp(temporary_path);
            if (descriptor < 0)
                continue;
            close(descriptor);
            error[0] = '\0';
            if (service->fetch(source->urls[url_index], temporary_path,
                               service->user, error, sizeof(error)) &&
                cbs_source_verify(source, temporary_path, recipe_path,
                                  recipe_source, location) &&
                rename(temporary_path, cache_path) == 0) {
                free(source->verified_path);
                source->verified_path = cbs_duplicate(cache_path);
                verified = 1;
                break;
            }
            unlink(temporary_path);
        }
        if (!verified) {
            snprintf(message, sizeof(message),
                     "source `%s` could not be fetched from `%s`; cause: %s",
                     source->name, last_url,
                     error[0] == '\0' ? "all mirrors failed" : error);
            cbs_diagnostic(recipe_path, recipe_source, location, "error",
                           "CPDL-E5001", CBS_DIAG_SOURCE, message);
            return 0;
        }
    }
    return 1;
}
