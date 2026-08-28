#include "cbs.h"
int cbs_observe_dependencies(CbsDependencyObserver observer,const char *path,void *user){return observer!=NULL&&path!=NULL&&observer(path,user);}
