#include "cbs.h"
#include <string.h>
int cbs_build_plan(const CbsNode *document,CbsBuildPlan *plan){static const CbsNodeKind kinds[5]={CBS_NODE_PHASE,CBS_NODE_PHASE,CBS_NODE_PHASE,CBS_NODE_PHASE,CBS_NODE_PHASE};const CbsNode*p;size_t i;if(!document||!plan||document->child_count!=1)return 0;memset(plan,0,sizeof(*plan));p=document->children[0];for(i=0;i<p->child_count&&plan->count<5;++i)if(p->children[i]->kind==kinds[plan->count])plan->phases[plan->count++]=p->children[i];return plan->count>0;}
