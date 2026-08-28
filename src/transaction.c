#include "cbs.h"
int cbs_transaction(CbsSandboxHook prepare,CbsSandboxHook commit,const char *root,void *user){if(!prepare||!commit||!root)return 0;if(!prepare(root,user))return 0;return commit(root,user);}
