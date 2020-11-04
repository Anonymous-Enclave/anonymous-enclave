#include "eapp.h"
int prime(unsigned long * args)
{
  unsigned long ret = 0;
  while(1){}
  EAPP_RETURN(ret);
}

int EAPP_ENTRY main(){
  unsigned long * args;
  EAPP_RESERVE_REG;
  prime(args);
}
